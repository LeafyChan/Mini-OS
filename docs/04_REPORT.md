# Part IV — Project Report

*Design, implementation and findings. Graded component: 10%.*

## 1. Aim and scope

MiniOS is a small command-driven program for inspecting and controlling a
Linux machine, built to make operating-system mechanisms visible rather
than to be useful software. The handbook's framing — *"I know why this
part exists, and I know which OS concept it demonstrates"* — was the test
applied to every feature.

This report covers the two modules merged so far:

| Module | Handbook | Concept | Commands |
|---|---|---|---|
| 1 | Chapter 3 | the `/proc` filesystem | `system`, `cpu`, `top` |
| 2 | Chapter 4 | process creation | `run`, `processes` |

Modules 3–5 (pipes, threads, semaphores) and the optional xv6 system call
are not merged. Section 8 says where that leaves the project.

## 2. Architecture

![](figures/fig1_architecture.pdf)

```
                      +------------------+
                      |      MiniOS      |
                      |   "control room" |
                      +---------+--------+
                                |
              +-----------------+-----------------+
              |                                   |
              v                                   v
          SYSTEM  (M1)                      PROCESS  (M2)
          /proc/uptime                      fork / exec / wait
          /proc/meminfo                     /proc/<pid>/stat
          /proc/loadavg
          /proc/stat
```

Every `.c` file includes `include/minios.h` and nothing from any other
`.c` file. The header declares, the sources implement. This is what makes
five people able to work without blocking each other, and it is described
in full in Part I.

### Code size

| File | Lines | Owner | Purpose |
|---|---|---|---|
| `include/minios.h` | 167 | shared | types and prototypes |
| `src/main.c` | 60 | Anvitha | start-up, argument handling |
| `src/shell.c` | 187 | Anvitha | prompt, command tables, dispatch |
| `src/util.c` | 193 | Anvitha | parsing, formatting, bars |
| `src/monitor.c` | 138 | Anvitha | `system` |
| `src/cpustat.c` | 207 | Anvitha | `cpu` |
| `src/dashboard.c` | 318 | Anvitha | `top` |
| `src/process.c` | 224 | Srujana | `run`, `processes` |
| **total** | **1494** | | |

Plus 97 lines of tests and 405 lines of experiment code.

## 3. Module 1 — reading the machine through /proc

### 3.1 The idea

`/proc` is not storage. The kernel generates the text at the moment of
the read, which means kernel state can be inspected with `fopen` and
`fscanf` and nothing else. Handbook Chapter 3 builds to one function,
`show_system_info()`, with four TODOs: open `/proc/uptime`, read a value,
read one memory value, display them. That is implemented in
`src/monitor.c`.

![](figures/fig5_proc_map.pdf)

### 3.2 What each file needed

| Source | Format | How we parse it |
|---|---|---|
| `/proc/uptime` | two floats | one `fscanf("%lf %lf")` |
| `/proc/meminfo` | `Key:   value kB` per line | `fscanf("%63[^:]: %lu")`, keep 7 keys |
| `/proc/loadavg` | `0.22 0.10 0.02 1/99 1806` | one `fscanf` with `%d/%d` |
| `/proc/stat` | counters per CPU, since boot | `sscanf` per `cpu*` line |

Two details that cost time:

- Keys absent on some kernels (`MemAvailable` on older ones) must not
  break the whole read. `read_meminfo` zeroes the struct first and
  returns success if *any* key was found.
- `/proc/uptime`'s idle figure is summed across all CPUs, so it routinely
  exceeds the uptime. We label it rather than "correct" it.

### 3.3 The extensions, and why they are not padding

The handbook warns: *"Do not make MiniOS huge... Which OS concept does
this feature teach?"* Both extensions have an answer.

**`cpu`** teaches that monotonic counters are meaningless alone.
`/proc/stat` counts clock ticks since boot. The usage *right now* only
exists as a difference between two readings:

```
busy% = 100 * (Δtotal − Δidle) / Δtotal
```

`idle` includes `iowait`, because the CPU had nothing to run then either.
Implemented at `src/cpustat.c:77`.

**`top`** teaches three mechanisms at once: ANSI escape sequences to
redraw in place, `termios` raw mode so a keypress arrives without Enter,
and `select()` to wait on *either* the refresh timer *or* stdin. The last
is the interesting one — a naive loop would either ignore the keyboard or
busy-wait. The terminal settings belong to the user, so `term_restore()`
runs on every exit path.

## 4. Module 2 — creating processes

### 4.1 The three calls

```
MiniOS> run date

  fork()      a second process exists; no program was loaded
  exec()      that process becomes 'date'; no process was created
  waitpid()   the parent collects the exit status
```

Implemented in `run_program()`, `src/process.c:39–66`. The handbook marks
three TODOs there; all three are filled.

The separation matters: the window between `fork` and `exec` is the only
place a child can change what the new program inherits — redirection,
closing descriptors, resource limits. A combined call would have nowhere
to put that.

### 4.2 Three defensive details

| Line | What | Why |
|---|---|---|
| `process.c:39` | `fflush(NULL)` before `fork` | otherwise buffered output is duplicated into the child — measured in Part II Experiment 5 |
| `process.c:50` | `_exit(127)` not `exit` | `exit` would flush the buffers inherited from the parent; 127 is the "not found" convention |
| `process.c:55` | `waitpid` in a retry loop | a signal returns `EINTR`, which means retry, not fail |

