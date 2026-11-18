#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
out=experiments/results.txt
m=./bin/minios
[ -x bin/e11_procscan ] || make -s all || exit 1

{
echo "machine: $(nproc) cpu, $(uname -sr), $(awk '/MemTotal/ {printf "%d MB", $2/1024}' /proc/meminfo)"
echo "date: $(date)"
echo

echo "e1 fork output on a terminal and in a pipe"
echo "terminal lines: $(script -qc ./bin/fork_demo /dev/null | wc -l)"
echo "pipe lines: $(./bin/fork_demo | wc -l)"
echo

echo "e2 two writes, one read"
./bin/pipe_two_msgs
./bin/minios_test --frames
echo

echo "e3 race, 4 threads, 1000000 increments, 6 trials"
$m -c "race 1000000 4 6"
echo

echo "e4 workers, 16 jobs of primes 60000 (needs cpu)"
$m -c "workers bench 16 primes 60000" | grep "=>"
echo "e4 workers, 16 jobs of sleep 40 ms (needs no cpu)"
$m -c "workers bench 16 sleep 40" | grep "=>"
echo

echo "e5 pipe against shared memory"
$m -c "ipcbench sweep"
echo

echo "e6 processes against threads"
$m -c "compare 100 sum 1000"
$m -c "compare 100 primes 20000"
$m -c "compare 40 sleep 5"
echo

echo "e7 queue test, 4 producers, 4 consumers, 5000 items each"
./bin/minios_test --stress
echo

echo "e8 cost of fork alone, and what exec adds"
./bin/e8_fork_vs_exec
echo

echo "e9 does fork copy the parent's memory? (copy-on-write)"
./bin/e9_cow
echo

echo "e10 zombie and orphan, read back from /proc"
./bin/e10_zombie_orphan
echo

echo "e11 cost of one /proc scan (the processes command)"
./bin/e11_procscan
} > $out 2>&1
cat $out
