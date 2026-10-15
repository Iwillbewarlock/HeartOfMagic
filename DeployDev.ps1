<#
.SYNOPSIS
    Copies the built plugin and the UI panel into the development mod folder.

.DESCRIPTION
    Two things this does that a plain copy does not:

      - It refuses to run while SkyrimSE is running. PrismaUI holds the panel's
        files open, so a copy would half-succeed and leave the mod in pieces.
      - It leaves lang/locale.js alone. That one line is the player's choice of
        language, the mod ships it set to 'en', and overwriting it on every
        deploy would put the dev install back into English every time.

    Scratch folders (_tmp) are never copied.

.EXAMPLE
    .\DeployDev.ps1
    .\DeployDev.ps1 -PanelOnly
    .\DeployDev.ps1 -WithISLPatch # also the ISL patch's scripts (otherwise only if the install has them)
    .\DeployDev.ps1 -WithBookXP   # also the BookXP script (otherwise only if the install has BookXP)
    .\DeployDev.ps1 -NoScripts    # plugin and panel without the Papyrus scripts (left as they are)
    .\DeployDev.ps1 -NpcSpellsOnly   # the NPC Spells addon only: DLL, ESL, rules JSON (an existing rules JSON is kept)
    .\DeployDev.ps1 -NpcSpellsOnly -Force   # the same, the install's rules JSON replaced with the shipped one
    .\DeployDev.ps1 -NpcSpellsOnly -NoSelfTest   # the same without the dev self test (its file removed from the install)

    Everything it will copy is checked before anything is copied (the plugin, the
    panel, each compiled script there and newer than its source), so a missing or
    stale build stops the deploy with the install untouched instead of half done.
#>
[CmdletBinding()]
param(
    [string]$ModPath = 'D:\TAKEALOOK\mods\HeartOfMagic-Librarian-Dev',
    [switch]$PanelOnly,
    [switch]$WithISLPatch,
    [switch]$WithBookXP,
    [switch]$NoScripts,
    [string]$NpcSpellsModPath = 'D:\TAKEALOOK\mods\HeartOfMagic-NPCSpells-Dev',
    [switch]$NpcSpellsOnly,
    [switch]$NoSelfTest,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) {
    throw 'SkyrimSE is running. Close the game first: the panel files are open and a copy would leave the mod half written.'
}
$repo = $PSScriptRoot

