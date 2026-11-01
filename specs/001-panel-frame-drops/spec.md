# Feature Specification: Panel Frame Drops

**Feature Branch**: `speckit-perf`

**Created**: 2026-11-01

**Status**: Draft

**Input**: User description: "HoML 최적화 - 패널 프레임 드랍"

## Baseline (2026-11-01)

- The latest game log (`SpellLearning.log`, 2026-11-01 11:58) has no `[Perf]` lines: the panel's
  developer mode was off, so there is no in-game frame data for the current build.
- Last recorded in-game figures (docs/DESIGN.md:828-842): the slowest frame after a zoom fell from
  97-144 ms to 12-19 ms, but the loop still turned only every 34-37 ms with animations stilled;
  Classic had 80 ms stalls, Arcane 223 ms stalls as shipped (before later fixes).
- Offline lab figures (docs/DESIGN.md:856-862, 2026-10-08, Ultralight 1.4.1 in prismaui-lab): e.g.
  Classic open worst 27 ms, zoom p95/worst 12/19 ms; lab numbers run better than the game.
- Written-down open items: the 34-37 ms loop turn may be PrismaUI's or the game's own time
  (DESIGN.md:873-880, two unverified hypotheses); Arcane's worst stall not yet confirmed in game
  (DESIGN.md:881-883).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Know where the panel stands today (Priority: P1)

The player (the user) opens the panel in game with developer mode on, uses it the usual way (open,
drag the tree, zoom, hover nodes, open a card) in each of the six designs, and the log records how
long frames take and how much of that is the panel's own drawing.

**Why this priority**: every later change is judged against this; without it optimization is guesswork
(constitution II).

**Independent Test**: one short play session produces `[Perf]` and `[Input]` lines for each design;
Claude reads the log and writes a per-design table.

**Acceptance Scenarios**:

1. **Given** developer mode on and the current build deployed, **When** the user plays the scripted
   session (about 1 minute per design), **Then** the log holds at least one `[Perf]` line per design
   and per action (idle, drag, zoom, hover).
2. **Given** that log, **When** Claude reads it, **Then** the report states for each design the worst
   frame, the longest stall, the loop turn and the panel's share of it.

---

### User Story 2 - No visible stutter while using the panel (Priority: P2)

The player drags, zooms and hovers in any design without the view hitching.

**Why this priority**: this is the user's stated goal ("프레임드랍 없는 수준").

**Independent Test**: the same scripted session after the change; compare the table with User Story 1.

**Acceptance Scenarios**:

1. **Given** the change deployed, **When** the scripted session runs, **Then** the panel's measured
   numbers meet SC-002 and SC-003 in every design.
2. **Given** a click during a drag or zoom, **When** the user presses, **Then** `[Input]` shows the
   press handled as the right kind (click or drag) with no late handling above SC-004.

---

### User Story 3 - Less load on the game while the panel is closed or idle (Priority: P3)

With the panel closed, or open and untouched, the game's frame rate is not lowered by the panel.

**Why this priority**: an earlier report of lower FPS with the panel closed (CHANGELOG.md:435).

**Independent Test**: game frame time with the panel closed, compared with the mod's panel removed.

**Acceptance Scenarios**:

1. **Given** the panel closed, **When** the player walks a fixed route, **Then** the game frame time
   differs from the no-panel run by no more than measurement noise.

### Edge Cases

- A very large tree (1,365 spells today; larger load orders exist) at the widest zoom.
- Fast repeated zoom in and out; dragging while a card preview opens.
- Designs with heavy decoration (Arcane, Night Grimoire) versus the plain ones.
- Time the panel cannot shorten (PrismaUI or the game) must be named, not counted as ours.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The measurement session MUST be written down as fixed steps so before and after runs
  are comparable.
- **FR-002**: Each report MUST split frame time into the panel's own drawing and the rest
  (PrismaUI/game), using the existing `[Perf]` fields.
- **FR-003**: Changes MUST keep every design working and every panel feature intact (constitution I
  and III); a list of features checked accompanies the change.
- **FR-004**: Desktop or lab timings MAY guide work but MUST NOT be the pass/fail evidence.
- **FR-005**: The two unverified hypotheses about the 34-37 ms loop turn MUST be checked or
  explicitly left open with the reason.

### Key Entities

- **Perf sample**: one `[Perf]` line (frames, avg, worst, stall, loop turn, our share, design, zoom).
- **Input sample**: one `[Input]` line (press time, movement, lateness, click or drag).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An in-game baseline table exists for all six designs and four actions.
- **SC-002**: Worst frame during drag, zoom and hover is at most
  33 ms (no frame longer than two 60 Hz frames; user 2026-11-01)
  in every design.
- **SC-003**: No stall of 50 ms or more in any 5-second `[Perf]` window during use.
- **SC-004**: No press handled later than 50 ms.
- **SC-005**: The user, playing normally, does not notice stutter (the one check that stays visual).

## Assumptions

- The user can run one scripted session before and one after (about 6-8 minutes each).
- Developer mode can be switched on in the panel's settings for those sessions.
- Time spent inside PrismaUI or the game is out of scope to fix, but is measured and reported.
