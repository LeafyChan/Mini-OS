# Part V — Individual Viva Preparation

*The handbook's own checklist and examiner questions, answered. Graded
component: 10%.*

The handbook is explicit that this is individual, and that siloing is not
allowed:

> Group work does not mean that one person can own processes, another
> pipes, another threads and nobody else learns them.

So this covers what we built **and** gives an honest position on what we
have not built yet.

## A. The handbook's 13-item checklist

Items 1–5 and 12 are covered by Modules 1 and 2. Items 6–11 and 13 belong
to Modules 3–5, which have not been handed over; honest answers for those
are in section D.

### 1. I can explain /proc in simple words

`/proc` looks like a folder of files but nothing is on disk. The kernel
generates the text at the moment you read it. It is a **view onto kernel
data structures, dressed up as files** — which means ordinary tools
(`cat`) and ordinary C (`fopen`, `fscanf`) can inspect the running system
with no special API.

*If pushed:* numbered directories are one per process. `/proc/self` is
whoever is asking. The files have size 0 because there is nothing stored
until you read.

*Show:* `src/monitor.c:19` — `fopen("/proc/uptime")`, then `fscanf`.

### 2. I can run and explain a fork() program

```
./build/ex_fork_demo
```

`fork()` is called once and returns twice, because after it there are two
processes. Both continue from the same line.

### 3. I can explain the parent/child return values from fork()

| where | fork() returns | why |
|---|---|---|
| parent | the child's PID | the parent may have many children; it needs to know which one |
| child | 0 | the child has only one parent and can always find it with `getppid()` |
| failure | −1 | no child was made; check `errno` |

The asymmetry is the point: it is the **only** way the two processes can
tell each other apart, since everything else about them is identical.

### 4. I can explain why parent and child do not automatically share ordinary variables

Each process has its own address space. After `fork()` the child has its
own copy of the variable, so writing it in one does not affect the other.

**The twist, which the handbook flags as "weird":** both can print the
*same address* while holding *different values*. Those are **virtual**
addresses. Each process has its own page table, so the same virtual
address maps to a different physical page.

**The part we measured (Experiment 2):** the copy does not happen at
`fork()`. Pages are shared read-only, and the first write triggers a
fault that copies that one page. Copy-on-write. We measured forcing the
copy at **33.6x** the cost of not forcing it.

*Show:* `./build/ex_fork_memory_demo`, then Part II Experiment 2.

### 5. I can explain fork → exec → wait

```
MiniOS> run date

MiniOS
  |
  +-- fork()            now there are two of us
       |
       +-- parent ----- waitpid()   blocks until the child is done
       |
       +-- child ------ execvp("date")   stops being MiniOS,
                                         becomes date
```

![](figures/fig3_fork_exec_wait.pdf)

Three separate jobs:

- `fork()` makes a process. It does **not** load a program.
- `exec()` loads a program. It does **not** make a process — it replaces
  the one calling it. On success it never returns.
- `waitpid()` lets the parent collect the exit status and stops the child
  becoming a zombie.

*Why separate?* Because the gap between fork and exec is where the child
can change things that the new program will inherit — redirect stdout,
close file descriptors, set resource limits. A single combined call would
leave nowhere to do that. This is exactly where shell redirection lives.

*Show:* `src/process.c:39–66`.

### 12. I can explain how the MiniOS components fit together

```
        src/shell.c              src/process.c
             |                         |
             |    both #include        |
             +-----------+-------------+
                         v
                 include/minios.h
```

`minios.h` declares; each `.c` implements; no `.c` includes another. The
dispatcher walks a list of command tables, one per module
(`src/shell.c:45`). Adding a module is one line in that list.

The modules do genuinely touch: M1's `top` calls M2's `proc_list()` once
per frame. That is the integration working, not a workaround.

*Show:* Part I sections 2 and 3.

## B. The handbook's examiner questions

Answers kept to what an examiner will accept out loud.

**Why does fork() return different values?**
So the two identical processes can tell which one they are. See A.3.

**Why does the child not start executing at main()?**
Because `fork()` does not start a program — it duplicates one already
running. The child resumes at the instruction after `fork()`, with the
same program counter, stack and open files. `main()` is only entered when
a program is *loaded*, which is `exec`'s job, not `fork`'s.

**Why are their ordinary variables independent?**
See A.4 — separate address spaces, copy deferred by copy-on-write.

**Why does the child use exec()?**
Because the child is still a copy of MiniOS, and we wanted `date`.
`exec` replaces the program while keeping the process.

**Why does the parent wait?**
Two reasons. To get the exit status, which is otherwise lost. And to reap
the child — without `wait`, the finished child stays as a zombie holding
a PID table slot. Demonstrated in Part II Experiment 3.

**Why must unused pipe ends be closed?** *(M3 — not built)*
A reader gets EOF only when **every** write end is closed. If the reader
still holds its own copy of the write end, it waits forever for itself.
See section D.

**What do threads share? / Why is count++ unsafe? / What is the critical
section? / What does each semaphore represent?** *(M4, M5 — not built)*
See section D.

**What is the path of a system call in xv6?** *(optional, not built)*
See section D.

## C. Questions about our actual code

The examiner has the source open. These are the lines worth being ready
for.

**`run_program` calls `fflush(NULL)` before `fork()`. Why?**
`src/process.c:39`. Buffered output that has not been flushed is *in the
buffer*, and `fork()` copies the buffer. Both processes then flush it, so
the line prints twice. Only visible when output is redirected, because a
terminal is line-buffered and a pipe is not. We measured it — Part II
Experiment 5.

