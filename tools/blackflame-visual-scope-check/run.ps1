param(
    [string]$RepositoryRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$BuildRoot
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe was not found. Run from a Visual Studio Developer PowerShell with the C++ tools enabled.'
}
if (-not $BuildRoot) { $BuildRoot = Join-Path $RepositoryRoot 'build\blackflame-visual-scope-check' }
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
foreach ($name in @('VisualSet.cpp', 'VisualSet.h')) {
    $source = Join-Path $RepositoryRoot "plugins\BlackFlame\src\$name"
    $copy = Join-Path $BuildRoot $name
    Copy-Item -LiteralPath $source -Destination $copy -Force
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($sourceHash -ne (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash) {
        throw "Production source copy changed: $name"
    }
    Write-Output "Actual production $name SHA256: $sourceHash"
}
"#pragma once`n#include `"FakeEngine.h`"" | Set-Content -LiteralPath (Join-Path $BuildRoot 'Common.h') -Encoding utf8
Push-Location $BuildRoot
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 "/I$BuildRoot" "/I$PSScriptRoot" /Fe:visual-scope-check.exe `
        (Join-Path $BuildRoot 'VisualSet.cpp') (Join-Path $PSScriptRoot 'test.cpp')
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $LASTEXITCODE" }
    & '.\visual-scope-check.exe'
    $testExit = $LASTEXITCODE
} finally { Pop-Location }
exit $testExit
