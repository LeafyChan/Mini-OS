# Part III — Project Demonstration

*The run sheet for the live demo. Graded component: 20%.*

Eight minutes, rehearsed. The aim is not to show every feature — it is to
show that we know **which OS idea each command demonstrates**.

## 1. Before you start

Run this the morning of, not five minutes before.

```
cd minios-m1-m2
make clean && make          expect: no warnings at all
./tests/run_all.sh          expect: 34 passed, 0 failed
./minios demo.txt           expect: runs to the end, no errors
ls build/exp1_fork_vs_exec  expect: it exists (the demo runs it)
```

Then set the room up:

```
[ ] terminal at least 100 x 30, or top's bars wrap and look broken
[ ] font size up — the examiner is reading from across a desk
[ ] clear the screen; no editor, no other tabs
[ ] cd into the project directory and stay there
[ ] close anything heavy; a loaded machine makes 'cpu' look odd
[ ] have docs/MiniOS_Evaluation.pdf open on a second window
```

## 2. Who says what

| Minutes | Part | Presenter |
|---|---|---|
| 0:00–0:30 | What MiniOS is, the architecture diagram | M1 |
| 0:30–3:00 | Module 1 — `system`, `cpu`, `top` | M1 |
| 3:00–5:00 | Module 2 — `run`, `processes` | M2 |
| 5:00–6:00 | Integration — `help`, the table list, `top` using M2 | M1 |
| 6:00–7:30 | Experiments — Exp 1, 2 and 5 | either |
| 7:30–8:00 | What is not built yet, and the plan | M1 |

Both of you should be able to deliver all of it. The examiner may swap
you deliberately.

## 3. The script

Each step: the command, then the one sentence that earns the mark. Say
the sentence **while** the output is on screen.

### Opening (30 s)

```
./minios
```

> "MiniOS is a small control room for the machine. Two modules are merged
> so far — Module 1 reads the system through /proc, Module 2 creates and
> watches processes. Everything you'll see is ordinary C calling ordinary
> system calls."

### Module 1 (2 min 30 s)

```
help
```

> "Commands are grouped by module. Each module owns one table, and the
> shell walks the tables in order. Adding Module 3 is one line."

```
system
```

> "All of this came out of /proc. Not a system call — we opened three
> text files that don't exist on disk. The kernel writes them when you
> read them."

*If asked where:* `/proc/uptime`, `/proc/meminfo`, `/proc/loadavg`.

```
cpu 500
```

> "Here's the one thing worth remembering. /proc/stat holds counters that
> only go up, since boot. A single reading is the average since boot,
> which is useless. We take two readings 500 ms apart and subtract — and
> *that* is the usage right now."

*Point at the formula if they want it:*

```
busy% = 100 * (change in total − change in idle) / change in total
```

```
top
```

> "Same two-reading trick, repeated every two seconds. The bars are
> redrawn with ANSI escape codes, the terminal is in raw mode so `q`
> is picked up immediately, and `select()` waits for either the timer
> or a keypress — so it's responsive without spinning the CPU."

Let it refresh **twice**, press `+` once to show the interval change,
then `q`.

> "Note the MiniOS row — it's pinned even when it's idle, so you can
> always see the process you're talking to."

### Module 2 (2 min)

```
run date
```

> "Three system calls. `fork` makes a second process — it does *not* load
> a program. `exec` loads `date` into that process — it does *not* make
> one. `waitpid` collects the exit status. The timing is ours."

```
run definitelynotreal
```

> "Status 127. `exec` failed, but the child already existed — so it still
> had to die, with `_exit`, not `exit`. If we'd used `exit` it would have
> flushed the buffers it inherited from the parent and we'd see duplicate
> output."

```
processes -m -n 10
```

> "One line of /proc/<pid>/stat per process. 52 fields; we parse six. The
> name is in brackets and can contain brackets itself — `(Web Content)` is
> real — so we search for the *last* close bracket, not the first."

```
processes -t
```

> "Only our own children. Right now that's just MiniOS, because `run`
> waits properly and leaves no zombies behind."

### Integration (1 min)

> "The two modules never include each other. They share one header."

Show `include/minios.h` and `src/shell.c:45`:

```c
static const command_t *const all_tables[] = {
    cmds_shell,      /* M1 */
    cmds_process,    /* M2 */
    NULL
};
```

> "And they genuinely touch: `top` is Module 1, but it calls Module 2's
> `proc_list()` once per frame. Module 1 can't list processes on its own
> any more — we deleted that code deliberately so there's one owner."

```
./tests/run_all.sh
```

> "34 checks, including three that specifically test the seam."

### Experiments (1 min 30 s)

Pick these three. Do not run all six live — too slow.

```
./build/exp1_fork_vs_exec
```

> "exec costs about nine times the fork before it. The second step is the
> expensive one, which surprised us — exec reads a file from disk and
> runs the dynamic linker; fork just duplicates bookkeeping."

```
./build/exp2_cow
```

> "This answers 'does fork copy the memory?'. No. When the child exits
> straight away, holding 256 MB costs 5.5 ms. When the child writes to
> every page and forces the copy, the same fork costs 184 ms — 34 times
> more. That's copy-on-write."

> "And it's why `run` is fast: the child calls exec immediately and throws
> the whole address space away, so almost nothing was ever copied."

```
./build/ex_fork_demo
./build/ex_fork_demo | cat
```

> "Same program, run twice. Piped, 'Before fork' prints twice. The program
> only prints it once — `fork` copied the *buffer*. A terminal is
> line-buffered so it's already flushed; a pipe isn't. That's why
> `run_program` calls `fflush(NULL)` before forking."

### Closing (30 s)

> "Modules 3 to 5 — pipes, threads, semaphores — aren't merged yet. The
> framework for them is done: a new module is one table, one line in the
> header, one line in the shell. Our Chapter 5 to 7 experiments are
> planned but need that code."

Do not oversell. An honest boundary reads better than a vague claim.

## 4. The one-command fallback

If anything goes wrong, or you are short on time:

```
./minios demo.txt
```

Runs every command non-interactively, echoing each one. Works with no
typing and no terminal tricks.

## 5. Questions likely to interrupt the demo

Short answers, so you can carry on.

| Question | Answer in one breath |
|---|---|
| "Is that real data?" | Yes — compare with `uptime` / `free -m` / `htop` in another terminal. |
| "Why no temperatures?" | This machine has no `/sys/class/thermal` sensors. Normal in a VM or WSL. The code handles zero sensors. |
| "Why is cpu0 at 0%?" | 500 ms is a short window on an idle machine. `cpu 2000` gives a longer one. |
| "Did you write this or copy it?" | Offer a line. `src/process.c:100` — the last-bracket parse — is the one to explain. |
| "What happens on Ctrl-C?" | Handler sets a flag only. `fgets` returns `EINTR`, we print a newline and reprompt. No `SA_RESTART`, on purpose. |
| "Show me a zombie." | `./build/exp3_zombie_orphan` — state `Z` before `wait()`, gone after. |

## 6. If it breaks live

```
top looks frozen          -> you're in raw mode; press q. If stuck, Ctrl-C.
terminal echo is gone     -> type  reset  and press Enter (blind). It works.
bars wrap / look wrong    -> window too narrow. Widen, rerun.
'cpu' shows 0% everywhere -> idle machine, short window. Try  cpu 2000.
a command errors          -> say what you expected, then ./minios demo.txt
build fails on their box  -> make clean && make. If it still fails, show
                             docs/results.txt from a known-good run.
```

**The rule:** if something fails, say what *should* have happened and
move on. Do not debug live. You have eight minutes.
