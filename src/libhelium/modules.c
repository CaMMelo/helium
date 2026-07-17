/* SPDX-License-Identifier: TBD */
/*
 * modules.c - Module resolution and interface files for Helium.
 */

#include "modules.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "inference.h"
#include "type_env.h"

static void *xalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p)
		abort();
	return p;
}

static char *xstrdup(const char *s)
{
	if (!s)
		return NULL;
	return strdup(s);
}

static void append_ptr(void ***items, size_t *count, size_t *cap, void *item)
{
	if (*count == *cap) {
		size_t newcap = *cap ? *cap * 2 : 4;
		void **tmp = realloc(*items, newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		*items = tmp;
		*cap = newcap;
	}
	(*items)[(*count)++] = item;
}

static int string_in_list(const char *s, char **list, size_t count);
static void append_string(char ***items, size_t *count, size_t *cap,
			  char *item);

static void append_string(char ***items, size_t *count, size_t *cap,
			  char *item)
{
	if (*count == *cap) {
		size_t newcap = *cap ? *cap * 2 : 4;
		char **tmp = realloc(*items, newcap * sizeof(*tmp));

		if (!tmp)
			abort();
		*items = tmp;
		*cap = newcap;
	}
	(*items)[(*count)++] = item;
}

static void format_error(char **error, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (n < 0)
		*error = strdup("module error");
	else
		*error = msg;
}

/* -------------------------------------------------------------------------- */
/* Interface type serializer                                                  */
/* -------------------------------------------------------------------------- */

static void ifc_type_string_append(char **buf, size_t *len, size_t *cap,
				   const char *text)
{
	size_t n = strlen(text);

	if (*len + n + 1 > *cap) {
		size_t newcap = *cap ? *cap * 2 : 64;

		while (*len + n + 1 > newcap)
			newcap *= 2;
		*buf = realloc(*buf, newcap);
		if (!*buf)
			abort();
		*cap = newcap;
	}
	memcpy(*buf + *len, text, n + 1);
	*len += n;
}

static void ifc_type_to_string_impl(struct helium_type *type, char **buf,
				    size_t *len, size_t *cap)
{
	char tmp[64];
	size_t i;

	if (!type)
		return;

	switch (type->kind) {
	case HELIUM_TYPE_VAR:
		ifc_type_string_append(buf, len, cap,
				       type->name ? type->name : "_");
		break;
	case HELIUM_TYPE_NAMED:
		ifc_type_string_append(buf, len, cap,
				       type->name ? type->name : "?");
		if (type->arg_count > 0) {
			ifc_type_string_append(buf, len, cap, "<");
			for (i = 0; i < type->arg_count; i++) {
				if (i > 0)
					ifc_type_string_append(buf, len, cap, ", ");
				ifc_type_to_string_impl(type->args[i], buf, len, cap);
			}
			ifc_type_string_append(buf, len, cap, ">");
		}
		break;
	case HELIUM_TYPE_FN:
		ifc_type_string_append(buf, len, cap, "fn(");
		for (i = 0; i < type->param_count; i++) {
			if (i > 0)
				ifc_type_string_append(buf, len, cap, ", ");
			ifc_type_to_string_impl(type->params[i], buf, len, cap);
		}
		ifc_type_string_append(buf, len, cap, ") -> ");
		ifc_type_to_string_impl(type->ret, buf, len, cap);
		break;
	case HELIUM_TYPE_ARRAY:
		ifc_type_string_append(buf, len, cap, "[");
		ifc_type_to_string_impl(type->elem_type, buf, len, cap);
		snprintf(tmp, sizeof(tmp), "; %ld", type->array_size);
		ifc_type_string_append(buf, len, cap, tmp);
		ifc_type_string_append(buf, len, cap, "]");
		break;
	}
}

static char *ifc_type_to_string(struct helium_type *type)
{
	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;

	ifc_type_to_string_impl(type, &buf, &len, &cap);
	return buf;
}

/* -------------------------------------------------------------------------- */
/* Interface type parser                                                      */
/* -------------------------------------------------------------------------- */

enum ifc_tok_kind {
	IFC_TOK_EOF,
	IFC_TOK_IDENT,
	IFC_TOK_INT,
	IFC_TOK_FN,
	IFC_TOK_LPAREN,
	IFC_TOK_RPAREN,
	IFC_TOK_ARROW,
	IFC_TOK_COMMA,
	IFC_TOK_LT,
	IFC_TOK_GT,
	IFC_TOK_LBRACKET,
	IFC_TOK_RBRACKET,
	IFC_TOK_SEMI,
	IFC_TOK_COLON,
};

struct ifc_tok {
	int kind;
	char *text;
	long ival;
};

struct ifc_lexer {
	const char *input;
	size_t pos;
	struct ifc_tok current;
	char **type_params;
	size_t type_param_count;
};

static int is_ident_char(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '.';
}

static void ifc_lexer_next(struct ifc_lexer *lex)
{
	const char *s = lex->input;
	size_t i = lex->pos;
	char c;

	free(lex->current.text);
	lex->current.text = NULL;
	lex->current.kind = IFC_TOK_EOF;
	lex->current.ival = 0;

	while (s[i] && isspace((unsigned char)s[i]))
		i++;

	if (!s[i]) {
		lex->pos = i;
		return;
	}

	c = s[i];

	if (c == '(') { lex->current.kind = IFC_TOK_LPAREN; i++; }
	else if (c == ')') { lex->current.kind = IFC_TOK_RPAREN; i++; }
	else if (c == '<') { lex->current.kind = IFC_TOK_LT; i++; }
	else if (c == '>') { lex->current.kind = IFC_TOK_GT; i++; }
	else if (c == '[') { lex->current.kind = IFC_TOK_LBRACKET; i++; }
	else if (c == ']') { lex->current.kind = IFC_TOK_RBRACKET; i++; }
	else if (c == ';') { lex->current.kind = IFC_TOK_SEMI; i++; }
	else if (c == ':') { lex->current.kind = IFC_TOK_COLON; i++; }
	else if (c == ',') { lex->current.kind = IFC_TOK_COMMA; i++; }
	else if (c == '-' && s[i + 1] == '>') {
		lex->current.kind = IFC_TOK_ARROW;
		i += 2;
	} else if (isdigit((unsigned char)c)) {
		char *end;

		lex->current.kind = IFC_TOK_INT;
		lex->current.ival = strtol(s + i, &end, 10);
		i = end - s;
	} else if (is_ident_char(c)) {
		size_t start = i;

		while (s[i] && is_ident_char(s[i]))
			i++;
		lex->current.kind = IFC_TOK_IDENT;
		lex->current.text = strndup(s + start, i - start);
		if (lex->current.text && strcmp(lex->current.text, "fn") == 0)
			lex->current.kind = IFC_TOK_FN;
	} else {
		lex->current.kind = IFC_TOK_EOF;
	}

	lex->pos = i;
}

static void ifc_lexer_init(struct ifc_lexer *lex, const char *input)
{
	lex->input = input;
	lex->pos = 0;
	lex->current.text = NULL;
	lex->current.kind = IFC_TOK_EOF;
	lex->type_params = NULL;
	lex->type_param_count = 0;
	ifc_lexer_next(lex);
}

static void ifc_lexer_free(struct ifc_lexer *lex)
{
	free(lex->current.text);
}

static struct helium_type *ifc_parse_type(struct ifc_lexer *lex, char **error);

static struct helium_type *ifc_parse_named(struct ifc_lexer *lex, char **error)
{
	struct helium_type *type;

	if (lex->current.kind != IFC_TOK_IDENT) {
		format_error(error, "expected type name");
		return NULL;
	}
	type = helium_type_named(lex->current.text, 0, 0);
	ifc_lexer_next(lex);

	if (lex->current.kind == IFC_TOK_LT) {
		ifc_lexer_next(lex);
		while (1) {
			struct helium_type *arg;

			arg = ifc_parse_type(lex, error);
			if (!arg) {
				helium_type_free(type);
				return NULL;
			}
			helium_type_add_arg(type, arg);
			if (lex->current.kind == IFC_TOK_COMMA) {
				ifc_lexer_next(lex);
				continue;
			}
			break;
		}
		if (lex->current.kind != IFC_TOK_GT) {
			format_error(error, "expected '>''");
			helium_type_free(type);
			return NULL;
		}
		ifc_lexer_next(lex);
	}

	if (string_in_list(type->name, lex->type_params,
			   lex->type_param_count))
		type->kind = HELIUM_TYPE_VAR;

	return type;
}

static struct helium_type *ifc_parse_fn(struct ifc_lexer *lex, char **error)
{
	struct helium_type *type;

	if (lex->current.kind != IFC_TOK_FN) {
		format_error(error, "expected 'fn'");
		return NULL;
	}
	ifc_lexer_next(lex);

	if (lex->current.kind != IFC_TOK_LPAREN) {
		format_error(error, "expected '('");
		return NULL;
	}
	ifc_lexer_next(lex);

	type = helium_type_fn(0, 0);

	if (lex->current.kind != IFC_TOK_RPAREN) {
		while (1) {
			struct helium_type *param;

			param = ifc_parse_type(lex, error);
			if (!param) {
				helium_type_free(type);
				return NULL;
			}
			helium_type_add_param(type, param);
			if (lex->current.kind == IFC_TOK_COMMA) {
				ifc_lexer_next(lex);
				continue;
			}
			break;
		}
	}

	if (lex->current.kind != IFC_TOK_RPAREN) {
		format_error(error, "expected ')'");
		helium_type_free(type);
		return NULL;
	}
	ifc_lexer_next(lex);

	if (lex->current.kind != IFC_TOK_ARROW) {
		format_error(error, "expected '->'");
		helium_type_free(type);
		return NULL;
	}
	ifc_lexer_next(lex);

	{
		struct helium_type *ret = ifc_parse_type(lex, error);

		if (!ret) {
			helium_type_free(type);
			return NULL;
		}
		helium_type_set_ret(type, ret);
	}

	return type;
}

static struct helium_type *ifc_parse_array(struct ifc_lexer *lex, char **error)
{
	struct helium_type *elem;
	struct helium_type *type;
	long size;

	if (lex->current.kind != IFC_TOK_LBRACKET) {
		format_error(error, "expected '['");
		return NULL;
	}
	ifc_lexer_next(lex);

	elem = ifc_parse_type(lex, error);
	if (!elem)
		return NULL;

	if (lex->current.kind != IFC_TOK_SEMI) {
		format_error(error, "expected ';'");
		helium_type_free(elem);
		return NULL;
	}
	ifc_lexer_next(lex);

	if (lex->current.kind != IFC_TOK_INT) {
		format_error(error, "expected array size");
		helium_type_free(elem);
		return NULL;
	}
	size = lex->current.ival;
	ifc_lexer_next(lex);

	if (lex->current.kind != IFC_TOK_RBRACKET) {
		format_error(error, "expected ']'");
		helium_type_free(elem);
		return NULL;
	}
	ifc_lexer_next(lex);

	type = helium_type_array(elem, size, 0, 0);
	return type;
}

static struct helium_type *ifc_parse_type(struct ifc_lexer *lex, char **error)
{
	if (lex->current.kind == IFC_TOK_FN)
		return ifc_parse_fn(lex, error);
	if (lex->current.kind == IFC_TOK_LBRACKET)
		return ifc_parse_array(lex, error);
	if (lex->current.kind == IFC_TOK_IDENT)
		return ifc_parse_named(lex, error);
	/* Unit type '()' is tokenized as '(' ')'. */
	if (lex->current.kind == IFC_TOK_LPAREN) {
		ifc_lexer_next(lex);
		if (lex->current.kind != IFC_TOK_RPAREN) {
			format_error(error, "expected ')'");
			return NULL;
		}
		ifc_lexer_next(lex);
		return helium_type_named("()", 0, 0);
	}
	format_error(error, "unexpected token in type");
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* Interface load / emit                                                      */
/* -------------------------------------------------------------------------- */

struct helium_module_interface *helium_module_interface_new(const char *name,
						    const char *source_path,
						    const char *object_path)
{
	struct helium_module_interface *iface = xalloc(sizeof(*iface));

	iface->name = xstrdup(name);
	iface->source_path = xstrdup(source_path);
	iface->object_path = xstrdup(object_path);
	return iface;
}

void helium_module_interface_free(struct helium_module_interface *iface)
{
	size_t i;

	if (!iface)
		return;
	free(iface->name);
	free(iface->source_path);
	free(iface->object_path);
	for (i = 0; i < iface->export_count; i++) {
		struct helium_interface_export *exp = iface->exports[i];
		size_t j;

		free(exp->name);
		helium_type_free(exp->type);
		for (j = 0; j < exp->type_param_count; j++)
			free(exp->type_params[j]);
		free(exp->type_params);
		free(exp);
	}
	free(iface->exports);
	free(iface);
}

void helium_module_interface_add_export(struct helium_module_interface *iface,
					const char *name,
					char **type_params,
					size_t type_param_count,
					struct helium_type *type)
{
	struct helium_interface_export *exp = xalloc(sizeof(*exp));
	size_t i;

	exp->name = xstrdup(name);
	exp->type = helium_type_copy(type);
	exp->type_params = NULL;
	exp->type_param_count = type_param_count;
	if (type_param_count) {
		exp->type_params = malloc(type_param_count *
					  sizeof(*exp->type_params));
		if (!exp->type_params)
			abort();
		for (i = 0; i < type_param_count; i++)
			exp->type_params[i] = xstrdup(type_params[i]);
	}
	append_ptr((void ***)&iface->exports, &iface->export_count,
		   &iface->export_capacity, exp);
}

struct helium_type *helium_module_interface_lookup(
				struct helium_module_interface *iface,
				const char *name)
{
	size_t i;

	if (!iface)
		return NULL;
	for (i = 0; i < iface->export_count; i++) {
		if (strcmp(iface->exports[i]->name, name) == 0)
			return iface->exports[i]->type;
	}
	return NULL;
}

struct helium_module_interface *helium_module_interface_load(
					const char *path, char **error)
{
	struct helium_module_interface *iface = NULL;
	struct ifc_lexer lex;
	FILE *f;
	char *buf = NULL;
	size_t size = 0;
	size_t len = 0;
	char line[1024];
	int have_module = 0;

	f = fopen(path, "r");
	if (!f) {
		format_error(error, "%s: %m", path);
		return NULL;
	}

	while (fgets(line, sizeof(line), f)) {
		size_t n = strlen(line);

		if (len + n + 1 > size) {
			size_t newsize = size ? size * 2 : 1024;

			while (len + n + 1 > newsize)
				newsize *= 2;
			buf = realloc(buf, newsize);
			if (!buf)
				abort();
			size = newsize;
		}
		memcpy(buf + len, line, n + 1);
		len += n;
	}
	fclose(f);

	ifc_lexer_init(&lex, buf ? buf : "");
	while (lex.current.kind != IFC_TOK_EOF) {
		if (lex.current.kind == IFC_TOK_IDENT &&
		    strcmp(lex.current.text, "module") == 0) {
			char *name;

			ifc_lexer_next(&lex);
			if (lex.current.kind != IFC_TOK_IDENT) {
				format_error(error, "%s: expected module name", path);
				goto fail;
			}
			name = xstrdup(lex.current.text);
			ifc_lexer_next(&lex);
			helium_module_interface_free(iface);
			iface = helium_module_interface_new(name, NULL, NULL);
			free(name);
			have_module = 1;
			continue;
		}

		if (lex.current.kind == IFC_TOK_IDENT) {
			char *name;
			char **params = NULL;
			size_t pcount = 0;
			size_t pcap = 0;
			struct helium_type *type;
			size_t i;

			if (!have_module) {
				format_error(error,
					     "%s: export before module declaration",
					     path);
				goto fail;
			}

			name = xstrdup(lex.current.text);
			ifc_lexer_next(&lex);

			if (lex.current.kind == IFC_TOK_LT) {
				ifc_lexer_next(&lex);
				while (lex.current.kind == IFC_TOK_IDENT) {
					append_string(&params, &pcount, &pcap,
						      xstrdup(lex.current.text));
					ifc_lexer_next(&lex);
					if (lex.current.kind == IFC_TOK_COMMA) {
						ifc_lexer_next(&lex);
						continue;
					}
					break;
				}
				if (lex.current.kind != IFC_TOK_GT) {
					format_error(error, "%s: expected '>'",
						     path);
					free(name);
					for (i = 0; i < pcount; i++)
						free(params[i]);
					free(params);
					goto fail;
				}
				ifc_lexer_next(&lex);
			}

			if (lex.current.kind != IFC_TOK_COLON) {
				format_error(error, "%s: expected ':'", path);
				free(name);
				for (i = 0; i < pcount; i++)
					free(params[i]);
				free(params);
				goto fail;
			}
			ifc_lexer_next(&lex);

			lex.type_params = params;
			lex.type_param_count = pcount;
			type = ifc_parse_type(&lex, error);
			lex.type_params = NULL;
			lex.type_param_count = 0;
			if (!type) {
				free(name);
				for (i = 0; i < pcount; i++)
					free(params[i]);
				free(params);
				goto fail;
			}
			helium_module_interface_add_export(iface, name, params,
							   pcount, type);
			helium_type_free(type);
			free(name);
			for (i = 0; i < pcount; i++)
				free(params[i]);
			free(params);
			continue;
		}

		format_error(error, "%s: unexpected token", path);
		goto fail;
	}
	ifc_lexer_free(&lex);

	free(buf);
	if (!have_module) {
		format_error(error, "%s: missing module declaration", path);
		helium_module_interface_free(iface);
		return NULL;
	}
	return iface;

fail:
	ifc_lexer_free(&lex);
	free(buf);
	helium_module_interface_free(iface);
	return NULL;
}

int helium_module_interface_emit(struct helium_module *module,
				 struct helium_env *env,
				 const char *path, char **error)
{
	FILE *f;
	size_t i;
	const char *module_name;

	f = fopen(path, "w");
	if (!f) {
		format_error(error, "%s: %m", path);
		return -1;
	}

	module_name = module->name ? module->name : "main";
	fprintf(f, "module %s\n", module_name);

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];
		struct helium_scheme *scheme;
		char *type_str;
		const char *name = NULL;

		if (decl->kind == HELIUM_DECL_FOREIGN && decl->u.foreign.injected)
			continue;

		if (decl->kind == HELIUM_DECL_BINDING)
			name = decl->u.binding->name;
		else if (decl->kind == HELIUM_DECL_FOREIGN)
			name = decl->u.foreign.name;
		else
			continue;

		scheme = helium_env_lookup(env, name);
		if (!scheme) {
			format_error(error, "%s: no type for '%s'", path, name);
			fclose(f);
			return -1;
		}

		type_str = ifc_type_to_string(scheme->type);
		if (!type_str) {
			format_error(error, "%s: failed to serialize type for '%s'",
				     path, name);
			fclose(f);
			return -1;
		}
		fprintf(f, "%s", name);
		if (scheme->var_count > 0) {
			size_t j;

			fprintf(f, "<");
			for (j = 0; j < scheme->var_count; j++) {
				if (j)
					fprintf(f, ", ");
				fprintf(f, "%s", scheme->vars[j]);
			}
			fprintf(f, ">");
		}
		fprintf(f, " : %s\n", type_str);
		free(type_str);
	}

	fclose(f);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Search paths and resolution                                                */
/* -------------------------------------------------------------------------- */

struct helium_search_path *helium_search_path_new(void)
{
	return xalloc(sizeof(struct helium_search_path));
}

void helium_search_path_free(struct helium_search_path *sp)
{
	size_t i;

	if (!sp)
		return;
	for (i = 0; i < sp->count; i++)
		free(sp->roots[i]);
	free(sp->roots);
	free(sp);
}

void helium_search_path_add(struct helium_search_path *sp, const char *root)
{
	append_ptr((void ***)&sp->roots, &sp->count, &sp->capacity,
		   xstrdup(root));
}

static int file_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *path_join(const char *a, const char *b)
{
	char *out;
	size_t len_a = strlen(a);
	int need_sep = len_a > 0 && a[len_a - 1] != '/';

	if (asprintf(&out, "%s%s%s", a, need_sep ? "/" : "", b) < 0)
		abort();
	return out;
}

/*
 * Convert an import path "a.b.c" to a relative file path "a/b/c.hel".
 */
static char *import_path_to_file(const char *import_path)
{
	char *rel;
	size_t len;
	size_t i;

	len = strlen(import_path);
	rel = xalloc(len + 5);
	memcpy(rel, import_path, len + 1);
	for (i = 0; i < len; i++) {
		if (rel[i] == '.')
			rel[i] = '/';
	}
	strcat(rel, ".hel");
	return rel;
}

/*
 * If @root looks like a .helium/<package>/<version>/ directory, return the
 * package name.  This lets module resolution strip the package prefix from
 * imports such as "std.io" when searching inside the package cache.
 */
static char *package_root_prefix(const char *root)
{
	const char *helium = strstr(root, "/.helium/");
	const char *pkg_start;
	const char *slash;
	const char *ver_start;
	const char *ver_end;

	if (!helium)
		helium = root;
	if (*helium == '/')
		helium++;
	if (strncmp(helium, ".helium/", 8) != 0)
		return NULL;
	pkg_start = helium + 8;
	slash = strchr(pkg_start, '/');
	if (!slash)
		return NULL;
	ver_start = slash + 1;
	ver_end = strchr(ver_start, '/');
	if (ver_end)
		return NULL;
	return strndup(pkg_start, slash - pkg_start);
}

/*
 * Return the last component of an import path as the namespace prefix.
 */
static char *import_path_prefix(const char *import_path)
{
	const char *p = strrchr(import_path, '.');

	if (p)
		return xstrdup(p + 1);
	return xstrdup(import_path);
}

int helium_module_resolve(const char *import_path,
				  struct helium_search_path *sp,
				  char **source_path,
				  char **module_name,
				  char **error)
{
	char *rel;
	char *prefix;
	size_t i;

	rel = import_path_to_file(import_path);
	prefix = import_path_prefix(import_path);

	for (i = 0; i < sp->count; i++) {
		const char *root = sp->roots[i];
		char *candidate = path_join(root, rel);
		char *pkg;

		if (file_exists(candidate)) {
			*source_path = candidate;
			*module_name = xstrdup(prefix);
			free(prefix);
			free(rel);
			return 0;
		}
		free(candidate);

		/*
		 * If the search root is a .helium/<pkg>/<version>/ cache directory,
		 * also try resolving imports of the form <pkg>.<name> directly as
		 * <name>.hel inside that directory.
		 */
		pkg = package_root_prefix(root);
		if (pkg) {
			size_t pkg_len = strlen(pkg);

			if (strncmp(import_path, pkg, pkg_len) == 0 &&
			    import_path[pkg_len] == '.') {
				char *stripped = import_path_to_file(import_path + pkg_len + 1);
				char *candidate2 = path_join(root, stripped);

				if (file_exists(candidate2)) {
					*source_path = candidate2;
					*module_name = xstrdup(prefix);
					free(pkg);
					free(stripped);
					free(prefix);
					free(rel);
					return 0;
				}
				free(candidate2);
				free(stripped);
			}
			free(pkg);
		}
	}

	format_error(error, "module not found: %s", import_path);
	free(prefix);
	free(rel);
	return -1;
}

static void add_cache_versions(struct helium_search_path *sp,
			       const char *cache_root)
{
	DIR *d;
	struct dirent *ent;

	d = opendir(cache_root);
	if (!d)
		return;

	while ((ent = readdir(d)) != NULL) {
		char *pkg_dir;
		DIR *pd;
		struct dirent *vent;

		if (ent->d_name[0] == '.')
			continue;
		if (asprintf(&pkg_dir, "%s/%s", cache_root, ent->d_name) < 0)
			abort();
		pd = opendir(pkg_dir);
		if (!pd) {
			free(pkg_dir);
			continue;
		}
		while ((vent = readdir(pd)) != NULL) {
			char *ver_dir;

			if (vent->d_name[0] == '.')
				continue;
			if (asprintf(&ver_dir, "%s/%s", pkg_dir, vent->d_name) < 0)
				abort();
			helium_search_path_add(sp, ver_dir);
			free(ver_dir);
		}
		closedir(pd);
		free(pkg_dir);
	}
	closedir(d);
}

struct helium_search_path *helium_search_path_for_project(
						const char *source_path, char **error)
{
	struct helium_search_path *sp;
	char *root;
	char *cache_root;
	const char *last_slash;

	(void)error;

	sp = helium_search_path_new();

	/*
	 * The source file's own directory is always searched so that relative
	 * imports within the same directory work without extra flags.
	 */
	last_slash = strrchr(source_path, '/');
	if (last_slash)
		root = strndup(source_path, last_slash - source_path);
	else
		root = xstrdup(".");
	helium_search_path_add(sp, root);

	/*
	 * Cached dependencies and locally-installed libraries live under the
	 * project's .helium/<name>/<version>/ directories.  The compiler does not
	 * walk ancestor directories or implicitly add cwd/lib.
	 */
	cache_root = path_join(root, ".helium");
	add_cache_versions(sp, cache_root);
	free(cache_root);

	free(root);
	return sp;
}

/* -------------------------------------------------------------------------- */
/* Import context                                                             */
/* -------------------------------------------------------------------------- */

struct helium_import_context *helium_import_context_new(void)
{
	return xalloc(sizeof(struct helium_import_context));
}

void helium_import_context_free(struct helium_import_context *ctx)
{
	size_t i;

	if (!ctx)
		return;
	for (i = 0; i < ctx->count; i++) {
		struct helium_import_info *info = ctx->imports[i];

		free(info->path);
		free(info->prefix);
		free(info->source_path);
		free(info->object_path);
		free(info->interface_path);
		helium_module_interface_free(info->iface);
		free(info);
	}
	free(ctx->imports);
	for (i = 0; i < ctx->object_count; i++)
		free(ctx->object_paths[i]);
	free(ctx->object_paths);
	free(ctx);
}

struct helium_import_info *helium_import_context_add(
				struct helium_import_context *ctx,
				const char *path,
				const char *prefix,
				const char *source_path,
				const char *object_path,
				const char *interface_path)
{
	struct helium_import_info *info = xalloc(sizeof(*info));

	info->path = xstrdup(path);
	info->prefix = xstrdup(prefix);
	info->source_path = xstrdup(source_path);
	info->object_path = xstrdup(object_path);
	info->interface_path = xstrdup(interface_path);
	info->iface = NULL;
	append_ptr((void ***)&ctx->imports, &ctx->count, &ctx->capacity, info);
	if (object_path)
		append_ptr((void ***)&ctx->object_paths, &ctx->object_count,
			   &ctx->object_capacity, xstrdup(object_path));
	return info;
}

void helium_import_context_add_object(struct helium_import_context *ctx,
				      const char *object_path)
{
	size_t i;

	for (i = 0; i < ctx->object_count; i++) {
		if (strcmp(ctx->object_paths[i], object_path) == 0)
			return;
	}
	append_ptr((void ***)&ctx->object_paths, &ctx->object_count,
		   &ctx->object_capacity, xstrdup(object_path));
}

struct helium_import_info *helium_import_context_lookup(
				struct helium_import_context *ctx,
				const char *prefix)
{
	size_t i;

	if (!ctx)
		return NULL;
	for (i = 0; i < ctx->count; i++) {
		if (strcmp(ctx->imports[i]->prefix, prefix) == 0)
			return ctx->imports[i];
	}
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* AST rewriting                                                              */
/* -------------------------------------------------------------------------- */

static void collect_module_binding_names(struct helium_module *module,
					 char ***names, size_t *count,
					 size_t *cap)
{
	size_t i;

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];
		const char *name = NULL;

		if (decl->kind == HELIUM_DECL_FOREIGN && decl->u.foreign.injected)
			continue;

		if (decl->kind == HELIUM_DECL_BINDING)
			name = decl->u.binding->name;
		else if (decl->kind == HELIUM_DECL_FOREIGN)
			name = decl->u.foreign.name;
		if (name)
			append_ptr((void ***)names, count, cap, xstrdup(name));
	}
}

