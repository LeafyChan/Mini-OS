#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
rc=0
for t in tests/test_m1_m2.sh tests/test_m3.sh tests/test_m4.sh tests/test_integration.sh; do
    sh "$t" || rc=1
    echo
done
[ "$rc" -eq 0 ] && echo "ALL SUITES PASSED"
exit $rc
