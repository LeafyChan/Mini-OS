
#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
XV6="$ROOT/xv6"
OUT="$ROOT/tests/out"
LOG="$OUT/m5_qemu.log"

mkdir -p "$OUT"

PASS=0
FAIL=0

pass() {
    echo "PASS  $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "FAIL  $1"
    FAIL=$((FAIL + 1))
}


# --------------------------------------------------
# 0. Test M5 patch application and idempotency.
# --------------------------------------------------

echo
echo "Testing M5 patcher..."

PATCH_TEST_ROOT="$(mktemp -d)"
PATCH_TEST_XV6="$PATCH_TEST_ROOT/xv6"

cleanup_patch_test() {
    git -C "$XV6" worktree remove --force "$PATCH_TEST_XV6" \
        >/dev/null 2>&1 || true
    rm -rf "$PATCH_TEST_ROOT"
}

trap cleanup_patch_test EXIT

if [[ ! -f "$ROOT/xv6-patches/reference.patch" ||
      ! -f "$ROOT/xv6-patches/apply.py" ]]; then
    fail "M5 patcher files are missing"

elif git -C "$XV6" worktree add --detach "$PATCH_TEST_XV6" fa2ddcb \
        >/dev/null 2>&1; then

    # Apply the patch to a clean xv6 baseline.
    if python3 "$ROOT/xv6-patches/apply.py" \
        --xv6 "$PATCH_TEST_XV6" \
        > "$PATCH_TEST_ROOT/first.log" 2>&1 &&
       grep -q "M5 patch applied successfully" "$PATCH_TEST_ROOT/first.log"; then

        # Save the source state after the first application.
        git -C "$PATCH_TEST_XV6" diff --binary \
            > "$PATCH_TEST_ROOT/before.diff"

        # Apply the patch a second time.
        if python3 "$ROOT/xv6-patches/apply.py" \
            --xv6 "$PATCH_TEST_XV6" \
            > "$PATCH_TEST_ROOT/second.log" 2>&1 &&
           grep -q "M5 patch is already applied" "$PATCH_TEST_ROOT/second.log"; then

            # Verify the second application changed nothing.
            git -C "$PATCH_TEST_XV6" diff --binary \
                > "$PATCH_TEST_ROOT/after.diff"

            if cmp -s "$PATCH_TEST_ROOT/before.diff" \
                      "$PATCH_TEST_ROOT/after.diff"; then
                pass "M5 patch applies cleanly and is idempotent"
            else
                fail "M5 patch changed source during second application"
            fi
        else
            fail "M5 patch failed its second application"
            cat "$PATCH_TEST_ROOT/second.log"
        fi
    else
        fail "M5 patch failed to apply to clean xv6"
        cat "$PATCH_TEST_ROOT/first.log"
    fi
else
    fail "Could not create clean xv6 baseline for patch test"
fi


echo "======================================"
echo "          MiniOS M5 Test Suite"
echo "======================================"

# --------------------------------------------------
# 1. Check syscall numbers are unique.
# --------------------------------------------------

GETYEAR=$(awk '/#define SYS_getyear/ {print $3}' "$XV6/kernel/syscall.h")
GETPINFO=$(awk '/#define SYS_getpinfo/ {print $3}' "$XV6/kernel/syscall.h")
SYNC=$(awk '/#define SYS_sync/ {print $3}' "$XV6/kernel/syscall.h")

if [[ -n "$GETYEAR" && -n "$GETPINFO" && -n "$SYNC" ]] &&
   [[ "$GETYEAR" != "$GETPINFO" &&
      "$GETYEAR" != "$SYNC" &&
      "$GETPINFO" != "$SYNC" ]]; then
    pass "M5 syscall numbers are unique"
else
    fail "M5 syscall numbers are missing or duplicated"
fi

# --------------------------------------------------
# 2. Build xv6.
# --------------------------------------------------

echo
echo "Building xv6..."

if make -C "$XV6" > "$OUT/m5_build.log" 2>&1; then
    pass "xv6 builds successfully"
else
    fail "xv6 build failed"
    echo "See tests/out/m5_build.log"
    tail -30 "$OUT/m5_build.log"
    echo
    echo "$PASS passed, $FAIL failed"
    exit 1
fi

# --------------------------------------------------
# 3-8. Boot QEMU and run xv6 tests.
# --------------------------------------------------

echo
echo "Booting xv6 in QEMU..."

