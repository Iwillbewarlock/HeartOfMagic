# Feature Specification: Game Loading Time

**Feature Branch**: `speckit-perf`

**Created**: 2026-11-01

**Status**: Draft

**Input**: User description: "HoML 최적화 - 게임 로딩 시간"

## Baseline (2026-11-01, `SpellLearning.log` 11:55-11:58, checked by Claude)

Load order: 4,329 plugins, 10,001 spell forms, 1,365-spell tree.

- Game start, data loaded: the plugin's handler ran from 11:55:18.443 to 11:55:20.114, about
  1.7 s on the game thread (worked out from timestamps). Largest parts:
  - Perk adapters: 1,420 ms (log line 101).
  - Vampire caster read: about 394 ms (lines 92-100), with eight "record could not be read" warnings.
  - Startup tree applied: 102 ms (line 565).
  - About 870 "Resolved ... persistent ID" info lines written twice (log noise, inferred).
- Panel ready 2.2 s after the startup tree (line 567); not covered by the plugin's own timing lines.
- Save load: the plugin's own work was `SpellResearch: load` in 79 ms (line 1076). The 18.9 s
  between revert and the cosave load is not logged by the plugin and is most likely the engine's
  own load (inferred).
- Background: the tome reach pass took 5,106 ms off the game thread.

### Baseline with stage timers (2026-11-01 19:21, Phase 0-1 build, `SpellLearning.log` lines 100-273)

Same load order (4,330 plugins). Copies of the log and both parity files: `build/parity-baseline/`.

| Step | ms | Target |
|---|---|---|
| Data loaded, whole handler | 1,862 | ≤ 500 |
| - perk adapters | 1,714 | |
| -- source (tomes 2, catalog ids resolved 34) | 36 | |
| -- plan | 1,478 | |
| --- spell JSON (vampire caster read, 507, is inside it) | 867 | |
| --- effect users index | 6 | |
| --- planner and effect resolves (the rest) | about 605 | |
| -- catalog 52, files 22, keywords 4, reconcile 2, report 3, parity 3 | 86 | |
| - tree | 97 | |
| - everything else | 51 | |
| Save loaded, whole handler | 111 | ≤ 100 |
| - Spell Research (its own `load` line: 75) | 91 | |
| - tomes 6, rest 13 | 19 | |
| Co-save read / revert | 0 / 0 | |

The perk adapter sub-lines add to 1,600 ms; the 114 ms between that and the handler's 1,714 is
outside the adapter's own timer (not split yet).

### After Phase 2c, candidate planner (2026-11-01 21:23, `SpellLearning.log` lines 100-133, 347)

Same load order and save. Both parity files are byte-identical to `build/parity-baseline/`
(SHA-256 compared); TreeParity PASS, 399 keywords on effects, 21 casting perks as before.

| Step | Before ms | After ms |
|---|---|---|
| Data loaded, whole handler | 1,862 | 1,381 |
| - perk adapters | 1,714 | 1,230 |
| -- plan | 1,478 | 985 |
| --- spell JSON (vampire caster read inside) | 867 | 767 |
| --- planner and effect resolves (the rest) | about 605 | about 210 |
| - tree | 97 | 103 |
| Save loaded, whole handler | 111 | 119 |
| - Spell Research (its own `load` line) | 91 (75) | 110 (91) |

The save load did not change in code; its 8 ms rise is run-to-run spread in Spell Research's
own step (not checked over several runs). SC-002 (≤ 500 ms) is not met: 881 ms over, most of it
the spell JSON (767) and the 245 ms outside the adapter's sub-lines.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Faster game start (Priority: P1)

The player launches the game; the plugin's work at data load takes less time before the main menu.

**Why this priority**: it is the largest measured cost (about 1.7 s, 1.4 s of it perk adapters) and
it hits every launch.

**Independent Test**: launch the game twice (before and after) on the same load order and compare
the data-loaded handler time and the perk adapter line.

**Acceptance Scenarios**:

1. **Given** the change deployed, **When** the game starts, **Then** the handler time meets SC-002
   and the perk adapters apply the same keywords (399 on effects today).

---

### User Story 2 - The plugin adds nothing noticeable to save loading (Priority: P2)

The player loads a save; the plugin's share of the load is measured and stays small.

**Why this priority**: the plugin's measured share is already small (79 ms); the goal is to measure
the whole share (cosave and post-load) and keep it so.

**Independent Test**: load the same save before and after; read the plugin's timing lines.

**Acceptance Scenarios**:

1. **Given** a save load, **When** the log is read, **Then** every plugin step during the load has a
   timing line and their sum meets SC-003.

---

### User Story 3 - Panel ready sooner after start (Priority: P3)

The panel is usable sooner after the main menu appears.

**Why this priority**: 2.2 s of waiting that is not yet explained.

**Independent Test**: time from startup tree applied to panel ready, before and after.

**Acceptance Scenarios**:

1. **Given** the change deployed, **When** the game starts, **Then** the log shows where the 2.2 s
   goes, and the panel's own part is shortened or shown to be PrismaUI's.

### Edge Cases

- First launch after a load order change (auto scan runs) versus an unchanged load order.
- No perk overhaul installed (no adapters) and Spell Research absent.
- New game versus loading a save; loading a second save in the same session.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Every plugin step at data load, new game and save load MUST have a timing line.
- **FR-002**: A faster step MUST give the same result (same keywords applied, same tree, same tomes),
  listed and checked (constitution III).
- **FR-003**: Repeated info lines that carry no new information SHOULD be reduced to one summary line,
  keeping warnings intact.
- **FR-004**: Engine load time outside the plugin is out of scope and MUST be reported as such, not
  claimed as a gain.

### Key Entities

- **Load step**: name, event (data loaded, new game, save load), thread, time in ms.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A table of load steps with times for the current build (from the baseline log above).
- **SC-002**: The plugin's game-thread time at data load is at most
  0.5 s (from about 1.7 s; user 2026-11-01).
- **SC-003**: The plugin's game-thread time during a save load is at most 100 ms.
- **SC-004**: Results unchanged: 399 keywords on effects, 1,365 of 1,365 tree entries applied,
  self-tests pass.

## Assumptions

- The user can launch the game twice and load one save each time.
- The perk adapter work may be cached or moved only if its results stay identical.
