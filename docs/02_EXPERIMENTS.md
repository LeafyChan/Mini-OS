# MiniOS — Experiments and Analysis

*Six experiments. Every number below was measured, not looked up. Graded component: 15%.*

**Reproduce everything:**

```
make
./experiments/run_experiments.sh
```

Raw output is in `docs/results.txt`.

---

## The machine

```
kernel    Linux 6.18.44
cpus      2
memory    8031 MB
compiler  gcc 13.3.0
flags     -std=c11 -O2 -g -Wall -Wextra
```

**Absolute times are machine-specific.** Run it on your own laptop and
the milliseconds will differ. The *ratios* are the result; they held
across every repeat we did.

---

## Experiment 1 — What does fork cost, and what does exec add?

**Question.** The handbook treats `fork()` and `exec()` as two separate
steps. How separate are they, in cost?

**Method.** 300 iterations each, after a warm-up round.

```
A.  fork + _exit + wait      just making a process
B.  fork + exec + wait       making it AND loading /bin/true
B - A                        the price of exec alone
```

**Result.**

Each benchmark is run 7 times and the **median** is reported. A single
timing on a shared machine is mostly noise; one unlucky run cannot move
the middle value, but it would wreck a mean.

| what | median | range over 7 reps |
|---|---|---|
| fork + _exit + wait | 0.125 ms | 0.117 – 0.149 |
| fork + exec + wait | 1.130 ms | 1.088 – 1.399 |
| **exec alone** | **1.005 ms** | — |

```
exec makes it slower by   9.04x     (worst case 11.98, best case 7.31)
```

**Analysis.** Making a process is the cheap half. **exec costs about 9x
the fork that precedes it**, and stayed above 7x in every rep.

That ordering is not obvious, and it is worth being able to explain:

- `fork()` duplicates bookkeeping — a task struct, page tables marked
  read-only. No program is read from disk.
- `exec()` does the heavy work — find the file, check permissions, tear
  the old address space down, map the new ELF, resolve the dynamic
  linker, load `libc`, run the relocations, then jump to `_start`.

**Why the spread is wide.** This is a 2-CPU shared VM, so scheduling
noise dominates the absolute numbers. The ratio stayed above 7x in every
rep (worst 11.98x, best 7.31x), and that range — not the single median —
is the claim we are willing to defend.

---

## Experiment 2 — Does fork() copy the parent's memory?

**Question.** Handbook 4.3 shows parent and child have independent
variables. The obvious conclusion is that fork copies everything. Is it
true?

**Method.** Parent allocates `N` MB and writes one byte in every 4096-byte
page, so the pages genuinely exist. Then time fork+wait, 40 rounds.

Two cases:

```
case A   child calls _exit(0) immediately — never touches the memory
case B   child writes one byte per page — forces every page to be real
```

**Result.**

| parent holds | A: child exits | B: child writes | B / A |
|---|---|---|---|
| 1 MB | 0.22 ms | 0.96 ms | 4.4x |
| 16 MB | 0.85 ms | 12.38 ms | 14.5x |
| 64 MB | 2.01 ms | 45.93 ms | 22.9x |
| 256 MB | 5.50 ms | 184.48 ms | **33.6x** |

Per megabyte, at 256 MB:

```
case A   0.0215 ms / MB
case B   0.7206 ms / MB      <- 34x more
```

Note the ratio *grows* with size (4.4x → 33.6x). At 1 MB the fixed cost of
fork itself dominates; by 256 MB the page copying dominates. That trend is
itself evidence for the explanation below.

![](figures/fig7_cow_chart.pdf)

**Analysis.** The answer is **no — fork does not copy the memory.**

![](figures/fig4_cow.pdf)

```
   fork()
     |
     +-- page tables duplicated, every page marked READ-ONLY
     |   both processes point at the SAME physical pages
     |
     +-- child writes to a page
           |
           v
         hardware fault  ->  kernel copies THAT ONE PAGE  ->  resume
```

This is **copy-on-write**. The copy is deferred until somebody writes,
and only the written page is copied.

**But case A is not flat, and that matters.** 1 MB costs 0.22 ms, 256 MB
costs 5.50 ms — 25x more for 256x the memory. So fork is not free even
when nothing is copied: the kernel still has to walk and duplicate the
page *tables*. 256 MB is 65,536 pages, and each needs an entry. That work
is proportional to the number of pages, just ~34x cheaper than copying
their contents.

