#!/usr/bin/env python3
"""Helium test harness.

Discovers and runs tests under tests/<phase>/good/ and tests/<phase>/bad/.
Good cases must compile (and run) successfully.  Bad cases must fail at the
expected phase with a recognizable error.

The harness is intentionally simple so that `hel test` can invoke it directly.
Because the bootstrap compiler is not yet fully implemented, tests that need a
real compiler are reported as SKIP when only the placeholder driver is present.
"""

import argparse
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional


ROOT = Path(__file__).resolve().parent.parent
TESTS_DIR = ROOT / "tests"
COMPILER = ROOT / "build" / "bin" / "helium"
PROJECT: Optional[Path] = None

# Exit statuses
EXIT_OK = 0
EXIT_FAILURES = 1
EXIT_BAD_ARGS = 2


@dataclass
class Test:
    path: Path
    phase: str
    name: str
    expect: str  # "success" or "error"
    skip: Optional[str] = None
    match: Optional[str] = None
    output: Optional[str] = None
    command: Optional[str] = None


@dataclass
class Result:
    test: Test
    status: str  # PASS, FAIL, SKIP
    message: str = ""
    stdout: str = ""
    stderr: str = ""


def discover_tests(filter_path: Optional[Path] = None,
                   filter_phase: Optional[str] = None) -> list[Test]:
    """Find all .hel and .test files under tests/ and parse their metadata."""
    tests: list[Test] = []

    if filter_path is not None:
        if filter_path.is_file():
            tests.append(parse_test(filter_path))
            return tests
        # Directory filter: fall through to normal discovery limited to that tree.
        base = filter_path
    else:
        base = TESTS_DIR

    for path in sorted(base.rglob("*")):
        if path.suffix not in (".hel", ".test"):
            continue
        if path.name.startswith("_"):
            continue
        test = parse_test(path)
        if filter_phase and test.phase != filter_phase:
            continue
        tests.append(test)

    return tests


def _infer_expect(path: Path) -> str:
    parts = path.parts
    if "bad" in parts:
        return "error"
    if "good" in parts:
        return "success"
    return "success"


def _infer_phase(path: Path) -> str:
    try:
        rel = path.relative_to(TESTS_DIR)
    except ValueError:
        try:
            rel = path.relative_to(PROJECT / "tests")
        except ValueError:
            return "unknown"
    parts = rel.parts
    if len(parts) >= 2:
        return parts[0]
    return "unknown"


def _read_sidecar(path: Path, suffix: str) -> Optional[str]:
    # Sidecar files use the same basename as the test but with .out/.err.
    sidecar = path.with_suffix(suffix)
    if sidecar.exists():
        return sidecar.read_text(encoding="utf-8")
    return None


def _parse_metadata_lines(lines: Iterable[str],
                          prefix: str) -> dict[str, str]:
    meta: dict[str, str] = {}
    for raw in lines:
        line = raw.strip()
        if not line.startswith(prefix):
            break
        content = line[len(prefix):].strip()
        if not content.startswith("@"):
            break
        content = content[1:]
        if " " in content:
            key, value = content.split(" ", 1)
        else:
            key, value = content, ""
        meta[key.strip()] = value.strip()
    return meta


def parse_test(path: Path) -> Test:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()

    if path.suffix == ".hel":
        meta = _parse_metadata_lines(lines, "//")
    else:
        meta = _parse_metadata_lines(lines, "#")

    phase = meta.get("phase", _infer_phase(path))
    name = meta.get("name", path.stem)
    expect = meta.get("expect", _infer_expect(path))
    skip = meta.get("skip")
    match = meta.get("match") or _read_sidecar(path, ".err")
    output = meta.get("output") or _read_sidecar(path, ".out")
    command = meta.get("command")
    return Test(
        path=path,
        phase=phase,
        name=name,
        expect=expect,
        skip=skip,
        match=match,
        output=output,
        command=command,
    )