static int string_in_list(const char *s, char **list, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (strcmp(list[i], s) == 0)
			return 1;
	}
	return 0;
}

static char *prefix_name(const char *prefix, const char *name)
{
	char *out;

	if (asprintf(&out, "%s_%s", prefix, name) < 0)
		abort();
	return out;
}

static void prefix_expr(struct helium_expr *expr, char **names,
			const char *prefix, size_t name_count);

static void prefix_exprs(struct helium_expr **exprs, size_t count,
			 char **names, const char *prefix, size_t name_count)
{
	size_t i;

	for (i = 0; i < count; i++)
		prefix_expr(exprs[i], names, prefix, name_count);
}

static void prefix_expr(struct helium_expr *expr, char **names,
			const char *prefix, size_t name_count)
{
	size_t i;

	if (!expr)
		return;

	switch (expr->kind) {
	case HELIUM_EXPR_IDENT:
		if (string_in_list(expr->u.ident.name, names, name_count)) {
			char *new_name = prefix_name(prefix, expr->u.ident.name);

			free(expr->u.ident.name);
			expr->u.ident.name = new_name;
		}
		break;
	case HELIUM_EXPR_BINARY:
		prefix_expr(expr->u.binary.left, names, prefix, name_count);
		prefix_expr(expr->u.binary.right, names, prefix, name_count);
		break;
	case HELIUM_EXPR_UNARY:
		prefix_expr(expr->u.unary.operand, names, prefix, name_count);
		break;
	case HELIUM_EXPR_CALL:
		prefix_expr(expr->u.call.func, names, prefix, name_count);
		prefix_exprs(expr->u.call.args, expr->u.call.arg_count,
			     names, prefix, name_count);
		break;
	case HELIUM_EXPR_BLOCK: {
		for (i = 0; i < expr->u.block.binding_count; i++)
			prefix_expr(expr->u.block.bindings[i]->value, names,
				    prefix, name_count);
		prefix_exprs(expr->u.block.exprs, expr->u.block.expr_count,
			     names, prefix, name_count);
		break;
	}
	case HELIUM_EXPR_IF:
		prefix_expr(expr->u.if_expr.cond, names, prefix, name_count);
		prefix_expr(expr->u.if_expr.then_branch, names, prefix, name_count);
		prefix_expr(expr->u.if_expr.else_branch, names, prefix, name_count);
		break;
	case HELIUM_EXPR_MATCH:
		prefix_expr(expr->u.match.value, names, prefix, name_count);
		for (i = 0; i < expr->u.match.arm_count; i++)
			prefix_expr(expr->u.match.arms[i]->expr, names, prefix,
				    name_count);
		break;
	case HELIUM_EXPR_LOOP:
		for (i = 0; i < expr->u.loop.binding_count; i++)
			prefix_expr(expr->u.loop.bindings[i]->init, names,
				    prefix, name_count);
		prefix_expr(expr->u.loop.body, names, prefix, name_count);
		break;
	case HELIUM_EXPR_RECUR:
		prefix_exprs(expr->u.recur.args, expr->u.recur.arg_count,
			     names, prefix, name_count);
		break;
	case HELIUM_EXPR_LAMBDA:
		prefix_expr(expr->u.lambda.body, names, prefix, name_count);
		break;
	case HELIUM_EXPR_RECORD_LIT:
		for (i = 0; i < expr->u.record_lit.field_count; i++)
			prefix_expr(expr->u.record_lit.fields[i]->value, names,
				    prefix, name_count);
		break;
	case HELIUM_EXPR_ARRAY_LIT:
		prefix_exprs(expr->u.array_lit.items, expr->u.array_lit.item_count,
			     names, prefix, name_count);
		break;
	case HELIUM_EXPR_FIELD:
		prefix_expr(expr->u.field.object, names, prefix, name_count);
		break;
	case HELIUM_EXPR_ANNOT:
		prefix_expr(expr->u.annot.expr, names, prefix, name_count);
		break;
	case HELIUM_EXPR_BIND:
		prefix_expr(expr->u.bind.left, names, prefix, name_count);
		prefix_expr(expr->u.bind.right, names, prefix, name_count);
		break;
	case HELIUM_EXPR_RETURN:
		prefix_expr(expr->u.ret.expr, names, prefix, name_count);
		break;
	case HELIUM_EXPR_FSTRING:
		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *p = expr->u.fstring.parts[i];

			if (p->is_expr)
				prefix_expr(p->u.expr, names, prefix, name_count);
		}
		break;
	default:
		break;
	}
}

