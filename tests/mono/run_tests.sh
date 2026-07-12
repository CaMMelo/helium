#!/bin/bash
# SPDX-License-Identifier: TBD
# run_tests.sh - Run the Helium monomorphization test suite.
#
# Usage: ./run_tests.sh <path-to-mono_test>

set -e

TEST_BIN="${1:-./mono_test}"
PASS=0
FAIL=0

run_good() {
	local src="$1"
	local base="${src%.hel}"
	local exp="${base}.expected"
	local out="${base}.out"

	if ! "$TEST_BIN" "$src" > "$out" 2> "${base}.err"; then
		echo "FAIL: $src (mono error)"
		FAIL=$((FAIL + 1))
		return
	fi

	if [ -e "$exp" ]; then
		if diff -u "$exp" "$out"; then
			echo "PASS: $src"
			PASS=$((PASS + 1))
		else
			echo "FAIL: $src (output mismatch)"
			FAIL=$((FAIL + 1))
		fi
	else
		echo "PASS: $src"
		PASS=$((PASS + 1))
	fi
}

run_bad() {
	local src="$1"
	local base="${src%.hel}"
	local exp="${base}.expected"
	local err="${base}.err"

	if "$TEST_BIN" "$src" > "${base}.out" 2> "$err"; then
		echo "FAIL: $src (expected error)"
		FAIL=$((FAIL + 1))
		return
	fi

	if [ -e "$exp" ]; then
		if grep -qF "$(cat "$exp")" "$err"; then
			echo "PASS: $src"
			PASS=$((PASS + 1))
		else
			echo "FAIL: $src (error mismatch)"
			echo "expected pattern: $(cat "$exp")"
			echo "actual error:"
			cat "$err"
			FAIL=$((FAIL + 1))
		fi
	else
		echo "PASS: $src"
		PASS=$((PASS + 1))
	fi
}

for src in good/*.hel; do
	[ -e "$src" ] || continue
	run_good "$src"
done

for src in bad/*.hel; do
	[ -e "$src" ] || continue
	run_bad "$src"
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
