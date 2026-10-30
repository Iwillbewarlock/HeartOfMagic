# BookXP - SpellLearning C++ API Test Plugin

Pure SKSE DLL plugin (no ESP) that grants spell learning XP when the player reads normal books. Tests the full `SpellLearningAPI.h` C++ integration.

## What It Does

1. At `kPostPostLoad`: receives `ISpellLearningAPI*` from SpellLearning's `kMessageType_APIReady` broadcast (SKSE messaging)
2. On `kDataLoaded`: Registers "Book Reading" as a custom XP source (creates UI controls)
3. On `BookMenu` open: Queries all active learning targets via API, grants XP to each, and shows
   "+XP to N spell(s)" with `api->ShowNotification("hud.bookXpGranted", ...)` (API v2) - the XP the spells
   really got after the multipliers and the cap ("7.5", or a range such as "3-15" when spells differ; up to
   1.5.0 it always said 15): in the panel's
   language, and not at all when Settings > XP Alerts is off. With an older Heart of Magic (API v1)
   it shows the English text itself. (A Papyrus version, `SL_BookXP_QuestScript`, shipped in earlier releases
   but no plugin ever attached it; it was removed.)
4. Once per book: the book on screen (`RE::BookMenu::GetTargetForm()`) gives XP once. A book joins the list when
   at least one spell got XP from it (read with no learning target it can give XP later); spell tomes are skipped.
   The base forms are kept in the SKSE co-save (unique id `SLBX`, record `READ` v1). A save without that record
   (from before this) takes the books the game marks read (`TESObjectBOOK::IsRead()`) at its load. When the book
   on screen cannot be told, XP is given without the check (logged as a warning).

## C++ API Demonstrated

| API Call | Stage | Purpose |
|----------|-------|---------|
| `RegisterListener("SpellLearning", OnSpellLearningMessage)` | Plugin load | Listen for SpellLearning's broadcast |
| `kMessageType_APIReady` → `ISpellLearningAPI*` | kPostPostLoad | Receive full API access |
| `api->RegisterXPSource("book_reading", ...)` | kDataLoaded | Register source → creates UI controls |
| `api->GetLearningTarget(school)` | BookMenu | Query active learning targets |
| `api->AddSourcedXP(formId, 15.0, "book_reading")` | BookMenu | Grant XP through cap system |

## Architecture

```
SL_BookXP.dll                              SpellLearning.dll
    │                                           │
    │◄──kPostPostLoad: APIReady broadcast───────┤ ISpellLearningAPI* as data
    │                                           │
    ├─kDataLoaded─►api->RegisterXPSource()────►│ RegisterModdedXPSource()
    │              "book_reading"                │ → UI creates slider controls
    │                                           │
    ├─BookMenu opens────────────────────────────│
    │  api->GetLearningTarget("Destruction")    │ Returns target FormID
    │  api->AddSourcedXP(formId, 15, "book..")  │ → cap/multiplier → XP added
    │                                           │
```

## Files

```
plugins/BookXP/
├── CMakeLists.txt         # Build config (uses add_commonlibsse_plugin)
├── src/
│   └── Main.cpp           # SKSE plugin (BookMenu watcher + API integration)
└── README.md              # (PCH.h, Common.h and SpellLearningAPI.h come from plugins/)
```

## Building the DLL

Built automatically as part of the HeartOfMagic super-build. From the repository root:

```powershell
.\BuildRelease.ps1                       # Builds all targets
cmake --build build --target SL_BookXP   # Build only BookXP
```

Shares CommonLibSSE-NG, vcpkg dependencies, and compiler flags with the main SpellLearning plugin via the top-level `CMakeLists.txt`.

## Deploy (DLL approach - no ESP needed)

```
SL_BookXP_RELEASE/
└── SKSE/
    └── Plugins/
        └── SL_BookXP.dll
```

## Balance

Default: 15 XP per book read, applied to each active learning target.

With default SpellLearning settings (cap 25%):

| Tier | Required XP | Max from Books | Books to Cap |
|------|------------|----------------|--------------|
| Novice | 100 | 25 | ~2 |
| Apprentice | 200 | 50 | ~3 |
| Adept | 400 | 100 | ~7 |
| Expert | 800 | 200 | ~13 |
| Master | 1500 | 375 | ~25 |

Users adjust via the "Book Reading" multiplier (0-200%) and cap (0-100%) sliders in SpellLearning settings
(Settings > Progression > Modded XP Sources). Up to 1.5.0 the saved values reached the game only after the panel
saved its settings once in that session; since 1.5.1 they apply from the game's start.

## XP Flow

```
Player opens book → BookMenu event fires
  → api->GetLearningTarget() for each school
  → api->AddSourcedXP(targetId, 15.0, "book_reading")
    → × source multiplier (user-configurable, default 100%)
    → × global multiplier
    → clamped to: requiredXP × source cap (default 25%)
    → minus already-tracked XP from "book_reading"
    → actual XP added to spell progress
    → SpellLearning_XPGained ModEvent fired
```
