#!/bin/sh
. "$(dirname "$0")/lib.sh"

echo "m4: threads and synchronization"
check "help lists module 4"             "Module 4"                ./bin/minios -c "help"
check "help workers shows usage"        "usage: workers"          ./bin/minios -c "help workers"
check "workers finishes all jobs"       "12/12 jobs finished"     ./bin/minios -c "workers 3 12"
check "workers works with 1 thread"     "6/6 jobs finished by 1"  ./bin/minios -c "workers 1 6 sum 1000 -q"
check "workers works with 8 threads"    "20/20 jobs finished by 8" ./bin/minios -c "workers 8 20 primes 2000 -q"
check "workers 0 jobs exits cleanly"    "0/0 jobs finished"       ./bin/minios -c "workers 2 0 -q"
check "workers computes correct sum"    "sum\\(1\\.\\.100\\) = 5050" ./bin/minios -c "workers 2 2 sum 100"
check "workers rejects 0 threads"       "threads must be"         ./bin/minios -c "workers 0"
check "workers rejects bad type"        "usage: workers"          ./bin/minios -c "workers 2 4 nosuch"
check "workers bench prints speed-up"   "speed-up"                ./bin/minios -c "workers bench 4 sleep 20"
check "sleep jobs scale with threads"   "4 thread.*speed-up [2-4]" ./bin/minios -c "workers bench 8 sleep 40"
check "race: semaphore is exact"        "semaphore.*correct"      ./bin/minios -c "race 200000 2 1"
check "race: mutex is exact"            "mutex.*correct"          ./bin/minios -c "race 200000 2 1"
check "race: atomic is exact"           "atomic.*correct"         ./bin/minios -c "race 200000 2 1"
check "race: unsafe reported"           "unsafe.*(WRONG|lucky)"   ./bin/minios -c "race 200000 2 1"
check "race rejects 1 thread"           "usage: race"             ./bin/minios -c "race 1000 1"
check "queue stress: exactly once"      "PASS"                    ./bin/minios_test --stress
check "minios_test --workers 4"         "16/16 jobs finished"     ./bin/minios_test --workers 4
check "threads and run coexist"         "exited with status 0"    sh -c 'printf "workers 2 4 -q\nrun /bin/true\n" | ./bin/minios /dev/stdin'

echo "m4 extensions: prodcons, pool, deadlock_demo"
M="timeout 30 ./bin/minios -c"
check "prodcons: every item exactly once" "30/30 items consumed exactly once.*PASS" $M "prodcons 3 2 30 5 20"
check "prodcons: buffer view shown"       "Buffer \["                $M "prodcons 1 1 8 5 40"
check "prodcons: buffer fills up"         "5/5 \| empty=0 full=5"    $M "prodcons 2 1 12 1 60"
check "pool demo: FIFO order"             "FIFO +ran: #1\(p1\) #2\(p5\) #3\(p3\)" $M "pool demo"
check "pool demo: priority order"         "PRIORITY +ran: #4\(p9\) #6\(p9\) #7\(p7\) #2\(p5\)" $M "pool demo"
check "pool: start, resize, wait, stop"   "done 20"                  $M "pool start 2 fifo; pool batch 20 sum 100000; pool resize 5; pool resize 1; pool wait; pool status; pool stop"
check "pool: paused wait is refused"      "resume first"             $M "pool start 1; pool pause; pool submit sum 1; pool wait"
check "pool: must start first"            "not running"              $M "pool status"
check "pool: cleaned up at exit"          "queued 5 jobs"            $M "pool start 3; pool batch 5 sleep 50"
check "deadlock: lock ordering completes" "deadlocks detected 0 \| rollbacks 0" $M "deadlock_demo order"
check "deadlock: trylock completes"       "T1 done|T2 done"          $M "deadlock_demo trylock"
check "deadlock: detector finds cycle"    "deadlocks detected 1 \| rollbacks 1" $M "deadlock_demo detect"
check "deadlock: real hang is killed"     "deadlock suspected"       $M "deadlock_demo hang"

summary
