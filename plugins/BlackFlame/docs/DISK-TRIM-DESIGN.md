# Black Flame: keeping only the files that are used (disk trim)

Status: built 2026-10-26 (contract 9), not yet run in game. Route: the single-launch slot route (`singleLaunch` true, as the
release ships it). Code: `DiskTrimRules.h` (the rules, tested by `blackflame-cosave-check`), `CopyContractPublished.h` (the
list both sides read, in the shared contract table), `BuildProgressTrim.*` (list and trim), `CopyRequestNeeded.*` (which dump
files are needed), `CopyRequestDump.*` (`Only`), builder `GreyProgress.*` (`StagedMatches` takes the list).

## Measured before (author's PC, ~4,000 mods, 2026-10-26)

| Where | Size | What it is |
|---|---|---|
| Data (MO2 Overwrite) `meshes|textures\heartofmagic\blackflame` | 0.91 GB | the published grey copies the game loads |
| `<SKSE log folder>\HeartOfMagic_BlackFlame\staging` | 0.93 GB | the builder's output: 2708 of its 2712 files (929 of 930 MiB) have the size of their Overwrite file |
| `<SKSE log folder>\HeartOfMagic_BlackFlame\data` | 0.81 GB | the dump; with the load order settled no model, muzzle, MPS or TXST object was left to build |

Of the dump, a read-only pass over request.json, progress.jsonl and the not-fire file (session scratchpad `measure.py`)
found only effect shader items still without a line: 160 palettes judged not fire (no line, 9.7 MiB, read again every run),
3 bases (4.3 MiB), and 120 bases the builder's not-fire file names (281 MiB, kept if that file were not read).

## What each work folder was for (evidence)

**staging** is the builder's output and its cache.
- The builder writes every object there and appends its line to `staging\progress.jsonl` (GreyKeyedRun.cpp,
  GreyKeyedTextures.cpp `Commit`).
- At its start the builder compacts progress.jsonl and keeps an ok/fallback line only while its file is in staging with its
  size and fnv64 (GreyProgress.cpp `Compact` -> `StagedMatches`); `ProgressLog::Reusable` skips an object only when the same
  holds for its file and every dep. So a staged file that is gone meant the object was built again.
- The DLL copies a line's file staging -> Data through the VFS (BuildProgressCopy.cpp `CopyIn`); it copied, never moved,
  for that reason.
- The DLL's start read already accepted a file that is only in Data (BuildProgress.h: kInData; a line is dropped only when its
  file is "neither in Data nor in staging").

