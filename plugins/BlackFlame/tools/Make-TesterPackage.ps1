#Requires -Version 7
<#
.SYNOPSIS
    Builds dist\HeartOfMagic-BlackFlame-Tester_<version>.zip from the last build (run BuildRelease.ps1 first).

.DESCRIPTION
    The zip is one mod folder for MO2 or Vortex:
      HeartOfMagic_BlackFlame.esp, _Copies.esp, _Local.esp (the last two empty: the builder's output replaces them)
      SKSE\Plugins\HeartOfMagic_BlackFlame.dll and .pdb
      SKSE\Plugins\HeartOfMagic_BlackFlame.json            (testerMode true: self-test, copy request, full log)
      SKSE\Plugins\HeartOfMagic_BlackFlame_greymap.json
      meshes\heartofmagic\blackflame\...                    (the grey meshes made from vanilla, as shipped)
      textures\HeartOfMagic\BlackFlame\GradBlackFlame.dds
      tools\BlackFlame\blackflame-greybuild.exe and "Build Black Flame copies.bat"
      README_TESTER.md
    Never in it: other mods' meshes or generated copies. The script refuses when plugins\BlackFlame\data holds a
    meshes\heartofmagic\blackflame\copies folder or a non-empty Copies/Local ESL (those are built on the tester's machine).
    It also refuses a working tree with uncommitted changes (the zip is named after the commit; -AllowDirty packages it
    anyway with "-dirty" in the name) and a DLL older than the newest file under plugins\BlackFlame\src (build first).

.EXAMPLE
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1 -Version 1.5.0-t2
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1 -AllowDirty
#>
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$data = Join-Path $repo 'plugins\BlackFlame\data'
$dllDir = Join-Path $repo 'build\plugins\BlackFlame\Release'
$builder = Join-Path $repo 'build\tools\Release\blackflame-greybuild.exe'
$readme = Join-Path $PSScriptRoot 'README_TESTER.md'

function Fail([string]$why) {
    Write-Error "Make-TesterPackage: $why"
    exit 1
}

# The zip is named after the commit: a working tree with changes would ship something no commit holds
$dirty = git -C $repo status --porcelain
if ($LASTEXITCODE -ne 0) { Fail "git status failed in $repo" }
if ($dirty -and -not $AllowDirty) {
    Fail "the working tree has uncommitted changes (commit them, or pass -AllowDirty to package with -dirty in the name)"
}
if (-not $Version) {
    $base = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim()
    $sha = (git -C $repo rev-parse --short HEAD).Trim()
    $Version = "$base-$sha"
}
if ($dirty) { $Version = "$Version-dirty" }

foreach ($file in @((Join-Path $dllDir 'HeartOfMagic_BlackFlame.dll'), (Join-Path $dllDir 'HeartOfMagic_BlackFlame.pdb'), $builder, $readme,
        (Join-Path $data 'HeartOfMagic_BlackFlame.esp'), (Join-Path $data 'HeartOfMagic_BlackFlame_Copies.esp'),
        (Join-Path $data 'HeartOfMagic_BlackFlame_Local.esp'), (Join-Path $data 'SKSE\Plugins\HeartOfMagic_BlackFlame.json'))) {
    if (-not (Test-Path $file)) { Fail "$file is missing (run .\BuildRelease.ps1 -preset Release-2026 first)" }
}
# The DLL must be built from the sources as they are now
$dll = Get-Item (Join-Path $dllDir 'HeartOfMagic_BlackFlame.dll')
$newestSource = Get-ChildItem -Recurse -File (Join-Path $repo 'plugins\BlackFlame\src') | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newestSource -and $newestSource.LastWriteTime -gt $dll.LastWriteTime) {
    Fail "the DLL ($($dll.LastWriteTime)) is older than $($newestSource.FullName) ($($newestSource.LastWriteTime)): run .\BuildRelease.ps1 -preset Release-2026 first"
}
# Only the shipped, vanilla-derived files: no generated copies, no populated copies plugin
if (Test-Path (Join-Path $data 'meshes\heartofmagic\blackflame\copies')) {
    Fail "plugins\BlackFlame\data\meshes\heartofmagic\blackflame\copies exists: generated copies must not ship (remove it)"
}
foreach ($esl in 'HeartOfMagic_BlackFlame_Copies.esp', 'HeartOfMagic_BlackFlame_Local.esp') {
    $size = (Get-Item (Join-Path $data $esl)).Length
    if ($size -gt 200) { Fail "$esl is $size bytes: only the empty ESL (no records) may ship (blackflame-greybuild --write-empty-esls)" }
}
if (Test-Path (Join-Path $data 'SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json')) {
    Fail "a copies map is in plugins\BlackFlame\data: it is generated per load order and must not ship"
}

$dist = Join-Path $repo 'dist'
$stage = Join-Path $dist "HeartOfMagic-BlackFlame-Tester_$Version"
$zip = "$stage.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item -Recurse (Join-Path $data '*') $stage
$plugins = Join-Path $stage 'SKSE\Plugins'
Copy-Item (Join-Path $dllDir 'HeartOfMagic_BlackFlame.dll') $plugins
Copy-Item (Join-Path $dllDir 'HeartOfMagic_BlackFlame.pdb') $plugins

# The tester config: the shipped one with testerMode on (it forces selfTest and writeRequest)
$configPath = Join-Path $plugins 'HeartOfMagic_BlackFlame.json'
$config = Get-Content $configPath -Raw | ConvertFrom-Json
$config.testerMode = $true
$config | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $configPath

$tools = Join-Path $stage 'tools\BlackFlame'
New-Item -ItemType Directory -Force $tools | Out-Null
Copy-Item $builder $tools
$bat = @'
@echo off
rem Builds the grey copies for this load order from what the game wrote at its last start.
rem Output: the folder given as the first argument, else "HeartOfMagic BlackFlame Copies" in your Documents folder.
setlocal
set "OUT=%~1"
if "%OUT%"=="" (
    for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')"`) do set "OUT=%%D\HeartOfMagic BlackFlame Copies"
)
if not exist "%OUT%" mkdir "%OUT%"
echo Output folder: %OUT%
"%~dp0blackflame-greybuild.exe" --from-game --out "%OUT%"
set "CODE=%ERRORLEVEL%"
echo.
if "%CODE%"=="0" (
    echo Done. Install the output folder as a mod placed AFTER Heart of Magic - Black Flame, then start the game again.
) else (
    echo The builder stopped with code %CODE%: read the lines above for the next step.
)
pause
exit /b %CODE%
'@
Set-Content -Encoding ASCII (Join-Path $tools 'Build Black Flame copies.bat') $bat
Copy-Item $readme (Join-Path $stage 'README_TESTER.md')

# Last guard: nothing outside the expected tree
$unexpected = Get-ChildItem -Recurse -File $stage | Where-Object { $_.FullName -match '\\copies\\' }
if ($unexpected) { Fail "generated copies found in the package: $($unexpected[0].FullName)" }

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
$count = (Get-ChildItem -Recurse -File $stage).Count
Write-Host "wrote $zip ($count files, version $Version)"
