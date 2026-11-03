# Part 0 — Team, and what we built against the handbook

## The team

**Kernel._.Crew** — CS2101 Operating Systems mini project.

![](figures/fig6_roadmap.pdf)

## Scope, stated plainly

This submission is **Modules 1 and 2**. Modules 3–5 are not merged, and
nothing in this report claims otherwise. Part IV section 8 lists exactly
what is missing; Part V section D gives honest answers for the concepts
those modules cover, because the viva is individual.

## What the handbook asks for, and what we handed in

The handbook specifies five commands for Chapters 3 and 4 and marks seven
TODOs. All seven are filled. Everything in the right-hand column beyond
that is ours.

### `system` — handbook section 3.3

The handbook's skeleton is four TODOs:

```c
void show_system_info(void)
{
    // TODO 1: open /proc/uptime
    // TODO 2: read the required value
    // TODO 3: read one memory value
    // TODO 4: display them clearly
}
```

| Handbook asks for | We produce |
|---|---|
| uptime | uptime **and** idle time, with a human-readable duration |
| **one** memory value | 7 keys from `/proc/meminfo`: total, free, available, buffers, cached, swap total, swap free |
| — | a usage bar and a used-percentage |
| — | load averages and runnable/total task counts from `/proc/loadavg` |
| — | host, kernel release and architecture via `uname` |
| — | our own PID, so the examiner can go and read `/proc/<pid>/status` |
| "display them clearly" | aligned columns, colour when the terminal supports it |

### `run` — handbook section 4.6

Three TODOs: create a child, exec, parent waits. All three filled.

| Handbook asks for | We produce |
|---|---|
| fork → exec → waitpid | the same, plus the three defences below |
| — | `fflush(NULL)` before `fork`, so buffered output is not duplicated |
| — | `_exit` not `exit` in the child, so inherited buffers are not flushed twice |
| — | `waitpid` retried on `EINTR`, so Ctrl-C does not abandon the child |
| — | exit status decoded, including **signal** deaths as 128 + signal |
| — | wall-clock timing per child, which makes `run` its own benchmark |
| handbook 4.5: "print the exit status" | done, and distinguishes exit 127 (exec failed) |

### `processes`

| Handbook asks for | We produce |
|---|---|
| "observing processes" | PID, PPID, state, thread count, RSS, CPU seconds, name |
| — | `-m` sort by memory, `-c` sort by CPU |
| — | `-n N` limit rows |
| — | `-t` only our own children — which is how you show you leave no zombies |
| — | state letters translated (`R` → running, `Z` → zombie, …) |
| — | a summary line counting each state |

### `help` and `quit`

| Handbook asks for | We produce |
|---|---|
| `help` | `help` grouped by module, **and** `help <command>` for one command's usage |
| `quit` | `quit` and `exit`, with the terminal restored on every exit path |

## Commands the handbook does not ask for

Both have to justify themselves against the handbook's own warning —
*"Do not make MiniOS huge… Which OS concept does this feature teach?"*

| Command | The concept it teaches |
|---|---|
| `cpu` | A monotonic counter is meaningless alone. `/proc/stat` counts ticks since boot; the usage *now* only exists as the difference between two readings. |
| `top` | Three mechanisms at once: ANSI redraw, `termios` raw mode, and `select()` waiting on *either* a timer *or* stdin — the alternative is a loop that ignores the keyboard or busy-waits. |

## Beyond the code

| Handbook's starter package | What we handed in |
|---|---|
| `tests/test_m1.sh`, `tests/test_m2.sh` | one suite, **34 checks**, including 3 that test the module seam itself |
| `examples/` — 3 relevant demo programs | the same 3, still compiling, plus 2 load generators used by the experiments |
| no experiments asked for in Ch 3–4 | **6 experiments**, 405 lines, with medians over repeated runs |
| report | 5-part report: integration, experiments, demonstration, design, viva |
| — | every figure and number in it regenerates from the raw data |

## Three extra modes

Not in the handbook, but they are what make the demo and the test suite
possible:

```
./minios                 interactive
./minios -c "system"     run one command and exit
./minios demo.txt        run a script of commands, echoing each
./minios --version
```

## What we are not claiming

- Modules 3–5 do not exist in this build.
- Module 2's own extensions (`ps`, `wait`, `kill`, `runlimit`, `limit`)
  were listed in its command table but their source files were not handed
  over. Those five rows are commented out, not deleted — see Part I
  section 4.2.
- The experiments cover Chapters 3–4 only. The handbook's week 8
  ("producer-consumer and performance") needs Modules 4 and 5.
- Thermal sensor readings are untested: this machine has no sensors. The
  zero-sensor path is exercised; the reading path is not.