**Why this makes `run` fast.** When you type `run date`, the child
immediately calls `exec`, which throws the whole inherited address space
away. Every page copied would have been wasted. Copy-on-write means
almost none were. That is the entire reason the fork+exec pair is a
sensible design and not an obviously wasteful one.

**A note on method.** The first version of this experiment used
`memset()` in the child and produced nonsense — 256 MB appeared *faster*
than 1 MB. At `-O2`, gcc saw that nothing read the buffer afterwards and
deleted the loop. The fix was `volatile char *`. The lesson is in the
code comment, and it is a fair thing to be asked about: **an optimiser
can delete the thing you are trying to measure.**

---

## Experiment 3 — Zombie and orphan processes

**Question.** What happens when the parent misbehaves?

**Method.** Create each condition deliberately, then read the state
letter and PPID back out of `/proc/<pid>/stat`.

**Result.**

```
ZOMBIE — child exits, parent does not wait()

  before wait()     pid=3841  state=Z  ppid=3840     <- ZOMBIE
  after  wait()     pid 3841 is gone from /proc


ORPHAN — parent exits first, child keeps running

  grandchild now    pid=3844  state=S  ppid=1        <- REPARENTED
```

**Analysis.**

**Zombie.** The child is finished — its memory is gone, it will never run
again. But its exit status has to be stored somewhere, because the parent
has not asked for it yet. The kernel keeps a stub entry in the process
table. State `Z`. The entry vanishes the instant `waitpid()` collects it.

A zombie is not a leak of memory. It is a leak of **PID table entries** —
and the table is finite, so a server that forks in a loop and never waits
eventually cannot fork at all.

**Orphan.** The child outlives its parent and would have PPID pointing at
nothing. Linux reparents it to PID 1. Observed: `ppid=1`. init calls
`wait()` forever, so the orphan gets reaped when it finally exits and
never becomes a permanent zombie.

**Tie-back to our code.** `run_program()` in `process.c` calls `waitpid`
in a loop that retries on `EINTR`. That loop is why `run` never leaves a
zombie behind. Verify it live:

```
MiniOS> run /bin/true
MiniOS> processes -t        # no Z rows
```

---

## Experiment 4 — What does our own `processes` command cost?

**Question.** `top` calls `proc_list()` once per frame. Is that
affordable at a fast refresh rate?

**Method.** Links against the project's real `process.o` — this measures
*our* implementation, not a rewrite. 100 full scans.

**Result.**

```
processes on this machine        64
one full /proc scan              0.413 ms   (median of 7 batches)
cost per process                 0.0065 ms
scans possible per second        2422
```

Projected cost of running `top` continuously:

| refresh | CPU used by the scan |
|---|---|
| 2.0 s (default) | 0.021 % |
| 0.1 s | 0.413 % |

**Analysis.** 6.5 microseconds per process. A full scan costs about
**3x what a bare `fork()` costs** (0.413 ms vs 0.125 ms) — a useful sense
of scale: reading the entire process table is the same order of magnitude
as creating one process.

The cost is linear in process count, because each process means one
`open` + `read` + `close` on `/proc/<pid>/stat`. On a busy server with
2000 processes this would be ~13 ms per scan, and a 0.1 s refresh would
start to cost real CPU. At 64 processes it is free.

**Design conclusion.** The 2-second default refresh is justified by the
measurement, not by taste. We could refresh 10x faster and still spend
under 0.5% of one core.

---

## Experiment 5 — stdio buffering survives fork()

**Question.** Run the handbook's `fork_demo` twice — once to the terminal,
once through a pipe. Same program, same input. Same output?

**Result.**

```
to a terminal                      through a pipe
---------------                    ----------------
Before fork                        Before fork
I am the parent. Child PID=16589   I am the parent. Child PID=16601
I am the child.  PID=16589         Before fork            <- again!
                                   I am the child.  PID=16601

"Before fork" printed 1 time       "Before fork" printed 2 times
```

(The *order* of the parent and child lines varies between runs — they are
two processes racing. The *count* does not, and the count is the result.)

**Analysis.** The program has exactly one `printf("Before fork\n")`. It
appears twice. Nothing forked twice — `fork()` copied a *buffer*.