void helium_prefix_module_names(struct helium_module *module,
				const char *module_name)
{
	char **names = NULL;
	size_t name_count = 0;
	size_t name_cap = 0;
	size_t i;

	collect_module_binding_names(module, &names, &name_count, &name_cap);

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		if (decl->kind == HELIUM_DECL_BINDING) {
			char *new_name = prefix_name(module_name,
						     decl->u.binding->name);

			free(decl->u.binding->name);
			decl->u.binding->name = new_name;
		} else if (decl->kind == HELIUM_DECL_FOREIGN &&
			   !decl->u.foreign.injected) {
			char *new_name = prefix_name(module_name,
						     decl->u.foreign.name);

			free(decl->u.foreign.name);
			decl->u.foreign.name = new_name;
		}
	}

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		if (decl->kind == HELIUM_DECL_BINDING)
			prefix_expr(decl->u.binding->value, names, module_name,
				    name_count);
	}

	for (i = 0; i < name_count; i++)
		free(names[i]);
	free(names);
}

/* -------------------------------------------------------------------------- */
/* Qualified access rewriting                                                 */
/* -------------------------------------------------------------------------- */

static void rewrite_qualified_expr(struct helium_expr *expr,
				   struct helium_import_context *ctx);

static void rewrite_qualified_exprs(struct helium_expr **exprs, size_t count,
				    struct helium_import_context *ctx)
{
	size_t i;