**Why `_exit(127)` and not `exit(127)` when exec fails?**
`src/process.c:50`. `exit()` flushes stdio and runs `atexit` handlers. The
child inherited copies of the parent's buffers, so flushing them would
duplicate the parent's pending output. `_exit()` ends the process without
touching them. 127 is the shell convention for "command not found".

**`waitpid` is in a `while` loop. Why?**
`src/process.c:55`. A signal can interrupt a blocking call, which returns
−1 with `errno == EINTR`. That is not a failure — it means "try again".
Without the loop, pressing Ctrl-C during `run` would abandon the child.

**Why search for the LAST `)` in `/proc/<pid>/stat`?**
`src/process.c:100`. Field 2 is the executable name in brackets, and that
name can itself contain spaces and brackets — `(Web Content)` is real.
Splitting on whitespace or the first `)` breaks on those. Searching
backwards is safe because every field after it is a number.

**Why multiply RSS by `sysconf(_SC_PAGESIZE)/1024`?**
`src/process.c:124`. The kernel reports RSS in **pages**, not bytes. Page
size is 4096 here but is not guaranteed, so we ask rather than hardcode.

**Explain the CPU percentage formula.**
`src/cpustat.c:77–85`.

```
busy% = 100 * (change in total − change in idle) / change in total
```

`/proc/stat` holds counters that only ever increase, since boot. A single
reading tells you the average since boot, which is useless. Two readings
and a subtraction tell you what happened in between. `idle` includes
`iowait` because the CPU had nothing to run then either.

**Why is `minios_interrupted` declared `volatile sig_atomic_t`?**
`include/minios.h`, set at `src/shell.c:131`. `volatile` stops the
compiler caching it in a register — the loop would never see the change.
`sig_atomic_t` guarantees the write is not torn halfway by a signal.

**Why no `SA_RESTART` on the SIGINT handler?**
`src/shell.c:140`. With it, `fgets` would silently resume after Ctrl-C and
the user would see nothing happen. Without it, `fgets` returns with
`EINTR`, we print a newline and give a fresh prompt — which is what a
shell should do.

**`cmp_rows` compares doubles without casting to int. Why the fuss?**
`src/dashboard.c:70`. `return (int)(x - y)` on doubles truncates: 0.4−0.3
becomes 0 and the sort silently does nothing. We compare and return −1/0/1
instead.

**Why does `top` restore the terminal on every exit path?**
`src/dashboard.c:60`. We turned off `ICANON` and `ECHO` in the user's
terminal. Those settings belong to the terminal, not to us — if we exit
without restoring them, the user's shell is left with no echo and they
have to blind-type `reset`.

## D. The modules we have not built — honest answers

Do not bluff these. The correct move is a short accurate answer plus
"that is Module N, which is not merged yet."

**Pipes (M3).** A pipe is a one-way byte stream in kernel memory with two
file descriptors — `fd[0]` to read, `fd[1]` to write. It carries bytes,
not messages, so a reader can get half of what was written and must frame
its own records. The reader sees EOF only when every copy of the write end
is closed, which is why each side closes the end it does not use.

**Threads (M4).** Threads of one process share the address space — globals
and the heap — so a pointer means the same thing in all of them. Each has
its own stack and registers. That sharing is the point and the danger.

**Why `count++` is unsafe (M4).** It is three machine operations: load,
add, store. Two threads can both load 5, both store 6, and one increment
disappears. The **critical section** is the stretch of code that must not
be interleaved — here, those three operations.

**Semaphores in a bounded buffer (M5).** Three, with distinct jobs:
`empty` counts free slots (producer waits on it), `full` counts items
(consumer waits on it), `mutex` protects the buffer structure itself.
Count semaphores are waited on **before** the mutex — the other order
deadlocks, because a thread would sleep holding the lock everyone needs.

**Full vs empty queue (M5).** They are different conditions with different
waiters: full blocks producers, empty blocks consumers. One flag cannot
represent both, which is why there are two counting semaphores.

**xv6 syscall path (optional).** User program calls the stub in `usys.S`,
which puts the syscall number in register `a7` and executes `ecall`. The
trap enters `uservec`, saves the user registers, switches to the kernel
page table, calls `syscall()`, which looks the number up in a table and
calls the handler. The return value goes back in `a0`, then `sret`
returns to user mode.

## E. Two-minute self-test before you walk in

Say each of these out loud. If you hesitate, reread the section.

```
[ ] /proc is the kernel answering, not files on disk
[ ] fork returns twice; child 0, parent the PID, -1 on failure
[ ] fork makes a process, exec loads a program, wait collects the status
[ ] the gap between fork and exec is where redirection happens
[ ] same address, different value = virtual memory + copy-on-write
[ ] zombie = finished but not yet waited for; leaks PID slots
[ ] orphan = parent died first; reparented to PID 1
[ ] one /proc/stat reading is useless; you need two and a subtraction
[ ] fflush(NULL) before fork, or buffered output doubles
[ ] the modules meet in one header and one table list
```

## F. If something goes wrong in the viva

- **You do not know.** Say so, then say what you would do to find out
  (`man 2 fork`, read the source, write a test). The handbook's own tone
  invites this: *"You are allowed to be confused."*
- **You are asked about a teammate's module.** Give the section D answer,
  then say plainly it is not merged yet.
- **The examiner finds a bug.** Agree if they are right. Say how you would
  test the fix. Defending a real bug costs more than the bug does.
- **A demo command fails.** `./minios demo.txt` runs everything
  non-interactively. Part III section 6 has the per-command fallbacks.