if ($NpcSpellsOnly) {
    # The same order as the main path: every check before the first copy, so a missing or stale build
    # leaves the install as it was. The mod folder is not created here (a typo would make a stray folder).
    if (-not (Test-Path $NpcSpellsModPath)) {
        throw "Mod folder not found: $NpcSpellsModPath - create the mod in MO2 first, or pass -NpcSpellsModPath."
    }
    $nsDll = Join-Path $repo 'build\plugins\NPCSpells\Release\HeartOfMagic_NPCSpells.dll'
    $nsEsl = Join-Path $repo 'build\npcspells\HeartOfMagic_NPCSpells.esp'
    $nsJson = Join-Path $repo 'plugins\NPCSpells\data\HeartOfMagic_NPCSpells.json'
    # Dev only, never in the release zip: in the install it makes the DLL run its in-game self test after a load
    # (plugins\NPCSpells\src\game\GameSelfTest.h)
    $nsSelfTest = Join-Path $repo 'plugins\NPCSpells\data\dev\HeartOfMagic_NPCSpells_selftest.json'
    foreach ($f in @($nsDll, $nsEsl, $nsJson, $nsSelfTest)) {
        if (-not (Test-Path $f)) { throw "Missing $f - nothing was copied. Run .\BuildRelease.ps1 first." }
    }
    # Stale: an output older than a source it is built from. The DLL is built from the addon's src and the
    # shared files its CMakeLists.txt names; the ESL is written by npcspells-esl from the slot table.
    $nsSrc = Join-Path $repo 'plugins\NPCSpells\src'
    $dllSources = @(Get-ChildItem $nsSrc -Recurse -File -Include '*.cpp', '*.h') + @(
        'plugins\spelllearning\src\EncodingUtils.cpp',
        'plugins\spelllearning\src\tomereach\TomeReachConfig.cpp', 'plugins\spelllearning\src\tomereach\TomeReachConfig.h',
        'plugins\spelllearning\src\tomereach\TomeReachRecords.cpp' | ForEach-Object { Get-Item (Join-Path $repo $_) })
    $eslSources = @('tools\npcspells-esl.cpp', 'plugins\NPCSpells\src\core\NpcSpellsSlots.cpp',
        'plugins\NPCSpells\src\core\NpcSpellsSlots.h', 'plugins\NPCSpells\src\core\NpcSpellsEsl.cpp',
        'plugins\NPCSpells\src\core\NpcSpellsEsl.h', 'plugins\NPCSpells\src\core\NpcSpellsTypes.h') |
        ForEach-Object { Get-Item (Join-Path $repo $_) }
    foreach ($check in @(@($nsDll, $dllSources), @($nsEsl, $eslSources))) {
        $built = (Get-Item $check[0]).LastWriteTime
        $newest = $check[1] | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest.LastWriteTime -gt $built) {
            throw "Stale: $(Split-Path $check[0] -Leaf) is older than $($newest.FullName) - nothing was copied. Run .\BuildRelease.ps1 first."
        }
    }

    # The rules JSON is the player's to edit: an existing one is kept unless -Force
    $pluginsDst = Join-Path $NpcSpellsModPath 'SKSE\Plugins'
    $jsonDst = Join-Path $pluginsDst 'HeartOfMagic_NPCSpells.json'
    $copies = @(@($nsDll, (Join-Path $pluginsDst 'HeartOfMagic_NPCSpells.dll')), @($nsEsl, (Join-Path $NpcSpellsModPath 'HeartOfMagic_NPCSpells.esp')))
    $jsonKept = (Test-Path $jsonDst) -and -not $Force
    if (-not $jsonKept) { $copies += , @($nsJson, $jsonDst) }
    $selfTestDst = Join-Path $pluginsDst 'HeartOfMagic_NPCSpells_selftest.json'
    if (-not $NoSelfTest) { $copies += , @($nsSelfTest, $selfTestDst) }

    New-Item -ItemType Directory -Force -Path $pluginsDst | Out-Null
    foreach ($pair in $copies) { Copy-Item $pair[0] $pair[1] -Force }
    if ($NoSelfTest -and (Test-Path $selfTestDst)) { Remove-Item $selfTestDst -Force }
    foreach ($pair in $copies) {
        if ((Get-FileHash $pair[0]).Hash -ne (Get-FileHash $pair[1]).Hash) { throw "Copy did not take: $($pair[1])" }
    }
    Write-Host "npcspells  dll, esl -> $NpcSpellsModPath (enable HeartOfMagic_NPCSpells.esp in MO2)"
    if ($jsonKept) {
        Write-Host "npcspells  rules json kept as it is in the install (pass -Force to replace it with the shipped one)"
    } else {
        Write-Host "npcspells  rules json -> $jsonDst"
    }
    if ($NoSelfTest) {
        Write-Host "npcspells  self test off: no HeartOfMagic_NPCSpells_selftest.json in the install"
    } else {
        Write-Host "npcspells  self test ON -> $selfTestDst (runs once per session after a load, spawns its sample NPCs next to the player; SELFTEST lines in HeartOfMagic_NPCSpells.log; -NoSelfTest removes it)"
    }
    return
}

if (-not (Test-Path $ModPath)) {
    throw "Mod folder not found: $ModPath"
}

$panelSrc = Join-Path $repo 'PrismaUI\views\SpellLearning\SpellLearningPanel'
$panelDst = Join-Path $ModPath 'PrismaUI\views\SpellLearning\SpellLearningPanel'
$dllSrc = Join-Path $repo 'build\plugins\spelllearning\Release\SpellLearning.dll'
$dllDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning.dll'

# =============================================================================
# Preflight: every source this run copies, checked before the first copy. A
# throw here leaves the install as it was (a check after the plugin was copied
# left a new DLL with old scripts).
# =============================================================================
if (-not (Test-Path (Join-Path $panelSrc 'index.html'))) { throw "No panel at $panelSrc" }
if (-not $PanelOnly) {
    if (-not (Test-Path $dllSrc)) { throw "No built plugin at $dllSrc. Run .\BuildRelease.ps1 -preset Release-2026 first." }
}

