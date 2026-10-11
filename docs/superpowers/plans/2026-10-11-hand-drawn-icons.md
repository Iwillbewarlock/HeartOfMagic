# Hand-drawn Icons and Tidier Panel Chrome Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every bracket-glyph "icon" (`[S]`, `[*]`, `X`, …) in the Heart of Magic panel with a real icon (game-icons.net shapes, redrawn per design), and take the AI-looking chrome off the panel (all-caps labels, boxed buttons, pill switches, the Modern Dark gradient).

**Architecture:** Each icon spot becomes an empty slot `<span class="hom-icon" data-icon="<id>">`. A new module `HomIcons` paints every slot's `background-image` from the active design's icon folder (`design.icons`, default `themes/icons-line/`), one 48px PNG per icon. Button text moves into `<span class="hom-label">` so translations stay plain text. Chrome rules live in one new common sheet `panel-chrome.css`; per-design colours stay in each design's sheet.

**Tech Stack:** PrismaUI (Ultralight) HTML/CSS/ES5 JS (`var` only), Node test runner `run-tests.js`, headless Chrome for rasterising and screenshots, PowerShell `System.Drawing` for image processing, Gemini through the `antigravity-offload` skill for the drawn sets.

**Spec:** `docs/superpowers/specs/2026-10-10-hand-drawn-icons-ui-design.md` (§12 overrides earlier sections).

## Global Constraints

- Work only in worktree `D:/DEV/HeartOfMagic/.claude/worktrees/hand-drawn-icons`, branch `feature/hand-drawn-icons` (from `master`).
- `PANEL` below = `PrismaUI/views/SpellLearning/SpellLearningPanel` inside the worktree.
- Find code by id/selector/text, never by line number (the survey ran on another branch).
- PrismaUI JS: `var` only; no `let`/`const`/arrow functions/template literals/`class`/`for...of`/`Array.from` (docs/MODULE_CONTRACTS.md).
- No Python anywhere in the repo or its tools. One-off image work happens outside the repo in `E:\Users\qtuna\hom-icons\`; only finished PNGs enter the repo.
- No new per-frame cost: no filters, shadows, gradients or image scaling drawn per frame; icons are DOM backgrounds set once per design change.
- Never put a CSS variable inside `url()`.
- UI must work in all five designs: Classic, Modern Dark, Arcane, Night Grimoire, Chalkboard. Add-on designs without `icons` get the line set.
- Never show raw codes or bracket text to the player.
- The 19 icon ids, exactly: `scan build apply clear replay save reset import fullscreen close hotkey tree-view display debug progression early tome tree-gen prereq`. The spots of the dropped ids (`export blacklist whitelist locks preset-save howto-settings howto-xp collapse expand`) lose their glyph but get no icon: text only (spec §12.11).
- Every task that edits `PANEL/index.html` ends by running `node tools/make-dev-harness.js` (repo root) — `run-tests.js` fails on a stale harness.
- Every task that edits `PANEL/lang/*.json` ends by running `node PANEL/lang/build-preloads.js`.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **A language switch rewrites a button's HTML.** After `switchLocale`, every action button that has an icon must still show it (the icon slot must sit outside any element carrying `data-i18n`/`data-i18n-html`). Test: `IconSlotsTest` asserts no `data-i18n*` element contains a `hom-icon`, and no lang value contains `hom-icon`/`btn-icon`.
2. **Script-written labels wipe the slot.** The Scan button's "Scanning…" / restore path and the fullscreen toggle write text into buttons. Expected: icon stays. Test: `HomIconsTest` covers `HomIcons.setLabel` keeping the slot; Task 4 replaces the three writers with it.
3. **An add-on design without `icons`, or with a wrong path.** Expected: the line set, never a blank or broken image box. Test: `HomIconsTest` "unknown set falls back"; `IconSetsTest` checks every set a shipped design names has all 19 PNGs.
4. **Night Grimoire / Chalkboard are JSON designs the harness does not load by itself.** Expected: screenshots of all five designs. Covered by the screenshot wrapper in Task 6 seeding both JSONs with `DesignPresets.onLoaded`.
5. **Korean and long translations in the new label spans.** Expected: label and icon on one line, no clipping. Covered by the Korean screenshot pass in Task 6 and Task 10.

---

### Task 1: Pick the 19 shapes (user gate)

**Files:**
- Create: `docs/icons/game-icons-picks.json`
- Create: `docs/icons/svg/<id>.svg` (19 files, copied from github.com/game-icons/icons)

**Interfaces:**
- Produces: `docs/icons/game-icons-picks.json` = `{ "<id>": { "author": "<folder>", "name": "<file stem>", "url": "https://game-icons.net/1x1/<author>/<name>.html" }, ... }` for all 19 ids; `docs/icons/svg/<id>.svg` for each.

- [ ] **Step 1: Fetch the icon index**

Run (Bash): `gh api repos/game-icons/icons/git/trees/master?recursive=1 --jq '.tree[].path' | grep '\.svg$' > E:/Users/qtuna/hom-icons/index.txt; wc -l E:/Users/qtuna/hom-icons/index.txt`
Expected: several thousand `author/name.svg` lines.

- [ ] **Step 2: Choose 2–3 candidates per id**

Search `index.txt` by meaning, e.g. `scan`: `magnifying-glass`, `spell-book`, `crystal-ball`; `build`: `tree-branch`, `vine-flower`; `apply`: `quill-ink`, `stamper`; `clear`: `trash-can`, `broom`; `save`: `scroll-unfurled`, `wax-seal`; `close`: `cancel`, `cross-mark`. Prefer one or two authors overall (lorc, delapouite) so line weight matches. Download candidates with `curl -sL https://raw.githubusercontent.com/game-icons/icons/master/<author>/<name>.svg -o E:/Users/qtuna/hom-icons/cand/<id>--<author>--<name>.svg`.

- [ ] **Step 3: Contact sheet artifact**

Build one HTML page: a row per id (Korean meaning + id), each candidate drawn inline at 48px and 24px on the Arcane parchment colour `#e3d4ae` and on dark `#1e1812`, with `author/name` under it. Publish it with the Artifact tool (quickstart first, intent `other`). Ask the user to pick per row (AskUserQuestion is too small for 19 rows; ask them to reply with `id: number` lines or "all first").

- [ ] **Step 4: Save the picks**

Copy each chosen SVG to `docs/icons/svg/<id>.svg` unchanged. Write `docs/icons/game-icons-picks.json` with author, name, url for all 19.

- [ ] **Step 5: Commit**

```bash
git add docs/icons
git commit -m "Icons: the 19 game-icons.net shapes picked for the panel (CC BY 3.0 sources)"
```

---

### Task 2: `HomIcons` module (TDD)

**Files:**
- Create: `PANEL/modules/homIcons.js`
- Create: `PANEL/modules/homIconsTest.js`
- Modify: `PANEL/run-tests.js` (two `loadModule` lines before `loadModule('unificationTest', …)`)

**Interfaces:**
- Produces (global `HomIcons`):
  - `HomIcons.IDS` — the 19 ids (array of strings, order as in Global Constraints)
  - `HomIcons.DEFAULT_SET` = `'themes/icons-line/'`
  - `HomIcons.setPath(path)` — path string ending in `/`; anything else → `DEFAULT_SET`; then `paint(document)`
  - `HomIcons.paint(root)` — sets `style.backgroundImage = 'url("' + set + id + '.png")'` on every `.hom-icon` under `root` whose `data-icon` is in `IDS`
  - `HomIcons.slot(id)` → HTML string `<span class="hom-icon" data-icon="<id>" aria-hidden="true" style="background-image:url(&quot;<set><id>.png&quot;)"></span>`
  - `HomIcons.setLabel(button, text)` — writes `text` into `button`'s `.hom-label` child if present, else `button.textContent = text`

- [ ] **Step 1: Write the failing test** — `PANEL/modules/homIconsTest.js`

```js
/**
 * HomIconsTest - node tests for modules/homIcons.js: paths, painting slots,
 * fallback for a design without icons, and labels that keep the icon slot.
 * Depends on: homIcons.js (global HomIcons).
 */
