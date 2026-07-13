#!/bin/bash
# Runtime test runner for SPEC-005.
#
# Usage: ./run_tests.sh <path-to-test_runtime>

set -e

TEST_BIN="${1:-../../build/test_runtime}"

echo "Running test_runtime..."
"$TEST_BIN"

if command -v valgrind >/dev/null 2>&1; then
	echo "Running valgrind..."
	valgrind --error-exitcode=1 --leak-check=full --quiet "$TEST_BIN"
fi

echo "All runtime tests passed."
