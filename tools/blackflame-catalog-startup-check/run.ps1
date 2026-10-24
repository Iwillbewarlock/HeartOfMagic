param([string]$RepoRoot = '', [string]$Main = '', [string]$BuildRoot = '', [string]$Label = 'current', [switch]$MissingOnly)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Run from Visual Studio Developer PowerShell with the C++ tools enabled.' }
if (-not $RepoRoot) { $RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent }
$source = Join-Path $RepoRoot 'plugins\BlackFlame\src'
if (-not $Main) { $Main = Join-Path $source 'Main.cpp' }
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot "build\blackflame-catalog-startup-check-$Label" }
$build = $BuildRoot
& node.exe (Join-Path $PSScriptRoot 'extract.js') $Main $build
if ($LASTEXITCODE -ne 0) { throw 'Production extraction failed' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'FakeEngine.h'), (Join-Path $PSScriptRoot 'test.cpp') -Destination $build -Force
foreach ($name in @('CatalogStartup.cpp', 'CatalogStartup.h', 'CatalogFile.h')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $build -Force
    if ((Get-FileHash -LiteralPath (Join-Path $source $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $build $name)).Hash) { throw "Production copy mismatch: $name" }
    Write-Output "$name SHA256 $((Get-FileHash -LiteralPath (Join-Path $build $name)).Hash)"
}
foreach ($name in @('Common.h', 'CopyFreshness.h', 'MainThreadQueue.h', 'MenuWatch.h')) {
    Set-Content -LiteralPath (Join-Path $build $name) -Value '#include "FakeEngine.h"'
}
Push-Location $build
try {
    $include = Join-Path $RepoRoot 'build\vcpkg_installed\x64-windows-static\include'
    & cl.exe /nologo /std:c++20 /EHsc /W4 "/I$include" /Fe:catalog-startup-check.exe /Fo:catalog-startup-check.obj test.cpp
    if ($LASTEXITCODE -ne 0) { throw 'Production seam compilation failed' }
    $result = 0
    $cases = if ($MissingOnly) { @('missing') } else { @('existing', 'empty', 'missing', 'invalid', 'wrong-root', 'wrong-spells', 'missing-spells', 'part-only', 'closed', 'closed-existing', 'close-waiting', 'proof-refused', 'proof-pending', 'queue-failed', 'queue-failed-later', 'no-queue') }
    foreach ($case in $cases) {
        & '.\catalog-startup-check.exe' $case
        if ($LASTEXITCODE -ne 0) { $result = 1 }
    }
} finally { Pop-Location }
exit $result