```
to a terminal                 through a pipe
stdout is line-buffered       stdout is fully buffered

printf -> flushed at '\n'     printf -> sits in the buffer
          buffer EMPTY                  buffer HOLDS "Before fork\n"
             |                             |
           fork()                       fork()
             |                             |
       child gets empty buffer      child gets a COPY of the
             |                      buffer, still holding the text
       child exits, nothing extra         |
                                    child exits -> flushes its copy
                                          -> prints it a second time
```

**Why it matters for MiniOS.** This is a real bug class, not a curiosity.
`run_program()` in `process.c` calls:

```c
fflush(NULL);              /* otherwise buffered output is duplicated */
pid = fork();
```

That one line is the fix. Delete it and `./minios script.txt > out.txt`
starts duplicating lines, while `./minios` interactively looks perfectly
fine — the worst kind of bug, because it only appears when redirected.

Also note the child uses `_exit(127)` and not `exit(127)` after a failed
exec. `exit()` would flush the inherited stdio buffers a second time;
`_exit()` skips that.

---

## Experiment 6 — MiniOS measuring itself

`run` reports wall-clock time for each child, so the shell can be its own
stopwatch.

```
[run] /bin/true          exited with status 0 after    2.6 ms
[run] /bin/date          exited with status 0 after    1.3 ms
[run] ex_hog_cpu 0.2     exited with status 0 after  207.1 ms
```

**Analysis.** `/bin/true` does nothing at all and still costs 1–3 ms —
that is pure fork+exec overhead, and it is the same order as Experiment 1
(1.13 ms) measured by a completely separate program. Two independent
measurements agreeing is the useful part. (`run` is a single untimed
sample, so it sits at the noisy end of the Exp 1 range rather than at
the median.)

`hog_cpu 0.2` was asked for 0.2 s of CPU and took 207.1 ms wall-clock.
The ~7 ms gap is fork+exec overhead plus scheduling. On a loaded
machine wall-clock would exceed CPU time by much more, because the
process would spend time in the run queue not running — which is exactly
the distinction `system` shows between uptime and idle time.

---

## Summary of findings

| # | question | finding |
|---|---|---|
| 1 | fork vs exec cost | exec is ~9x the cost of the fork before it (never below 7x) |
| 2 | does fork copy memory | no — copy-on-write; forcing the copy costs 34x more |
| 2b | is fork free then | no — page-table setup is still linear in page count |
| 3 | zombie | exists until `wait()`; leaks PID slots, not memory |
| 3b | orphan | reparented to PID 1, reaped normally |
| 4 | cost of our /proc scan | 6.5 us per process; `top` uses 0.021% CPU |
| 5 | buffering across fork | duplicated output when piped; `fflush(NULL)` fixes it |
| 6 | self-measurement | same order as Exp 1, from independent code |

**The one sentence version.** `fork()` is cheap because it copies almost
nothing; `exec()` is the expensive step; and the one thing `fork()` does
copy that people forget about is the stdio buffer.

---

## Questions an examiner could ask about this section

- *Why is exec slower than fork?* — fork duplicates bookkeeping; exec
  reads a file from disk, builds a new address space and runs the
  dynamic linker.
- *If fork doesn't copy memory, why do parent and child see different
  values?* — the pages are shared read-only; the first write faults and
  the kernel copies that single page. Independence is preserved, the
  copying is just deferred.
- *Your Experiment 2 case A still grows with size. Doesn't that disprove
  copy-on-write?* — no, it measures page-table duplication, which is
  proportional to page count. Case B is 31x more expensive, which is the
  content copy.
- *Why did your first version of Experiment 2 give nonsense?* — `-O2`
  dead-store elimination removed the child's write loop. Fixed with
  `volatile`.
- *Why does `run` call `fflush(NULL)`?* — Experiment 5. Without it,
  buffered output is duplicated into the child.
- *How many zombies can you create before something breaks?* — until the
  PID table is exhausted; see `/proc/sys/kernel/pid_max`.
- *Why report a median and not an average?* — one scheduling hiccup on a
  shared VM adds a large outlier. It moves a mean a lot and a median not
  at all.
- *Would `top` be usable on a machine with 2000 processes?* — the scan is
  linear, so ~10 ms per frame. Fine at 2 s refresh, questionable at 0.1 s.