def _compiler_is_placeholder(proc: subprocess.CompletedProcess) -> bool:
    """Detect the bootstrap placeholder driver that always prints a banner."""
    combined = (proc.stdout or "") + (proc.stderr or "")
    return "bootstrap" in combined.lower()


def _run_command(cmd: list[str], cwd: Optional[Path] = None,
                 timeout: int = 30) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        cwd=cwd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def _compiler_repo_root(compiler: Path) -> Path:
    """Return the directory that contains src/runtime for the compiler."""
    resolved = compiler.resolve()
    # build/bin/helium -> build/bin -> build -> repo root
    return resolved.parent.parent.parent


def _normalize_output(text: str) -> str:
    return text.rstrip("\n").replace("\r\n", "\n")


def _matches(value: Optional[str], pattern: Optional[str]) -> bool:
    if pattern is None:
        return True
    if value is None:
        return False
    try:
        return re.search(pattern, value, re.MULTILINE) is not None
    except re.error as exc:
        raise ValueError(f"bad regex {pattern!r}: {exc}") from exc


def run_hel_test(test: Test) -> Result:
    cmd = shlex.split(test.command) if test.command else ["hel", "test"]
    proc = _run_command([str(x) for x in cmd], cwd=PROJECT or ROOT)

    if _compiler_is_placeholder(proc):
        return Result(test, "SKIP",
                      "hel package manager is still a bootstrap placeholder")

    return _check_command_result(test, proc)


def _check_command_result(test: Test,
                          proc: subprocess.CompletedProcess) -> Result:
    if test.expect == "success":
        if proc.returncode != 0:
            return Result(test, "FAIL", f"command exited {proc.returncode}",
                          proc.stdout, proc.stderr)
        if test.output is not None:
            actual = _normalize_output(proc.stdout)
            expected = _normalize_output(test.output)
            if actual != expected:
                return Result(
                    test, "FAIL",
                    f"output mismatch:\nexpected: {expected!r}\n"
                    f"actual:   {actual!r}",
                    proc.stdout, proc.stderr)
        if test.match is not None and not _matches(proc.stdout, test.match):
            return Result(
                test, "FAIL",
                f"stdout did not match /{test.match}/",
                proc.stdout, proc.stderr)
        return Result(test, "PASS", "", proc.stdout, proc.stderr)

    # Expected failure.
    if proc.returncode == 0:
        return Result(test, "FAIL", "expected failure but command succeeded",
                      proc.stdout, proc.stderr)
    if test.match is not None and not _matches(proc.stderr, test.match):
        return Result(
            test, "FAIL",
            f"stderr did not match /{test.match}/",
            proc.stdout, proc.stderr)
    return Result(test, "PASS", "", proc.stdout, proc.stderr)


def run_helium_test(test: Test) -> Result:
    if not COMPILER.exists():
        return Result(test, "SKIP", f"compiler not found: {COMPILER}")

    if test.command:
        cmd = [str(x) for x in shlex.split(test.command)]
        proc = _run_command(cmd, cwd=ROOT)
        if _compiler_is_placeholder(proc):
            return Result(test, "SKIP",
                          "compiler is still a bootstrap placeholder")
        return _check_command_result(test, proc)

    # Normal compile-and-run flow.
    # Run the compiler from its own repo root so that the hard-coded
    # src/runtime/helium_runtime.c path is found.
    with tempfile.TemporaryDirectory(prefix="helium_test_") as tmp:
        binary = Path(tmp) / "a.out"
        repo = _compiler_repo_root(COMPILER)
        compile_cmd = [
            str(COMPILER), str(test.path.resolve()), "-o", str(binary)
        ]
        compile_cmd.extend(["-I", str(repo / "lib")])
        test_lib = test.path.parent / "lib"
        if test_lib.is_dir():
            compile_cmd.extend(["-I", str(test_lib)])
        proc = _run_command(compile_cmd, cwd=repo)

        if _compiler_is_placeholder(proc):
            return Result(test, "SKIP",
                          "compiler is still a bootstrap placeholder")

        if test.expect == "success":
            if proc.returncode != 0:
                return Result(test, "FAIL",
                              f"compile failed with exit {proc.returncode}",
                              proc.stdout, proc.stderr)
            run_proc = _run_command([str(binary)], cwd=PROJECT or ROOT)
            return _check_command_result(test, run_proc)

        # Expected compile failure.
        if proc.returncode == 0:
            return Result(test, "FAIL",
                          "expected compile failure but it succeeded",
                          proc.stdout, proc.stderr)
        if test.match is not None and not _matches(proc.stderr, test.match):
            return Result(
                test, "FAIL",
                f"stderr did not match /{test.match}/",
                proc.stdout, proc.stderr)
        return Result(test, "PASS", "", proc.stdout, proc.stderr)


