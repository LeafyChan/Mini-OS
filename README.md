# MiniOS

**Operating Systems (CS2101) mini project.** MiniOS is one small Linux shell, written in C, that lets you inspect the machine through `/proc`, launch programs, pass results between processes, run worker threads and watch a race condition happen. A fifth module adds our own system call to the xv6 teaching kernel.

Each piece is a separate module owned by one team member, and every command exists to show one operating-system idea.

> It reads Linux's `/proc`, so it must run on Linux (Ubuntu, WSL, or the lab machines). On macOS use a Linux VM.

## Quick start

```bash
sudo apt update && sudo apt install build-essential git
git clone https://github.com/LeafyChan/Mini-OS.git
cd Mini-OS
make              # builds bin/minios, bin/minios_test and every example
./bin/minios      # then type: help, system, quit
make test         # last line should say: ALL SUITES PASSED
```

Version: `./bin/minios --version` prints `MiniOS 3.0`.

If `make test` fails on your machine, post the last 20 lines in the group chat before changing any code.

## Modules and status

| Module | Topic (handbook chapter) | Commands | Files | Status |
|---|---|---|---|---|
| M1 | Shell and `/proc` inspector (Ch 3) | `help`, `system`, `cpu`, `top`, `quit`, `exit` | `main.c` `shell.c` `util.c` `monitor.c` `cpustat.c` `dashboard.c` | Done |
| M2 | Processes: fork, exec, wait (Ch 4) | `run`, `processes` | `process.c` | Baseline done |
| M3 | Pipes, IPC and jobs (Ch 5) | `submit`, `jobs`, `ipcbench`, pipelines with `<` `>` `>>` | `ipc.c` `jobs.c` `pipeline.c` `shmbench.c` | Done |
| M4 | Threads and synchronization (Ch 6-7) | `workers`, `race` | `sync.c` `workers.c` | Baseline done |
| Team | Processes against threads | `compare` | `compare.c` | Done |
| M5 | xv6 kernel syscall (Ch 8, Appendix A) | `getyear` (inside xv6) | separate xv6 tree | Not in this repo yet |

Planned extensions from the Team Master Plan that are **not built yet**:

- M2: background jobs with `&`, `ps`, `wait`, `kill` (SIGCHLD job table), and `runlimit` / `limit` (CPU and memory caps).
- M4: `pool` (thread pool with FIFO and priority scheduling), `prodcons` (live buffer view), `deadlock_demo`.
- M5: `getyear()` through the 11 steps, then `getpinfo()`, the `ps_xv6` tool and the GDB trace report.

## Commands

Type `help` for the full list, or `help <command>` for one entry.

### M1: shell and system inspector

| Command | What it does | OS idea |
|---|---|---|
| `help [command]` | Lists commands from every module table | Command dispatch |
| `system` | Uptime, memory (with a usage bar), load average, MiniOS's own PID | `/proc` as a file interface |
| `cpu [sample_ms]` | Per-core CPU % from two `/proc/stat` samples, plus temperatures when the machine has them | Jiffies and sampling deltas |
| `top [interval_s] [frames]` | Live dashboard: CPU bar, memory, top processes with MiniOS pinned. Press `q` to leave. | Terminal control, `select()` |
| `quit` / `exit` | Leave MiniOS | Clean shutdown |

CPU % is `100 * (delta_total - delta_idle) / delta_total`. One sample is meaningless, so `cpu` always takes two. VMs and WSL usually have no thermal zones, and `cpu` says so instead of failing.

### M2: processes

| Command | What it does | OS idea |
|---|---|---|
| `run <program> [args...]` | fork, then exec in the child, then wait in the parent. Prints exit status and elapsed time. | fork / exec / wait |
| `processes [-m\|-c] [-n N] [-t]` | Table of processes from `/proc/<pid>/stat`. `-m` sort by memory, `-c` by CPU time, `-n N` limit rows, `-t` only MiniOS and its children. | The process table |

A missing program exits with status 127, like a normal shell.

### M3: pipes, jobs and pipelines

| Command | What it does | OS idea |
|---|---|---|
| `submit <job> [arg] [priority]` | Forks a worker process; the result comes back over a pipe | IPC with a pipe |
| `jobs` | History of submitted jobs and their results | |
| `ipcbench [msgs] [bytes]` or `ipcbench sweep` | Same data sent over a pipe and over shared memory, checksum verified | Kernel copy against shared memory |
| `a \| b \| c` | Pipelines, up to 8 stages. `<` on the first stage, `>` and `>>` on the last. The `run` prefix is optional. | `dup2`, closing unused pipe ends |