# The compiled Papyrus scripts (BuildRelease.ps1 puts them in build\Scripts\output),
# laid out as the release has them (plugins/cmake/Papyrus.cmake, Distribution.cmake):
# the core ones in Scripts\ always; the BookXP script (in the release's Scripts\ too,
# but only of use with SL_BookXP.dll) and the ISL patch's three (optional\ISLPatch, a
# FOMOD option) only when the install already has that part or the switch asks for it.
# Each .psc goes to Scripts\Source next to its .pex, as in the release.
$deployScripts = -not $PanelOnly -and -not $NoScripts
$pexSrc = Join-Path $repo 'build\Scripts\output'
$pscSrc = Join-Path $repo 'Scripts\Source'
$scriptsDst = Join-Path $ModPath 'Scripts'
$scripts = @()
if ($deployScripts) {
    if (-not (Test-Path $pexSrc)) {
        throw ("No compiled scripts at $pexSrc - nothing was copied. Run .\BuildRelease.ps1 -preset Release-2026 " +
               "first (with the Papyrus compiler), or pass -NoScripts to deploy without the scripts.")
    }
    # Mirrors PAPYRUS_SPELLLEARNING_SOURCES / PAPYRUS_ISL_SOURCES in plugins/cmake/Papyrus.cmake
    $scripts = @('DEST_FormExt', 'SpellLearning', 'SpellLearning_DEST', 'SpellLearning_DEST_Handler',
                 'SpellLearning_ISL', 'SpellLearning_ISL_Handler')
    $hasBookXP = (Test-Path (Join-Path $ModPath 'SKSE\Plugins\SL_BookXP.dll')) -or
                 (Test-Path (Join-Path $scriptsDst 'SL_BookXP_QuestScript.pex'))
    if ($WithBookXP -or $hasBookXP) { $scripts += 'SL_BookXP_QuestScript' }
    if ($WithISLPatch -or (Test-Path (Join-Path $scriptsDst 'DEST_ISL_PlayerSpellLearningScript.pex'))) {
        $scripts += @('DEST_AliasExt', 'DEST_ISL_PlayerSpellLearningScript', 'DEST_UIExt')
    }
    foreach ($name in $scripts) {
        $pex = Join-Path $pexSrc "$name.pex"
        $psc = Join-Path $pscSrc "$name.psc"
        if (-not (Test-Path $psc)) { throw "No source $psc - nothing was copied." }
        if (-not (Test-Path $pex)) { throw "Not compiled: $pex - nothing was copied. Run .\BuildRelease.ps1 first, or pass -NoScripts." }
        if ((Get-Item $pex).LastWriteTime -lt (Get-Item $psc).LastWriteTime) {
            throw "Stale: $name.pex is older than $name.psc - nothing was copied. Run .\BuildRelease.ps1 first, or pass -NoScripts."
        }
    }
}

if (-not $PanelOnly) {
    Copy-Item $dllSrc $dllDst -Force
    Write-Host ("plugin  {0}" -f (Split-Path $dllDst -Leaf))
}

# The design presets shipped with the mod (Night Grimoire, Chalkboard): data
# files the plugin lists at runtime, so the dev install needs them to show them
$designSrc = Join-Path $repo 'SKSE\Plugins\SpellLearning\presets\design'
$designDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning\presets\design'
if (Test-Path $designSrc) {
    New-Item -ItemType Directory -Force -Path $designDst | Out-Null
    Copy-Item (Join-Path $designSrc '*.json') $designDst -Force
    # Presets the mod shipped once and no longer does leave the install too. Only
    # those, by name: any other file there (an add-on design being tried out) stays.
    $retired = @('CandlelitTome.json')
    foreach ($name in $retired) {
        $old = Join-Path $designDst $name
        if ((Test-Path $old) -and -not (Test-Path (Join-Path $designSrc $name))) { Remove-Item $old -Force }
    }
    Write-Host ("designs {0}" -f ((Get-ChildItem $designSrc -Filter '*.json').Count))
}

