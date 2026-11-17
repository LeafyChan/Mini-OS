# MiniOS M5 — xv6 Kernel Implementation

## 1. Overview

M5 extends the MiniOS project with a modified xv6-riscv kernel. The implementation adds a system call for retrieving the current year, process accounting, a process-information system call, and a user-space process-monitoring utility.

The xv6 source is maintained as a Git submodule, independently version-controlled on the `m5-kernel` branch.

## 2. Repository Structure

```text
Mini-OS/
├── xv6/                         # xv6-riscv Git submodule
│   ├── kernel/
│   │   ├── proc.c
│   │   ├── proc.h
│   │   ├── pstat.h
│   │   ├── syscall.c
│   │   ├── syscall.h
│   │   ├── sysproc.c
│   │   ├── trap.c
│   │   └── defs.h
│   ├── user/
│   │   ├── getyeartest.c
│   │   ├── getpinfotest.c
│   │   └── ps_xv6.c
│   ├── docs/
│   │   └── xv6_trace_report.md
│   └── Makefile
│
├── xv6-patches/
│   ├── apply.py
│   └── reference.patch
│
└── tests/
    ├── test_m5.sh
    └── run_all.sh
```

## 3. Implemented Features

### 3.1 getyear() system call

Implements a system call that returns the year 2026.

The user-space test program `getyeartest` verifies the system call.

### 3.2 Process accounting

The xv6 process structure is extended with accounting fields:

- `ctime`: process creation time.
- `rtime`: accumulated runtime ticks.
- `nsched`: number of times the process was scheduled.

Runtime accounting is updated during timer interrupts, and scheduling counts are updated when a process enters the RUNNING state.

### 3.3 getpinfo() system call

Implements:

```c
int getpinfo(struct pstat *);
```

The system call retrieves process information and copies it to user space.

The process-information structure includes:

- PID and parent PID.
- Process state.
- Memory size.
- Runtime ticks.
- Scheduling count.
- Creation time.
- Process name.

The implementation validates user-space addresses and avoids holding process-table spinlocks while performing `copyout()`.

### 3.4 ps_xv6 utility

A user-space process-monitoring utility that displays process information in a table.

It supports normal process listing and CPU-load testing through its `spin` mode.

## 4. Patch Management

The `xv6-patches` directory contains:

- `reference.patch`: consolidated Git patch containing the M5 changes.
- `apply.py`: Python patch application and verification utility.

The patch is based on the xv6 baseline commit:

```text
fa2ddcb
```

The patcher:

1. Checks that the xv6 source directory exists.
2. Verifies that it is a Git repository.
3. Checks whether the patch has already been applied.
4. Applies the patch if it can be applied cleanly.
5. Verifies whitespace correctness.
6. Optionally builds xv6.

The patcher is idempotent: running it again on an already-patched source tree does not apply the changes a second time.

### Usage

Apply the patch:

```bash
python3 xv6-patches/apply.py
```

Apply the patch to a specific xv6 directory:

```bash
python3 xv6-patches/apply.py --xv6 /path/to/xv6
```

Apply the patch and build xv6:

```bash
python3 xv6-patches/apply.py --build
```

## 5. Build and Test

Initialize the repository and its submodule:

```bash
git clone --recurse-submodules <repository-url>
cd Mini-OS
```

Build the MiniOS application:

```bash
make
```

Run the M5 tests:

```bash
bash tests/test_m5.sh
```

Run the complete test suite:

```bash
bash tests/run_all.sh
```

The M5 test suite verifies:

- Unique system-call numbers.
- Successful xv6 compilation.
- Clean patch application.
- Patch idempotency.
- QEMU boot and shell startup.
- `getyear()` functionality.
- `getpinfo()` functionality.
- `ps_xv6` output.
- Runtime accounting under CPU load.
- Absence of kernel panics during testing.

## 6. Verified Test Results

The complete MiniOS test suite was executed successfully.

| Test suite | Passed | Failed |
|---|---:|---:|
| M1 and M2 | 34 | 0 |
| M3 | 20 | 0 |
| M4 | 19 | 0 |
| M5 | 4 | 0 |
| Integration | 16 | 0 |
| **Total** | **93** | **0** |

All test suites passed in the recorded run.

## 7. Debugging and System Call Trace

The GDB-based system-call execution trace is documented separately in:

```text
xv6/docs/xv6_trace_report.md
```

It records the transition from user mode to kernel mode, system-call dispatch, return-value handling, and restoration of user execution.

## 8. Git Workflow

The parent MiniOS repository and xv6 submodule are independently version-controlled.

After updating xv6:

```bash
cd xv6
git add .
git commit -m "Describe xv6 changes"
git push origin m5-kernel
```

Update the parent repository's submodule reference:

```bash
cd ..
git add xv6
git commit -m "Update xv6 submodule"
git push origin main
```

This ensures that a fresh clone retrieves the intended xv6 revision.
