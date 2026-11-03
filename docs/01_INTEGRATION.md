# MiniOS — Functionality and Integration

*How Module 1 and Module 2 became one program. Graded component: 45%.*

---

![](figures/fig1_architecture.pdf)

## 1. What each module owns

```
MODULE 1  (Anvitha)               MODULE 2  (Srujana)
handbook Chapter 3                handbook Chapter 4
--------------------              --------------------
src/shell.c    prompt + dispatch  src/process.c   run, processes
src/util.c     helpers
src/monitor.c  system             fork() -> exec() -> waitpid()
src/cpustat.c  cpu
src/dashboard.c top

reads:  /proc/uptime              reads:  /proc/<pid>/stat
        /proc/meminfo
        /proc/loadavg
        /proc/stat
```

Nobody edits anybody else's `.c` file. That is the whole rule.

---

## 2. How they are joined

One header, one list. Nothing else.

```
          src/shell.c          src/process.c
               |                     |
               |  both #include      |
               +----------+----------+
                          v
                 include/minios.h
                  (the contract)
```

`minios.h` says *what exists*.
Each `.c` file says *how it works*.
Neither module includes the other's source.

### The seam, in two places

**A. `include/minios.h`** — one line per module's table:

```c
extern const command_t cmds_shell[];     /* M1 */
extern const command_t cmds_process[];   /* M2 */
```

**B. `src/shell.c`** — one row per module:

```c
static const command_t *const all_tables[] = {
    cmds_shell,      /* M1 — Anvitha */
    cmds_process,    /* M2 — Srujana */
    NULL
};
```

That is the entire integration. Two lines of real change.

![](figures/fig2_dispatch.pdf)

---

## 3. What a typed word does now

```
you type:  run date
    |
    v
shell_run()              reads the line
    |
    v
shell_execute_line()     trim -> split into words
    |
    v
shell_find_command()     walks table 1 (M1): help system cpu top quit
    |                    not found
    |                    walks table 2 (M2): run processes
    |                    FOUND
    v
cmd_run()                in process.c — M2's code
    |
    v
run_program()            fork -> exec -> waitpid
```

Before the merge it stopped after table 1.
After the merge it keeps walking. That is it.

---

## 4. The three conflicts we had to resolve

Honest list. These are real things that broke.

### 4.1 Both modules defined `processes`

M1's standalone build had its own `src/procinfo.c` with `proc_read`,
`proc_list` and `cmd_processes`. M2's `process.c` has the same three
functions.

```
    BEFORE                         AFTER
    procinfo.c  proc_list()        procinfo.c   DELETED
    process.c   proc_list()        process.c    proc_list()   <- kept
    -> duplicate symbol               -> one owner
```

**Decision: M2 keeps it.** Chapter 4 is their chapter.
`src/procinfo.c` was deleted and the `processes` row was removed from
M1's table in `shell.c`.

M1's `top` now calls M2's `proc_list()`. That is not a workaround —
that is the integration working.

### 4.2 Five commands with no code behind them

`process.c` arrived with a 7-row command table. Only 2 of the 7
handlers are in the file:

```
run        cmd_run        -> defined in process.c   OK
processes  cmd_processes  -> defined in process.c   OK
ps         cmd_ps         -> bgjobs.c    NOT HANDED OVER
wait       cmd_wait       -> bgjobs.c    NOT HANDED OVER
kill       cmd_kill       -> bgjobs.c    NOT HANDED OVER
runlimit   cmd_runlimit   -> rlimit.c    NOT HANDED OVER
limit      cmd_limit      -> rlimit.c    NOT HANDED OVER
```

Linking with all 7 gives five `undefined reference` errors.

**Decision: the five rows are commented out, not deleted.**
See the marked block in `src/process.c`. To restore:

1. drop `bgjobs.c` and `rlimit.c` into `src/`
2. uncomment the prototypes at the bottom of `include/minios.h`
3. uncomment the five rows in `src/process.c`
4. `make`

Nothing else changes. The Makefile picks up new `.c` files on its own.

### 4.3 A symbol in the wrong file

`minios_interrupted` (the Ctrl-C flag) was defined in `shell.c`, but
`util.c` reads it inside `sleep_ms()`. Any tool that links `util.c`
without `shell.c` failed to link.

**Fix:** moved the definition into `util.c`, next to the code that
uses it. `shell.c` still sets it from the signal handler.

---

## 5. Build it

```
make              everything           -> ./minios, build/ex_*, build/exp*
make examples     just the handbook demos
make experiments  the same, then tells you how to run them
make clean        start over
```

Flags: `-std=c11 -O2 -g -Wall -Wextra -D_GNU_SOURCE`

Builds with **zero warnings**. That is deliberate — a warning in this
codebase means something is actually wrong.

---

## 6. Run it

```
./minios                  interactive
./minios demo.txt         run a script of commands
./minios -c "system"      run one command
./minios --version
```

### The commands

```
MODULE 1
  help [command]              list commands, or explain one
  system                      uptime, memory, load  (Ch 3)
  cpu [sample_ms]             per-core usage, temperatures
  top [interval] [frames]     live dashboard, q to quit

MODULE 2
  run <program> [args]        fork -> exec -> wait  (Ch 4)
  processes [-m|-c] [-n N] [-t]  process table from /proc

  quit / exit
```

---

## 7. Prove it works

```
./tests/run_all.sh
```

34 checks. Every command in section 6, plus the four handbook
example programs, plus three checks that specifically test the
seam between the modules:

```
PASS  top uses M2's proc_list
PASS  both tables reachable
PASS  script mode runs mixed cmds
```

Last run: **34 passed, 0 failed.**

---

## 8. Demonstrating it

The full run sheet — timings, the exact commands, what to say for each
one, and what to do if something fails in the room — is **Part III**.

The short version, if you only need the order:

```
./minios
help -> system -> cpu 500 -> run date -> run nosuchthing
     -> processes -m -n 10 -> processes -t -> top (q) -> quit
```

No typing at all:

```
./minios demo.txt
```

## 9. Adding Module 3, 4 or 5

The seam is designed for this. For a new module `foo`:

```
1. write src/foo.c, ending with:

       const command_t cmds_foo[] = {
           { "bar", cmd_bar, "bar [n]", "what it does" },
           { NULL, NULL, NULL, NULL }
       };

2. add to include/minios.h:

       extern const command_t cmds_foo[];
       cmd_status_t cmd_bar(int argc, char **argv);

3. add to the list in src/shell.c:

       cmds_foo,     /* M3 */

4. add the label to owners[] in cmd_help()

5. make
```

No other file is touched. No Makefile edit — it globs `src/*.c`.

**Two rules for the team:**

- You may **add** to `minios.h`. You may not **change or delete**
  a line someone else added — their `.c` file depends on it.
- Two modules must never claim the same command word. The first
  table in the list wins, silently, which is a horrible bug to find.