# The librarian's rule files (the hand-made 80_manual.json among them): the
# plugin reads them at every scan, so a changed rule needs them in the install
$rulesSrc = Join-Path $repo 'SKSE\Plugins\SpellLearning\librarian'
$rulesDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning\librarian'
if (Test-Path $rulesSrc) {
    New-Item -ItemType Directory -Force -Path $rulesDst | Out-Null
    Copy-Item (Join-Path $rulesSrc '*.json') $rulesDst -Force
    Write-Host ("rules   {0}" -f ((Get-ChildItem $rulesSrc -Filter '*.json').Count))

    # The perk adapter files: read at load and after every full scan
    $adaptersSrc = Join-Path $rulesSrc 'adapters'
    if (Test-Path $adaptersSrc) {
        $adaptersDst = Join-Path $rulesDst 'adapters'
        New-Item -ItemType Directory -Force -Path $adaptersDst | Out-Null
        # An adapter file the repo no longer has would keep running in the install
        $shipped = (Get-ChildItem $adaptersSrc -Filter '*.json').Name
        Get-ChildItem $adaptersDst -Filter '*.json' | Where-Object { $shipped -notcontains $_.Name } | Remove-Item -Force
        Copy-Item (Join-Path $adaptersSrc '*.json') $adaptersDst -Force
        Write-Host ("adapters {0}" -f ((Get-ChildItem $adaptersSrc -Filter '*.json').Count))
    }
    # Other mods' archetype mappings (Spell Research): read when their import finishes and at save load
    $compatSrc = Join-Path $rulesSrc 'compat'
    if (Test-Path $compatSrc) {
        $compatDst = Join-Path $rulesDst 'compat'
        New-Item -ItemType Directory -Force -Path $compatDst | Out-Null
        Copy-Item (Join-Path $compatSrc '*.json') $compatDst -Force
        Write-Host ("compat  {0}" -f ((Get-ChildItem $compatSrc -Filter '*.json').Count))
    }
}

# The spell gate's rules (SpellScannerGate.cpp)
$gateSrc = Join-Path $repo 'SKSE\Plugins\SpellLearning\gate'
if (-not $PanelOnly -and (Test-Path $gateSrc)) {
    $gateDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning\gate'
    New-Item -ItemType Directory -Force -Path $gateDst | Out-Null
    Copy-Item (Join-Path $gateSrc '*.json') $gateDst -Force
    Write-Host ("gate    {0}" -f ((Get-ChildItem $gateSrc -Filter '*.json').Count))
}

# The dev install is a debug install: its panel scans also write the analysis
# dump (debug/debug.json, shipped the same way in the debug symbols archive)
$debugSrc = Join-Path $repo 'debug\debug.json'
if (Test-Path $debugSrc) {
    $debugDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning'
    New-Item -ItemType Directory -Force -Path $debugDst | Out-Null
    Copy-Item $debugSrc $debugDst -Force
    Write-Host "debug   debug.json (panel scans write spell_scan_output.json)"
}

# The scripts checked in the preflight above
if ($deployScripts) {
    New-Item -ItemType Directory -Force -Path (Join-Path $scriptsDst 'Source') | Out-Null
    foreach ($name in $scripts) {
        $pex = Join-Path $pexSrc "$name.pex"
        Copy-Item $pex (Join-Path $scriptsDst "$name.pex") -Force
        Copy-Item (Join-Path $pscSrc "$name.psc") (Join-Path $scriptsDst "Source\$name.psc") -Force
    }
    # Verified like the panel below: a copy that did not take stops the deploy
    foreach ($name in $scripts) {
        $a = (Get-FileHash (Join-Path $pexSrc "$name.pex")).Hash
        $b = (Get-FileHash (Join-Path $scriptsDst "$name.pex")).Hash
        if ($a -ne $b) { throw "Copy did not take: Scripts\$name.pex" }
    }
    Write-Host ("scripts {0} .pex ({1})" -f $scripts.Count, ($scripts -join ', '))
} elseif ($NoScripts -and -not $PanelOnly) {
    Write-Host 'scripts skipped (-NoScripts)'
}

# Keep the language line the dev install is set to
$localeDst = Join-Path $panelDst 'lang\locale.js'
$keptLocale = if (Test-Path $localeDst) { Get-Content $localeDst -Raw -Encoding UTF8 } else { $null }

Get-ChildItem $panelSrc -Exclude '_tmp' | Copy-Item -Destination $panelDst -Recurse -Force

if ($keptLocale) {
    Set-Content $localeDst $keptLocale -Encoding utf8 -NoNewline
    $line = ($keptLocale -split "`n" | Where-Object { $_ -match '_i18nLocale' } | Select-Object -First 1).Trim()
    Write-Host ("panel   copied, kept {0}" -f $line)
} else {
    Write-Host 'panel   copied'
}

$checked = @('modules\bridgeView.js', 'modules\spellCard.js', 'patch-ui.css', 'patch-tree-tools.css', 'index.html')
foreach ($file in $checked) {
    $a = (Get-FileHash (Join-Path $panelSrc $file)).Hash
    $b = (Get-FileHash (Join-Path $panelDst $file)).Hash
    if ($a -ne $b) { throw "Copy did not take: $file" }
}
Write-Host 'verified'