python3 - "$XV6" "$LOG" <<'PYTHON'
import os
import re
import selectors
import subprocess
import sys
import time

xv6, logfile = sys.argv[1], sys.argv[2]

commands = [
    ("getyeartest", r"Kernel says year = 2026"),
    ("getpinfotest", r"getpinfotest: OK"),
    ("ps_xv6", r"PID\s+PPID\s+STATE"),
    ("ps_xv6 spin 500000000", r"PID\s+PPID\s+STATE"),
]

output = ""
results = []
proc = None
selector = None


def read_until(selector, pattern, timeout):
    global output

    deadline = time.monotonic() + timeout
    matcher = re.compile(pattern, re.M)

    while time.monotonic() < deadline:
        clean_output = output.replace("\r", "")

        if matcher.search(clean_output):
            return True

        for key, _ in selector.select(timeout=0.2):
            try:
                data = os.read(key.fd, 4096)
            except OSError:
                return False

            if not data:
                return False

            chunk = data.decode("utf-8", errors="replace")
            output += chunk
            print(chunk, end="", flush=True)

            if matcher.search(output.replace("\r", "")):
                return True

    return False


try:
    proc = subprocess.Popen(
        ["make", "qemu"],
        cwd=xv6,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0
    )

    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)

    # Wait for the initial shell prompt.
    if not read_until(selector, r"(?m)^\$ ?", 45):
        results.append(("xv6 boots and reaches shell", False))
        raise RuntimeError("Timed out waiting for xv6 shell")

    results.append(("xv6 boots and reaches shell", True))

    # Execute commands sequentially.
    for index, (command, expected) in enumerate(commands):
        marker = f"M5_COMMAND_DONE_{index}"

        # Start searching only after this command is sent.
        start = len(output)

        proc.stdin.write(
            (command + "\n" +
             "echo " + marker + "\n").encode()
        )
        proc.stdin.flush()

        marker_pattern = (
            rf"(?m)^\$ ?{re.escape(marker)}\s*$"
        )

        if not read_until(selector, marker_pattern, 120):
            results.append(
                (f"{command} completes", False)
            )
            raise RuntimeError(
                f"Timed out waiting for completion of {command}"
            )

        command_output = output[start:].replace("\r", "")

        # Remove the echoed marker command.
        command_output = command_output.replace(
            "echo " + marker, ""
        )

        # Check expected output.
        success = re.search(
            expected, command_output, re.S
        ) is not None

        results.append(
            (f"{command} produces expected output", success)
        )

        # Runtime check: require a nonzero TICKS value
        # in the process table after the CPU workload.
        if command.startswith("ps_xv6 spin"):
            tick_values = re.findall(
                r"^\s*\d+\s+\d+\s+\w+\s+\d+\s+(\d+)\s+",
                command_output,
                re.M
            )

            nonzero_ticks = any(
                int(value) > 0 for value in tick_values
            )

            results.append(
                ("Runtime ticks charged under load", nonzero_ticks)
            )

except Exception as exc:
    print(f"\nQEMU test error: {exc}")

finally:
    if proc is not None:
        proc.terminate()

        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    with open(logfile, "w", encoding="utf-8") as f:
        f.write(output)

    if selector is not None:
        selector.close()

    if proc is not None and proc.stdout:
        proc.stdout.close()

# --------------------------------------------------
# 9. Check for kernel panic.
# --------------------------------------------------

panic = re.search(
    r"\bpanic:",
    output,
    re.I
) is not None

results.append(("No kernel panic during tests", not panic))

for description, success in results:
    print(
        ("PASS  " if success else "FAIL  ") + description
    )

failed = sum(
    not success for _, success in results
)

print(
    f"\nQEMU checks: {len(results) - failed} passed, {failed} failed"
)
print(f"QEMU log saved to: {logfile}")

sys.exit(1 if failed else 0)
PYTHON

QEMU_STATUS=$?

if [[ "$QEMU_STATUS" -eq 0 ]]; then
    pass "QEMU boot and runtime tests"
else
    fail "QEMU boot or runtime tests"
fi

# --------------------------------------------------
# Final summary.
# --------------------------------------------------

echo
echo "======================================"
echo "M5 TEST SUMMARY"
echo "======================================"
echo "$PASS passed, $FAIL failed"
echo
echo "QEMU log: tests/out/m5_qemu.log"
echo "Build log: tests/out/m5_build.log"
echo

[[ "$FAIL" -eq 0 ]]
