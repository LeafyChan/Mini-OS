#!/bin/sh
. "$(dirname "$0")/lib.sh"

echo "m1 to m4 in one process"
check "all four modules in one session" "6/6 jobs finished.*|processes" \
    sh -c 'printf "system\nrun /bin/true\nsubmit sum 100\nworkers 2 6 -q\n" | ./bin/minios /dev/stdin'
check "fork works after threads ran"   "via pipe"   ./bin/minios -c "workers 4 8 -q; submit sum 10"
check "threads work after fork"        "8/8 jobs"   ./bin/minios -c "submit sum 10; workers 4 8 -q"
check "pipeline after threads"         "^ok$"       ./bin/minios -c "workers 2 4 -q; echo ok | cat"
check "run after a pipeline"           "status 0"   ./bin/minios -c "echo a | cat; run /bin/true"
check "process and thread give the same answer" "5050" ./bin/minios -c "submit sum 100; workers 1 1 sum 100"
check "compare prints three rows"      "4 threads"  ./bin/minios -c "compare 10"
check "compare rejects a bad type"     "usage: compare" ./bin/minios -c "compare 10 nosuch"
check "race still works"               "semaphore.*correct" ./bin/minios -c "submit sum 5; race 100000 2 1"
check "the exit status is 0 on success" "^0$"       sh -c './bin/minios -c "submit sum 5" >/dev/null 2>&1; echo $?'
check "the exit status is 1 on failure" "^1$"       sh -c './bin/minios -c "nosuchcmd" >/dev/null 2>&1; echo $?'

if [ -x bin/pipe_demo ]; then
  check "pipe_demo runs"               "Child received"   ./bin/pipe_demo
  check "thread_demo runs"             "Main: worker finished" ./bin/thread_demo
  check "semaphore_demo is exact"      "200000"           ./bin/semaphore_demo
  check "prodcons_demo consumes all"   "Consumed 20"      ./bin/prodcons_demo
  check "fork_demo prints 4 lines when piped" "^4$"       sh -c './bin/fork_demo | wc -l'
fi

summary
