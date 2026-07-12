#!/bin/bash
# Runtime test runner for SPEC-005.

set -e

cd "$(dirname "$0")/../.."

BUILD_DIR="build"
TEST_BIN="$BUILD_DIR/test_runtime"
RUNTIME_OBJ="$BUILD_DIR/obj/runtime/helium_runtime.o"

CC=${CC:-gcc}
CFLAGS="-Wall -Wextra -std=c11 -O2 -g"

make -s

mkdir -p "$BUILD_DIR"

$CC $CFLAGS -Isrc/runtime tests/runtime/test_runtime.c \
    "$RUNTIME_OBJ" -o "$TEST_BIN"

echo "Running test_runtime..."
"$TEST_BIN"

if command -v valgrind >/dev/null 2>&1; then
	echo "Running valgrind..."
	valgrind --error-exitcode=1 --leak-check=full --quiet "$TEST_BIN"
fi

echo "All runtime tests passed."
