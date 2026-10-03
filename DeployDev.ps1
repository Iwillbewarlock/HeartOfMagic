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
    .\DeployDev.ps1 -NoCombat     # without the SL_Combat addon
    .\DeployDev.ps1 -CombatOnly   # only the SL_Combat addon, nothing else touched
    .\DeployDev.ps1 -WithISLPatch # also the ISL patch's scripts (otherwise only if the install has them)
    .\DeployDev.ps1 -WithBookXP   # also the BookXP script (otherwise only if the install has BookXP)
#>
[CmdletBinding()]
param(
    [string]$ModPath = 'D:\TAKEALOOK\mods\HeartOfMagic-Librarian-Dev',
    [switch]$PanelOnly,
    [switch]$NoCombat,
    [switch]$CombatOnly,
    [switch]$WithISLPatch,
    [switch]$WithBookXP
)

$ErrorActionPreference = 'Stop'

if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) {
    throw 'SkyrimSE is running. Close the game first: the panel files are open and a copy would leave the mod half written.'
}
if (-not (Test-Path $ModPath)) {
    throw "Mod folder not found: $ModPath"
}

$repo = $PSScriptRoot

# -CombatOnly: just the combat addon (DLL, and its settings file the first time).
# The panel, the main plugin and the data files stay as they are in the install,
# so work in progress elsewhere in the tree does not ride along.
if ($CombatOnly) {
    $combatSrc = Join-Path $repo 'build\plugins\Combat\Release\SL_Combat.dll'
    if (-not (Test-Path $combatSrc)) { throw "No built addon at $combatSrc. Run .\BuildRelease.ps1 -preset Release-2026 first." }
    $pluginsDst = Join-Path $ModPath 'SKSE\Plugins'
    New-Item -ItemType Directory -Force -Path $pluginsDst | Out-Null
    Copy-Item $combatSrc (Join-Path $pluginsDst 'SL_Combat.dll') -Force
    $combatCfgDst = Join-Path $pluginsDst 'SL_Combat.json'
    if (-not (Test-Path $combatCfgDst)) { Copy-Item (Join-Path $repo 'SKSE\Plugins\SL_Combat.json') $combatCfgDst }
    # The icon view an earlier build installed (replaced by SkyPrompt)
    $combatViewDst = Join-Path $ModPath 'PrismaUI\views\SL_Combat'
    if (Test-Path $combatViewDst) { Remove-Item $combatViewDst -Recurse -Force }
    Write-Host 'combat  SL_Combat.dll only (-CombatOnly; settings: SKSE\Plugins\SL_Combat.json)'
    return
}

$panelSrc = Join-Path $repo 'PrismaUI\views\SpellLearning\SpellLearningPanel'
$panelDst = Join-Path $ModPath 'PrismaUI\views\SpellLearning\SpellLearningPanel'
$dllSrc = Join-Path $repo 'build\plugins\spelllearning\Release\SpellLearning.dll'
$dllDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning.dll'

if (-not $PanelOnly) {
    if (-not (Test-Path $dllSrc)) { throw "No built plugin at $dllSrc. Run .\BuildRelease.ps1 -preset Release-2026 first." }
    Copy-Item $dllSrc $dllDst -Force
    Write-Host ("plugin  {0}" -f (Split-Path $dllDst -Leaf))

    # The combat addon (M1 spell counter, docs/COMBAT.md): not in the release yet,
    # so only the dev install gets it. -NoCombat takes it out again. Its settings
    # file is copied once and then left alone, so a tester's tuning survives.
    $combatSrc = Join-Path $repo 'build\plugins\Combat\Release\SL_Combat.dll'
    $combatDst = Join-Path $ModPath 'SKSE\Plugins\SL_Combat.dll'
    $combatCfgSrc = Join-Path $repo 'SKSE\Plugins\SL_Combat.json'
    $combatCfgDst = Join-Path $ModPath 'SKSE\Plugins\SL_Combat.json'
    if ($NoCombat) {
        if (Test-Path $combatDst) { Remove-Item $combatDst -Force }
        # The icon view an earlier build installed (replaced by SkyPrompt)
        $combatViewDst = Join-Path $ModPath 'PrismaUI\views\SL_Combat'
        if (Test-Path $combatViewDst) { Remove-Item $combatViewDst -Recurse -Force }
        Write-Host 'combat  removed (-NoCombat)'
    } elseif (Test-Path $combatSrc) {
        Copy-Item $combatSrc $combatDst -Force
        if (-not (Test-Path $combatCfgDst)) { Copy-Item $combatCfgSrc $combatCfgDst }
        Write-Host 'combat  SL_Combat.dll (settings: SKSE\Plugins\SL_Combat.json)'
    }
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

# The dev install is a debug install: its panel scans also write the analysis
# dump (debug/debug.json, shipped the same way in the debug symbols archive)
$debugSrc = Join-Path $repo 'debug\debug.json'
if (Test-Path $debugSrc) {
    $debugDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning'
    New-Item -ItemType Directory -Force -Path $debugDst | Out-Null
    Copy-Item $debugSrc $debugDst -Force
    Write-Host "debug   debug.json (panel scans write spell_scan_output.json)"
}

# The compiled Papyrus scripts (BuildRelease.ps1 puts them in build\Scripts\output),
# laid out as the release has them (plugins/cmake/Papyrus.cmake, Distribution.cmake):
# the core ones in Scripts\ always; the BookXP script (in the release's Scripts\ too,
# but only of use with SL_BookXP.dll) and the ISL patch's three (optional\ISLPatch, a
# FOMOD option) only when the install already has that part or the switch asks for it.
# Each .psc goes to Scripts\Source next to its .pex, as in the release.
if (-not $PanelOnly) {
    $pexSrc = Join-Path $repo 'build\Scripts\output'
    $pscSrc = Join-Path $repo 'Scripts\Source'
    $scriptsDst = Join-Path $ModPath 'Scripts'
    if (-not (Test-Path $pexSrc)) { throw "No compiled scripts at $pexSrc. Run .\BuildRelease.ps1 -preset Release-2026 first (with the Papyrus compiler)." }
    # Mirrors PAPYRUS_SPELLLEARNING_SOURCES / PAPYRUS_ISL_SOURCES in plugins/cmake/Papyrus.cmake
    $scripts = @('DEST_FormExt', 'SpellLearning', 'SpellLearning_DEST', 'SpellLearning_DEST_Handler',
                 'SpellLearning_ISL', 'SpellLearning_ISL_Handler')
    $hasBookXP = (Test-Path (Join-Path $ModPath 'SKSE\Plugins\SL_BookXP.dll')) -or
                 (Test-Path (Join-Path $scriptsDst 'SL_BookXP_QuestScript.pex'))
    if ($WithBookXP -or $hasBookXP) { $scripts += 'SL_BookXP_QuestScript' }
    if ($WithISLPatch -or (Test-Path (Join-Path $scriptsDst 'DEST_ISL_PlayerSpellLearningScript.pex'))) {
        $scripts += @('DEST_AliasExt', 'DEST_ISL_PlayerSpellLearningScript', 'DEST_UIExt')
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $scriptsDst 'Source') | Out-Null
    foreach ($name in $scripts) {
        $pex = Join-Path $pexSrc "$name.pex"
        if (-not (Test-Path $pex)) { throw "Not compiled: $pex" }
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