Job types for `submit` and `workers`: `sum`, `primes`, `fib`, `hash`, `sleep`. A plain name such as `submit job1` becomes a hash job over that name.

Messages over the pipe carry a 4-byte length prefix, because a pipe is a byte stream with no message boundaries.

### M4: threads and synchronization

| Command | What it does | OS idea |
|---|---|---|
| `workers [threads] [jobs] [type] [arg] [-q]` | Worker threads drain the 5-slot semaphore queue. `workers bench` times 1, 2, 4 and 8 threads. | Threads, bounded buffer |
| `race [iters] [threads] [trials]` | Unsafe counter against semaphore, mutex and atomic versions | Race conditions, critical sections |

The queue uses three semaphores: `mutex` (starts at 1) guards the buffer, `empty` (starts at 5) counts free slots, `full` (starts at 0) counts filled slots. Producers and consumers always wait on `empty` / `full` **before** taking `mutex`.

### Team: `compare`

`compare [jobs] [type] [arg]` runs the same jobs as separate processes, on 1 thread and on 4 threads, and prints total time and time per job.

### Running without the prompt

```bash
./bin/minios -c "system; run date"      # run commands and exit
./bin/minios script.txt                 # run a script file, one command per line
```

A semicolon always splits commands, also inside quotes. Blank lines and lines starting with `#` are ignored. The exit status is 0 on success and 1 if a command failed.

## Project layout

```
include/minios.h        the ONE shared contract (structs and cross-module functions)
src/
  main.c shell.c util.c monitor.c cpustat.c dashboard.c      M1
  process.c                                                  M2
  ipc.c jobs.c pipeline.c shmbench.c                         M3
  sync.c workers.c                                           M4
  compare.c                                                  team
examples/               the handbook programs, built with -O0
tests/                  test scripts and C test helper
experiments/run.sh      re-runs every experiment
docs/results.txt        output of the last experiment run
```

How a command is found: `shell.c` loops over four command tables, `cmds_shell` (M1), `cmds_process` (M2), `cmds_ipc` (M3) and `cmds_threads` (M4). Adding a command means adding one row to your own table. A line containing `|` goes to M3's pipeline code first.

## Handbook examples

Built into `bin/` by `make`, unchanged from the handbook. Run any of them as `./bin/<name>`.

| Program | Handbook section | Shows |
|---|---|---|
| `uptime_demo` | 3.1 | Reading `/proc/uptime` |
| `fork_demo` | 4.1 | Prints 3 lines on a terminal but 4 when piped (`./bin/fork_demo \| wc -l`) |
| `fork_memory_demo` | 4.3 | Same address, independent values after fork |
| `exec_wait_demo` | 4.4 | fork, exec `date`, wait |
| `pipe_demo` | 5.1 | Parent to child over a pipe |
| `pipe_two_msgs` | 5.2 | Two writes arrive as one read |
| `thread_demo`, `thread_shared_demo` | 6.1, 6.2 | Creating threads, sharing a variable |
| `race_demo` | 7 | The counter that lies; run it a few times |
| `semaphore_demo` | 7.3 | The same counter, protected |
| `prodcons_demo` | 7.5 | Producer-consumer with three semaphores |
| `hog_cpu`, `hog_mem` | | Load generators for `cpu`, `top`, `processes` |

## Tests

```bash
make test       # runs every suite
make tsan       # ThreadSanitizer build, runs the queue stress test
```

| Suite | Script | Checks | Covers |
|---|---|---|---|
| M1 + M2 | `tests/test_m1_m2.sh` | 34 | help, `system`, `cpu`, `top`, `run`, exit status 127, `processes`, handbook examples |
| M3 | `tests/test_m3.sh` | 20 | `submit`, `jobs`, message framing, pipelines, `<` `>` `>>`, no descriptor leaks, `ipcbench`, no `/dev/shm` leftovers |
| M4 | `tests/test_m4.sh` | 19 | `workers`, `race` fixes are exact, bench speed-up, 20,000-item queue stress |
| Integration | `tests/test_integration.sh` | 16 | All four modules in one process, fork after threads and threads after fork |

Total: 89 checks, all passing at the time of writing. The build uses `-Wall -Wextra -Wpedantic -Werror`. `tests/tsan.supp` suppresses only the deliberate race in the `race` demo.