	for (i = 0; i < count; i++)
		rewrite_qualified_expr(exprs[i], ctx);
}

static void rewrite_qualified_expr(struct helium_expr *expr,
				   struct helium_import_context *ctx)
{
	size_t i;

	if (!expr)
		return;

	if (expr->kind == HELIUM_EXPR_FIELD &&
	    expr->u.field.object &&
	    expr->u.field.object->kind == HELIUM_EXPR_IDENT) {
		struct helium_import_info *info;

		info = helium_import_context_lookup(ctx,
					    expr->u.field.object->u.ident.name);
		if (info) {
			char *new_name = prefix_name(info->prefix,
						     expr->u.field.name);

			free(expr->u.field.object);
			free(expr->u.field.name);
			expr->kind = HELIUM_EXPR_IDENT;
			expr->u.ident.name = new_name;
			return;
		}
	}

	switch (expr->kind) {
	case HELIUM_EXPR_BINARY:
		rewrite_qualified_expr(expr->u.binary.left, ctx);
		rewrite_qualified_expr(expr->u.binary.right, ctx);
		break;
	case HELIUM_EXPR_UNARY:
		rewrite_qualified_expr(expr->u.unary.operand, ctx);
		break;
	case HELIUM_EXPR_CALL:
		rewrite_qualified_expr(expr->u.call.func, ctx);
		rewrite_qualified_exprs(expr->u.call.args, expr->u.call.arg_count,
					ctx);
		break;
	case HELIUM_EXPR_BLOCK: {
		for (i = 0; i < expr->u.block.binding_count; i++)
			rewrite_qualified_expr(expr->u.block.bindings[i]->value,
					       ctx);
		rewrite_qualified_exprs(expr->u.block.exprs,
					expr->u.block.expr_count, ctx);
		break;
	}
	case HELIUM_EXPR_IF:
		rewrite_qualified_expr(expr->u.if_expr.cond, ctx);
		rewrite_qualified_expr(expr->u.if_expr.then_branch, ctx);
		rewrite_qualified_expr(expr->u.if_expr.else_branch, ctx);
		break;
	case HELIUM_EXPR_MATCH:
		rewrite_qualified_expr(expr->u.match.value, ctx);
		for (i = 0; i < expr->u.match.arm_count; i++)
			rewrite_qualified_expr(expr->u.match.arms[i]->expr, ctx);
		break;
	case HELIUM_EXPR_LOOP:
		for (i = 0; i < expr->u.loop.binding_count; i++)
			rewrite_qualified_expr(expr->u.loop.bindings[i]->init, ctx);
		rewrite_qualified_expr(expr->u.loop.body, ctx);
		break;
	case HELIUM_EXPR_RECUR:
		rewrite_qualified_exprs(expr->u.recur.args,
					expr->u.recur.arg_count, ctx);
		break;
	case HELIUM_EXPR_LAMBDA:
		rewrite_qualified_expr(expr->u.lambda.body, ctx);
		break;
	case HELIUM_EXPR_RECORD_LIT:
		for (i = 0; i < expr->u.record_lit.field_count; i++)
			rewrite_qualified_expr(expr->u.record_lit.fields[i]->value,
					       ctx);
		break;
	case HELIUM_EXPR_ARRAY_LIT:
		rewrite_qualified_exprs(expr->u.array_lit.items,
					expr->u.array_lit.item_count, ctx);
		break;
	case HELIUM_EXPR_FIELD:
		rewrite_qualified_expr(expr->u.field.object, ctx);
		break;
	case HELIUM_EXPR_ANNOT:
		rewrite_qualified_expr(expr->u.annot.expr, ctx);
		break;
	case HELIUM_EXPR_BIND:
		rewrite_qualified_expr(expr->u.bind.left, ctx);
		rewrite_qualified_expr(expr->u.bind.right, ctx);
		break;
	case HELIUM_EXPR_RETURN:
		rewrite_qualified_expr(expr->u.ret.expr, ctx);
		break;
	case HELIUM_EXPR_FSTRING:
		for (i = 0; i < expr->u.fstring.part_count; i++) {
			struct helium_fstring_part *p = expr->u.fstring.parts[i];

			if (p->is_expr)
				rewrite_qualified_expr(p->u.expr, ctx);
		}
		break;
	default:
		break;
	}
}

void helium_rewrite_qualified_access(struct helium_module *module,
				     struct helium_import_context *ctx)
{
	size_t i;

	for (i = 0; i < module->decl_count; i++) {
		struct helium_top_decl *decl = module->decls[i];

		if (decl->kind == HELIUM_DECL_BINDING)
			rewrite_qualified_expr(decl->u.binding->value, ctx);
	}
}
