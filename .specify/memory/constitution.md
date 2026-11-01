<!--
Sync Impact Report
- Version change: template → 1.0.0 (first ratification)
- Principles: I. CLAUDE.md Is the Rulebook; II. Measure in Game First; III. Every Feature Survives;
  IV. Build, Test and Review Gates; V. Smallest Change That Works
- Added sections: Performance Standards, Development Workflow, Governance
- Removed sections: none
- Deferred TODOs: none
-->
# Heart of Magic Constitution

## Core Principles

### I. CLAUDE.md Is the Rulebook

`CLAUDE.md` at the repository root is the project's binding rule set (600-line file limit, read
CommonLibSSE-NG headers before using RE/SKSE types, `var` only and no ES modules in PrismaUI code,
theme variables for colours, all six designs supported, docs updated in the same change, no AI
footers). This constitution MUST NOT restate or override those rules; where the two disagree,
`CLAUDE.md` wins and this file is amended.

Rationale: one source of rules; Spec Kit adds the spec → plan → tasks order, not a second rulebook.

### II. Measure in Game First

A performance change MUST start from a measured baseline taken in game (the `[Perf]` log lines,
timers in the SKSE log, or a self-test that writes PASS/FAIL), and MUST report the same measurement
after the change. Desktop Chrome or harness timings MAY guide work but MUST NOT be the evidence a
change is judged by, because the in-game Ultralight view renders on the CPU without a JIT or GPU.
Each report states which part of the remaining time is ours and which is the game's or PrismaUI's.

Rationale: desktop timings have misled this project before.

### III. Every Feature Survives

An optimization or port MUST list what the old path did (inputs read, options honoured, outputs
written, rare data shapes, add-on content) and confirm each item on the new path, saying which were
verified and which were not. Byte-equal output on a few test inputs is not enough on its own.

Rationale: a faster path that silently drops a feature is a regression.

### IV. Build, Test and Review Gates

Before a change is called done: `.\BuildRelease.ps1` passes (never while Skyrim is running), the
tools self-tests and panel tests pass, and a substantial change gets an independent review
(reviewer agent and GPT/Codex, findings checked against code before acting). What could only be
checked in game is listed for the user with why it cannot be automated.

### V. Smallest Change That Works

Fix the measured bottleneck where every caller passes through; no speculative optimization, no
abstraction nobody asked for. A simplification with a known limit says so in a code comment.

## Performance Standards

- Panel: no visible frame drops in game while the panel is open and in use (the user's target);
  partial gains are progress, not done.
- Scan and tree build: wall time of a full scan on the user's load order, reported in milliseconds
  with the spell count.
- Game loading: time the plugin adds at data load and at save load, reported in milliseconds.
- Times are frame times or wall times in ms, not FPS.

## Development Workflow

Spec Kit order per feature: specify → clarify (when open questions remain) → plan → tasks →
implement → converge. Work happens on a feature branch; release branches are not used for trials.

## Governance

This constitution sits under `CLAUDE.md` and the user's own instructions. Amendments are made by
editing this file with a version bump (MAJOR: a principle removed or redefined; MINOR: a principle
or section added; PATCH: wording) and updating the dates. Each spec and plan checks itself against
the principles above.

**Version**: 1.0.0 | **Ratified**: 2026-11-01 | **Last Amended**: 2026-11-01
