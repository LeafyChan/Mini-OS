# MiniOS

**Operating Systems (CS2101) mini project.** A small Linux shell in C. From one prompt you can inspect the machine through `/proc`, launch programs, pass results between processes, run worker threads and watch race conditions happen. A fifth module adds custom system calls to the xv6 teaching kernel.

Every feature must teach one OS concept; if it doesn't, it doesn't belong here (the handbook says the same: don't make MiniOS huge).

> MiniOS reads Linux's `/proc`, so it runs on Linux only (Ubuntu, WSL, lab machines). On macOS use a Linux VM.

**Contents:** [Quick start](#quick-start) · [Modules and owners](#modules-and-owners) · [Commands](#commands) · [Layout](#layout) · [Tests](#tests) · [Experiments](#experiments) · [xv6 (M5)](#xv6-kernel-work-m5) · [Workflow](#team-workflow) · [Troubleshooting](#troubleshooting) · [Demo](#demo-script) · [Viva](#viva-preparation) · [Status](#status-against-the-team-master-plan)

---

## Quick start

```bash
sudo apt install build-essential git
git clone --recurse-submodules https://github.com/LeafyChan/Mini-OS.git
cd Mini-OS

make               # builds bin/minios, bin/minios_test, examples and experiments
./bin/minios       # try: help, system, quit
make test          # every test suite
make experiments   # every experiment, writes docs/results.txt
```

- `./bin/minios -c "system; run date"` runs commands and exits; `./bin/minios script.txt` runs one command per line (blank lines and `#` comments skipped). Exit status is 0 on success, 1 if a command failed. A `;` always splits commands, even inside quotes.
- `./bin/minios --version` prints `MiniOS 3.0`.
- M5 also needs: `sudo apt install gcc-riscv64-linux-gnu qemu-system-misc gdb-multiarch`. If you cloned without `--recurse-submodules`, run `git submodule update --init`.
- If a test fails on your machine, post the last 20 lines of output in the group chat before changing any code.

---

## Modules and owners

Five members, five modules, no shared `.c` files. Any member can be asked about any module in the viva.

| Module | Member (roll no.) | Lab weeks | Handbook | Source files | Tests and examples |
|---|---|---|---|---|---|
| **M1** Shell and `/proc` | Anvitha (SE25UDSC041) | 1 | Ch 2, 3, 9 | `main.c` `shell.c` `util.c` `monitor.c` `cpustat.c` `dashboard.c` `Makefile` | `test_m1_m2.sh`, `uptime_demo` |
| **M2** Processes | Srujana (SE25UARI035) | 2-3 | Ch 4 | `process.c` | `test_m1_m2.sh`, `fork_demo` `fork_memory_demo` `exec_wait_demo` `hog_cpu` `hog_mem` |
| **M3** Pipes, IPC, jobs | Aarnavi (SE25UARI093) | 4 | Ch 5 | `ipc.c` `jobs.c` `pipeline.c` `shmbench.c` | `test_m3.sh`, `pipe_demo` `pipe_two_msgs` |
| **M4** Threads and sync | Advik (SE25UARI118) | 5-8 | Ch 6, 7 | `sync.c` `workers.c` | `test_m4.sh` `minios_test.c` `tsan.supp`, `thread_demo` `thread_shared_demo` `race_demo` `semaphore_demo` `prodcons_demo` |
| **M5** xv6 kernel | Krishna (SE25UARI133) | 9-12 | Ch 8, App. A | `xv6/` submodule, `xv6-patches/`, `docs/m5_implementation.md` | `test_m5.sh`, `getyeartest` `getpinfotest` `ps_xv6` |
| **Team** | all | – | Ch 12 | `compare.c` `include/minios.h` `README.md` `tests/run_all.sh` `tests/lib.sh` `experiments/` `docs/results.txt` | `test_integration.sh` |

Branches: `feature/m1-shell`, `feature/m2-processes`, `feature/m3-ipc`, `feature/m4-threads`, `feature/m5-xv6`.

All handbook items are done: `help`/`quit` loop, `system` (TODOs 1-4), `run` (TODOs 1-3), `processes`, `send_result`/`receive_result` (TODOs 1-3), `submit`/`jobs`, `workers` (TODOs 1-3) with the 4/8-worker timing test, `race`, the semaphore queue (TODOs 1-10), xv6 `getyear()` (all 11 steps), the example programs, one test script per member, experiments and this report.

Beyond the baseline: `cpu`, `top` (M1); pipelines with `<` `>` `>>`, `ipcbench` (M3); `getpinfo()`, `ps_xv6`, GDB trace report (M5); `compare` (team).

**Marks:** functionality and integration 45%, demonstration 20%, experiments and analysis 15%, report 10%, individual viva 10%.

---

## Commands

`help` lists everything; `help <command>` shows one entry.

| Module | Command | What it does | OS idea |
|---|---|---|---|
| M1 | `system` | Uptime, memory with usage bar, load average, own PID | `/proc` as a file interface |
| M1 | `cpu [sample_ms]` | Per-core CPU % from two `/proc/stat` samples, plus temperatures if available | Jiffies, sampling deltas |
| M1 | `top [interval_s] [frames]` | Live dashboard; `q` quits | Raw terminal mode, `select()` |
| M1 | `quit` / `exit` | Leave | Clean shutdown |
| M2 | `run <prog> [args]` | fork, exec in child, waitpid in parent; prints status and time. Missing program → 127 | fork / exec / wait |
| M2 | `processes [-m\|-c] [-n N] [-t]` | Process list from `/proc/<pid>/stat`; sort by memory/CPU, limit rows, MiniOS tree only | The process table |
| M3 | `submit <job> [arg] [prio]` / `jobs` | Worker process computes, result returns over a pipe; `jobs` shows history | IPC with a pipe |
| M3 | `a \| b \| c` | Up to 8 stages; `<` on first, `>`/`>>` on last; `run` prefix optional | `dup2`, closing unused ends |
| M3 | `ipcbench [msgs] [bytes]` / `ipcbench sweep` | Same data over pipe and shared memory, checksum-verified | Kernel copy vs shared memory |
| M4 | `workers [threads] [jobs] [type] [arg] [-q]` | Threads drain the 5-slot semaphore queue; `workers bench` times 1/2/4/8 threads | Threads, bounded buffer |
| M4 | `race [iters] [threads] [trials]` | Unsafe counter vs semaphore, mutex, atomic | Race conditions, critical sections |
| Team | `compare [jobs] [type] [arg]` | Same jobs as processes, 1 thread, 4 threads | Process vs thread cost |

Job types: `sum`, `primes`, `fib`, `hash`, `sleep`. A plain name (`submit job1`) becomes a hash job.

How a command is found: `shell.c` walks four tables (`cmds_shell`, `cmds_process`, `cmds_ipc`, `cmds_threads`). A line containing `|` goes to M3's pipeline code first. Adding a command means adding one row to your own table.

Key formulas and rules: CPU % = `100 * (Δtotal − Δidle) / Δtotal`, which needs two samples. The queue uses `mutex` (1), `empty` (5) and `full` (0), and producers and consumers wait on `empty`/`full` *before* `mutex`. `processes` parses from the *last* `)` because process names can contain spaces and parentheses. Pipe messages carry a 4-byte length prefix because pipes keep no message boundaries.

---

## Layout

```text
include/minios.h     shared contract (structs, cross-module functions)
src/                 M1-M4 + compare.c (see table above)
examples/            handbook programs, built with -O0
experiments/         run.sh + e8-e11 C experiments
tests/               one script per module, integration, C test helper
docs/                results.txt, m5_implementation.md
xv6/                 xv6-riscv submodule (branch m5-kernel)
xv6-patches/         reference.patch + apply.py
```

Handbook examples (`./bin/<name>`): `uptime_demo` (3.1), `fork_demo` (4.1, 3 lines on a terminal, 4 when piped), `fork_memory_demo` (4.3), `exec_wait_demo` (4.4), `pipe_demo` (5.1), `pipe_two_msgs` (5.2), `thread_demo` / `thread_shared_demo` (6.1-6.2), `race_demo` (7), `semaphore_demo` (7.3), `prodcons_demo` (7.5). `hog_cpu` and `hog_mem` generate load for `cpu`, `top` and `processes`.

---

## Tests

```bash
make test    # all suites (bash tests/run_all.sh)
make tsan    # ThreadSanitizer build + queue stress test
```

| Suite | Checks | Covers |
|---|---:|---|
| `test_m1_m2.sh` | 34 | help, `system`, `cpu`, `top`, `run`, status 127, `processes`, examples |
| `test_m3.sh` | 20 | `submit`, `jobs`, framing, pipelines, redirection, no fd leaks, `ipcbench`, clean `/dev/shm` |
| `test_m4.sh` | 19 | `workers`, `race`, bench speed-up, 20,000-item queue stress |
| `test_m5.sh` | 4 | Patch applies idempotently, syscall numbers unique, xv6 builds, QEMU boots and runs checks |
| `test_integration.sh` | 16 | All four Linux modules in one process; fork after threads and vice versa |

**93 checks.** The four Linux suites (89 checks) passed on 18 Nov 2026. M5 needs the submodule, the RISC-V toolchain and QEMU; without them it fails for environment reasons, not code reasons. The build uses `-Wall -Wextra -Wpedantic -Werror`; `tsan.supp` suppresses only the deliberate race in `race`.

---

## Experiments

`make experiments` (or `sh experiments/run.sh`) re-runs everything and writes `docs/results.txt`.

| ID | Chapter | Experiment | What to look for |
|---|---|---|---|
| e1 | 4 | `fork_demo` on a terminal vs a pipe | 3 lines vs 4: "Before fork" is still in the stdio buffer when fork copies it |
| e2 | 5 | Two writes, one read | Pipes keep no message boundaries, hence the length prefix |
| e3 | 7 | `race`, 4 threads | Unsafe totals are sometimes right by luck, sometimes badly wrong |
| e4 | 6 | `workers bench`, CPU jobs vs sleep jobs | CPU jobs stop scaling at the core count; sleep jobs keep scaling |
| e5 | 5 | `ipcbench sweep` | Pipe vs shared memory across message sizes |
| e6 | 4-6 | `compare` | Processes vs threads |
| e7 | 7 | 4 producers, 4 consumers, 5,000 items each | Every item delivered exactly once |
| e8 | 4 | fork+`_exit`+wait vs fork+exec+wait (median of 7) | exec costs several times more than fork itself |
| e9 | 4 | fork time with 1-256 MB in the parent; child idle vs child writing | Idle child: grows far slower than memory (only page tables are copied). Writing child: about 35x slower at 256 MB (real page copies) |
| e10 | 4 | Zombie and orphan, read from `/proc/<pid>/stat` | State `Z` until `wait()`; orphan's PPID becomes 1 |
| e11 | 3-4 | Cost of one full `/proc` scan (calls M2's real `proc_list()`) | Whether `top` can afford fast refresh rates |

e8-e11 were lost from `main` when commit `57bb867` replaced the experiments folder. They are restored from commit `1ffe811` and now build with the rest of the project under `-Werror`.

---

## xv6 kernel work (M5)

Lives in the `xv6/` submodule (branch `m5-kernel`). Full details: `docs/m5_implementation.md`.

- **`getyear()`** returns 2026, added through all 11 Appendix A steps. It uses syscall **23** because this xv6 already uses 22 for `sync`.
- **Accounting:** `ctime` (creation tick), `rtime` (ticks running), `nsched` (times scheduled).
- **`getpinfo(struct pstat *)`** copies process info to user space one entry at a time, never while holding a spinlock.
- **`ps_xv6`** prints the process table; `ps_xv6 spin N` starts CPU burners. **`getyeartest`** and **`getpinfotest`** are the user test programs.
- **GDB trace report:** `xv6/docs/xv6_trace_report.md`, one system call from `ecall` to `sret`.

```bash
python3 xv6-patches/apply.py                     # apply to xv6/
python3 xv6-patches/apply.py --xv6 /path/to/xv6  # another checkout
python3 xv6-patches/apply.py --build             # apply and build
```

`reference.patch` is relative to xv6 commit `fa2ddcb`; the patcher is idempotent. The submodule has its own history: push from inside `xv6/`, then commit the updated pointer in the parent repo.

---

## Team workflow

- Stay in your own files. Call other modules only through `include/minios.h`.
- Header changes: post the exact line in the group chat and get one "OK". Never rename, delete or reorder existing entries; add to your module's section.
- `fflush(NULL)` before every `fork()`; every new thread calls `block_signals_in_thread()` first.
- `git add <file>` by name, never `git add .`. Merge small and often (every 2-3 days).

```text
your branch -> make + your suite -> make test -> Pull Request -> 1 review -> merge to main
```

---

## Troubleshooting

| You see | Try first |
|---|---|
| `make` fails after `git pull` | `make clean && make`; then message the owner of the file in the error |
| Someone else's test fails | `git log -3 <file>` and tell the owner; don't fix it yourself |
| Pipe or pipeline hangs | Ctrl-C; check every unused pipe end is closed |
| Queue hangs | Semaphore order: `empty`/`full` before `mutex` |
| Terminal broken after `top` | `reset` |
| `xv6/` empty or clone denied | `git submodule update --init`; the URL is SSH, so you need a GitHub SSH key and repo access |
| M5 can't build or boot | Check the RISC-V toolchain and `qemu-system-riscv64`; read `tests/out/m5_build.log` and `m5_qemu.log` |

---

## Status against the Team Master Plan

| Member | Extensions in the repo | Planned, not in the repo |
|---|---|---|
| M1 | `cpu`, `top` | – |
| M2 | – | `run ... &` with `ps`/`wait`/`kill`; `runlimit`/`limit` |
| M3 | pipelines with redirection; `ipcbench` | – |
| M4 | – | `pool`; live `prodcons` view; `deadlock_demo` |
| M5 | `getpinfo()`, `ps_xv6`, trace report | – |
| Team | `compare`; e8-e11 | `docs/experiments.md` (numbers live in `docs/results.txt`) |

Differences from the plan, so nobody is surprised: 93 checks across 5 suites (plan said 157 across 7); version prints `MiniOS 3.0` (plan said `2.0-extended`); the trace report is at `xv6/docs/xv6_trace_report.md`. The unfinished M2/M4 extensions are optional; don't claim them in the report or demo unless they are merged and tested.
