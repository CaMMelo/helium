# Helium bootstrap build system
# Uses the Linux kernel coding style for C code.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -g
LDFLAGS ?=

LLVM_CFLAGS := $(shell llvm-config --cflags 2>/dev/null)
LLVM_LIBS := $(shell llvm-config --libs --ldflags --system-libs 2>/dev/null)

LEX := flex
YACC := bison

BUILD_DIR := build
SRC_DIR := src

LIBHELIUM_DIR := $(SRC_DIR)/libhelium
HELIUM_DIR := $(SRC_DIR)/helium
HEL_DIR := $(SRC_DIR)/hel
RUNTIME_DIR := $(SRC_DIR)/runtime

LEXER_C := $(LIBHELIUM_DIR)/lexer.c
PARSER_C := $(LIBHELIUM_DIR)/parser.tab.c
PARSER_H := $(LIBHELIUM_DIR)/parser.tab.h
LIBHELIUM_SRCS := $(filter-out $(LEXER_C) $(PARSER_C),$(wildcard $(LIBHELIUM_DIR)/*.c))
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

LIBHELIUM_OBJS := $(patsubst $(LIBHELIUM_DIR)/%.c,$(OBJ_DIR)/libhelium/%.o,$(LIBHELIUM_SRCS))
LIBHELIUM_OBJS += $(OBJ_DIR)/libhelium/lexer.o
LIBHELIUM_OBJS += $(OBJ_DIR)/libhelium/parser.tab.o

HELIUM_SRCS := $(wildcard $(HELIUM_DIR)/*.c)
HELIUM_OBJS := $(patsubst $(HELIUM_DIR)/%.c,$(OBJ_DIR)/helium/%.o,$(HELIUM_SRCS))

HEL_SRCS := $(wildcard $(HEL_DIR)/*.c)
HEL_OBJS := $(patsubst $(HEL_DIR)/%.c,$(OBJ_DIR)/hel/%.o,$(HEL_SRCS))

RUNTIME_SRCS := $(wildcard $(RUNTIME_DIR)/*.c)
RUNTIME_OBJS := $(patsubst $(RUNTIME_DIR)/%.c,$(OBJ_DIR)/runtime/%.o,$(RUNTIME_SRCS))

TEST_LEXER_DIR := tests/lexer
TEST_LEXER_BIN := $(BUILD_DIR)/lexer_test
TEST_LEXER_SRC := $(TEST_LEXER_DIR)/lexer_test.c

TEST_PARSER_DIR := tests/parser
TEST_PARSER_BIN := $(BUILD_DIR)/parser_test
TEST_PARSER_SRC := $(TEST_PARSER_DIR)/parser_test.c

TEST_TYPE_DIR := tests/type
TEST_TYPE_BIN := $(BUILD_DIR)/type_test
TEST_TYPE_SRC := $(TEST_TYPE_DIR)/type_test.c

TEST_MONO_DIR := tests/mono
TEST_MONO_BIN := $(BUILD_DIR)/mono_test
TEST_MONO_SRC := $(TEST_MONO_DIR)/mono_test.c

TEST_RUNNER := ./tests/run_tests.py

.PHONY: all clean test

all: $(BIN_DIR)/helium $(BIN_DIR)/hel $(BUILD_DIR)/runtime

# Static library for compiler internals
$(BUILD_DIR)/libhelium.a: $(LIBHELIUM_OBJS)
	@mkdir -p $(BUILD_DIR)
	ar rcs $@ $^

# Compiler driver
$(BIN_DIR)/helium: $(HELIUM_OBJS) $(BUILD_DIR)/libhelium.a
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LLVM_LIBS)

# Package manager
$(BIN_DIR)/hel: $(HEL_OBJS) $(BUILD_DIR)/libhelium.a
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

# Runtime objects
$(BUILD_DIR)/runtime: $(RUNTIME_OBJS)
	@mkdir -p $(BUILD_DIR)/runtime
	@touch $(BUILD_DIR)/runtime

# Generated lexer object
$(OBJ_DIR)/libhelium/lexer.o: $(LEXER_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) -I$(LIBHELIUM_DIR) -c $< -o $@

$(LEXER_C): $(LIBHELIUM_DIR)/lexer.l
	$(LEX) -o $@ $<

# Generated parser object
$(OBJ_DIR)/libhelium/parser.tab.o: $(PARSER_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) -I$(LIBHELIUM_DIR) -c $< -o $@

$(PARSER_C) $(PARSER_H): $(LIBHELIUM_DIR)/parser.y
	$(YACC) -d -o $(PARSER_C) $<

# Generic pattern rules
$(OBJ_DIR)/libhelium/%.o: $(LIBHELIUM_DIR)/%.c $(PARSER_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) -I$(LIBHELIUM_DIR) -c $< -o $@

$(OBJ_DIR)/helium/%.o: $(HELIUM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(LIBHELIUM_DIR) -c $< -o $@

$(OBJ_DIR)/hel/%.o: $(HEL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(LIBHELIUM_DIR) -c $< -o $@

$(OBJ_DIR)/runtime/%.o: $(RUNTIME_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(RUNTIME_DIR) -c $< -o $@

# Lexer test harness
$(TEST_LEXER_BIN): $(TEST_LEXER_SRC) $(BUILD_DIR)/libhelium.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIBHELIUM_DIR) -o $@ $< $(BUILD_DIR)/libhelium.a

# Parser test harness
$(TEST_PARSER_BIN): $(TEST_PARSER_SRC) $(BUILD_DIR)/libhelium.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIBHELIUM_DIR) -o $@ $< $(BUILD_DIR)/libhelium.a

# Type system test harness
$(TEST_TYPE_BIN): $(TEST_TYPE_SRC) $(BUILD_DIR)/libhelium.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIBHELIUM_DIR) -o $@ $< $(BUILD_DIR)/libhelium.a

# Monomorphization test harness
$(TEST_MONO_BIN): $(TEST_MONO_SRC) $(BUILD_DIR)/libhelium.a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIBHELIUM_DIR) -o $@ $< $(BUILD_DIR)/libhelium.a

test: all $(TEST_LEXER_BIN) $(TEST_PARSER_BIN) $(TEST_TYPE_BIN) $(TEST_MONO_BIN)
	@echo "Running lexer tests..."
	@cd $(TEST_LEXER_DIR) && ./run_tests.sh $(abspath $(TEST_LEXER_BIN))
	@echo "Running parser tests..."
	@cd $(TEST_PARSER_DIR) && ./run_tests.sh $(abspath $(TEST_PARSER_BIN))
	@echo "Running type tests..."
	@cd $(TEST_TYPE_DIR) && ./run_tests.sh $(abspath $(TEST_TYPE_BIN))
	@echo "Running mono tests..."
	@cd $(TEST_MONO_DIR) && ./run_tests.sh $(abspath $(TEST_MONO_BIN))
	@echo "Running general test harness..."
	$(TEST_RUNNER)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(LEXER_C)
	rm -f $(PARSER_C) $(PARSER_H)
