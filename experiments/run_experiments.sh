#!/bin/sh
# Runs every experiment and prints one report.
#
#   ./experiments/run_experiments.sh
#   ./experiments/run_experiments.sh > docs/results.txt
#
# Run it from the project root, after "make experiments".

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

if [ ! -x build/exp1_fork_vs_exec ]; then
    echo "build the experiments first:   make experiments"
    exit 1
fi

line() { printf '%s\n' "----------------------------------------------------------"; }

echo "MiniOS — Experiments and Analysis"
echo "date    : $(date)"
echo "kernel  : $(uname -sr)"
echo "cpus    : $(getconf _NPROCESSORS_ONLN)"
echo "memory  : $(awk '/MemTotal/ {printf "%d MB", $2/1024}' /proc/meminfo)"
echo "compiler: $(${CC:-gcc} --version | head -1)"
line

echo "EXPERIMENT 1 — what does fork cost, and what does exec add?"
line
./build/exp1_fork_vs_exec
echo

echo "EXPERIMENT 2 — does fork() copy the parent's memory?"
line
./build/exp2_cow
echo

echo "EXPERIMENT 3 — zombie and orphan processes"
line
./build/exp3_zombie_orphan
echo

echo "EXPERIMENT 4 — cost of MiniOS's own /proc scan"
line
./build/exp4_procscan
echo

echo "EXPERIMENT 5 — stdio buffering across fork()"
line
echo "EXP5 fork_demo straight to the terminal:"
if command -v script >/dev/null 2>&1; then
    # the trailing sleep keeps the pty open long enough for the child's
    # line to land -- without it, script closes when the PARENT exits and
    # the child's output is lost about two runs in three.
    script -qec 'sh -c "./build/ex_fork_demo; sleep 0.4"' /dev/null 2>/dev/null \
        | tr -d '\r' | grep -E 'fork|PID' | sed 's/^/EXP5   /'
    N_TTY=$(script -qec 'sh -c "./build/ex_fork_demo; sleep 0.4"' /dev/null 2>/dev/null \
        | tr -d '\r' | grep -c 'Before fork')
else
    ./build/ex_fork_demo | sed 's/^/EXP5   /'
    N_TTY=na
fi
echo "EXP5 the same program, output piped:"
./build/ex_fork_demo | sed 's/^/EXP5   /'
N_PIPE=$(./build/ex_fork_demo | grep -c 'Before fork')
echo "EXP5 'Before fork' printed  $N_TTY time(s) on a terminal, $N_PIPE time(s) through a pipe"
echo

echo "EXPERIMENT 6 — the same work, measured by MiniOS itself"
line
echo "EXP6 'run' reports wall-clock time for each child:"
./minios -c "run /bin/true"       | sed 's/^/EXP6   /'
./minios -c "run /bin/date"       | sed 's/^/EXP6   /'
./minios -c "run ./build/ex_hog_cpu 0.2" | sed 's/^/EXP6   /'
line
echo "done."
