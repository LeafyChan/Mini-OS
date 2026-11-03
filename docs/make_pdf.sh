#!/bin/sh
# Builds docs/MiniOS_Evaluation.pdf from the two markdown documents.
#
#   ./docs/make_pdf.sh
#
# Needs: pandoc + a LaTeX engine (xelatex).
#   sudo apt install pandoc texlive-xetex texlive-fonts-recommended lmodern
#
# Builds two documents:
#   MiniOS_Evaluation.pdf   the full report, all five parts
#   MiniOS_Handout.pdf      the 2-page summary for the examiner
#
# Edit any of 0*.md or handout.md, then re-run this.
# Figures live in docs/figures/ -- redraw the chart with
#   python3 docs/figures/make_chart.py

DOCS=$(cd "$(dirname "$0")" && pwd)
cd "$DOCS" || exit 1

command -v pandoc  >/dev/null 2>&1 || { echo "pandoc not installed";  exit 1; }
command -v xelatex >/dev/null 2>&1 || { echo "xelatex not installed"; exit 1; }

TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

# ---------------- title page + front matter ----------------
cat > "$TMP/front.md" <<'FRONT'
---
title: "MiniOS --- Modules 1 and 2"
subtitle: "Integration, Experiments, Demonstration, Report and Viva"
author:
  - "**Kernel.\\_.Crew**"
  - "CS2101 Operating Systems --- Mini Project"
  - "Module 1 --- Anvitha  ·  Module 2 --- Srujana"
  - "Modules 3--5 --- Aarnavi, Advik, Krishna (upcoming)"
geometry: "a4paper, margin=2.2cm"
fontsize: 11pt
linestretch: 1.15
colorlinks: true
linkcolor: RoyalBlue
toc: true
toc-depth: 2
numbersections: false
header-includes:
  - \usepackage{fancyhdr}
  - \pagestyle{fancy}
  - \fancyhf{}
  - \fancyhead[L]{\small MiniOS --- Modules 1 and 2}
  - \fancyhead[R]{\small CS2101 Operating Systems}
  - \fancyfoot[C]{\small\thepage}
  - \renewcommand{\headrulewidth}{0.4pt}
  - \usepackage{fvextra}
  - \DefineVerbatimEnvironment{Highlighting}{Verbatim}{breaklines,commandchars=\\\{\},fontsize=\footnotesize}
  - \usepackage{booktabs}
  - \usepackage{longtable}
  - \usepackage{graphicx}
  - \usepackage{float}
  - \makeatletter\def\fps@figure{H}\makeatother
  - \setkeys{Gin}{width=\linewidth,keepaspectratio}
---

\newpage

# About this report

This report accompanies the MiniOS source submission. It covers all five
graded components.

| Component | Weight | Where |
|---|---|---|
| Team, scope and comparison with the handbook | --- | Part 0 |
| MiniOS functionality and integration | 45% | Part I |
| Experiments and analysis | 15% | Part II |
| Project demonstration | 20% | Part III |
| Report / documentation | 10% | Part IV |
| Individual viva | 10% | Part V |

**Part 0** names the team and sets out, command by command, what the
handbook asks for and what we actually handed in.

**Part I** explains how Module 1 and Module 2 were merged into one program:
what each module owns, where the two meet, the three conflicts that had to
be resolved, how to build and run it, how to verify it, and how Modules 3
to 5 attach later.

**Part II** reports six experiments on process creation and `/proc`. Every
number was measured on the machine described there, not quoted from a
textbook. The programs that produced them are in `experiments/` and the
raw output is in `docs/results.txt`.

**Part III** is the run sheet for the live demonstration: timings, the
exact commands, the sentence to say for each one, and what to do when
something fails in the room.

**Part IV** is the project report — design decisions, what each module
does and why, testing method, findings, and an honest list of what is not
built.

**Part V** is viva preparation: the handbook's own 13-item checklist and
examiner questions answered, plus the code-level questions an examiner
can ask with our source open in front of them.

## Scope

This covers **Modules 1 and 2** (handbook Chapters 3 and 4). Modules 3 to
5 — pipes, threads, semaphores — and the optional xv6 system call are not
merged yet; Part IV section 8 states exactly where that leaves the
project, and Part V section D gives honest answers for the concepts they
cover.

## Status at the time of writing

```
build          clean, zero warnings
               -std=c11 -O2 -g -Wall -Wextra

test suite     34 checks, 34 passing
               ./tests/run_all.sh

experiments    6 experiments, all reproducible
               ./experiments/run_experiments.sh
```

## How to reproduce everything in this report

```
make                               build everything
./tests/run_all.sh                 run the 34 checks
./experiments/run_experiments.sh   reproduce Part II
./docs/figures/make_chart.py       redraw Figure 7 from the raw data
./docs/make_pdf.sh                 rebuild this PDF
```

FRONT

# ---------------- body: the two documents ----------------
# A markdown '---' on its own line becomes a stray rule in print, so it is
# dropped -- but only outside fenced code blocks, where it may be real art.
strip_rules() {
    awk '
        /^```/ { fence = !fence }
        !(!fence && $0 == "---") { print }
    ' "$1"
}

strip_rules 00_SCOPE.md \
    | sed 's|^# Part 0 — Team, and what we built against the handbook|# Part 0 — Team, Scope and Comparison|' \
    > "$TMP/z.md"

strip_rules 01_INTEGRATION.md \
    | sed 's|^# MiniOS — Functionality and Integration|# Part I — Functionality and Integration|' \
    > "$TMP/a.md"

strip_rules 02_EXPERIMENTS.md \
    | sed 's|^# MiniOS — Experiments and Analysis|# Part II — Experiments and Analysis|' \
    > "$TMP/b.md"

strip_rules 03_DEMONSTRATION.md > "$TMP/c.md"
strip_rules 04_REPORT.md        > "$TMP/d.md"
strip_rules 05_VIVA.md          > "$TMP/e.md"

{
    cat "$TMP/front.md" "$TMP/z.md"
    printf '\n\\newpage\n\n'
    cat "$TMP/a.md"
    for part in b c d e; do
        printf '\n\\newpage\n\n'
        cat "$TMP/$part.md"
    done
} > "$TMP/all.md"

pandoc "$TMP/all.md" \
    --resource-path="$DOCS" \
    -o MiniOS_Evaluation.pdf \
    --pdf-engine=xelatex \
    --highlight-style=tango \
    -V date="$(date '+%-d %B %Y')" \
    -V mainfont="DejaVu Serif" \
    -V monofont="DejaVu Sans Mono" || exit 1

echo "wrote docs/MiniOS_Evaluation.pdf"

# ---------------- the 2-page handout ----------------
pandoc handout.md \
    -o MiniOS_Handout.pdf \
    --pdf-engine=xelatex \
    --highlight-style=tango \
    --resource-path="$DOCS" \
    -V mainfont="DejaVu Serif" \
    -V monofont="DejaVu Sans Mono" || exit 1

echo "wrote docs/MiniOS_Handout.pdf"
