ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
PASS=0
FAIL=0

check() {
    desc=$1; pat=$2; shift 2
    out=$("$@" 2>&1)
    if printf '%s' "$out" | grep -qE "$pat"; then
        printf 'PASS  %s\n' "$desc"
        PASS=$((PASS + 1))
    else
        printf 'FAIL  %s\n' "$desc"
        printf '      wanted /%s/\n' "$pat"
        printf '      got   : %s\n' "$(printf '%s' "$out" | head -3)"
        FAIL=$((FAIL + 1))
    fi
}

check_silent() {
    desc=$1; shift
    out=$("$@" 2>&1); rc=$?
    if [ -z "$out" ] && [ "$rc" -eq 0 ]; then
        printf 'PASS  %s\n' "$desc"
        PASS=$((PASS + 1))
    else
        printf 'FAIL  %s  (rc=%d, output: %s)\n' "$desc" "$rc" "$out"
        FAIL=$((FAIL + 1))
    fi
}

[ -x ./minios ] || { echo "build first: make"; exit 1; }

echo "=== M1 — shell and /proc ==="
check "help lists both modules"      "Module 1.*"        sh -c './minios -c help'
check "help shows M2 section"        "Module 2"          sh -c './minios -c help'
check "help <cmd> shows usage"       "usage: system"     ./minios -c "help system"
check "unknown command is rejected"  "Unknown command"   ./minios -c "notacommand"
check_silent "blank line does nothing"        ./minios -c "   "
check_silent "a # comment does nothing"       ./minios -c "# note to self"
check "system prints uptime"         "Uptime"            ./minios -c "system"
check "system prints memory"         "Memory"            ./minios -c "system"
check "system prints load average"   "Load avg"          ./minios -c "system"
check "system prints our own PID"    "MiniOS PID"        ./minios -c "system"
check "cpu prints a per-core table"  "cpu0"              ./minios -c "cpu 200"
check "cpu rejects a silly sample"   "usage: cpu"        ./minios -c "cpu 999999"
check "top renders a frame"          "MiniOS top"        ./minios -c "top 0.3 1"
check "top shows the CPU bar"        "CPU all"           ./minios -c "top 0.3 1"
check "top lists MiniOS itself"      "<- MiniOS"         ./minios -c "top 0.3 1"
check "version flag works"           "MiniOS 2"          ./minios --version

echo
echo "=== M2 — fork, exec, wait ==="
check "run executes a program"       "hello"             ./minios -c "run echo hello"
check "run reports exit status 0"    "status 0"          ./minios -c "run /bin/true"
check "run reports a failure"        "status 1"          ./minios -c "run /bin/false"
check "missing program gives 127"    "status 127"        ./minios -c "run definitelynotreal"
check "run times the child"          "after .* ms"       ./minios -c "run /bin/true"
check "run with no argument errors"  "usage: run"        ./minios -c "run"
check "processes prints a table"     "PID.*PPID.*STATE"  ./minios -c "processes -n 3"
check "processes counts the total"   "[0-9]+ processes"  ./minios -c "processes -n 3"
check "processes -m sorts by memory" "RSS"               ./minios -c "processes -m -n 3"
check "processes -t finds only us"   "<- MiniOS"         ./minios -c "processes -t"
check "bad flag is rejected"         "usage: processes"  ./minios -c "processes -z"

echo
echo "=== integration — the two modules actually touching ==="
check "top uses M2's proc_list"      "COMMAND"           ./minios -c "top 0.3 1"
check "both tables reachable"        "run <program>"     sh -c './minios -c help'
check "script mode runs mixed cmds"  "exited with status" sh -c 'printf "system\nrun /bin/true\n" | ./minios /dev/stdin'

echo
echo "=== handbook example programs ==="
if [ -x build/ex_fork_demo ]; then
  check "fork_demo forks"            "I am the child"    ./build/ex_fork_demo
  check "fork_memory_demo runs"      "Child:  secret=100" ./build/ex_fork_memory_demo
  check "exec_wait_demo execs date"  "child exit status" ./build/ex_exec_wait_demo
  check "uptime_demo reads /proc"    "uptime"            ./build/ex_uptime_demo
else
  echo "SKIP  examples not built (run: make examples)"
fi

echo
echo "------------------------------"
printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] && echo "ALL TESTS PASSED"
exit $((FAIL > 0))
