$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe was not found. Run from Visual Studio Developer PowerShell with the C++ tools enabled.'
}
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sourceRoot = Join-Path $repoRoot 'plugins\BlackFlame\src'
$buildRoot = Join-Path $repoRoot 'build\blackflame-visual-closure-check'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
foreach ($name in @('FireClosure.cpp', 'FireClosure.h')) {
    $sourcePath = Join-Path $sourceRoot $name
    $copyPath = Join-Path $buildRoot $name
    Copy-Item -LiteralPath $sourcePath -Destination $copyPath -Force
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($sourceHash -ne (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash) {
        throw "Production source copy changed: $name"
    }
    Write-Output "Actual production $name SHA256: $sourceHash"
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'test.cpp') -Destination (Join-Path $buildRoot 'test.cpp') -Force
foreach ($name in @('Common.h', 'FireSet.h', 'VisualSet.h', 'EncodingUtils.h')) {
    "#pragma once`n#include `"FakeEngine.h`"" | Set-Content -LiteralPath (Join-Path $buildRoot $name) -Encoding utf8
}
Push-Location $buildRoot
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /Z7 "/I$PSScriptRoot" /Fe:closure-regression.exe /Fo:closure-regression.obj test.cpp
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $LASTEXITCODE" }
    & '.\closure-regression.exe'
    $testExit = $LASTEXITCODE
} finally { Pop-Location }
exit $testExit
