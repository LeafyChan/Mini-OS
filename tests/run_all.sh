#!/usr/bin/env bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

make -s || { echo "FAIL: build"; exit 1; }

rc=0
for test in tests/test_m1_m2.sh tests/test_m3.sh tests/test_m4.sh tests/test_m5.sh tests/test_integration.sh; do
    echo
    echo "$test"
    if bash "$test"; then
        echo "PASS: $test"
    else
        echo "FAIL: $test"
        rc=1
    fi
done

echo
if [[ "$rc" -eq 0 ]]; then
    echo "ALL TEST SUITES PASSED"
else
    echo "SOME TEST SUITES FAILED"
fi
exit "$rc"
