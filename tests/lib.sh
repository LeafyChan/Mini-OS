ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
PASS=0
FAIL=0
mkdir -p tests/out

check() {
    desc=$1; pat=$2; shift 2
    out=$("$@" 2>&1)
    if printf '%s' "$out" | grep -qE "$pat"; then
        printf 'PASS  %s\n' "$desc"; PASS=$((PASS + 1))
    else
        printf 'FAIL  %s\n      wanted /%s/\n      got   : %s\n' "$desc" "$pat" "$(printf '%s' "$out" | head -3)"
        FAIL=$((FAIL + 1))
    fi
}

check_silent() {
    desc=$1; shift
    out=$("$@" 2>&1); rc=$?
    if [ -z "$out" ] && [ "$rc" -eq 0 ]; then
        printf 'PASS  %s\n' "$desc"; PASS=$((PASS + 1))
    else
        printf 'FAIL  %s  (rc=%d, output: %s)\n' "$desc" "$rc" "$out"; FAIL=$((FAIL + 1))
    fi
}

test_if() {
    desc=$1; shift
    if eval "$*" >/dev/null 2>&1; then
        printf 'PASS  %s\n' "$desc"; PASS=$((PASS + 1))
    else
        printf 'FAIL  %s\n' "$desc"; FAIL=$((FAIL + 1))
    fi
}

summary() {
    printf '%d passed, %d failed\n' "$PASS" "$FAIL"
    [ "$FAIL" -eq 0 ]
}

[ -x ./bin/minios ] || { echo "build first: make"; exit 1; }
