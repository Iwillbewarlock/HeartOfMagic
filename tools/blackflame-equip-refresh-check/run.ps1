param(
    [string]$RepoRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$BuildRoot = ''
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from a Visual Studio Developer PowerShell with C++ tools.'
}
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepoRoot 'build\blackflame-equip-refresh-check' }
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
foreach ($file in @('EquipAbilityRefresh.cpp','VisualEquipContext.cpp','VisualEquipRules.h')) {
    $original = Join-Path $RepoRoot "plugins\BlackFlame\src\$file"
    $copy = Join-Path $BuildRoot $file
    Copy-Item -LiteralPath $original -Destination $copy -Force
    $hash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
    if ($hash -ne (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash) { throw "Source copy changed: $file" }
    Write-Output "Actual production source $file SHA256: $hash"
}
foreach ($header in @('EquipAbilityRefresh.h','VisualEquipContext.h','Config.h','VisualSet.h','FormCopies.h','Forms.h','MainThreadQueue.h','BuffState.h','GameThread.h','Patch.h')) {
    "#pragma once`n#include `"FakeEngine.h`"" | Set-Content -LiteralPath (Join-Path $BuildRoot $header) -Encoding utf8
}
Push-Location $BuildRoot
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 "/I$PSScriptRoot" /Fe:refresh-regression.exe 'EquipAbilityRefresh.cpp' 'VisualEquipContext.cpp' (Join-Path $PSScriptRoot 'test.cpp')
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $LASTEXITCODE" }
    & '.\refresh-regression.exe'
    $testExit = $LASTEXITCODE
} finally { Pop-Location }
exit $testExit
