# Tree Building System — Architecture & Algorithms

**Purpose:** In-depth reference for how spell trees are built, sorted, matched, and laid out across all layers of the system: C++ native builders (TreeBuilder/TreeNLP), JS layout engines, root preview modules, and PreReqMaster NLP scoring.

---

## System Overview

The tree building pipeline has four layers, each with a distinct responsibility:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  JAVASCRIPT (UI Layer)                                                  │
│                                                                         │
│  ┌─────────────┐    ┌──────────────┐    ┌─────────────────────────┐    │
│  │ Root Preview │    │ Growth Module │    │ PreReqMaster (PRM)      │    │
│  │ SUN / FLAT   │───>│ CLASSIC/TREE │───>│ NLP prerequisite locks  │    │
│  │ grid + arcs  │    │ layout + BFS │    │ TF-IDF scoring + cycles │    │
│  └─────────────┘    └──────┬───────┘    └──────────┬──────────────┘    │
│                             │ buildTree()            │ applyLocks()     │
├─────────────────────────────┼────────────────────────┼──────────────────┤
│  C++ NATIVE BUILDERS (TreeBuilder + TreeNLP)         │                  │
│  ┌──────────────────────────┴────────────────────────┘                  │
│  │ OnProceduralTreeGenerate() → TreeBuilder::Build()                 │
│  │ Reads "command" field → routes to correct builder mode              │
│  │ Build runs on background thread → result dispatched to game thread  │
│  │ via SKSE AddTask → onProceduralTreeComplete() to JS                 │
│  │                                                                      │
│  │ Builder Modes:                                                       │
│  │   ├─ "build_tree_classic"  → BuildClassic()  (Tier-first)           │
│  │   ├─ "build_tree"          → BuildTree()     (NLP thematic)         │
│  │   ├─ "build_tree_graph"    → BuildGraph()    (Edmonds' MSA)         │
│  │   ├─ "build_tree_thematic" → BuildThematic() (3D similarity BFS)   │
│  │   └─ "build_tree_oracle"   → BuildOracle()   (LLM-guided chains)   │
│  │                                                                      │
│  │ NLP Engine (TreeNLP):                                                │
│  │   TF-IDF, cosine similarity, char n-grams, Levenshtein,            │
│  │   fuzzy matching, theme scoring, PRM scoring                        │
│  └──────────────────────────────────────────────────────────────────────┘
└─────────────────────────────────────────────────────────────────────────┘
```

**Data flows one direction:** C++ builds the tree structure (nodes + parent/child links) → JS positions nodes on a visual grid and renders them.

---

## Layer 1: Root Preview Modules (JS)

Root modules define **where school root nodes sit** and generate the **grid of candidate positions** that growth modules consume. They do NOT know about spell trees — only geometry.

### SUN Mode (`treePreviewSun.js`)

Radial wheel layout. Schools get angular arcs, spells grow outward from a central ring.

**Algorithm:**
1. Distribute school arcs around 360 degrees (equal or proportional to spell count)
2. Place root node(s) at `ringTier` radius within each school's arc
3. Generate grid points using pluggable algorithm (`naive`, `linear`, `equalArea`, `fibonacci`, `square`)
4. Tag each grid point with its school based on angular position

**Arc Distribution:**
```
proportional=false: each school gets 360° / schoolCount
proportional=true:  arc width = (schoolSpells / totalSpells) × 360°
```

**Growth Direction:**
- Normal: higher tiers radiate outward from root ring
- Inverted: higher tiers grow inward toward center

**Output (`getGridData()`):**
```javascript
{
    mode: 'sun',
    schools: [{ name, color, arcStart, arcSize, ... }],
    rootNodes: [{ x, y, dir, school, color, formId }],
    grid: { tierSpacing, ringTier, ... },
    gridPoints: [{ x, y, school }, ...]
}
```

### FLAT Mode (`treePreviewFlat.js`)

Linear layout. Schools arranged along a horizontal or vertical line with growth extending orthogonally.

**Algorithm:**
1. Distribute schools along a line (equal or proportional spacing)
2. Place root nodes on the line
3. Generate rectangular grid extending from the line
4. Tag each grid point with school based on position along the line

**Output:** Same schema as SUN, different geometry.

---

## Layer 2: C++ Native Tree Builders

C++ builds the **tree structure** — which spell is parent/child of which. Five builder modes exist in `TreeBuilder.cpp`.

### Classic Builder (`TreeBuilder::BuildClassic`)

**Purpose:** Tier-first ordering. Novice spells are always ancestors of higher-tier spells.

**Core constraint:** `node.depth = tier_index` — Novice=0, Apprentice=1, Adept=2, Expert=3, Master=4.

**Algorithm:**

1. **Group spells by school** — unknown schools → "Hedge Wizard"
2. **Discover themes** via `DiscoverThemesPerSchool()` (TF-IDF)
3. **Build NLP data** — extract text per spell for similarity scoring
4. **Per-school tree building:**
   - Group spells by tier (unknown → Novice)
   - Pick root: prefer vanilla Novice spell (`formId >> 24 < 0x05`)
   - Assign themes to nodes via keyword frequency in spell text
   - **Tier-by-tier connection:** For each tier in order (Novice→Master):
     - Shuffle spells for variety
     - For each spell, find best parent from lower tiers
     - Link via `LinkNodes()`, then override `depth = tier_index`
     - Track available parents (those with remaining capacity)
   - Force-connect orphans to least-loaded connected node
5. **Validate & auto-fix** unreachable nodes

**Parent Selection Scoring (`_find_best_parent`):**

| Factor | Weight | Description |
|--------|--------|-------------|
| Theme match | +10.0 | Same theme as parent |
| Theme mismatch | -2.0 | Different theme |
| Text similarity | +0 to +5.0 | Jaccard word overlap × 5 |
| Load balancing | -1.5 per child | Prefer parents with fewer children |
| Tier proximity | +2.0 / +1.0 | Immediate predecessor / two tiers back |
| Random jitter | ±0.5 | Variety |

**Text Similarity (Jaccard):**
```
words_a = tokenize(text_a)  // lowercase, strip punctuation, filter short words
words_b = tokenize(text_b)
similarity = |words_a ∩ words_b| / |words_a ∪ words_b|
```

Uses `TreeNLP::Tokenize()` — no external libraries required.

### Tree Builder (`TreeBuilder::BuildTree`)

**Purpose:** NLP-driven thematic trees. Parent/child links based on spell content similarity. Tree structure emerges from themes, not tier ordering.

**Algorithm:**

1. **Group spells by school**
2. **Discover themes** via TF-IDF keyword extraction (`DiscoverThemesPerSchool()`)
3. **Merge hints** — vanilla Skyrim element names (fire, frost, shock, heal, etc.)
4. **Per-school configuration** — apply shape/density/convergence per school (from LLM or defaults):
   ```
   Destruction → explosion, Restoration → tree, Alteration → mountain,
   Conjuration → portals, Illusion → organic
   ```
5. **Compute similarity matrix** — pairwise TF-IDF cosine similarity between all spells in school
6. **Per-school tree building:**
   - Create TreeNodes, assign themes (fuzzy match → fallback)
   - Select root (prefer vanilla roots like Flames, Healing, etc.)
   - **Group spells by theme** via `GroupSpellsBestFit()` - a spell needs a score above 30, the same
     test the nodes' `theme` and `themes` use (it used to be 30 or above, so a spell scoring exactly 30
     sat in a branch whose theme it did not carry and `SharesTheme` treated it as a stranger there)
   - **Round-robin connection:** Cycle through themes, placing one spell per theme per round. This ensures each theme gets fair access to shallow parent positions:
     ```
     Round 1: fire[0] → frost[0] → shock[0] → heal[0]
     Round 2: fire[1] → frost[1] → shock[1] → heal[1]
     ...
     ```
   - Per-theme parent coherence: each theme tracks its "current parent" so fire spells chain together
   - **Convergence insertion** for high-tier spells (Expert/Master get extra prerequisites)
   - Connect orphans, enforce high-tier convergence, validate reachability
   - Assign sections (root/trunk/branch) based on percentile depth

### Graph Builder (`TreeBuilder::BuildGraph`)

**Purpose:** Directed minimum spanning tree using Edmonds' algorithm. Creates arborescences from NLP similarity weights.

**Algorithm:**
1. Build complete weighted digraph from pairwise TF-IDF cosine similarity
2. Apply tier ordering bias (lower→higher tier edges preferred)
3. Run Edmonds' minimum spanning arborescence from root
4. Validate reachability, fix orphans

### Thematic Builder (`TreeBuilder::BuildThematic`)

**Purpose:** 3D similarity BFS. Builds per-theme branches using multi-dimensional similarity scoring.

**Algorithm:**
1. Discover themes, group spells by best-fit theme
2. Per theme: BFS expansion from theme seed spell
3. Similarity scoring: weighted combination of TF-IDF text sim, name n-gram sim, and effect similarity
4. Cross-theme convergence at higher tiers

### Oracle Builder (`TreeBuilder::BuildOracle`)

**Purpose:** LLM-guided semantic chain grouping. Uses OpenRouter API to create thematic spell chains.

**Algorithm (with LLM):**
1. Batch spells per school (configurable batch size)
2. Send to LLM with prompt requesting thematic chain assignments
3. Parse LLM response into chain groups
4. Build tree from chains with inter-chain links

**Fallback (no LLM / LLM failure):**
1. Use NLP-based cluster-lane approach
2. K-means style clustering on TF-IDF vectors
3. Per-cluster linear chain with cross-cluster links

**Parent Selection Scoring (`_score_parent`):**

| Factor | Weight | Description |
|--------|--------|-------------|
| Theme match | +100 | Same element/keyword |
| Theme coherence | +70 | Theme-chain bonus |
| Element isolation | -50 / -9999 | Cross-element penalty (strict = reject) |
| Tier progression | +50 / +30 / -20 | Adjacent tier / skip-one / big skip |
| TF-IDF similarity | +0 to +60 | Cosine similarity × 60 |
| Capacity penalty | -30 × ratio | children_count / max_children |
| Same-tier link | +10 | If allowed by config |

**Convergence Points:**
- Extra prerequisite links (not parent/child) added to high-tier spells
- Probability increases with tier: Novice 0.5×, Master 10× base chance
- **Forced** for Expert with <2 prereqs and Master with <3 prereqs
- Prefer prerequisites from a different theme (cross-branch convergence)

**Reachability Validation (`_ensure_all_reachable`):**
- Simulates progressive unlock starting from root
- A node unlocks when ALL its prerequisites are unlocked
- If nodes remain unreachable after 20 repair passes, logs warning
- Repair strategies: remove blocking prereqs → find new parent → spread across available

### Theme Rule 1 — Trait Themes (`TreeBuilder::ThemeFromTraits`), tried first by every builder

A full scan gives each spell a `traits` column (see ARCHITECTURE.md): what the spell is, in a fixed
vocabulary built from engine values and the base game's own keywords, never from text. A spell that
has a telling trait takes its theme from it, and `GetSpellPrimaryTheme` returns it with the top score
before any word matching happens. One theme per spell, most telling first:

1. a summon: `summon_fire` / `summon_frost` / `summon_shock` / `summon_undead` / `summon_familiar`, else `summon`
2. the element: `fire`, `frost`, `shock`, `poison`, `disease`
3. what it does: `cloak`, `rune`, `ward`, `armor`, `heal`, `paralysis`, `calm`, `fear` ... (`damage` is skipped, every attack has it)

Why: the word themes below only work where spell names are English. On the Korean dev load order the
tree saved on 2026-09-02 had these as its biggest themes - `spel` 299, `_misc` 212, `adar` 174, `kit` 125,
`madabsorbredonekeyworddamageignore` 107 - fragments of mod keyword names, so branches were grouped by
which framework had tagged a spell. With trait themes the same load order gives (classic builder,
1440 tome spells): Destruction `fire` 61 / `shock` 48 / `frost` 47 / `absorb` 21 ..., Conjuration
`summon_familiar` 203 / `summon_undead` 187 / `summon_shock` 60 / `summon_fire` 35 / `bound` 22 ...,
and 312 spells (22%) with no theme at all - honest about what cannot be told, instead of a junk theme.

Two things keep the word path clean for the spells that still need it: only vanilla style `Magic*`
keywords are read as words (framework keywords are identifiers, not vocabulary), and effects the
record flags Hide in UI are left out of the theme text (their internal English names, e.g.
"TA CC Control Base Effect", sit on hundreds of spells). Spells with a trait theme are also kept out of
the TF-IDF corpus, so the discovered words only describe the remainder.

**Rule 1 and Rule 2 work together, in this order** (`GetSpellPrimaryTheme`):

1. Rule 1 - summon kind, element, what the spell does (above).
2. Rule 2 - the word themes below (the original method). It is kept on purpose: it is the only thing
   that can name a nature the game has no value for - water, wind, stone, blood. Such spells have no
   resist value and no vanilla keyword, so rule 1 cannot say anything about them.
3. Rule 1's weaker answers, in this order: the spell's shape (`cloak`, `rune`, `stagger`) and then
   the actor value it changes (`value.speedmult`, `value.fireresist`, `value.carryweight` ...). Shape is
   not nature, so a wind cloak lands in `wind` when the words can tell and in `cloak` when they
   cannot. The actor value is the last word of all: health, magicka and stamina are skipped there,
   since every attack changes health and one bucket of everything is no better than none.

**Rule 2 reads editor ids**, not just names. Names are translated; editor ids are English on every
load order (`Fireball`, `FireDamageFFAimedArea`, `WindBladeSpell`). The engine drops spell and effect
editor ids after loading, so the scan asks powerofthree's Tweaks for them (`SpellScanner::GetEditorId`;
without po3 Tweaks they are empty and rule 2 falls back to names alone). `BuildThemeText` splits them
into words and weighs them like the name. Filters that keep ids from polluting the themes:

- the shorthand in ids (`MGEF`, `FFSelf`, `ConcAimed` ...) is on the stop list;
- **mod tags** - an author's prefix such as `ABY_`, `NAT_`, `GRIM_` - are found from the data
  (`FindModTags`) by **position**: the word an id starts with, leading >= 80% of that plugin's spell
  ids. Frequency is no test. The first version used it ("on most of one plugin's spells and hardly
  anywhere else") and threw away `shadow` for Abyss and `blood` for Bloodmoon - a mod about one thing
  puts that word on every spell too, and it is the word the branch should be named after - while short
  real prefixes (`dar`, `nat`) slipped through. A nature word never leads the id; the prefix does.
  Except when a mod names every spell after what it does: Witcher Horses starts 19 of its 20 ids with
  `Conjure`, so `conjure` became a "prefix" and vanished as a word from every mod's ids. Since
  2026-09-23 a word that the base game's own spell ids use (Skyrim, Update, Dawnguard, Hearthfires,
  Dragonborn - no author prefixes there) is never a mod tag. Only the spells' own ids count: effect ids
  use shorthand (`Mag...`) that would pass GTS Spells' real `MAG_` prefix off as a word. On the dev load
  order this frees `conjure` and nothing else of the 23 tags; with a fixed seed the classic builder's
  parent links sharing a theme go from 82.9% to 85.0% and sharing an id word from 62.3% to 63.8%, and
  the graph builder's from 4.5% to 16.9% and 38.1% to 42.7%;
- a word with a digit in it is never a theme (`alt50`, `ill25`, `100`);
- discovered themes keep their slots and the vanilla hint words are appended after them
  (`MergeWithHints`). The hints are fire / frost / shock / summon ... - what rule 1 already settles -
  and when they went first they used 8 of the 12 slots, so `wind` and `arcane` fell off the end.

**How the word themes are ranked and handed out**
- ranked by how many spells carry the word, not by the TF-IDF sum. The sum measures how heavy a word
  is inside its own document, which favours short documents: `blink`, on one spell with a two word id,
  beat the eight polymorph spells with their long descriptions. A theme is worth the spells it brings
  together, so a word on a single spell is not a theme at all (`kMinSpellsPerTheme`);
- they get three times the room `topN` used to give (`kWordThemeRoom`). `topN` was sized for when the
  words had to cover fire, frost and the rest; rule 1 has those now, and what is left for the words is
  a long tail of small natures (polymorph, teleport, corpse, aura ...);
- a spell whose id holds two themes (`LUN_MoonTouch`) stays with the bigger group unless the smaller
  one scores clearly higher (`kClearWinMargin`) - the fuzzy part of the score is too noisy to split them.

**Editor id words also feed the pairwise similarity** (`ComputeSimilarityMatrix`, used by every
builder), with the author's prefix taken off first. Before, it read names, descriptions and effect
names only - Korean text the tokenizer cannot split - so on a translated load order two spells only
looked alike by accident.

On the dev load order (Korean, 1440 tome spells): rule 1 names 1122 spells, rule 2 names 256 -
Destruction `blood` 16, `shadow` 13, `stone` 13, `arcane` 11, `water` 11, `wind` 11; Restoration `astral` 10,
`sun` 10, `moon` 6; Alteration `polymomrph` 8 (the mod's own spelling), `resist` 8, `lock` 6, `teleport` 4 ... -
and 62 (4%) stay without a theme. With names only and no traits, rule 2 named 52 and 318 had none.

**Keyword affinity - nature and shape both count.** A spell gets one theme, so `LUN_MoonTouch` had to
be either a moon spell or a touch spell, and nothing mechanical tells a nature word from a shape word.
It does not have to: both are its keywords. `ComputeSimilarityMatrix` keeps, per spell, one bag of
keywords - its traits (minus the school, the matrix is per school) and its editor id words (minus the
author's prefix and anything with a digit) - and two spells are alike by the keywords they share, each
weighed by how few spells carry it (inverse document frequency, from the data): sharing
`form.projectile` with eight hundred others says little, sharing `word.moon` with thirteen says a lot.
A keyword only one spell has is dropped, it cannot be shared. The result goes into the effect
affinity, which every builder already weighs highest, as the better of the two values - without a full
scan there are no keywords and the effect names decide alone, as before.

**A spell answers to every theme it qualifies for** (`TreeNode::themes`, `GetSpellThemes`, `SharesTheme`).
One theme per spell was the root of the trouble. Every builder scored parents by "same theme: bonus,
different theme: penalty" (classic +15/25 and -10, tree +170 and -50), judged by the single pick - so a
moon touch spell filed under `touch` counted as a *mismatch* against every other moon spell and was
pushed away from its own family, while the keyword affinity above pulled the other way. Now a node
keeps the whole list: all its trait themes (a fire atronach is `summon_fire` and plain `fire`), and
every word theme that is literally one of its words. All nine comparisons in the five builders ask
`SharesTheme` - any theme in common - instead of comparing the pick. `theme` stays, as the first of the
list: a branch still needs one name and the panel one colour. Nodes that never got a list (LLM chains
in the oracle builder) fall back to comparing the single theme.

**Themes most of a school carries do not count as shared** (2026-09-23; `DropCommonThemes`,
`TreeNode::matchThemes`, `BuildConfig::commonThemeShare`). With every theme counting, a word on nearly
every spell of a school made nearly every pair "share a theme": in Conjuration `summon` is on 89% of
the spells and `conjure` on 41%, and 41% of the classic builder's theme matches (49% in the tree
builder) rested on words like these alone. Each builder now calls `DropCommonThemes` once per school
after assigning themes; a theme carried by at least `commonThemeShare` (0.4) of a school with 10 or
more spells is left out of `matchThemes`, which is what `SharesTheme` compares. `themes` and `theme` -
the output, branch names, colours - are unchanged. On the dev load order 0.4 drops exactly `summon` and
`conjure` in Conjuration; the widest theme anywhere else is `fire` in Destruction at 32%.

Same scan, seed 42, links whose spell and parent share a theme other than the dropped ones:

| builder | off | on (0.4) | Conjuration off -> on |
|---|---|---|---|
| classic | 67.5% | 75.6% | 57% -> 77% |
| tree | 59.7% | 76.2% | 36% -> 72% |
| graph | 16.9% | 50.3% | 0% -> 54% (max chain depth 91 -> 27) |

Thematic and oracle trees do not change (their theme comparisons only place leftover nodes).

*To turn it off*: `"common_theme_share": 0` in a build request's config (or a `-c` config file for
`treebuilder-test`), or set the default `commonThemeShare` in `TreeBuilder.h` to `0` and rebuild. With 0
every builder produces exactly the trees it did before - checked on all five, same seed, no parent
changed.

Same 1440 spells, links where spell and parent share an editor id word / an element or kind trait:

| | id word | element or kind |
|---|---|---|
| single theme, effect names only | 38% | 60% |
| + keyword affinity | 45% | 62% |
| + every theme counts (classic) | 49% | 61% |
| + every theme counts (tree builder) | 46% | 54% |

**All five builders, same scan, same seed.** The harness now runs the oracle builder too: only its call
out to the API is stubbed, so it takes its own NLP fallback (cluster lanes) - the path a player without
an API key gets. "id word" and "elem/kind" are the share of parent-child links whose two spells have an
editor id word, or an element or kind trait, in common.

| builder | links | id word | elem/kind | themed | orphans |
|---|---:|---:|---:|---:|---:|
| classic | 1435 | 49% | 61% | 96% | 0 |
| tree | 2370 | 46% | 54% | 96% | 0 |
| graph | 1435 | 37% | 52% | 96% | 0 |
| thematic | 1435 | 30% | 66% | 100% | 0 |
| oracle (fallback) | 1435 | 42% | 69% | 96% | 0 |

No builder leaves an unreachable node. The tree builder's extra links are its convergence
prerequisites. Thematic groups by nature hardest and by spelling least - it walks out from one seed
spell per theme - while classic and oracle sit in between.

**Name similarity compares editor ids** (`ComputeSimilarityMatrix`), with the author's prefix taken
off. It is a character trigram comparison - "Firebolt" and "Fireball" share most of their trigrams -
which on a translated load order was run on Korean names that share none of it, and the graph builder
weighs it heavily. That alone took graph from 24% to 37% on id words and 48% to 52% on traits; the
builders that lean on it less did not move.
972 of the 1440 spells answer to two or more themes. The moon family under the classic builder, before
and after: `LuminousMoonbeam` hung under `SLENDetectAroused` and `MoonFire` under `INQ_HolyDagger`; now
`LuminousMoonbeam <- MoonFire <- Moonlight`, `LunarAura <- MoonlightTouch`, `LunarSingularity <- LunarBolt`.
**Audit of translated text, 2026-09-22.** Every place a spell's wording drives behaviour was checked,
because on a translated load order those places are reading Korean. The tokenizer drops non-ASCII, so
Korean names and descriptions do not mislead anything - they simply vanish, and whatever was supposed
to read them found nothing. Fixed:

- *effect name similarity* (`ComputeSimilarityMatrix`), the score every builder weighs highest: it
  compared the names of **all** effects, hidden helpers included. One mod's script controller sits on
  hundreds of unrelated spells, and the score is the best matching pair, so a shared helper made any
  two of them a perfect match. Hide in UI effects are now left out.
- *theme scoring* (`CalculateThemeScore`): its strongest signal is the theme word appearing in the
  spell's **name**, worth 40 of 100. Themes are English words, so on Korean that never fired. It reads
  the editor id now.
- *sort order*: spells tied on tier and cost were ordered by name, so the same load order ordered them
  differently once translated. They go by form id now.
- *JS*: `classicThemeEngine` discovered themes from name + effect names, and `edgeScoring` matched
  English element words against name + description. Both now also read editor id words
  (`spellIdWords` in `uiHelpers.js`). `editorIdWords` also splits an acronym from the word after it
  (`WTIceVolley` -> `wt ice volley`) and letters from digits (`DES100` -> `des 100`).
  `detectSpellElement` matched by substring, which the ids exposed: `RestoreHealth` read as earth
  (`ore`), `Necrotic` / `Daedroth` as poison (`rot`), `Voice` / `Sacrifice` / `Novice` as frost
  (`ice`) - 84 of 1,428 spells on the dev load order. Keywords of three letters or fewer
  (`SHORT_KEYWORD`) now count only at the start of a word; longer ones still count anywhere, so
  `Hearthfire` and `Blastbones` keep theirs. The same rule fixes English descriptions (`restores`).

*Tried and rejected*: comparing effect **editor ids** instead of names. Effect ids follow a convention -
`FireDamageFFAimed`, `FrostDamageFFAimed` - so most of the string is the delivery and two different
elements agree on nearly all of it. The graph builder fell from 37% to 29% on shared id words. What
the ids are good for is their words, which the keyword affinity already reads.

*On reading these numbers*: both measures are proxies and each flatters a different part of the
machinery. "Shares an id word" rewards whatever drives name similarity, "shares an element or kind"
rewards theme matching. Excluding the hidden helpers moved graph from 37%/52% to 30%/54% - worse by one
measure, better by the other - because with the effect score no longer stuck near 1.0 it competes with
name similarity again. The change is kept on the grounds that a shared script controller is not
evidence two spells belong together, not because a number went up.

| builder | id word | elem/kind | (before this audit) |
|---|---:|---:|---|
| classic | 51% | 63% | 49% / 61% |
| tree | 45% | 54% | 46% / 54% |
| graph | 30% | 54% | 37% / 52% |
| thematic | 29% | 64% | 30% / 66% |
| oracle | 43% | 67% | 42% / 69% |
**Audit, 2026-09-22.** Every rule was re-run over all 1440 spells / 4246 effects of the dev load
order and each answer sorted into: arbitrary (more than one candidate, first one wins), missing
(nothing to say), or self-contradicting (two sources disagree). Found and fixed:

- *reanimate read as a summon.* Raising a corpse carries vanilla's `MagicSummonUndead` too, so 6
  spells came out as undead summons. `reanimate` now wins, in the themes and in the icon rules.
- *fortify read as a heal.* Courage, Rally and Call to Arms raise health, so they were `heal`. An
  effect flagged Recover hands the value back when it ends: that is a fortify, not a heal.
- *area without a radius.* 8 spells were `area.blast` because of an explosion record kept only for the
  flash. An area now needs a radius or an effect area.
- *description from a hidden effect.* 4 cards took their text from a Hide in UI helper. A visible
  effect's description goes first.
- *effect order decided which kind named the branch.* Vanilla Paralyze leads with a Rally helper, so
  it themed as `rally`; so did Mass Paralysis and one mod spell. Kinds are now taken in a fixed order
  of how much each narrows down what a spell is (`kKindPriority`) - paralysis beats rally on every
  load order, because that is what the engine's archetypes mean. Nine spells carried two kinds; all
  nine now pick the telling one. Measured alternatives first: the costliest effect gets Paralyze
  wrong (the Rally helper costs more), and rarity-within-this-load-order would not carry to another.
- *ties in the word themes were resolved by hash order.* Terms with equal TF-IDF scores were sorted
  by score alone out of an `unordered_map`, so which themes survived the topN cut was not guaranteed
  to be the same twice. Ties now break on the term.
- *nothing said about 187 value-modifier spells.* They harm or help an actor value with no element
  and no archetype to name - the third tier now reads the actor value itself (a closed engine enum,
  its own name is the theme), which names 66 spells and takes the unthemed from 138 to 111.

Checks that came back clean: no spell has a resist value and a vanilla keyword that disagree about
its element (0 of 1440); every spell has one of the five schools; no spell has zero effects; only 3
spells have every effect flagged Hide in UI, where the rules read the hidden ones rather than give up.

**Known and left as it is**
- school is the school of the first effect (upstream behaviour); the engine uses the costliest effect.
  They differ for 2 of 1440 spells here. Changing it would move spells between schools in existing trees.
- a spell with two elements (5 here, the Creation Club fire-and-frost spells and one dispel) goes to
  whichever element its first such effect has. They really are dual-element; no single answer is right.
- 111 spells stay unthemed, 49 of them one mod's. 168 spells have nothing but Script effects: a script
  does whatever its author wrote, and no record says what. That is the floor for structure alone.
- an icon pack that keyworded a spell twice (252 here) gives whichever keyword the spell lists first.
- "vanilla keyword" is judged by the defining plugin; a mod that injects a record into a vanilla
  plugin's id range would pass. It would also have to be named exactly `MagicSummonFire` or the like.
### Theme Rule 2 — TF-IDF Theme Discovery (`TreeBuilder::DiscoverThemesPerSchool`)

**Shared by all builders.** Discovers keyword themes per school from spell text using `TreeNLP::ComputeTfIdf()`.

**Algorithm:**
```
For each word across all spells in school:
    TF = term_count / total_tokens
    DF = documents_containing_word
    IDF = log((total_docs + 1) / (DF + 1)) + 1   (smoothed)
    score = TF × IDF
Sort descending → take top N
```

**Stop words:** English common words + spell-specific ("spell", "magic", "damage", "target", "health", "magicka", "novice", "master", etc.)

**Hint merging:** Discovered themes are supplemented with vanilla Skyrim elements:
```
Destruction: fire, frost, shock, cold, lightning
Restoration: heal, cure, restore, buff, bless
Alteration:  skin, armor, shield, polymorph, transmute
Conjuration: summon, conjure, bound, portal, familiar
Illusion:    invisibility, charm, fury, calm, fear
```

### Cross School Bridges (`TreeBuilder::ComputeCrossSchoolBridges`, `TreeBuilderBridges.cpp`)

Every builder splits the spells by school and grows five trees that never see each other. Bridges are the
links between them: `TreeBuilder::Build` appends the same `bridges` array to the output of all five builders.

```json
{ "from": "0x..", "to": "0x..", "fromSchool": "Destruction", "toSchool": "Conjuration",
  "affinity": 0.48, "evidence": 5.1, "shared": ["element.fire", "word.flame"], "twoWay": false }
```

**Decided (2026-09-22): a bridge really does open the spell.** Mastering one Conjuration fire spell can
open a Destruction fire spell of the same tier or one above, without progressing Destruction. Asked
whether to tighten this, the author said no: Skyrim's own skill level already limits how useful a spell
is, so a spell learned early is not a spell that can be used early. Access stays generous; balance is
left to the skill level.

A bridge is an **extra way in, never a requirement**. The layout is meant to add `from` to the soft
prerequisites of `to` (which already mean "any one of these"), so a school's tree stays complete without
them. That is why bridges sit next to the trees and are not written into them.

Rules, all mechanical:

| Rule | Why |
|---|---|
| Only telling keywords count: traits except `form.*`, `area.*`, `cast.*`, `kind.damage` and the shape kinds (cloak, rune, stagger); plus editor id words | Two projectiles or two cloaks are not kin |
| An id word counts only when spells of **two plugins** use it | A word one plugin uses is the author's shorthand (`styy`, `dcd`); being rare it outweighed everything and bridged a mod to itself |
| An id word that repeats a trait of the same spell is dropped (`word.frost` next to `element.frost`) | One fact, counted once |
| Weighted Jaccard over IDF weights (whole load order) ≥ `kMinAffinity` 0.34 | |
| `from` is the same tier or one below (`kMaxTierGap`) | You learn the source first |
| Per target, the closest source of each other school | |
| Per school pair: most **evidence** (summed weight of what is shared) first, at most 12, at most 3 with the identical shared set, a source used at most twice | A one-keyword spell is a "perfect" match for anything; one mod's family of detect spells took every slot |
| Two spells of one tier that pick each other become one record, `twoWay` | |

Measured on the 1440-spell test load order: 180 bridges (44 two-way) touching 245 spells; identical for
all five builders. Pairs: Conjuration-Destruction, Alteration-Conjuration, Destruction-Restoration and
Conjuration-Restoration are full (24), Illusion-Restoration has 6.

**`schoolLinks`** (`[{a, b, kin}]`, next to `bridges`) counts, per pair of schools, every spell that found
kin in the other school - before the caps, which make most pairs look equally full. Test load order:
Conjuration-Destruction 172, Destruction-Restoration 136, then 49 and below; Illusion-Restoration 17.
Nothing consumes it any more (see below); it is kept because it is the one number that says which schools
are kin, and the next attempt at an intermixed layout will want it.

**Decided: the bridges move nothing.** Both attempts to let them shape the tree are gone.

- *Pulling bridged spells toward the neighbour school's border* moved nothing measurable: classic
  placement follows the parent along a spoke.
- *Reordering the schools around the wheel* so the kin schools became neighbours did work, and that was
  the problem - it rearranged the whole picture. The author asked for the familiar shape back, so the
  schools keep the order the scan finds them in, which is what HoM always did.

**What `modules/schoolBridges.js` still does**, the same for all five build modes: right before
`SaveSpellTree`, `applyToOutput` adds `from` to the `softPrereqs` of `to` (both ways for `twoWay`), never
to `prerequisites`, never to roots or spells that are open anyway, and copies the applied bridges to
`output.bridges` for the viewer. Test load order: 180 bridges, 224 cross-school soft prerequisites, all
1440 spells still placed, and the schools sit exactly where the scan put them.

**In the viewer** (`modules/bridgeView.js`, hooked into `CanvasRenderer.render` after the nodes): all 180
lines at once cover the wheel, so a bridge is drawn only for the selected or hovered spell - dashed, bowed
toward the centre, coloured by the first shared trait. Bridged spells the player has reached wear a thin
ring; locked ones do not. The spell card lists the spell's bridges (arrow for the direction, school, what
is shared; a click selects the other spell, `???` while it is locked). The section opens with the keyword
chips, not before: it names another spell and the keywords the two share, which is more than the card
itself is showing at that point. A bridge opens its target once the source is **mastered** - learned, at
100%, or already known - the same test every other soft prerequisite uses (`IsSpellMastered`). The trait filter needs every
spell's traits after a restart, when the scan is gone, so `applyToOutput` bakes `traits` into each saved
node.

**The keywords on the spell card are the filter**: press "fire" on any spell and every fire spell in
every school lights up. A chip is pressable when the tree carries that keyword and it is not the school
(the school tabs do that) nor `kind.damage`. A bar of keyword buttons above the tree was tried and
removed the same day - thirteen buttons in two rows, and the card's own chips say the same thing where
the player is already looking. What is left up there is one pill, shown only while a filter is on, which
names the keyword and turns it off again; without it a filter set from one spell card could only be
cleared from that same card.

`BridgeView` makes the same three checks `renderNodes` makes before it draws anything - viewport, hidden
schools, and **discovery mode**. Without the last one the filter and the bridge rings would light up
spells the renderer is deliberately hiding, giving away where undiscovered spells sit. A consequence: on
a save with discovery on, a keyword lights only the spells the player has already found. Labels are drawn
after the tree transform is undone, where the veil cannot reach them, so `renderLabels` drops
non-matching spells itself. Lit dots are sized in screen pixels, not tree units, or they vanish at the
zoomed-out view where a filter is most wanted.

*Tried and dropped:* nudging bridged spells and their themes toward the neighbour's border in
`classicLayout.js` (`_findSlots` score term, theme sector order). Classic placement follows the parent
along a spoke; with and without the nudge the mean bridge length stayed at ~63% of the tree radius.
Found on the way and left alone: `_computeThemeSectors` deals theme centres over a fixed 140° whatever
the school's wedge (72° with five schools), and its angular score barely differs between neighbouring
grid points, so theme sectors hardly steer classic placement at all.

**Known limits.** A single id word can still bridge two spells, and nothing mechanical tells a telling
word (`twilight`, `mudcrab`) from a generic one (`explosion`, `cloud`); rarity does not separate them
(checked: `summon` is rarer than `shadow`). On a load order rich in mods, an element alone carries the
least evidence, so vanilla pairs such as Firebolt → Flame Atronach lose their place to better-documented
modded pairs; on a small load order they are what is left.
### TreeNode Data Model (`TreeBuilder::TreeNode`)

```cpp
struct TreeNode {
    std::string formId;          // "0x00012FCD"
    std::string name;            // "Flames"
    std::string tier;            // "Novice"
    std::string school;          // "Destruction"
    std::string theme;           // "fire" - the branch name (may be empty)
    std::vector<std::string> themes;  // every theme the spell answers to, theme first
    std::string section;         // "root" / "trunk" / "branch" (may be empty)

    std::vector<std::string> children;       // formIds of child nodes
    std::vector<std::string> prerequisites;  // formIds of prerequisite nodes
    int depth = 0;                           // distance from root (0 = root)
    bool isRoot = false;
};
```

**`LinkNodes(parent, child)`:**
- Adds child.formId to parent.children
- Adds parent.formId to child.prerequisites
- Sets child.depth = parent.depth + 1

**Note:** Classic builder overrides depth after linking to enforce `depth = tier_index`.

---

## Layer 3: C++ ↔ JS Integration

### Command Flow (`UIManager.cpp` → `TreeBuilder`)

```cpp
OnProceduralTreeGenerate(argument):
    1. Parse JSON from JS
    2. Read "command" field (default: "build_tree")
    3. Extract spells array and config
    4. Launch background std::thread for TreeBuilder::Build()
       → TreeBuilder has zero RE:: dependencies, safe to run off game thread
       → OpenMP used for inner-loop parallelism (similarity matrices)
    5. On completion, dispatch result back to game thread via SKSE AddTask
    6. Callback packages {success, treeData, elapsed}
       → InteropCall("onProceduralTreeComplete", response)
```

**Commands:**
| Command | Builder | Mode |
|---------|---------|------|
| `build_tree` | `BuildTree()` | NLP thematic |
| `build_tree_classic` | `BuildClassic()` | Tier-first |
| `build_tree_graph` | `BuildGraph()` | Edmonds' MSA |
| `build_tree_thematic` | `BuildThematic()` | 3D similarity BFS |
| `build_tree_oracle` | `BuildOracle()` | LLM-guided chains |
| `prm_score` | `TreeNLP::ProcessPRMRequest()` | PRM scoring |

---

## Layer 4: JS Growth Layout Modules

Growth modules take the tree structure from C++ and **position nodes on the grid** from the root preview module.

### Classic Growth Layout (`classicLayout.js`)

Positions spell nodes on the 2D grid using wave-based BFS with tier zone and theme scoring.

**Algorithm:**

**Phase 1 — Grid Graph Construction (Spatial Hashing):**
- Hash all grid points into cells (cellSize = tierSpacing × 1.5)
- For each point, find 8 nearest neighbors within maxDist using 3×3 cell neighborhood
- Result: adjacency graph for O(1) neighbor lookup

**Phase 2 — Root Seeding:**
- Snap each school root to nearest unoccupied grid point

**Phase 3 — Wave-Based BFS:**
```
wave 0: root nodes
wave 1: children of roots
wave 2: children of wave-1 nodes
...

Per wave:
  shuffle nodes (fair ordering)
  for each node's children:
    check tier zone constraint → defer if mismatch
    find best adjacent slot via _findSlots()
    place on grid, mark occupied
```

**Phase 4 — Deferred Node Placement (up to 5 passes):**
- Nodes deferred due to tier zone mismatch are retried
- Parents may now be placed, opening new adjacent slots

**Phase 5 — Force-Placement:**
- Any remaining unplaced nodes get nearest unoccupied grid point

**Making room (`_densifyGrid`):** when a school has more nodes than grid points - before Phase 1, and
again each time Phase 5 runs out of open points - grid points are added. First sideways, innermost
first and repeated from the new points: on the Sun grid one tier spacing along the point's ring in both
directions, inside the school's arc; on the Flat grid one step left, right, up and down, inside the box
the school's points cover. Then outward, one tier past the outermost points, round after round: away
from the center (Sun), or along the school's growth direction (its root's `dir`) without leaving its
band (Flat). Nothing is added inside the center mask.

Every added point keeps `_densifyMinSpacing()` from all others. That is the mod's existing node-spacing
rule from `config.js`: `GRID_CONFIG` derives both the minimum node distance and the tier spacing from the
node size (`minNodeSpacingMultiplier` and `tierSpacingMultiplier`, 0.7 each; the wheel layouts in
`layoutEngine.js` / `layoutGenerator.js` use it), so nodes keep one tier spacing apart. It is applied as
tier spacing × (0.7 / 0.7) × `DENSIFY_TOLERANCE` (0.9), because a step along a ring is a chord slightly
shorter than the tier spacing - 36 units at the default spacing of 40. There is no fixed floor: the Sun
preview's tier density sets the spacing to `min(40, 250 / density)`, down to 25 at density 10 and lower
above it, and a floor above the spacing would leave no room at all (the first version of this fix had a
28-unit floor and dropped spells from the tree at density 10+). The Classic grid layout never read the
`GRID_CONFIG` rule before.

Until 2026-09-23 it added the midpoint of any two points up to 2.2 tiers apart, with no spacing check.
On the naive sun grid (30 dots a ring, so far out the dots of one ring are hundreds of units apart) the
only close pairs are between rings, and Phase 5's repeated calls halved those 40-unit gaps to 20, 10, 5.
Measured on a real 1,428-spell load order (reproduced offline with `treebuilder-test` and the panel's
layout code, identical to the tree the game saved): Conjuration, 650 spells on 252 grid points, had 645
spells within 24 units of another and 154 within 14, down to 2.6 - they could not be clicked apart.
With the fix: 8 within 24, 2 within 14 (the grid's own inner rings), median and 10th-percentile spacing
40, and the school reaches no further out than before. The Fibonacci and equal-area grids have enough points for this load
order and never densify.

**Slot Scoring Formula (`_findSlots`):**

```
totalScore = directionScore + radialBonus - depthPenalty + tierZoneScore + themeSectorScore
```

| Factor | Range | Description |
|--------|-------|-------------|
| Direction score | [-1, +1] | Dot product of slot vector with growth direction |
| Radial bonus | ±radialWeight | Positive if slot is farther from center than parent |
| Depth penalty | -depth × 0.3 | Higher depth nodes penalized (crowding control) |
| Tier zone score | [-15, +3] | Bonus if slot falls within tier's configured zone % |
| Theme sector score | [-3.5, +2.5] | Bonus if slot's angle matches theme's arc sector |

**Tier Zone Scoring Detail:**
```
radiusPct = (slotRadius - ringRadius) / (maxRadius - ringRadius) × 100

if radiusPct < zone.min:
    score -= 5.0 + (zone.min - radiusPct) / 100 × 10.0   (too close)
elif radiusPct > zone.max:
    score -= 5.0 + (radiusPct - zone.max) / 100 × 10.0   (too far)
else:
    centeredness = 1.0 - |radiusPct - zoneCenter| / zoneHalf
    score += 3.0 × centeredness                             (in zone)
```

**Three Spell Matching Modes:**

| Mode | Behavior |
|------|----------|
| `simple` | No theme awareness. Pure direction + density scoring. |
| `layered` | Uses `node.theme` from C++ builder for sector-based bias. Theme match bonus in slot scoring. |
| `smart` | Runs `ClassicThemeEngine.discoverAndAssign()` (JS-side keyword analysis) before layout. More refined theme grouping. |

These modes control **visual clustering** (spatial grouping of similar spells), not tree structure. The C++ builder determines parent/child links; the layout engine determines where each node sits on screen.

### Tree Growth Layout (`treeGrowthTree.js`)

Corridor-based trunk layout with section allocation.

**Settings:**
```
pctBranches: 30%   — outer canopy
pctTrunk: 50%      — central corridor
pctRoot: 20%       — inner root zone
trunkThickness: 70px
```

**Section assignment** (done in C++): Nodes sorted by depth, then allocated by percentile:
- First 20% of nodes → `root` section
- Next 50% → `trunk` section
- Remaining 30% → `branch` section

**Layout:** Trunk module computes a central corridor. Nodes in `trunk` section fill the corridor. `branch` nodes spread outward. `root` nodes cluster near the tree base.

**Ghost preview:** Semi-transparent nodes show where the trunk will fill before building.

### Decluttering before save (`layoutDeclutter.js`, `layoutLineClear.js`, `layoutLineGrid.js`, 2026-09-26)

Every growth mode (classic, tree, graph, oracle, thematic) bakes x/y into the tree and calls
`LayoutDeclutter.applyAsync(output, onDone)` after `SchoolBridges.applyToOutput`, and saves
(`SaveSpellTree`), loads and switches tabs in `onDone`. It runs once, when a tree is applied; the renderer
only reads the saved positions. The game's browser has no JIT and takes seconds for a big tree (6.3 s
measured in game before the speed-ups below; later about 5 s of work and 11 s of wall time for a 953-spell
tree, sliced), so in game the plugin does it (see **Native pass** below). Without the plugin, the JavaScript
pass does it: `applyAsync` works `SLICE_MS` (150 ms) at a time and lets the panel draw between
(`setTimeout`), with "Arranging spells... N%" on the tree builder's status line. The line search is a job (`LayoutLineClear.start` / `step`) that stops
after its time and picks up where it left off; the push-apart step is quick, but its rounds may also end a
slice (between rounds). Applying again
before it is done drops the first run. `apply(output)` does it all at once (tests) - same result either way. None of the layouts checked
what the tree then looks like. (The old `layoutEngine.js` had a line-against-spell check; it was removed
in v1.2.5 with a note that curved edges would handle it at render time, which they never did.
`LayoutEngine.resolveOverlaps` is still there and still not called.) In the game's saved tree (1,428
spells): 21 pairs of spells closer than two drawn spells (24 units), 2,550 cases of a parent-to-child line
passing within 20 units of the centre of a spell it does not end at, 428 pairs of lines meeting at a spell
less than 20 degrees apart (283 under 10), 1,384 pairs of lines not sharing a spell running closer than 11
units (8 px at the default zoom) without crossing, 329 crossing at less than 15 degrees, and spells on the
heart.

Three steps, the lines staying straight throughout - the spells move, not the lines:
1. **Room:** every spell (roots too) is moved out from the centre by `SPREAD` (1.35). Roots keep their
   direction, so the baked spokes and sectors still fit.
2. **Off the lines** (`LayoutLineClear`): every parent-to-child line should keep `LINE_CLEAR` (20 tree
   units: a locked spell's 7 plus about 10 px at the default zoom) from the centre of every spell it does
   not end at; two lines meeting at a spell should be at least `MIN_ANGLE` (30 degrees) apart; two lines not
   sharing a spell should keep `LINE_GAP` (11 units, 8 px at the default zoom) apart, or where they cross,
   cross at least `MIN_CROSS` (15 degrees) steeply. For each spell where any of that fails, a search tries
   12 directions x 5 distances (10-64) round where the layout put it and round where it is now, and takes
   the cheapest spot: lines passing it + spells its own lines pass + up to `ANGLE_COST` (3) per pair of
   lines meeting too narrowly, times `1 + shorter line / LONG_LINE (150)` - long lines at a narrow angle run
   together a long way, the bundles out of a spell with many children - + up to `LINE_GAP_COST` (1) per line
   its own lines run too close to or cross too shallowly + a price for spells closer than
   `2 x NODE_RADIUS + GAP` + a price for the heart + 0.01 per unit moved. Two lines closer to parallel than
   `BUNDLE_ANGLE` (20 degrees) need `LINE_GAP` more room for every `BUNDLE_LEN` (250) they run side by side,
   up to `BUNDLE_MAX` (3) x `LINE_GAP`. A spell whose longest line is longer than `REACH_LINE` (200) searches
   that many times further (up to `MAX_REACH_SCALE` 3): far out, a small move turns a line little. The
   nearest spots are tried first and the first spot with nothing wrong ends the search. The cheap parts are counted first and a spot is dropped as soon as it costs more than the
   best so far, so the dear line-against-line part is often not worked out at all. Six passes at most; a pass after
   the first looks only at spells whose surroundings changed in the one before. Lock lines and cross-school
   bridges (drawn only for the selected or hovered spell) are not kept clear of.
3. **Apart:** spells are pushed apart a little at a time (at most `MAX_STEP` 10 per round, `ITERATIONS` 60)
   until no two are closer than `2 x NODE_RADIUS + GAP` (16 = a known spell with its XP ring, 6) and none
   sits within `HEART_CLEARANCE` (50) of the globe.

**Native pass (`LayoutDeclutter.cpp`, 2026-09-26).** The same pass in C++, in
`plugins/spelllearning/src/treebuilder/`: `LayoutDeclutter.cpp` (collect, spread, the push-apart rounds,
rounding, the log line), `LayoutLineClear.cpp` (passes, one spell's search, dirty marks),
`LayoutLineClearCost.cpp` (the cost of a spot), `LayoutLineGrid.cpp` (grids and fans) and `LayoutMath.cpp`.
It is a port, not a new design: the same constants, the same order of work (schools by name, nodes in
order), every sum taken term by term in the same order, the same tie-breaks and early exits, and the same
rounding (`Math.round(v * 100) / 100`, halves up). `sin`, `cos` and `atan2` are fdlibm (`LayoutMath`), as V8
has them - the MSVC runtime's differ in the last bit for about one `atan2` in five, and one such bit can pick
another spot. On the game's 1,428-spell tree the positions are identical to `LayoutDeclutter.apply` in node
(the tree stringified is byte for byte the same; also with `noRotate`, a flat layout, stacked spells and the
test tree), in about 0.29 s on one core (node, with its JIT: 0.54 s). Every state lives in one call, so two
calls can run at once. (The game's own browser engine may take its `Math.sin`/`atan2` from the
platform runtime rather than fdlibm; if so, its JavaScript fallback can land a few spells differently from
node and from the plugin; the plugin's result does not depend on it.)

`applyAsync` sends the tree to the plugin when `window.callCpp` is there: `DeclutterTree` with `{ id,
schools: [{ name, startAngle, endAngle, nodes: [{ formId, x, y, isRoot, children }] }], globe, layoutMode,
noRotate }` - only the positioned spells, in `_collect`'s order - and shows "Arranging spells..." once. The
plugin (`UIManagerDeclutter.cpp`) hops to the game thread (never call back into the view from inside its
listener), starts a worker thread that parses the request and runs the pass, and hops back to the game
thread to call `onDeclutterResult` with `{ id, positions: [[formId, x, y], ...], moved, rounds,
overlapsLeft, linesLeft, linesMoved, passes, ms }`; it logs the same `[LayoutDeclutter] ...` line as the
script, with `(native)`. The panel writes the positions onto the nodes (checking each formId) and calls
`onDone`. It falls back to the sliced JavaScript pass when the reply has an `error` or does not match the
tree, or when no reply comes in `NATIVE_TIMEOUT_MS` (30 s; after that, an older plugin without the listener
is not waited for again that session). A newer `applyAsync` supersedes an older one: a reply whose id is
not the latest request's is dropped. The browser harness (`dev-harness-bridge.js`) answers `DeclutterTree`
with an error at once, so it arranges the tree itself.

**Keep the two in step.** A change to the JavaScript pass (a constant, a step, the order of a sum) goes into
the C++ files too. `tools/declutter-test` (`declutter-test -i tree.json -o reply.json [-r runs]`) runs the
native pass on a saved tree; apply its positions to the tree in `_collect`'s order and compare with
`LayoutDeclutter.apply` on the same tree.

Roots never move except for the spread, and a spell that starts inside its school's sector
(`startAngle`/`endAngle`) is kept inside it; flat and unturned layouts (`layoutMode: 'flat'`, `noRotate`)
have no sectors to keep. Deterministic (fixed order, ties split along the golden angle).

Measured on that tree: spell-on-line cases 2,550 → 181, lines meeting under 20 degrees 428 → 29 (under 10:
283 → 5), pairs of lines over 150 long out of one spell under 15 degrees apart 172 → 6, lines running closer
than 11 units 1,384 → 58, long lines side by side (250+ together, under 20 degrees, closer than 22) → 3,
shallow crossings 329 → 60, no touching pairs; about 0.9 s in node, 8 s without a JIT. Fewer passes are
quicker but leave more (4 passes: 0.6 s, 197 on lines; 2 passes: 0.5 s, 291). What is left is mostly long lines
across a dense area, and spells with many children in a narrow sector. Getting it that fast: the cells
near a line are its box's cells whose centre is near the line (numeric keys), not every cell of the box;
the spells and lines near a spell's own lines are gathered once per spell (with each line's box and
direction), not per spot tried; the cheap parts of the cost come first; and the dirty passes. Without the
line-against-line part it went 2.9 s → 0.3 s; with it, 0.72 s → 0.62 s (13 s → 5 s without a JIT). Then
for the game's browser, with the same result to the unit: no callback per grid cell (`_cellsAlong` hands
back an array of keys), the spell-against-spell and line-past-spot parts of the cost walked in place instead
of through helper calls and new arrays, and each neighbour's other lines (direction and length) worked out
once per spell searched instead of per spot: 5.7 s → 4.6 s without a JIT. A second round, again with the
same result to the unit (checked against the previous code on the game tree and on made-up trees, with
other pass and direction counts): a spell's search sorts the spells near its lines by the first ring of
spots that can bring a line within `LINE_CLEAR` of them (`_ringSpells`: a spot ρ away moves the point a
share t along the line by (1 - t)ρ), so the inner rings look at a fraction of them; one walk over the
cells gathers both the spells and the lines near a spell's lines (`_gatherFan`); `_cellsAlong` tests each
column only over the rows the line can reach there; the line-gap part has `_cross` and `_pointSeg2`
written out; `_narrow` is called only for a pair it counts; the spell-against-spell part compares squared
distances first; grid keys are small integers (`KEY_SPAN`); and the push-apart rounds build no string
keys or arrays. 4.95 s → 4.3 s in `node --jitless` (the game tree, best of five, before and after measured the same day;
the 4.6 s above was another day's run). What costs the time
now: the cost of each spot tried (about 390,000 of them), and gathering the lines near a spell's lines.
Tried and dropped along the way:
- pushing spells off lines with forces: pushes from many lines cancel out and jam spells together; even
  with the tree spaced out 1.5 times, 617 cases were left and spells moved 25 on average;
- bending the lines round spells at render time (a gap, then an arc, then a swerve): the lines read as
  broken or wavy, and it cost every tree repaint;
- skipping a spell whose cost has not changed since its last search found nothing better: 12% quicker,
  but 195 spells left on lines instead of 181;
- 8 directions instead of 12: 20% quicker, but lines meeting under 20 degrees 29 → 46;
- skipping `atan2` with dot or cross products first (angle cost, line gap): slower without a JIT - the
  built-in `atan2` is cheap next to the extra steps of interpreted script around it;
- cutting the lines near a spell's lines down per ring up front, as for spells: it cost more than it saved,
  since most spots never get as far as the line-gap part.

**No line shows through a spell.** The tree draws its lines first and the spells over them, and a
see-through spell (locked 0.4, undiscovered 0.6, available `availableAlpha`) is filled with the backdrop
first (`CanvasRenderer._backdrop`: the design's page colour, else the background colour; `NodeBatch.flush`'s
`backdrop`, `renderNode`, `_renderNodeSimple`, `renderMysteryNode`), so what is left of a line stops at the
spell's edge instead of showing through it. This applies to every tree, also those built before.

Tests: `modules/layoutDeclutterTest.js` (run by `run-tests.js`). A tree built before this keeps its
positions until it is built again.

---

## Layer 4: PreReqMaster (PRM)

PRM adds **extra prerequisite locks** to the built tree using NLP scoring. It runs after tree building and before final rendering.

### Purpose

Makes the spell tree more interesting by requiring players to master thematically related spells before unlocking others. Example: to unlock "Incinerate" (Expert), you might need to master "Firebolt" (Apprentice) — a thematic link discovered by NLP.

### Pipeline

```
Tree built (C++) → TreeGrowth.setTreeBuilt(true)
  → PreReqMaster.autoApplyLocks() triggered
    → buildLockRequest()       (eligibility filtering)
    → Send to C++ or JS       (NLP scoring)
    → applyLocksWithScorer()   (weighted random + cycle detection)
    → Render lock edges
```

### Phase 1: Lock Request Building

**Budget Calculation:**
```
totalLocks = totalNonRootSpells × globalLockPercent / 100

Distribution across schools:
  'even':          equal per school
  'proportional':  based on school spell count
  'random':        shuffled allocation
```

**Per-Tier Budget:**
```
tierPercents = { novice: 0%, apprentice: 10%, adept: 25%, expert: 40%, master: 50% }

Novice spells get 0 locks (no restrictions on basics).
Master spells get locks on 50% of them (gatekeep the powerful stuff).
```

**Candidate Pool Filtering:**

For each eligible spell, candidates must pass these filters:

| Filter | Purpose |
|--------|---------|
| Same school or nearby (grid distance) | Pool source control |
| Same / previous / higher tier (configurable) | Tier constraint |
| Not a descendant | Prevents deadlock (can't require mastering your own child) |
| Not only-reachable-through self | Prevents soft deadlock |
| Not already a prerequisite | No redundancy |
| Not already locked (optional) | Chain lock prevention |

Pool capped at 50 candidates per spell (performance).

### Phase 2: NLP Scoring

**Sent to C++ (`TreeNLP::ProcessPRMRequest()`) or JS fallback.**

**Algorithm (identical in both):**

1. **Text preparation** per spell:
   ```
   text = name(×2 weight) + description + effect names
   ```
2. **Tokenization:** lowercase, alphanumeric only, length > 2, stop words removed
3. **Per-pair TF-IDF corpus:** spell + its candidates form a mini corpus
4. **TF-IDF computation:**
   ```
   TF = term_count / total_tokens
   IDF = log((n_docs + 1) / (DF + 1)) + 1    (smoothed)
   weight = TF × IDF
   ```
5. **Cosine similarity:** dot product of TF-IDF vectors / (magnitude_A × magnitude_B)
6. **Proximity blending** (if `poolSource == "nearby"`):
   ```
   prox_score = max(0, 1 - distance / max_distance)
   final = (1 - proximityBias) × nlp_score + proximityBias × prox_score
   ```

**Output per spell:** Top 5 candidates with scores (0.0 to 1.0).

### Phase 3: Lock Application

**Weighted random selection** from top 5 candidates:
- Higher NLP score → higher probability of selection
- Not always the #1 match — allows meaningful variety

**Constraints during application:**

| Constraint | Method |
|------------|--------|
| Target usage cap (max 2) | Prevents one popular spell from being everyone's prereq |
| Cycle detection | Kahn's algorithm (topological sort) on combined tree + lock edges |
| Cycle removal | Remove lock edges where both endpoints are in detected cycles |
| Reachability validation | BFS from roots verifies all nodes still reachable |

**Kahn's Algorithm (cycle detection):**
```
1. Build adjacency: prereq → [dependents] (tree edges + lock edges)
2. Compute in-degree for each node
3. Queue all nodes with in-degree = 0
4. Process queue: for each node, decrement neighbors' in-degree
5. Nodes still with in-degree > 0 after processing = in a cycle
6. Remove lock edges connecting cycle nodes
```

### Lock Rendering

- Hidden by default — revealed when user clicks a spell node
- Rendered as distinct chain-link visual (not same as tree edges)
- Shows NLP similarity score
- Locks become **hard prerequisites** — the locked spell cannot receive XP until the lock prerequisite is mastered

### Fallback Strategy

```
Try C++ NLP (via "prm_score" command → TreeNLP::ProcessPRMRequest())
  → Success: use top candidates
  → Failure: fall back to JS TF-IDF scorer (synchronous, identical algorithm)
```

---

## Shared Output Format

All C++ builders output the same JSON schema so all downstream JS systems (layout, apply, PRM) work identically:

```json
{
  "version": "1.0",
  "schools": {
    "Destruction": {
      "root": "0x00012FCD",
      "layoutStyle": "tier_first" | "organic" | "radial",
      "nodes": [
        {
          "formId": "0x00012FCD",
          "name": "Flames",
          "tier": 0,
          "school": "Destruction",
          "children": ["0x0001C789", "0x0001C78A"],
          "prerequisites": [],
          "depth": 0,
          "theme": "fire",
          "section": "root"
        }
      ],
      "config_used": {
        "shape": "tier_first" | "organic",
        "density": 0.6,
        "symmetry": 0.3,
        "source": "classic" | "default" | "llm"
      }
    }
  },
  "generatedAt": "2026-02-10T...",
  "generator": "ClassicTreeBuilder (Tier-First)" | "SpellTreeBuilder",
  "seed": 123456,
  "validation": {
    "all_valid": true,
    "total_nodes": 47,
    "reachable_nodes": 47
  }
}
```

---

## End-to-End Example: Classic Growth Build

```
1. User clicks "Build Tree" in Classic Growth tab
2. classicMain.js sends:
   {
     command: "build_tree_classic",
     spells: [228 spells],
     config: { tier_zones: {Novice: {min:0, max:40}, ...}, ... }
   }

3. UIManager.cpp reads command="build_tree_classic", runs TreeBuilder::BuildClassic()

4. BuildClassic():
   - Groups 228 spells into 5 schools
   - Discovers themes: Destruction → [fire, frost, shock, ...]
   - Per school:
     - Picks "Flames" as Destruction root (vanilla Novice)
     - Places all Novice spells as root's children (depth 0)
     - Places Apprentice spells as children of Novice nodes (depth 1)
       → "Firebolt" links to "Flames" (high text similarity)
     - Places Adept spells under Apprentice (depth 2)
     - Expert under Adept (depth 3), Master under Expert (depth 4)
   - Validates: all 47 Destruction nodes reachable ✓

5. Result returns through background thread → SKSE AddTask → game thread → InteropCall to JS

6. proceduralTreeBuilder.js routes to TreeGrowthClassic.loadTreeData()

7. classicLayout.js runs:
   - Gets SUN grid (400 grid points, 5 school arcs)
   - Builds spatial hash graph
   - BFS wave placement:
     Wave 0: Flames at grid center → slot score includes tier zone bonus
     Wave 1: Fire Novice spells in inner 40% ring
     Wave 2: Apprentice spells in 10-55% ring
     ...
   - Tier zones NOW work because C++ put Novice at depth 0
   - Theme sectors cluster fire spells in one angular region

8. PRM runs (if enabled):
   - Picks 30% of non-root spells for locks
   - Scores candidates: "Incinerate" ↔ "Firebolt" similarity = 0.72
   - Applies lock: must master "Firebolt" to unlock "Incinerate"
   - Kahn's algorithm confirms no cycles

9. Canvas renders positioned, locked tree
```

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Separate C++ builder modes | Classic needs tier-first ordering; Tree needs NLP thematic ordering; Graph uses directed MST. Different algorithms for different goals. |
| Native C++ NLP engine | Eliminates Python subprocess, pip dependencies, Wine/Proton IPC issues. All algorithms are deterministic math that runs faster in C++. |
| All builders in single TreeBuilder.cpp | Shared NLP engine (TreeNLP), shared theme discovery, shared validation. No duplication. |
| Tier zones in JS layout, not C++ | Layout is visual concern. C++ builds structure; JS decides spatial placement. |
| Three spell matching modes | Users want control over visual clustering without rebuilding the tree. |
| PRM as post-processing | Locks are additive — they don't change the tree structure, only add prerequisite gates. |
| Weighted random for PRM | Always picking #1 NLP match would feel mechanical. Top-5 selection adds variety. |
| Kahn's cycle detection | Lock edges can create circular dependencies. Topological sort catches them efficiently. |
| Round-robin theme interleaving (Tree mode) | Without it, the largest theme monopolizes root's children. Interleaving distributes growth fairly. |
| Convergence enforcement (Tree mode) | Expert/Master spells should feel hard-won. Forced multi-prerequisite gates create meaningful progression. |