**data** (the dump) is what the builder reads its sources from. `CopyRequestV5::Write` dumped every NIF of the closure and
every planned texture every start (the incremental dump of 2026-10-23 only avoided rewriting unchanged files). The builder
reads a source only for an object it builds (an object whose line is reusable is skipped first: GreyKeyedRun.cpp, the EFSH
passes), plus our two palettes every run (`VerifiedPrefix`). A source listed as dumped but absent makes the object *failed*
(retryable), never a fallback (GreyKeyedRun.cpp "is not in the dump", GreyKeyedTextures.cpp and GreyTexStats.cpp "listed as
dumped but not in the dump", GreyKeyedEfsh/Bases).

Hard links and moves were ruled out: staging lives under `Documents\My Games` and Data copies land in MO2's Overwrite through
usvfs (other volume, virtual path), and the builder needs to know what is published.

## Change A: no duplicate in staging

1. `BuildProgress::ReadAtStart` now runs before the dump; then `WritePublished` (kDataLoaded, before the builder launch, not
   when another game holds the folder) removes the old `staging\published.tsv` and writes a new one: every progress line's own
   file and copy dep whose Data file has exactly the line's size and fnv64 (`DataFiles`: this session's hash, or the last
   session's for the same size and write time). The "keep" case (Data holds another build of the key) does not match, so it
   is not listed and its staging file stays.
2. Builder: `StagedMatches` is true when staging holds the bytes **or** the list names that path with that fnv64 (and size).
   Compaction and `Reusable` both go through it, so a published object is never rebuilt for a missing staging file.
3. `StartTrim` (after the launch, a below-normal detached thread) deletes the staging file of each listed path after hashing
   the Data file again without the cache; it keeps our copies a source names as a texture (`SlotKeys::OwnCopiesNamed`: the
   builder reads those from staging only, GreyTexStats.cpp) and stops when a load begins. Empty `copies\<key16>\` folders go
   too. Files copied this session are trimmed at the next start.
4. Contract 9: a builder that does not read the list would rebuild every trimmed object at every launch, so an older builder
   refuses the request (`contractVersion`). Keys and output bytes are unchanged: no rebuild on update.

## Change B: the dump holds only what will be built

1. `CopyRequestNeeded::Compute` lists the request's objects (every usable slot's model, muzzle and TXST key; every keyed
   effect shader palette and base) and the dump files each reads: a model or muzzle its NIF and every texture it names
   (`NifDump::TexturePathsIn`, the plan's own rule), a TXST its slot textures, a palette or base its path. Our palettes always.
2. An object is done (`DiskTrimRules::LineDone`) when its last line is ok or fallback with its own file and every copy dep
   listed (with the line's bytes) or in staging (with its size), or not_dumped. too_large is never done (a larger job memory
   cap tries again). A base with no line that the builder's not-fire file names is done (keys only; a new fire test version
   makes the builder judge it again, find no source, record it failed, and the next start keeps everything).
3. Any failed line at the start, a progress file not read to its end, or no list: the whole dump (as before).
4. `Dump` with `Only`: needed files are written or kept by their stamps; any other dumped file is deleted and left out of the
   manifest's stamps. The manifest's `needed` value (a hash of the set) is part of the "unchanged" test. The request is the
   same as before: a left-out texture is still listed as dumped, so a mapping gap gives a failed object (and a full dump at the
   next start), never a silent fallback.

## Review (GPT adversarial review of the design, 2026-10-26), checked against the code

| Finding | Held? | What was done |
|---|---|---|
| Deleting on the cached hash (DataFiles.cpp: same size and write time reuse the last hash) | yes | the trim hashes the Data file again before each delete |
| Our copies named by a source are read from staging only (SlotKeys.cpp TextureHash, GreyTexStats.cpp) | yes | those staging files are never trimmed (`OwnCopiesNamed`) |
| too_large with a larger cap, and the keep case, are not "done" for the builder | yes | `LineDone` follows the builder: too_large never done, the list must hold the line's own bytes |
| Not-fire EFSH bases have no line and would stay in the dump (281 MiB measured) | yes | the DLL reads the not-fire file's keys |
| Switching MO2 profiles A -> B -> A: B does not list A's trimmed files, the builder drops their lines, A rebuilds them | yes, accepted | one rebuild (about 1.5 minutes of GPU work on the author's PC) after such a switch; the Overwrite folder is shared by all profiles, so only copies moved into a profile-only mod are affected |

## Limits

- A trimmed copy that later leaves Data (the Overwrite cleared, moved into a mod another profile lacks) is rebuilt at the next
  start that cannot see it.
- A start that has something to build (a new or updated mod, a builder identity change) reads and writes those sources at
  kDataLoaded, as before; an unchanged load order dumps nothing.
- A mapping gap in `CopyRequestNeeded` costs one extra launch (failed object, then a full dump), named in the log.

## Expected after (author's PC)

staging: 930 MiB -> about 1 MiB (progress.jsonl, keepsets, caches, copies a source names). data: 827 MiB -> about 15 MiB
(not-fire palettes, the 3 pending bases, our palettes). Work folder total about 0.02 GB; with the 0.91 GB in Overwrite about
0.93 GB in all, down from about 2.6 GB. To be confirmed by the log lines "disk trim: ..." after the first two starts.

## How the existing files go (no manual delete)

First start of the new version: `published.tsv` is written, the dump step deletes every dump file nothing pending reads
("files left out" in the "request v5" line), and the trim thread deletes the staging duplicates at the main menu ("disk trim:
N staging duplicate(s) ... deleted"). A load that starts before it is done leaves the rest for the next start.
