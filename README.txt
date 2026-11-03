MiniOS 2.0  —  Modules 1 + 2 merged
===================================

CS2101 Operating Systems mini project  —  Kernel._.Crew

    Module 1   system, cpu, top        Anvitha    (handbook Ch 3)
    Module 2   run, processes          Srujana    (handbook Ch 4)
    Module 3-5 pipes, threads, sems    Aarnavi, Advik, Krishna  (upcoming)


BUILD AND RUN
-------------
    make        builds MiniOS, the handbook demos and the experiments
    ./minios

    help            list every command, grouped by module
    system          uptime + memory + load      (M1, Ch 3)
    cpu 500         per-core CPU %              (M1)
    top             live dashboard, q to quit   (M1)
    run date        fork -> exec -> wait        (M2, Ch 4)
    processes -m    process table from /proc    (M2)
    quit


PROVE IT WORKS
--------------
    ./tests/run_all.sh              34 checks, all passing
    ./minios demo.txt               the whole demo, no typing


THE MEASUREMENTS
----------------
    make experiments
    ./experiments/run_experiments.sh


WHAT IS IN HERE
---------------
    docs/MiniOS_Handout.pdf      2 pages, hand this to the examiner
    docs/MiniOS_Evaluation.pdf   the full report, all five parts
    docs/make_pdf.sh             rebuilds both PDFs
    docs/figures/                the diagrams (SVG) + the chart script

    docs/00_SCOPE.md          team + handbook comparison
    docs/01_INTEGRATION.md    how M1 and M2 were merged   <- 45%
    docs/02_EXPERIMENTS.md    six experiments + analysis  <- 15%
    docs/03_DEMONSTRATION.md  the live demo run sheet     <- 20%
    docs/04_REPORT.md         design, testing, findings   <- 10%
    docs/05_VIVA.md           checklist + examiner Q&A    <- 10%
    docs/results.txt          raw output of the last run

    include/minios.h          the shared contract
    src/                      the program
    examples/                 handbook demo programs, standalone
    tests/run_all.sh          the test suite
    experiments/              the measurement programs


WHO WROTE WHAT
--------------
    M1   shell.c  util.c  monitor.c  cpustat.c  dashboard.c
    M2   process.c
    both include/minios.h   (add only, never edit someone else's line)


THE MERGE, IN ONE PICTURE
-------------------------
        src/shell.c              src/process.c
             |                         |
             |    both #include        |
             +-----------+-------------+
                         v
                 include/minios.h

    shell.c holds a list of command tables:

        all_tables[] = { cmds_shell, cmds_process, NULL }
                              M1          M2

    A typed word is looked up in each table in turn.
    Adding M3 = one more line in that list.

    Full detail: docs/01_INTEGRATION.md section 2.


NOT YET MERGED
--------------
    M2's process.c arrived with 7 commands in its table, but only
    2 of the handlers were in the file:

        run, processes        -> present, working
        ps, wait, kill        -> need bgjobs.c   (not handed over)
        runlimit, limit       -> need rlimit.c   (not handed over)

    Those 5 rows are commented out, not deleted. See the marked
    block in src/process.c for the 4-step restore.


THE ONE FORMULA
---------------
    A single reading of /proc/stat means nothing.
    Take two, subtract:

        busy% = 100 * (change in total - change in idle)
                      -----------------------------------
                            change in total
