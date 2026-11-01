# Feature Specification: Scan Time

**Feature Branch**: `speckit-perf`

**Created**: 2026-11-01

**Status**: Draft

**Input**: User description: "HoML 최적화 - 스캔 시간"

## Baseline (2026-11-01)

- The latest game log has no scan or tree-build lines: the auto scan skipped ("load order
  unchanged"), so the current build's scan time on the user's load order (4,329 plugins, 10,001
  spell forms, 1,365-spell tree) is not measured.
- Recorded: `CollectScan` 0.42 s and `FinishScan` about 1.3 s, mostly perk adapters, still stop the
  game (docs/ARCHITECTURE.md:516). The freeze was expected to fall from about 5 s to about 1.7 s on
  3,800 spells, worked out from old timings and "not yet measured in game" (CHANGELOG.md:439-442).
- Tree build runs on a background thread; no whole-build time for the real tree is written down.
- Existing tools: timing lines in `UIManagerScanner.cpp`, `AutoScan.cpp`, the Classic builder; the
  AutoScan HITCH self-test (`selftest.json` `"autoScan"`) that compares the longest frame during the
  scan with the main-menu baseline plus 25 ms.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Measure today's scan on the real load order (Priority: P1)

The user starts one full scan (panel "Scan" and a tree build) and one auto scan with the self-test
on; the log records each step's time and the longest game freeze.

**Why this priority**: the 1.7 s figure is an estimate; the work starts from a measurement
(constitution II).

**Independent Test**: one session with `selftest.json` `"autoScan"` on and one manual full scan;
Claude reads the log and writes the step table.

**Acceptance Scenarios**:

1. **Given** the self-test on, **When** the game reaches the main menu with a changed load order (or a
   forced rescan), **Then** the log holds the AutoScan step lines and a HITCH PASS/FAIL line.
2. **Given** the panel open, **When** the user runs a full scan and builds the tree, **Then** the log
   holds the collect, perk adapter, Spell Research, send and build times.

---

### User Story 2 - Shorter game freeze during a full scan (Priority: P2)

The player presses Scan and the game does not hang noticeably.

**Why this priority**: a frozen game is the part the player feels; background time matters less.

**Independent Test**: the same full scan after the change; compare game-thread times.

**Acceptance Scenarios**:

1. **Given** the change deployed, **When** the user runs a full scan, **Then** the game-thread time
   meets SC-002 and the scan output is the same as before (SC-004).

---

### User Story 3 - Faster tree build (Priority: P3)

After the scan, the tree is ready sooner.

**Why this priority**: it runs in the background, so it is waiting time, not a freeze.

**Independent Test**: the build time line before and after on the same scan data.

**Acceptance Scenarios**:

1. **Given** the same scan data, **When** the tree is built, **Then** the build time drops and the
   tree is identical, or every difference is explained.

### Edge Cases

- Load orders with add-on spell mods, perk overhauls (adapters) and Spell Research present or absent.
- A scan started while the game is busy (cell load) or with the panel closed.
- Very large load orders (10,000+ spell forms).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Every scan step that runs on the game thread MUST have a timing line.
- **FR-002**: An optimized step MUST produce the same output as before on the user's load order
  (scan JSON and tree compared), or every difference MUST be listed and explained (constitution III).
- **FR-003**: Work MAY move off the game thread only where the engine allows it (headers checked).
- **FR-004**: The scan stays rule-based with no AI (the public release condition).

### Key Entities

- **Scan step**: name, thread, time in ms, spell count.
- **Scan output**: the scan JSON and the built tree, used for before/after comparison.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A measured step table for the current build on the user's load order.
- **SC-002**: The longest game-thread freeze during a full scan is at most
  0.5 s (from about 1.7 s estimated; user 2026-11-01).
- **SC-003**: The AutoScan HITCH self-test passes.
- **SC-004**: Scan output and tree unchanged on the user's load order (diff empty or explained).

## Assumptions

- The user can force one rescan (load order change or a reset) and run one manual full scan.
- `debug.json` in the dev install lets the full scan also write `spell_scan_output.json` for diffing.
