
#!/usr/bin/env bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

echo "======================================"
echo "       MiniOS Complete Test Suite"
echo "======================================"

# Build the parent MiniOS project first.
echo
echo "Building MiniOS..."

if ! make; then
    echo "FAIL: MiniOS build failed."
    exit 1
fi

echo "PASS: MiniOS build successful"

# Run all module test suites.
rc=0

TESTS=(
    tests/test_m1_m2.sh
    tests/test_m3.sh
    tests/test_m4.sh
    tests/test_m5.sh
    tests/test_integration.sh
)

for test in "${TESTS[@]}"; do
    echo
    echo "======================================"
    echo "Running: $test"
    echo "======================================"

    if bash "$test"; then
        echo "PASS: $test"
    else
        echo "FAIL: $test"
        rc=1
    fi
done

echo
echo "======================================"
echo "       COMPLETE TEST SUMMARY"
echo "======================================"

if [[ "$rc" -eq 0 ]]; then
    echo "ALL TEST SUITES PASSED"
else
    echo "SOME TEST SUITES FAILED"
fi

exit "$rc"
