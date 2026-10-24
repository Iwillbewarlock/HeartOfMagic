$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Run from Visual Studio Developer PowerShell with the C++ tools enabled.' }
if (-not (Get-Command node.exe -ErrorAction SilentlyContinue)) { throw 'Node.js is required to extract production method bodies.' }
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sourceRoot = Join-Path $repoRoot 'plugins\BlackFlame\src'
$buildRoot = Join-Path $repoRoot 'build\blackflame-script-visual-check'
& node.exe (Join-Path $PSScriptRoot 'extract.js') $sourceRoot $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'Production seam extraction failed.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'FakeEngine.h'), (Join-Path $PSScriptRoot 'test.cpp') -Destination $buildRoot -Force
Push-Location $buildRoot
try {
    & cl.exe /nologo /std:c++20 /EHsc /W4 /Z7 /Fe:script-visual-check.exe /Fo:script-visual-check.obj test.cpp
    if ($LASTEXITCODE -ne 0) { throw 'Production seam compilation failed.' }
    & '.\script-visual-check.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Production seam regression failed.' }
    # Deliberate mutant, not a historical source snapshot: remove exactly the two reviewed guards.
    $seamPath = Join-Path $buildRoot 'Seam.cpp'
    $seam = [IO.File]::ReadAllText($seamPath)
    $guards = [regex]::Matches($seam, 'info\.visualInstance && info\.state').Count
    if ($guards -ne 2) { throw "Expected two provenance guards; found $guards. Update the mutation seam explicitly." }
    try {
        [IO.File]::WriteAllText($seamPath, $seam.Replace('info.visualInstance && info.state', 'info.state'))
        Write-Output 'MUTANT: two visualInstance guards removed from generated seam only.'
        & cl.exe /nologo /std:c++20 /EHsc /W4 /Z7 /Fe:script-visual-mutant.exe /Fo:script-visual-mutant.obj test.cpp
        if ($LASTEXITCODE -ne 0) { throw 'Mutant compilation failed.' }
        & '.\script-visual-mutant.exe'
        if ($LASTEXITCODE -eq 0) { throw 'Mutation survived: regression did not detect source-only tag inheritance.' }
        Write-Output 'PASS mutation rejected; production method bodies passed.'
    } finally { [IO.File]::WriteAllText($seamPath, $seam) }
} finally { Pop-Location }
exit 0
