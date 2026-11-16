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

summary