### 4.3 Parsing `/proc/<pid>/stat`

52 fields on one line; we use six. The trap is field 2, the executable
name, which is bracketed and may itself contain spaces and brackets —
`(Web Content)` is a real process name. Splitting on whitespace, or on
the first `)`, breaks. We search for the **last** `)` (`process.c:100`)
and read numbers after it.

RSS is reported in **pages**, not bytes, so it is scaled by
`sysconf(_SC_PAGESIZE)` rather than a hardcoded 4096 (`process.c:124`).

## 5. Integration

Covered fully in Part I. In brief: one shared header, and a list of
command tables that the dispatcher walks in order. Merging the two
modules required two lines of real change plus the resolution of three
conflicts (a duplicated `proc_list`, five handlers whose source was not
handed over, and one symbol defined in the wrong file).

The clearest evidence that the merge is real rather than cosmetic: M1's
`top` calls M2's `proc_list()` every frame. M1's own copy of that code
was deleted so there is exactly one owner.

## 6. Testing

### 6.1 Method

A shell suite, `tests/run_all.sh`, runs the actual binary and matches its
output against an expected pattern. Three groups:

- **per module** — every command in the manual, with both valid and
  invalid arguments
- **integration** — three checks aimed at the seam itself: that `top`
  reaches M2's code, that both tables are reachable from `help`, and that
  a script mixing M1 and M2 commands runs
- **examples** — the four handbook demo programs still compile and behave

### 6.2 Result

```
34 passed, 0 failed
```

from a clean `make clean && make`, with zero compiler warnings under
`-std=c11 -O2 -g -Wall -Wextra`.

### 6.3 Two bugs the suite caught

**A flaky `top` test.** `top` shows the busiest 8 processes, and on an
idle machine MiniOS itself was sometimes rank 9 — so the test passed or
failed depending on load. The fix was in the program, not the test:
MiniOS's own row is now pinned regardless of rank, which is also better
behaviour for a user.

**A test that could not fail.** The "blank line is harmless" check used a
pattern that matched empty output, so it passed whatever happened. It was
replaced with an explicit assertion that the output is empty *and* the
exit code is 0. A test that cannot fail is worse than no test.

## 7. Findings

Full detail and method in Part II. The three that changed how we think:

**exec is the expensive half.** Making a process costs ~0.13 ms; loading
a program into it costs ~1.0 ms, roughly 9x more. The intuition that
"creating a process is expensive" is backwards — creation is cheap,
loading is not.

**fork does not copy memory.** Holding 256 MB and forking costs 5.5 ms if
the child exits immediately, and 184 ms if the child writes to every page
— 34x more. Pages are shared read-only and copied one at a time on first
write. This is precisely why `fork` followed immediately by `exec` is not
the waste it appears to be.

**fork copies the stdio buffer.** A program with one `printf` before
`fork` prints it twice when its output is piped, and once when it is on a
terminal. Buffering mode, not the program, decides. This is a real bug
class and the reason for `fflush(NULL)` in `run_program`.

A fourth, about method rather than operating systems: the first version
of the copy-on-write experiment measured nothing, because `gcc -O2`
deleted the child's write loop as a dead store. The result looked
plausible — 256 MB appearing *faster* than 1 MB — which is the dangerous
kind of wrong. `volatile` fixed it. We kept the episode in the report
because the lesson generalises: **an optimiser can delete the thing you
are trying to measure.**

## 8. Limitations and what remains

**Not built.** Modules 3–5 — pipes (Chapter 5), threads and race
conditions (Chapters 6–7), semaphores and the bounded-buffer queue
(Chapter 7) — and the optional xv6 system call (Chapter 8, Appendix A).

**Partly handed over.** Module 2's own extensions (`ps`, `wait`, `kill`,
`runlimit`, `limit`) were listed in its command table but their source
files were not provided. The five rows are commented out with a four-step
restore note rather than deleted.

**Experiments stop at Chapter 4.** The handbook's week 8 is
"producer-consumer and performance", which is experiment material we
cannot produce without Modules 4 and 5.

**Known rough edges.**

- `processes` allocates a fixed 8192-entry array; a machine with more
  processes would be silently truncated.
- `top` re-reads the entire process table every frame rather than
  tracking changes. Measured at 0.413 ms for 64 processes, so it does not
  matter here, but it is linear and would at 2000.
- Thermal readings are untested on this machine — it has no sensors. The
  zero-sensor path is exercised; the reading path is not.

**What the next merge needs.** Nothing structural. A new module is one
table in `minios.h`, one line in `shell.c`'s table list, and its `.c`
file dropped into `src/`. The Makefile already globs. Part I section 9
has the five-step procedure.

## 9. References

1. Course handbook — *Operating Systems: MiniOS, A Learn → Run →
   Understand → Modify → Build Project*, Version 4 Revised. Chapters 3,
   4, 10, 12, 13.
2. `proc(5)` man page — the format of every file under `/proc`,
   particularly the 52 fields of `/proc/<pid>/stat`.
3. `fork(2)`, `execve(2)`, `wait(2)`, `select(2)`, `termios(3)` man pages.
4. All measurements in Part II were produced by the programs in
   `experiments/`; raw output is in `docs/results.txt`.
