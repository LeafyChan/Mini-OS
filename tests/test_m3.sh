#!/bin/sh
. "$(dirname "$0")/lib.sh"

echo "m3: pipes, jobs and pipelines"
check "help lists module 3"            "Module 3"                       ./bin/minios -c "help"
check "submit sum"                     "sum\\(1\\.\\.100\\) = 5050.*via pipe" ./bin/minios -c "submit sum 100"
check "submit primes"                  "primes <= 100: 25"              ./bin/minios -c "submit primes 100"
check "submit with a plain name"       "hash\\('job1'"                  ./bin/minios -c "submit job1"
check "submit rejects a bad number"    "usage: submit"                  ./bin/minios -c "submit sum -5"
check "jobs lists the history"         "sum +10 +4 .*sum\\(1\\.\\.10\\) = 55" ./bin/minios -c "submit fib 10; submit sum 10 4; jobs"
check "jobs is empty at the start"     "no jobs yet"                    ./bin/minios -c "jobs"
check "frames stay apart"              "PASS"                           ./bin/minios_test --frames

want=$(printf 'pear\napple\nfig\nkiwi\n' | sort | head -n 3 | tr '\n' ' ')
got=$(./bin/minios -c "printf 'pear\napple\nfig\nkiwi\n' | sort | head -n 3" 2>&1 | tr '\n' ' ')
test_if "3 stage pipeline matches the system shell" '[ "$got" = "$want" ]'

rm -f tests/out/p1.txt tests/out/p2.txt
check "> writes a file"                "^a$"        ./bin/minios -c "printf 'b\na\n' | sort > tests/out/p1.txt; run cat tests/out/p1.txt"
check ">> appends to a file"           "^2 "        ./bin/minios -c "echo one | cat > tests/out/p2.txt; echo two | cat >> tests/out/p2.txt; run wc -l tests/out/p2.txt"
check "< reads a file"                 "^2$"        ./bin/minios -c "cat < tests/out/p1.txt | wc -l"
check "a quoted | is not a pipe"       "^a\\|b$"    ./bin/minios -c "echo 'a|b' | cat"
check "a failed stage is reported"     "1 of 2 commands failed" ./bin/minios -c "nosuchprog | cat"
check "> is allowed on the last stage only" "only allowed on the last" ./bin/minios -c "echo x > f | cat"
check "pipeline limit is 8"            "at most 8"  ./bin/minios -c "echo 1|cat|cat|cat|cat|cat|cat|cat|cat"

fds="sh -c 'ls /proc/\$PPID/fd | wc -l'"
out=$(./bin/minios -c "run $fds; echo a | cat | cat; submit sum 5; ipcbench 10 64; workers 2 4 -q; run $fds" 2>&1 | grep -E '^[0-9]+$')
test_if "no descriptor leak after pipes, jobs, shm and threads" '[ "$(printf "%s\n" "$out" | sort -u | wc -l)" -eq 1 ]'

check "ipcbench checks both methods"   "shm .* ok$"                     ./bin/minios -c "ipcbench 1000 4096"
check "ipcbench sweep reaches 1 MB"    "shm +200 +1048576 .* ok$"       ./bin/minios -c "ipcbench sweep"
test_if "no shared memory object is left" '! ls /dev/shm 2>/dev/null | grep -q minios_bench'

summary
