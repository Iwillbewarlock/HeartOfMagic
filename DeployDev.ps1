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
#>
[CmdletBinding()]
param(
    [string]$ModPath = 'D:\TAKEALOOK\mods\HeartOfMagic-Librarian-Dev',
    [switch]$PanelOnly
)

$ErrorActionPreference = 'Stop'

if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) {
    throw 'SkyrimSE is running. Close the game first: the panel files are open and a copy would leave the mod half written.'
}
if (-not (Test-Path $ModPath)) {
    throw "Mod folder not found: $ModPath"
}

$repo = $PSScriptRoot
$panelSrc = Join-Path $repo 'PrismaUI\views\SpellLearning\SpellLearningPanel'
$panelDst = Join-Path $ModPath 'PrismaUI\views\SpellLearning\SpellLearningPanel'
$dllSrc = Join-Path $repo 'build\plugins\spelllearning\Release\SpellLearning.dll'
$dllDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning.dll'

if (-not $PanelOnly) {
    if (-not (Test-Path $dllSrc)) { throw "No built plugin at $dllSrc. Run .\BuildRelease.ps1 -preset Release-2026 first." }
    Copy-Item $dllSrc $dllDst -Force
    Write-Host ("plugin  {0}" -f (Split-Path $dllDst -Leaf))
}

# The design presets shipped with the mod (Night Grimoire, Candlelit Tome): data
# files the plugin lists at runtime, so the dev install needs them to show them
$designSrc = Join-Path $repo 'SKSE\Plugins\SpellLearning\presets\design'
$designDst = Join-Path $ModPath 'SKSE\Plugins\SpellLearning\presets\design'
if (Test-Path $designSrc) {
    New-Item -ItemType Directory -Force -Path $designDst | Out-Null
    Copy-Item (Join-Path $designSrc '*.json') $designDst -Force
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

$checked = @('modules\bridgeView.js', 'modules\spellCard.js', 'patch-ui.css', 'index.html')
foreach ($file in $checked) {
    $a = (Get-FileHash (Join-Path $panelSrc $file)).Hash
    $b = (Get-FileHash (Join-Path $panelDst $file)).Hash
    if ($a -ne $b) { throw "Copy did not take: $file" }
}
Write-Host 'verified'
