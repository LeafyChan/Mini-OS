#!/bin/sh
. "$(dirname "$0")/lib.sh"

echo "m1: shell and /proc"
check "help lists module 1"          "Module 1"        sh -c './bin/minios -c help'
check "help shows M2 section"        "Module 2"          sh -c './bin/minios -c help'
check "help <cmd> shows usage"       "usage: system"     ./bin/minios -c "help system"
check "unknown command is rejected"  "Unknown command"   ./bin/minios -c "notacommand"
check_silent "blank line does nothing"        ./bin/minios -c "   "
check_silent "a # comment does nothing"       ./bin/minios -c "# note to self"
check "system prints uptime"         "Uptime"            ./bin/minios -c "system"
check "system prints memory"         "Memory"            ./bin/minios -c "system"
check "system prints load average"   "Load avg"          ./bin/minios -c "system"
check "system prints our own PID"    "MiniOS PID"        ./bin/minios -c "system"
check "cpu prints a per-core table"  "cpu0"              ./bin/minios -c "cpu 200"
check "cpu rejects a silly sample"   "usage: cpu"        ./bin/minios -c "cpu 999999"
check "top renders a frame"          "MiniOS top"        ./bin/minios -c "top 0.3 1"
check "top shows the CPU bar"        "CPU all"           ./bin/minios -c "top 0.3 1"
check "top lists MiniOS itself"      "<- MiniOS"         ./bin/minios -c "top 0.3 1"
check "version flag works"           "MiniOS 3"          ./bin/minios --version

echo
echo "m2: fork, exec, wait"
check "run executes a program"       "hello"             ./bin/minios -c "run echo hello"
check "run reports exit status 0"    "status 0"          ./bin/minios -c "run /bin/true"
check "run reports a failure"        "status 1"          ./bin/minios -c "run /bin/false"
check "missing program gives 127"    "status 127"        ./bin/minios -c "run definitelynotreal"
check "run times the child"          "after .* ms"       ./bin/minios -c "run /bin/true"
check "run with no argument errors"  "usage: run"        ./bin/minios -c "run"
check "processes prints a table"     "PID.*PPID.*STATE"  ./bin/minios -c "processes -n 3"
check "processes counts the total"   "[0-9]+ processes"  ./bin/minios -c "processes -n 3"
check "processes -m sorts by memory" "RSS"               ./bin/minios -c "processes -m -n 3"
check "processes -t finds only us"   "<- MiniOS"         ./bin/minios -c "processes -t"
check "bad flag is rejected"         "usage: processes"  ./bin/minios -c "processes -z"

echo
echo "m1 and m2 together"
check "top uses M2's proc_list"      "COMMAND"           ./bin/minios -c "top 0.3 1"
check "both tables reachable"        "run <program>"     sh -c './bin/minios -c help'
check "script mode runs mixed cmds"  "exited with status" sh -c 'printf "system\nrun /bin/true\n" | ./bin/minios /dev/stdin'

echo
echo "handbook examples"
if [ -x bin/fork_demo ]; then
  check "fork_demo forks"            "I am the child"    ./bin/fork_demo
  check "fork_memory_demo runs"      "Child:  secret=100" ./bin/fork_memory_demo
  check "exec_wait_demo execs date"  "child exit status" ./bin/exec_wait_demo
  check "uptime_demo reads /proc"    "uptime"            ./bin/uptime_demo
else
  echo "SKIP  examples not built (run: make examples)"
fi

summary