# Phases that have their own dedicated test binaries/harnesses.  The general
# harness is for end-to-end compiler/codegen tests, so it skips these.
_DEDICATED_PHASES = {"lexer", "parser", "type", "mono"}


def run_test(test: Test) -> Result:
    if test.skip:
        return Result(test, "SKIP", test.skip)

    if test.phase in _DEDICATED_PHASES:
        return Result(test, "SKIP",
                      "covered by dedicated phase harness")

    if test.path.suffix == ".test":
        return run_hel_test(test)

    return run_helium_test(test)


def report(results: list[Result], verbose: bool) -> int:
    counts = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    for result in results:
        counts[result.status] += 1
        label = result.status
        print(f"[{label}] {result.test.phase}/{result.test.name}")
        if result.status == "FAIL" or (verbose and result.message):
            if result.message:
                for line in result.message.splitlines():
                    print(f"       {line}")
            if verbose and result.stdout:
                print("       --- stdout ---")
                for line in result.stdout.splitlines():
                    print(f"       {line}")
            if verbose and result.stderr:
                print("       --- stderr ---")
                for line in result.stderr.splitlines():
                    print(f"       {line}")

    print()
    print(f"Total:  {len(results)}")
    print(f"Passed: {counts['PASS']}")
    print(f"Failed: {counts['FAIL']}")
    print(f"Skipped: {counts['SKIP']}")

    return EXIT_FAILURES if counts["FAIL"] else EXIT_OK


def main(argv: list[str]) -> int:
    global COMPILER
    parser = argparse.ArgumentParser(
        description="Helium test harness",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                  run all tests
  %(prog)s tests/lexer      run only lexer tests
  %(prog)s tests/parser/good/simple_function.hel  run a single test
  %(prog)s --list           list discovered tests
""")
    parser.add_argument("path", nargs="?",
                        help="test file or directory to run")
    parser.add_argument("--phase", dest="phase",
                        help="run only tests in the given phase")
    parser.add_argument("--list", action="store_true",
                        help="list discovered tests and exit")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="show stdout/stderr for every test")
    parser.add_argument("--compiler", default=str(COMPILER),
                        help="path to the helium compiler")
    parser.add_argument("--project", default=None,
                        help="project directory for user-project tests")
    args = parser.parse_args(argv)

    COMPILER = Path(args.compiler)

    if args.project:
        global PROJECT
        global TESTS_DIR
        PROJECT = Path(args.project).resolve()
        TESTS_DIR = PROJECT / "tests"
    else:
        TESTS_DIR = ROOT / "tests"

    if args.path:
        filter_path = Path(args.path).resolve()
    else:
        filter_path = None

    tests = discover_tests(filter_path, args.phase)

    if args.list:
        for test in tests:
            skip = f" [skip: {test.skip}]" if test.skip else ""
            print(f"{test.phase:10} {test.expect:7} {test.path}{skip}")
        return EXIT_OK

    if not tests:
        print("No tests discovered.")
        return EXIT_OK

    results = [run_test(t) for t in tests]
    return report(results, args.verbose)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