## Experiments

```bash
sh experiments/run.sh      # writes docs/results.txt
```

| ID | Experiment | What to look for |
|---|---|---|
| e1 | `fork_demo` on a terminal and in a pipe | 3 lines against 4: "Before fork" is still in the stdio buffer when fork copies it |
| e2 | Two writes, one read | Pipes keep no message boundaries, so we add a length prefix |
| e3 | `race` with 4 threads | Unsafe totals are sometimes right by luck and sometimes badly wrong |
| e4 | `workers bench` with CPU jobs and sleep jobs | CPU jobs stop scaling at the core count; sleep jobs keep scaling |
| e5 | `ipcbench sweep` | Pipe against shared memory across message sizes |
| e6 | `compare` | Processes against threads |
| e7 | 4 producers, 4 consumers, 5,000 items each | Every item delivered exactly once |

The numbers currently in `docs/results.txt` were measured on a **1-CPU** machine, so e4 shows no speed-up for CPU jobs. Re-run on a multi-core laptop before the demo and use those numbers in the report.

## Team workflow

- Stay in your own files. Nobody edits another member's `.c` file.
- `include/minios.h` is shared. Post the exact line in the group chat and get one "OK" before changing it. Never rename, delete or reorder anything already in it.
- Put new declarations in your own module's section of the header, and call other modules only through the header.
- Call `fflush(NULL)` before every `fork()`, otherwise buffered text is printed twice.
- Every new thread should call `block_signals_in_thread()` first, so signals are handled on the main thread.
- Work on your own branch (for example `feature/m2-processes`), run `make test` before pushing, and open a Pull Request for one teammate to review.
- Use `git add <file>` by name, not `git add .`, so you never commit someone else's file.

## If something breaks

| You see | Try first |
|---|---|
| `make` fails after `git pull` | `make clean && make`; if it still fails, message the owner of the file in the error |
| A test fails that is not yours | `git log -3` on that file and tell its owner |
| A pipe or pipeline hangs | Press Ctrl-C. Check that every unused pipe end is closed |
| A queue hangs | Check the order of the semaphore waits: `empty` / `full` before `mutex` |
| Terminal looks broken after `top` | Run `reset`. `top` restores the terminal on `q` and on Ctrl-C |

## Demo order (2-3 minutes per member)

Say in one sentence which OS idea each command shows.

1. **M1:** `help`, `system`, then `run bin/hog_cpu 20` in one terminal and `cpu 1000` and `top 1` in another. One core should jump close to 100%.
2. **M2:** `run date`, `run nosuchprogram` (status 127), `processes -t`.
3. **M3:** `submit primes 50000`, `submit job1`, `jobs`, then `run printf 'pear\napple\nfig\n' | sort | head -n 2`, then `ipcbench sweep`.
4. **M4:** `race`, `workers 3 9`, `workers bench 16 sleep 40`.
5. **Team:** `compare 100 sum 1000`.

## Viva: one-line answers everyone should know

| Question | Answer |
|---|---|
| What is `/proc`? | Files the kernel generates when you read them, so programs can inspect kernel state |
| Why does `fork()` return different values? | Child PID to the parent, 0 to the child, so the two copies can tell themselves apart |
| Same address, different values? | Addresses are virtual; each process has its own page table (copy-on-write) |
| Why exec? Why wait? | exec swaps in a new program; wait collects the exit status so no zombie is left |
| Why close unused pipe ends? | The reader only sees end-of-file when every write end is closed |
| What do threads share? | Code, globals, heap, open files; each has its own stack and registers |
| Why is `count++` unsafe? | It is read, add, write; two threads can overwrite each other's update |
| What do `mutex`, `empty`, `full` mean? | The lock, the number of free slots, the number of filled slots |
| Why wait on `empty` before `mutex`? | Holding the lock while sleeping would stop consumers freeing a slot: deadlock |
| Path of a system call in xv6? | stub, `ecall`, trampoline, `usertrap`, `syscall()`, `sys_getyear`, back via `sret` |

Every member must be able to answer all of these, not just the owner of that module.

## Authors

Group project, 5 students.

| Module | Member | Roll no. |
|---|---|---|
| M1 | Anvitha | SE25UDSC041 |
| M2 | Srujana | SE25UARI035 |
| M3 | Aarnavi | SE25UARI093 |
| M4 | Advik | SE25UARI118 |
| M5 | Krishna | SE25UARI133 |