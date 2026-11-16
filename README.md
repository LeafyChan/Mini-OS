# MiniOS

One Linux shell with four modules.

| module | commands | files |
|---|---|---|
| m1 shell and system | help, system, cpu, top, quit | main.c, shell.c, util.c, monitor.c, cpustat.c, dashboard.c |
| m2 processes | run, processes | process.c |
| m3 pipes and jobs | submit, jobs, pipelines, ipcbench | ipc.c, jobs.c, pipeline.c, shmbench.c |
| m4 threads | workers, race | sync.c, workers.c |
| team | compare | compare.c |

Build and test:

    make
    make test
    make tsan
    ./bin/minios

Run the experiments again on your own machine:

    sh experiments/run.sh

The numbers go to docs/results.txt.

Use `-c "cmd; cmd"` to run commands without the prompt.
A semicolon always splits commands, also inside quotes.