var HomIconsTest = {
    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _slot: function(id) {
        var attrs = { 'data-icon': id };
        return { className: 'hom-icon', style: {}, getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; } };
    },

    _root: function(slots) {
        return { querySelectorAll: function(sel) { return sel === '.hom-icon' ? slots : []; } };
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('HomIcons');

        this.check(HomIcons.IDS.length === 19, '19 icon ids');
        this.check(HomIcons.IDS.indexOf('scan') !== -1 && HomIcons.IDS.indexOf('prereq') !== -1, 'ids include scan and prereq');

        var a = this._slot('scan'), b = this._slot('nope');
        HomIcons._set = HomIcons.DEFAULT_SET;
        HomIcons.paint(this._root([a, b]));
        this.check(a.style.backgroundImage === 'url("themes/icons-line/scan.png")', 'paint uses the default line set');
        this.check(!b.style.backgroundImage, 'unknown id is left alone');

        var savedDoc = typeof document !== 'undefined' ? document : undefined;
        var c = this._slot('save');
        global.document = this._root([c]);
        HomIcons.setPath('themes/arcane/icons/');
        this.check(c.style.backgroundImage === 'url("themes/arcane/icons/save.png")', 'setPath repaints with the design set');
        HomIcons.setPath(undefined);
        this.check(c.style.backgroundImage === 'url("themes/icons-line/save.png")', 'design without icons falls back to the line set');
        HomIcons.setPath('themes/x');
        this.check(HomIcons._set === HomIcons.DEFAULT_SET, 'path without trailing slash falls back');
        global.document = savedDoc;

        var html = HomIcons.slot('close');
        this.check(html.indexOf('class="hom-icon"') !== -1 && html.indexOf('data-icon="close"') !== -1, 'slot() markup');
        this.check(html.indexOf('icons-line/close.png') !== -1, 'slot() carries the current set');

        var label = { textContent: 'Scan Spells' };
        var btn = { textContent: '', querySelector: function(sel) { return sel === '.hom-label' ? label : null; } };
        HomIcons.setLabel(btn, 'Scanning...');
        this.check(label.textContent === 'Scanning...' && btn.textContent === '', 'setLabel writes the label span, not the button');
        var plain = { textContent: 'X', querySelector: function() { return null; } };
        HomIcons.setLabel(plain, 'Y');
        this.check(plain.textContent === 'Y', 'setLabel falls back to textContent');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.HomIconsTest = HomIconsTest;
```

In `run-tests.js`, add before `loadModule('unificationTest', …)`:

```js
loadModule('homIcons', './modules/homIcons.js');
loadModule('homIconsTest', './modules/homIconsTest.js');
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd PANEL && node run-tests.js`
Expected: `✗ Failed to load homIcons` and the HomIcons suite counted as failed.

- [ ] **Step 3: Implement** — `PANEL/modules/homIcons.js`

```js
/**
 * HomIcons - the panel's drawn icons. Every icon spot is an empty
 * <span class="hom-icon" data-icon="<id>">; this paints its background from the
 * active design's icon folder (design.icons, e.g. "themes/arcane/icons/"), one
 * 48px PNG per id. A design without icons, or with a path that does not end in
 * "/", gets the line set. DesignPresets.apply calls setPath.
 *
 * Limit: one file per icon (19 per set) instead of an atlas, because
 * background-position on a sprite is unproven in Ultralight. Upgrade path: an
 * atlas once that is measured in game.
 *
 * Depends on: nothing.
 */
var HomIcons = {
    IDS: ['scan', 'build', 'apply', 'clear', 'replay', 'save', 'reset', 'import', 'fullscreen', 'close',
          'hotkey', 'tree-view', 'display', 'debug', 'progression', 'early', 'tome', 'tree-gen', 'prereq'],
    DEFAULT_SET: 'themes/icons-line/',
    _set: 'themes/icons-line/',

    setPath: function(path) {
        var ok = typeof path === 'string' && path.length > 1 && path.charAt(path.length - 1) === '/';
        this._set = ok ? path : this.DEFAULT_SET;
        if (typeof document !== 'undefined' && document) this.paint(document);
    },

    paint: function(root) {
        if (!root || !root.querySelectorAll) return;
        var slots = root.querySelectorAll('.hom-icon');
        for (var i = 0; i < slots.length; i++) {
            var id = slots[i].getAttribute('data-icon');
            if (this.IDS.indexOf(id) === -1) continue;
            slots[i].style.backgroundImage = 'url("' + this._set + id + '.png")';
        }
    },

    slot: function(id) {
        return '<span class="hom-icon" data-icon="' + id + '" aria-hidden="true" style="background-image:url(&quot;' +
            this._set + id + '.png&quot;)"></span>';
    },

    setLabel: function(button, text) {
        if (!button) return;
        var label = button.querySelector ? button.querySelector('.hom-label') : null;
        if (label) label.textContent = text;
        else button.textContent = text;
    }
};

if (typeof window !== 'undefined') window.HomIcons = HomIcons;
if (typeof global !== 'undefined') global.HomIcons = HomIcons;
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd PANEL && node run-tests.js`
Expected: `HomIcons` suite all `[PASS]`, overall exit code 0.

- [ ] **Step 5: Commit**

```bash
git add PANEL/modules/homIcons.js PANEL/modules/homIconsTest.js PANEL/run-tests.js
git commit -m "HomIcons: paint icon slots from the design's icon folder, line set as fallback"
```

(Use the real `PrismaUI/views/...` paths in `git add`.) Note: `Distribution.cmake` globs `modules/*Test.js` at configure time; reconfigure before the next release build so the new test is stripped.

---

### Task 3: The two line sets (PNG)

**Files:**
- Create: `PANEL/themes/icons-line/<id>.png` (19, neutral `#c8c2b4`)
- Create: `PANEL/themes/icons-line-gold/<id>.png` (19, dull gold `#b89a5a`)
- Create: `PANEL/modules/iconSetsTest.js`; Modify: `PANEL/run-tests.js`

**Interfaces:**
- Consumes: `docs/icons/svg/<id>.svg` (Task 1), `HomIcons.IDS` (Task 2)
- Produces: the two folders; global `IconSetsTest`

- [ ] **Step 1: Write the failing test** — `PANEL/modules/iconSetsTest.js`

```js
/**
 * IconSetsTest - every icon folder a shipped design names (plus the line set
 * and the gold line set) has a PNG for each of the 19 ids.
 * Depends on: homIcons.js, node fs.
 */
var IconSetsTest = {
    passed: 0,
    failed: 0,
    SETS: ['themes/icons-line/', 'themes/icons-line-gold/'],

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('IconSets');
        var fs = require('fs'), path = require('path');
        var panel = path.join(__dirname, '..');
        for (var s = 0; s < this.SETS.length; s++) {
            var missing = [];
            for (var i = 0; i < HomIcons.IDS.length; i++) {
                if (!fs.existsSync(path.join(panel, this.SETS[s], HomIcons.IDS[i] + '.png'))) missing.push(HomIcons.IDS[i]);
            }
            this.check(missing.length === 0, this.SETS[s] + ' has all 19' + (missing.length ? ' (missing: ' + missing.join(', ') + ')' : ''));
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.IconSetsTest = IconSetsTest;
```

Add `loadModule('iconSetsTest', './modules/iconSetsTest.js');` after the homIconsTest line.

- [ ] **Step 2: Run to verify it fails** — `node run-tests.js` → `IconSets` two `[FAIL]` lines listing all 19 missing.

- [ ] **Step 3: Rasterise** — write `E:\Users\qtuna\hom-icons\line\sheet.html` (outside the repo):

```html
<!doctype html><meta charset="utf-8">
<style>html,body{margin:0;background:transparent}#g{display:grid;grid-template-columns:repeat(7,192px);grid-auto-rows:192px}
#g div{display:flex;align-items:center;justify-content:center}#g svg{width:176px;height:176px}</style>
<div id="g"></div>
<script>
var IDS=['scan','build','apply','clear','replay','save','reset','import','fullscreen','close','hotkey','tree-view','display','debug','progression','early','tome','tree-gen','prereq'];
var colour=(location.hash||'#c8c2b4');
var root='file:///D:/DEV/HeartOfMagic/.claude/worktrees/hand-drawn-icons/docs/icons/svg/';
IDS.forEach(function(id){var d=document.createElement('div');document.getElementById('g').appendChild(d);
 fetch(root+id+'.svg').then(function(r){return r.text()}).then(function(t){
  t=t.replace(/<path d="M0 0h512v512H0z"[^>]*\/>/,'').replace(/fill="#fff"/g,'fill="'+colour+'"');
  d.innerHTML=t;});});
</script>
```

Run (Bash), once per colour:
```bash
C="/c/Program Files/Google/Chrome/Application/chrome.exe"
"$C" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files --default-background-color=00000000 --window-size=1344,576 --virtual-time-budget=5000 --screenshot="E:/Users/qtuna/hom-icons/line/neutral.png" "file:///E:/Users/qtuna/hom-icons/line/sheet.html#%23c8c2b4"
"$C" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files --default-background-color=00000000 --window-size=1344,576 --virtual-time-budget=5000 --screenshot="E:/Users/qtuna/hom-icons/line/gold.png" "file:///E:/Users/qtuna/hom-icons/line/sheet.html#%23b89a5a"
```
Open both PNGs with Read: 19 icons in a 7×3 grid, transparent background, no black squares (if a black square shows, the SVG's background path differs — adjust the `replace` regex to that file's first `<path>`).

- [ ] **Step 4: Slice and shrink** — `E:\Users\qtuna\hom-icons\slice.ps1`:

```powershell
param([string]$Sheet, [string]$OutDir, [int]$Cell = 192, [int]$Cols = 7, [int]$Size = 48)
Add-Type -AssemblyName System.Drawing
$ids = 'scan','build','apply','clear','replay','save','reset','import','fullscreen','close','hotkey','tree-view','display','debug','progression','early','tome','tree-gen','prereq'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$src = [System.Drawing.Bitmap]::FromFile($Sheet)
for ($i = 0; $i -lt $ids.Count; $i++) {
    $x = ($i % $Cols) * $Cell; $y = [math]::Floor($i / $Cols) * $Cell
    $dst = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'; $g.CompositingMode = 'SourceCopy'
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $Size, $Size), (New-Object System.Drawing.Rectangle $x, $y, $Cell, $Cell), 'Pixel')
    $g.Dispose(); $dst.Save((Join-Path $OutDir ($ids[$i] + '.png')), [System.Drawing.Imaging.ImageFormat]::Png); $dst.Dispose()
}
$src.Dispose()
```

Run:
```powershell
& E:\Users\qtuna\hom-icons\slice.ps1 -Sheet E:\Users\qtuna\hom-icons\line\neutral.png -OutDir D:\DEV\HeartOfMagic\.claude\worktrees\hand-drawn-icons\PrismaUI\views\SpellLearning\SpellLearningPanel\themes\icons-line
& E:\Users\qtuna\hom-icons\slice.ps1 -Sheet E:\Users\qtuna\hom-icons\line\gold.png -OutDir D:\DEV\HeartOfMagic\.claude\worktrees\hand-drawn-icons\PrismaUI\views\SpellLearning\SpellLearningPanel\themes\icons-line-gold
```

- [ ] **Step 5: Run to verify it passes** — `node run-tests.js` → `IconSets` both `[PASS]`. Open 3 PNGs (`scan`, `close`, `prereq`) with Read: centred, transparent, recognisable at 48px.

- [ ] **Step 6: Commit**

```bash
git add PANEL/themes/icons-line PANEL/themes/icons-line-gold PANEL/modules/iconSetsTest.js PANEL/run-tests.js
git commit -m "Icons: neutral and gold line sets, 19 PNGs each, from the picked game-icons shapes"
```

---

### Task 4: Slots in place of every glyph

**Files:**
- Modify: `PANEL/index.html` (glyph spans, header buttons, close buttons, collapse icon, preset-save buttons, how-to headings, `<link>` and `<script>` tags)
- Create: `PANEL/panel-chrome.css` (slot sizing and the collapse chevron only in this task)
- Modify: `PANEL/lang/*.json` (13 files, 14 keys), regenerate `PANEL/lang/*.js`
- Modify: `PANEL/modules/uiHelpers.js` (`restoreScanButton`), `PANEL/modules/buttonHandlers.js` (scan "Scanning" text), `PANEL/script.js` (`toggleFullscreen`), `PANEL/modules/prereqMaster.js` (collapse icon), `PANEL/modules/colorPicker.js` (close button), `PANEL/modules/designPresets.js` (`apply`, built-in `icons`)
- Modify: `SKSE/Plugins/SpellLearning/presets/design/NightGrimoire.json`, `Chalkboard.json` (`"icons"`)
- Create: `PANEL/modules/iconSlotsTest.js`; Modify: `PANEL/run-tests.js`
- Regenerate: `PANEL/dev-harness.html`

**Interfaces:**
- Consumes: `HomIcons.slot/paint/setPath/setLabel` (Task 2), line sets (Task 3)
- Produces: markup contract — every icon is `<span class="hom-icon" data-icon="<id>" aria-hidden="true"></span>`; every button label that a script or translation rewrites is `<span class="hom-label" …>`; the text-only spots (export, blacklist, whitelist and apply-locks buttons, the two preset-save buttons) have a label span and no slot, the two how-to headings are plain `<h4 data-i18n=…>`, and the Prereq Master collapse indicator is an empty `.prm-collapse-icon` drawn by CSS as a chevron (`collapsed` class when closed); designs carry `icons` (built-ins: Classic `themes/icons-line/`, Modern `themes/icons-line-gold/`, Arcane `themes/icons-line-gold/` until Task 7; JSON designs `themes/icons-line-gold/` until Tasks 8–9)

- [ ] **Step 1: Write the failing test** — `PANEL/modules/iconSlotsTest.js`

```js
/**
 * IconSlotsTest - markup and translations carry no bracket glyphs: every icon
 * is an empty .hom-icon slot with a known id, no slot sits inside an element
 * that a translation rewrites, and no lang value holds an icon.
 * Depends on: homIcons.js, node fs.
 */
var IconSlotsTest = {
    passed: 0,
    failed: 0,
    GLYPH: /<span class="(btn-icon|settings-block-icon|prm-collapse-icon)[^"]*"[^>]*>[^<]+<\/span>|>\s*(\[[A-Za-z*~^+\-v ]\]|X|×)\s*</,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('IconSlots');
        var fs = require('fs'), path = require('path');
        var panel = path.join(__dirname, '..');
        var html = fs.readFileSync(path.join(panel, 'index.html'), 'utf8');

        var m = html.match(this.GLYPH);
        this.check(!m, 'index.html has no bracket glyph' + (m ? ' (found: ' + m[0].slice(0, 60) + ')' : ''));

        var slots = html.match(/<span class="hom-icon[^"]*" data-icon="([^"]+)"/g) || [];
        var bad = [];
        for (var i = 0; i < slots.length; i++) {
            var id = slots[i].replace(/.*data-icon="([^"]+)".*/, '$1');
            if (HomIcons.IDS.indexOf(id) === -1) bad.push(id);
        }
        // 30 = 9 action buttons + 4 save/reset/import buttons + 9 settings heads + fullscreen + 7 closes (Steps 4-6)
        this.check(slots.length >= 30 && bad.length === 0, slots.length + ' slots, all known ids' + (bad.length ? ' (unknown: ' + bad.join(', ') + ')' : ''));

        var nested = html.match(/<[^>]+data-i18n(-html)?="[^"]*"[^>]*>(?:(?!<\/button>|<\/h4>|<\/span>).)*hom-icon/);
        this.check(!nested, 'no slot inside a translated element');

        var langDir = path.join(panel, 'lang');
        var files = fs.readdirSync(langDir).filter(function(f) { return /\.json$/.test(f); });
        var dirty = [];
        for (var f = 0; f < files.length; f++) {
            var text = fs.readFileSync(path.join(langDir, files[f]), 'utf8');
            if (/btn-icon|hom-icon|"\[[A-Za-z*~^+\-v]\] /.test(text)) dirty.push(files[f]);
            var js = files[f].replace(/\.json$/, '.js');
            this.check(fs.existsSync(path.join(langDir, js)), js + ' exists');
        }
        this.check(files.length === 13 && dirty.length === 0, files.length + ' lang files without icon glyphs' + (dirty.length ? ' (dirty: ' + dirty.join(', ') + ')' : ''));
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof global !== 'undefined') global.IconSlotsTest = IconSlotsTest;
```

Add `loadModule('iconSlotsTest', './modules/iconSlotsTest.js');` after iconSetsTest.

- [ ] **Step 2: Run to verify it fails** — `node run-tests.js` → `IconSlots` fails on the glyph regex and the dirty lang files.

- [ ] **Step 3: Lang values to plain text**

For each of the 13 `PANEL/lang/*.json`, edit these keys:
- Strip the leading `<span class="btn-icon">[?]</span>` and one following space from: `buttons.scanSpells`, `buttons.exportScan`, `buttons.buildTree`, `buttons.applyTree`, `buttons.clearTree`, `buttons.replay`, `filter.blacklist`, `filter.whitelist`, `prm.applyLocks`, `prm.clearLocks`.
- Strip the leading `[i] `, `[^] `, `[+] ` from: `howto.currentSettings`, `howto.gainingXp`, `presets.save`, `settings.presets.save`.

Use a one-off Node script outside the repo (keys are nested; edit the text so formatting stays, do not re-serialise). Save as `E:\Users\qtuna\hom-icons\strip-lang.js`:

```js
// One-off: drop icon glyphs from the panel's translation values. Arg: the lang folder.
var fs = require('fs'), path = require('path');
var dir = process.argv[2];
fs.readdirSync(dir).filter(function(f) { return /\.json$/.test(f); }).forEach(function(f) {
    var file = path.join(dir, f), t = fs.readFileSync(file, 'utf8'), o = t;
    // In the JSON text the quotes are escaped: <span class=\"btn-icon\">[*]</span>
    t = t.replace(/<span class=\\"btn-icon\\">\[[^\]]*\]<\/span>\s?/g, '');
    t = t.replace(/("(?:currentSettings|gainingXp|save)"\s*:\s*")\[[i^+]\] /g, '$1');
    if (t !== o) { fs.writeFileSync(file, t); console.log('edited ' + f); }
});
```

Run:
```bash
node E:/Users/qtuna/hom-icons/strip-lang.js D:/DEV/HeartOfMagic/.claude/worktrees/hand-drawn-icons/PrismaUI/views/SpellLearning/SpellLearningPanel/lang
node D:/DEV/HeartOfMagic/.claude/worktrees/hand-drawn-icons/PrismaUI/views/SpellLearning/SpellLearningPanel/lang/build-preloads.js
```
Expected: `edited <file>` for all 13 json files.

Then `grep -c "btn-icon\|\"\[[i^+]\] " *.json` → every file `0`. Spot-check `en.json` values: `"scanSpells": "Scan Spells"`, `"save": "Save"`.

- [ ] **Step 4: Markup — action buttons with `data-i18n-html`**

For each button id `scanBtn, saveBtn, blacklistBtn, whitelistBtn, easyBuildBtn, easyApplyBtn, easyClearBtn, easyReplayBtn, prmApplyBtn, prmClearBtn, tgBuildBtn, tgApplyBtn, tgClearBtn`: move `data-i18n-html="<key>"` off the `<button>` onto an inner label span and replace the glyph span with a slot (text-only buttons below: drop it). Example (`scanBtn`):

Before:
```html
<button id="scanBtn" class="btn btn-primary" data-i18n-html="buttons.scanSpells">
    <span class="btn-icon">[*]</span> Scan Spells
</button>
```
After:
```html
<button id="scanBtn" class="btn btn-primary">
    <span class="hom-icon" data-icon="scan" aria-hidden="true"></span><span class="hom-label" data-i18n-html="buttons.scanSpells">Scan Spells</span>
</button>
```
Ids: scanBtn→`scan`, easyBuildBtn/tgBuildBtn→`build`, easyApplyBtn/tgApplyBtn→`apply`, easyClearBtn/tgClearBtn/prmClearBtn→`clear`, easyReplayBtn→`replay`.

Text only (no slot, label span only): `saveBtn` (export), `blacklistBtn`, `whitelistBtn`, `prmApplyBtn` (locks). Example (`saveBtn`):
```html
<button id="saveBtn" class="btn btn-secondary dev-only hidden" style="display:none;" disabled>
    <span class="hom-label" data-i18n-html="buttons.exportScan">Export Scan</span>
</button>
```
(keep each button's own id, classes, attributes and key).

- [ ] **Step 5: Markup — buttons with a sibling label span**

`#saveSettingsBtn` (`[S]`→`save`), `#resetSettingsBtn` and the early-learning reset button (`[R]`→`reset`), the Import footer button (`[v]`→`import`): replace `<span class="btn-icon">[?]</span>` with the slot and add `hom-label` to the existing sibling `<span data-i18n="…">` class list.

- [ ] **Step 6: Markup — settings block heads, how-to, preset save, header, closes, collapse**

- Each `<span class="settings-block-icon">…</span>` → `<span class="hom-icon settings-block-icon" data-icon="<id>" aria-hidden="true"></span>` with: Hotkey→`hotkey`, Tree View→`tree-view`, UI Display→`display`, Developer & Debug→`debug`, Progression→`progression`, Early Spell Learning→`early`, Spell Tome Learning→`tome`, Tree Generation→`tree-gen`, Pre Req Master→`prereq`. Identify each block by its `settings-block-title` `data-i18n` key, not by the glyph.
- How-to `<h4 data-i18n="howto.currentSettings">[i] Current Settings</h4>` → `<h4 data-i18n="howto.currentSettings">Current Settings</h4>` (text only, no slot); same for `howto.gainingXp`.
- `#saveScannerPresetBtn` / `#saveSettingsPresetBtn`: move `data-i18n` off the button → `<button …><span class="hom-label" data-i18n="presets.save">Save</span></button>` (text only, no slot; keep each button's own key).
- `#fullscreenBtn` text `[ ]` → slot `fullscreen`; `#closeBtn` `X`, `#close-details`, `#close-howto`, `#heart-settings-close`, `#color-picker-close`, `#modal-close-btn`, `#spawn-modal-close` `×` → slot `close`. Keep ids, classes and `title`/`data-i18n-title`.
- `<span class="prm-collapse-icon">[-]</span>` → `<span class="prm-collapse-icon" aria-hidden="true"></span>` (no icon; Step 9 CSS draws a chevron, open = no `collapsed` class).
- Add `<link rel="stylesheet" href="panel-chrome.css">` right after the `patch-ui.css` link, and `<script src="modules/homIcons.js"></script>` right before `modules/uiHelpers.js`.

- [ ] **Step 7: Script writers**

- `uiHelpers.js` `restoreScanButton`: replace the `innerHTML` line with
  ```js
  HomIcons.setLabel(scanBtn, t('buttons.scanSpells'));
  ```
- `buttonHandlers.js` (the line `scanBtn.textContent = t('status.scanning');`): replace with `HomIcons.setLabel(scanBtn, t('status.scanning'));`
- `script.js` `toggleFullscreen`: delete `btn.textContent = state.isFullscreen ? '[ ]' : '[ ]';` (keep the `title` line).
- `prereqMaster.js`: replace `collapseIcon.textContent = content.classList.contains('collapsed') ? '[+]' : '[-]';` with
  ```js
  if (content.classList.contains('collapsed')) collapseIcon.classList.add('collapsed');
  else collapseIcon.classList.remove('collapsed');
  ```
  (the chevron is CSS, Step 9; no text is written).
- `colorPicker.js`: replace `closeBtn.textContent = '×';` with `closeBtn.innerHTML = HomIcons.slot('close');`
- `grep -rn "\[\*\]\|\[S\]\|\[X\]\|'×'\|\"×\"" PANEL/modules PANEL/script.js` → only `spellCardView.js` (spell card, out of scope) may remain.

- [ ] **Step 8: Designs choose a set**

`designPresets.js`: in `BUILT_IN`, add `icons: 'themes/icons-line/'` to `classic`, `icons: 'themes/icons-line-gold/'` to `modern` and `arcane`. In `apply`, right after `this._applyCss(preset);` add:
```js
        // The design's icon folder (design.icons); none or a bad path -> the line set
        if (typeof HomIcons !== 'undefined') HomIcons.setPath(preset.icons);
```
Add one line to the header comment's field list: ` *   icons    - folder of the 19 icon PNGs (modules/homIcons.js), ending in "/"`.
In both JSON designs add `"icons": "themes/icons-line-gold/",` next to `"cssFile"`.

- [ ] **Step 9: Slot sizing and collapse chevron** — create `PANEL/panel-chrome.css`

```css
/* Panel chrome shared by every design (docs/superpowers/specs/2026-10-10-hand-drawn-icons-ui-design.md).
   Loaded after patch-ui.css; a design's own sheet comes later and can override colours. */

/* Icon slots: HomIcons sets background-image; one 48px PNG shown at 1.5em */
.hom-icon {
    display: inline-block;
    width: 1.5em;
    height: 1.5em;
    flex: none;
    vertical-align: -0.38em;
    margin-right: 0.45em;
    background: center / contain no-repeat;
}
.header-btn .hom-icon,
.popup-close .hom-icon,
.modal-close .hom-icon { margin-right: 0; }
.settings-block-icon.hom-icon { font-size: 1em; }

/* Prereq Master collapse indicator: a small chevron, not [+]/[-]. Down = open, right = collapsed */
.prm-collapse-icon {
    display: inline-block;
    width: 0.45em;
    height: 0.45em;
    border-right: 2px solid currentColor;
    border-bottom: 2px solid currentColor;
    transform: rotate(45deg);
    margin: 0 0.5em 0.15em 0.15em;
}
.prm-collapse-icon.collapsed { transform: rotate(-45deg); }
```

- [ ] **Step 10: Regenerate harness and run tests**

Run: `node tools/make-dev-harness.js` (repo root) then `cd PANEL && node run-tests.js`
Expected: all suites pass, including `IconSlots`.

- [ ] **Step 11: Screenshot check** — run the Task 6 Step 1 wrapper for `arcane~SCAN`, `modern~SETTINGS`, `classic~SETTINGS`, then Korean (`arcane~SETTINGS~ko`, see wrapper) and open the PNGs: every slot shows an icon, the text-only spots show just their label, the Prereq Master head shows a chevron, no `[` text, no empty boxes.

- [ ] **Step 12: Commit**

```bash
git add -A PrismaUI SKSE tools
git commit -m "Panel: icon slots in place of every bracket glyph; translations are plain text again"
```

---

### Task 5: Harness "Load Test Data" passes the scan object

**Files:**
- Modify: `PANEL/dev-harness-toolbar.js` (the `window.updateSpellData(JSON.stringify(allSpells));` line)

- [ ] **Step 1:** Replace that line with
```js
        window.updateSpellData(JSON.stringify({ scanMode: 'all_spells', spellCount: allSpells.length, spells: allSpells }));
```
- [ ] **Step 2:** Screenshot `arcane~SCAN` (Task 6 wrapper): the status line reads "753 spells scanned across 5 schools", not "undefined".
- [ ] **Step 3:** `node run-tests.js` passes (the toolbar is not generated, no harness rebuild needed).
- [ ] **Step 4: Commit** — `git commit -am "Dev harness: Load Test Data hands over a scan object, so the status line counts"`

---

### Task 6: Chrome tidy (caps, buttons, switches, cards, Modern)

**Files:**
- Modify: `PANEL/panel-chrome.css`, `PANEL/themes/design-modern.css`; read (edit only if a screenshot shows a pill/gradient/box coming back) `PANEL/themes/design-arcane.css`, `PANEL/themes/design-darkbook.css`

**Interfaces:**
- Consumes: Task 4 markup
- Produces: the screenshot wrapper reused by Tasks 4, 5, 7–10

- [ ] **Step 1: Screenshot wrapper** (outside the repo) — `E:\Users\qtuna\hom-shots\wrap.html`:

```html
<!doctype html><meta charset="utf-8">
<style>html,body{margin:0;background:#000}iframe{border:0;width:1600px;height:1000px;display:block}</style>
<iframe id="f" src="file:///D:/DEV/HeartOfMagic/.claude/worktrees/hand-drawn-icons/PrismaUI/views/SpellLearning/SpellLearningPanel/dev-harness.html"></iframe>
<script>
var p=(location.hash||'#arcane~SCAN').slice(1).split('~'), design=p[0], page=p[1]||'', lang=p[2]||'';
var JSONS={'night-grimoire':'NightGrimoire','chalkboard':'Chalkboard'};
var base='file:///D:/DEV/HeartOfMagic/.claude/worktrees/hand-drawn-icons/SKSE/Plugins/SpellLearning/presets/design/';
var f=document.getElementById('f');
function btn(w,label){var b=[].slice.call(w.document.querySelectorAll('button')).filter(function(x){return x.textContent.trim()===label})[0];if(b)b.click();}
function leaf(w,label){var t=[].slice.call(w.document.querySelectorAll('button')).filter(function(x){return x.textContent.trim().toUpperCase()===label&&x.offsetParent})[0];if(t)t.click();}
function go(w){
  try{w.settings.designPreset=design;w.DesignPresets.apply(design);}catch(e){}
  if(lang&&w.switchLocale)w.switchLocale(lang);
  btn(w,'Load Test Data');
  setTimeout(function(){btn(w,'Build Tree');setTimeout(function(){if(page)leaf(w,page);},2500);},1200);
}
f.onload=function(){setTimeout(function(){
  var w=f.contentWindow;
  if(JSONS[design]){fetch(base+JSONS[design]+'.json').then(function(r){return r.json()}).then(function(d){
    w.DesignPresets.onLoaded([{key:JSONS[design],data:d}]);go(w);});}
  else go(w);
},2000)};
</script>
```

Shoot (Bash), designs `classic modern arcane night-grimoire chalkboard` × pages `'' SCAN SETTINGS`:
```bash
C="/c/Program Files/Google/Chrome/Application/chrome.exe"; O=E:/Users/qtuna/hom-shots
for d in classic modern arcane night-grimoire chalkboard; do for p in TREE SCAN SETTINGS; do
 h="$d~$p"; [ $p = TREE ] && h="$d~"
 timeout 60 "$C" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files --window-size=1600,1000 --virtual-time-budget=20000 --screenshot="$O/$1-$d-$p.png" "file:///$O/wrap.html#$h" >/dev/null 2>&1
done; done
```
(save as `E:\Users\qtuna\hom-shots\shoot.sh`, run `bash shoot.sh before` on master's state of this branch before editing CSS and `bash shoot.sh after` afterwards). If the panel's language switch function has another name, find it with `grep -n "function switchLocale\|switchLocale =" PANEL/modules/i18n.js` and adjust.

- [ ] **Step 2: Baseline** — `bash shoot.sh before`; open the 15 PNGs; confirm Night Grimoire and Chalkboard really differ from Arcane (if identical, the JSON seeding failed — fix before going on).

- [ ] **Step 3: Classify uppercase rules**

Run: `grep -n "text-transform: *uppercase" PANEL/styles-skyrim.css PANEL/patch-ui.css` and map each hit to its selector.
- KEEP: `.panel-title`, `.header-btn-wide`, `.howto-tab-text`, `.scanner-mode-btn`, `.school-label`, `.center-hub-text`, `.school-badge`, `.state-badge`, `.btn-unlock`, `.btn-learn`, `.learning-status-badge`, `.detail-label`, `.prereq-type-label`, `.tooltip-state`, `.renderer-badge`, `.find-spell-state`, and the patch-ui school-tab rule.
- FLIP: every other hit.
- A hit whose selector is not obviously chrome or tree: stop and report it instead of guessing.

- [ ] **Step 4: Chrome rules** — append to `PANEL/panel-chrome.css` (fill the FLIP selector list from Step 3; the list below is the survey's and must match):

```css
/* Words, not shouting: only the app name, the top menu and the tabs stay in capitals */
.btn, .btn-small, .popup-section-title, .empty-text, .howto-header h3, .howto-section h4,
.footer-goals-label, .state-legend, .modal-header h3, .blacklist-list-header,
.whitelist-section-header, .lock-label, .settings-subsection, .subsection-title,
.tier-xp-item label, .hotkey-input, .cheat-warning, .scanner-presets-label,
.scan-stats-header, .scan-stat-label, .tree-preview-title, .tree-preview-settings-title,
.easy-presets-label {
    text-transform: none;
    letter-spacing: 0.02em;
}
.settings-block-title {
    text-transform: none;
    font-variant: small-caps;
    letter-spacing: 0.06em;
}

/* Buttons: a line, not a box. Primary = accent text over an underline; danger = red text */
.btn {
    background: transparent;
    background-image: none;
    border: 0;
    border-bottom: 1px solid transparent;
    border-radius: 0;
    box-shadow: none;
}
.btn-primary, .btn-success {
    color: var(--accent-gold, #c9a227);
    border-bottom-color: var(--accent-gold, #c9a227);
}
.btn-danger { color: var(--accent-red, #b5523b); }
.btn:hover:not(:disabled) { border-bottom-color: currentColor; }

/* Switches: a square check like SkyUI's MCM */
.toggle-switch { width: 18px; height: 18px; }
.toggle-switch .toggle-slider {
    border-radius: 0;
    background: transparent;
    border: 1px solid var(--accent-gold, #c9a227);
    box-shadow: none;
}
.toggle-switch .toggle-slider:before { display: none; }
.toggle-switch input:checked + .toggle-slider {
    background: var(--accent-gold, #c9a227);
    box-shadow: inset 0 0 0 3px var(--bg-primary, #14100a);
}

/* Settings: groups divided by a rule, not boxed cards */
#contentSettings .settings-block {
    background: transparent;
    border: 0;
    border-top: 1px solid var(--divider, rgba(200, 190, 170, 0.25));
    border-radius: 0;
    box-shadow: none;
}
#contentSettings .settings-block-header { background: transparent; border: 0; }

/* "Advanced options" is a note, not a red pill */
.cheat-warning { background: transparent; border: 0; padding: 0; }
```

Check the variable names exist: `grep -n "\-\-accent-gold\|\-\-accent-red\|\-\-bg-primary\|\-\-divider" PANEL/styles-skyrim.css | head`. Use whichever names the theme defines (keep the fallbacks).

- [ ] **Step 5: Modern Dark**

In `design-modern.css`: the `.btn-primary` rule with `linear-gradient(135deg, …)` → `background: transparent; color: var(--accent-gold); border-color: transparent;` and delete its hover glow `box-shadow`; the toggle rules that make a pill with a round knob (`border-radius` 13px/50%, green checked colour) → delete so `panel-chrome.css` applies; the `.settings-block` rules with background/border-radius → delete the background, radius and border lines.

- [ ] **Step 6: After shots** — `bash shoot.sh after`; open the 15 PNGs next to the `before` ones. Check per design: no pill switch, no gradient, no boxed settings cards, caps only on app name/menu/tabs, tree and spell card unchanged. If Arcane or Night Grimoire/Chalkboard (`design-darkbook.css`) bring a pill or box back, delete that rule's offending lines in that file. Shoot `arcane~SETTINGS~ko` and check Korean labels on one line with their icons.

- [ ] **Step 7: Tests and commit** — `node run-tests.js` passes;
```bash
git commit -am "Panel chrome: capitals only for name, menu and tabs; line buttons, square checks, ruled settings groups"
```

---

### Task 7: Ink set for Arcane (Gemini)

**Files:**
- Create: `PANEL/themes/arcane/icons/<id>.png` (19)
- Modify: `PANEL/modules/designPresets.js` (Arcane `icons`), `PANEL/modules/iconSetsTest.js` (`SETS` + `'themes/arcane/icons/'`)

- [ ] **Step 1: Test first** — add `'themes/arcane/icons/'` to `IconSetsTest.SETS`; `node run-tests.js` → that line fails.

- [ ] **Step 2: Reference images** — render each `docs/icons/svg/<id>.svg` black on white at 512px (Task 3 sheet with `#000000`, one icon per page or slice the sheet at 192px and upscale is NOT good enough — render one at a time with `--window-size=512,512` and a page that draws only `#<id>`).

- [ ] **Step 3: Gemini** — load the `antigravity-offload` skill and follow it. Brief for the first icon (`scan`), with its reference PNG attached:

> Redraw this icon as a small pen-and-ink illustration from an old spellbook. Same silhouette and composition as the reference. Dark brown-black ink (#2a1c10) on a plain flat white background (#ffffff), no paper texture, no shadow, no glow, no colour, no text or letters. Hand-inked line with slight pressure variation and a little cross-hatching for shade, like a woodcut-era grimoire marginal drawing. Square 1:1, the drawing centred, filling about 80% of the frame. One icon only.

Judge it (Read): silhouette matches the reference; line weight readable at 24px (shrink a copy to 24px and look); no text, glow, colour, texture. Reroll with the specific fault named until it passes. Then for the other 18, attach the reference AND the approved `scan` drawing: "Same ink style, line weight and hatching density as the second image."

- [ ] **Step 4: Ink to alpha** — `E:\Users\qtuna\hom-icons\ink.ps1`: for each `<id>.png` from Gemini, shrink to 48px (HighQualityBicubic, as `slice.ps1`), then per pixel `alpha = 255 - luminance`, colour = fixed ink `#2a1c10`; save to `PANEL/themes/arcane/icons/<id>.png`.

```powershell
param([string]$InDir, [string]$OutDir, [string]$Hex = '2a1c10', [switch]$LightOnDark)
Add-Type -AssemblyName System.Drawing
$r = [Convert]::ToInt32($Hex.Substring(0,2),16); $g = [Convert]::ToInt32($Hex.Substring(2,2),16); $b = [Convert]::ToInt32($Hex.Substring(4,2),16)
New-Item -ItemType Directory -Force $OutDir | Out-Null
Get-ChildItem $InDir -Filter *.png | ForEach-Object {
    $src = [System.Drawing.Bitmap]::FromFile($_.FullName)
    $s = New-Object System.Drawing.Bitmap 48, 48
    $gr = [System.Drawing.Graphics]::FromImage($s); $gr.InterpolationMode = 'HighQualityBicubic'; $gr.PixelOffsetMode = 'HighQuality'
    $gr.DrawImage($src, 0, 0, 48, 48); $gr.Dispose(); $src.Dispose()
    $out = New-Object System.Drawing.Bitmap 48, 48, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt 48; $y++) { for ($x = 0; $x -lt 48; $x++) {
        $p = $s.GetPixel($x, $y); $lum = [int](0.299*$p.R + 0.587*$p.G + 0.114*$p.B)
        $a = if ($LightOnDark) { $lum } else { 255 - $lum }
        $out.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, $r, $g, $b)) } }
    $out.Save((Join-Path $OutDir $_.Name), [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose(); $s.Dispose()
}
```
Run: `& E:\Users\qtuna\hom-icons\ink.ps1 -InDir E:\Users\qtuna\hom-icons\ink-raw -OutDir <worktree>\PrismaUI\views\SpellLearning\SpellLearningPanel\themes\arcane\icons` (Gemini files must be named `<id>.png` in `ink-raw`).

- [ ] **Step 5: Switch Arcane** — `designPresets.js` Arcane `icons: 'themes/arcane/icons/'`. `node run-tests.js` passes. Shoot `arcane~SCAN`, `arcane~SETTINGS`, `arcane~` and look: ink icons readable on the dark chrome? The chrome is dark leather — if ink-brown vanishes there, put the Arcane chrome icons on gold instead (rerun `ink.ps1 -Hex d8b86c`) and note it in the commit; parchment areas keep ink.

- [ ] **Step 6: Commit** — `git add` the folder, test and designPresets; `git commit -m "Arcane: ink-drawn icon set"`

---

### Task 8: Gold set for Night Grimoire

**Files:** Create `PANEL/themes/nightgrimoire/icons/<id>.png` (19); Modify `NightGrimoire.json` (`"icons"`), `iconSetsTest.js` (`SETS`).

- [ ] **Step 1:** Add `'themes/nightgrimoire/icons/'` to `SETS`; tests fail.
- [ ] **Step 2:** `& E:\Users\qtuna\hom-icons\ink.ps1 -InDir E:\Users\qtuna\hom-icons\ink-raw -OutDir <worktree>\…\themes\nightgrimoire\icons -Hex d8b86c`
- [ ] **Step 3:** `NightGrimoire.json` `"icons": "themes/nightgrimoire/icons/"`. Tests pass. Shoot `night-grimoire~`, `~SCAN`, `~SETTINGS`; gold icons visible on the night cover.
- [ ] **Step 4: Commit** — `git commit -m "Night Grimoire: the ink icons in gold"`

---

### Task 9: Chalk set for Chalkboard (Gemini)

**Files:** Create `PANEL/themes/chalkboard/icons/<id>.png` (19); Modify `Chalkboard.json`, `iconSetsTest.js`.

- [ ] **Step 1:** Add `'themes/chalkboard/icons/'` to `SETS`; tests fail.
- [ ] **Step 2: Gemini** (same skill, references from Task 7 Step 2): 

> Redraw this icon as a quick chalk drawing on a school blackboard. Same silhouette as the reference. White chalk (#f2efe6) on a plain flat black background (#000000), no board texture, no smudge cloud, no colour, no text. Chalk line with dry broken edges and slight grain, drawn in a few confident strokes. Square 1:1, centred, about 80% of the frame. One icon only.

Same judging and "match the second image" rule as Task 7, first approved icon as the style anchor.
- [ ] **Step 3:** `& …\ink.ps1 -InDir E:\Users\qtuna\hom-icons\chalk-raw -OutDir <worktree>\…\themes\chalkboard\icons -Hex f2efe6 -LightOnDark`
- [ ] **Step 4:** `Chalkboard.json` `"icons": "themes/chalkboard/icons/"`. Tests pass. Shoot `chalkboard~`, `~SCAN`, `~SETTINGS`.
- [ ] **Step 5: Commit** — `git commit -m "Chalkboard: chalk-drawn icon set"`

---

### Task 10: Credits and the before/after report

**Files:** Modify `README.md` (## Credits), `THIRD-PARTY-NOTICES.md.in` (new section after the fonts section), `docs/NEXUS_PAGE.bbcode` (Credits block).

- [ ] **Step 1:** From `docs/icons/game-icons-picks.json`, list the distinct authors. Add to all three files: "Panel icons (19): shapes from game-icons.net by <authors>, licensed CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/), redrawn as ink and chalk drawings for Heart of Magic." In `THIRD-PARTY-NOTICES.md.in` also list each of the 19 `id → author/name`.
- [ ] **Step 2:** `bash shoot.sh final`; build one comparison page (before vs final, 5 designs × 3 screens) and send it to the user.
- [ ] **Step 3:** `node run-tests.js` passes; `node tools/make-dev-harness.js --check` exits 0.
- [ ] **Step 4: Commit** — `git commit -am "Credits: game-icons.net authors for the panel icons (CC BY 3.0)"`
- [ ] **Step 5:** Ask the user for one in-game check only if a desktop screenshot cannot show it (Ultralight PNG backgrounds): open the panel once in Arcane and look at the Scan button.
