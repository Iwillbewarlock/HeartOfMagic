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
      textures\HeartOfMagic\BlackFlame\GradBlackFlame.dds and GradBlackFlameAdd.dds (Godskin: additive palette shaders)
      optional\palettes\GradBlackFlameAdd_blackblade.dds, _messmer.dds (not loaded: copy one over GradBlackFlameAdd.dds)
      tools\BlackFlame\blackflame-greybuild.exe and "Build Black Flame copies.bat"
      README_TESTER.md
      THIRD-PARTY-NOTICES.md (from THIRD-PARTY-NOTICES-TESTER.md.in: the DLL and the builder, their libraries with the
      versions vcpkg installed, the exact commit) and licenses\ (copied from the CommonLib submodule and vcpkg's installed
      copyright files at packaging time, so never an older build's copies)
      source\HeartOfMagic-<version>-source.zip when the commit is reachable from no branch or tag on origin
      (-AllowUnpublished): the Corresponding Source of the GPL binaries, from git archive (a dirty tree: git stash create,
      tracked files), with the CommonLibSSE-NG submodule's tree at plugins/external/commonlibsse-ng/ in the same zip
    Never in it: other mods' meshes or generated copies. The script refuses when plugins\BlackFlame\data holds a
    meshes\heartofmagic\blackflame\copies folder or a non-empty Copies/Local ESL (those are built on the tester's machine).
    It also refuses a working tree with uncommitted changes (the zip is named after the commit; -AllowDirty packages it
    anyway with "-dirty" in the name), a DLL older than the newest file under plugins\BlackFlame\src (build first), and a
    commit no branch or tag on origin reaches (after git fetch origin; local tags and other remotes do not count: the
    notices would name source nobody can get): -AllowUnpublished packages it with the source zip inside. -AllowDirty needs
    -AllowUnpublished (no remote holds uncommitted changes). An untracked file under the DLL's or the builder's sources
    is refused always (no zip and no commit would hold it). The builder is refused when its output identity
    (blackflame-greybuild --identity) is a dev one ("+dev") or not kBuilderOutputVersion of CopyContractKeys.h, and when
    its --self-test (the golden test on the repository's fixtures included) has a FAIL, and when its kBuilderOutputVersion has
    no released pin (GreyGoldenTable.h kReleasedPins; the refusal prints the row to add). Procedure for a new output
    version: run the builder's --self-test, add the row its "NOTE golden table v<n> fnv64 <hex>" line gives, commit, build
    Release again, package.

.EXAMPLE
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1 -Version 1.5.0-t2
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1 -AllowUnpublished
    .\plugins\BlackFlame\tools\Make-TesterPackage.ps1 -AllowDirty -AllowUnpublished
#>
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$AllowDirty,
    [switch]$AllowUnpublished
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$data = Join-Path $repo 'plugins\BlackFlame\data'
$dllDir = Join-Path $repo 'build\plugins\BlackFlame\Release'
$builder = Join-Path $repo 'build\tools\Release\blackflame-greybuild.exe'
$readme = Join-Path $PSScriptRoot 'README_TESTER.md'
# T8, U2: the DLL statically links CommonLibSSE-NG (a GPL combined work) and the builder nifly (GPL-3.0): the package's
# own notice (not the Librarian release's) names both binaries and the exact commit; the licence texts are copied from
# their origins now (the CommonLib submodule, vcpkg's installed copyright files), so none can be an older build's
$noticesTemplate = Join-Path $PSScriptRoot 'THIRD-PARTY-NOTICES-TESTER.md.in'
$commonlib = Join-Path $repo 'plugins\external\commonlibsse-ng'
$vcpkgShare = Join-Path $repo 'build\vcpkg_installed\x64-windows-static\share'
# port -> shown name, licence; DLL: CommonLib's own links (spdlog, DirectXTK) and the DLL's (nlohmann-json), spdlog's fmt.
# W8: no xbyak and no zlib here: neither one's strings are in HeartOfMagic_BlackFlame.dll (xbyak's "code is too big",
# zlib's "incorrect header check"), while SpellLearning.dll has both - the main release notice lists them for its DLLs
$dllPorts = [ordered]@{ 'fmt' = @('{fmt}', 'MIT'); 'spdlog' = @('spdlog', 'MIT'); 'nlohmann-json' = @('JSON for Modern C++ (nlohmann-json)', 'MIT');
    'directxtk' = @('DirectX Tool Kit (DirectXTK)', 'MIT'); 'directxmath' = @('DirectXMath', 'MIT') }
$builderPorts = [ordered]@{ 'nifly' = @('nifly', 'GPL-3.0'); 'directxtex' = @('DirectXTex', 'MIT'); 'fmt' = @('{fmt}', 'MIT');
    'nlohmann-json' = @('JSON for Modern C++ (nlohmann-json)', 'MIT') }
$commonlibLicenses = @('COPYING.txt', 'EXCEPTIONS.md', 'licenses\LICENSE-MIT.txt')

# G6: to stderr and exit 1 (Write-Error under ErrorActionPreference Stop would throw before the exit code is set)
function Fail([string]$why) {
    [Console]::Error.WriteLine("Make-TesterPackage: $why")
    exit 1
}

# The newest of the files a binary is compiled from
function Newest([string[]]$paths) {
    $files = foreach ($p in $paths) {
        $full = Join-Path $repo $p
        if (Test-Path $full -PathType Container) { Get-ChildItem -Recurse -File $full } elseif (Test-Path $full) { Get-Item $full }
    }
    $files | Sort-Object LastWriteTime -Descending | Select-Object -First 1
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

# U2: the notices name the commit as the Corresponding Source, so a remote must hold it - or the source goes in the zip
$commit = (git -C $repo rev-parse HEAD).Trim()
# W6: published = reachable from a branch or tag that origin has now (fetched first; offline counts as unpublished)
$published = $false
$promptBefore = $env:GIT_TERMINAL_PROMPT
try {
    $env:GIT_TERMINAL_PROMPT = '0'  # X12, Z15: no credential prompt for the fetch nor for ls-remote
    git -C $repo fetch --quiet --tags origin 2>$null
    if ($LASTEXITCODE -eq 0) {
        foreach ($line in @(git -C $repo ls-remote --heads --tags origin)) {
            $sha = ($line -split '\s+')[0]
            if ($sha) {
                git -C $repo merge-base --is-ancestor $commit $sha 2>$null
                if ($LASTEXITCODE -eq 0) { $published = $true; break }
            }
        }
    } else {
        Write-Host "Make-TesterPackage: git fetch origin failed (offline, no access, or a tag conflict): the commit counts as unpublished"
    }
} finally {
    $env:GIT_TERMINAL_PROMPT = $promptBefore  # Y19, Z15: put back as it was, whatever happened
}
if ($dirty -and -not $AllowUnpublished) {
    Fail "-AllowDirty packages changes no commit holds: pass -AllowUnpublished too (the source zip then goes in the package)"
}
if (-not $published -and -not $AllowUnpublished) {
    Fail "commit $commit is on no remote branch or tag: push it, or pass -AllowUnpublished to put its source zip in the package"
}
$withSource = $dirty -or -not $published

foreach ($file in @((Join-Path $dllDir 'HeartOfMagic_BlackFlame.dll'), (Join-Path $dllDir 'HeartOfMagic_BlackFlame.pdb'), $builder, $readme,
        (Join-Path $data 'HeartOfMagic_BlackFlame.esp'), (Join-Path $data 'HeartOfMagic_BlackFlame_Copies.esp'),
        (Join-Path $data 'HeartOfMagic_BlackFlame_Local.esp'), (Join-Path $data 'SKSE\Plugins\HeartOfMagic_BlackFlame.json'))) {
    if (-not (Test-Path $file)) { Fail "$file is missing (run .\BuildRelease.ps1 -preset Release-2026 first)" }
}
$licenceSources = @($noticesTemplate) + @($commonlibLicenses | ForEach-Object { Join-Path $commonlib $_ }) +
    @(@($dllPorts.Keys) + @($builderPorts.Keys) | Select-Object -Unique | ForEach-Object { Join-Path $vcpkgShare "$_\copyright" })
foreach ($file in $licenceSources) {
    if (-not (Test-Path $file)) { Fail "$file is missing (git submodule update --init --recursive, then .\BuildRelease.ps1 -preset Release-2026)" }
}
# The version vcpkg installed, from the port's SPDX file (the package entry named after the port)
function PortVersion([string]$port) {
    $spdx = Join-Path $vcpkgShare "$port\vcpkg.spdx.json"
    if (-not (Test-Path $spdx)) { Fail "$spdx is missing: run .\BuildRelease.ps1 -preset Release-2026 first" }
    $entry = (Get-Content $spdx -Raw | ConvertFrom-Json).packages | Where-Object { $_.name -eq $port } | Select-Object -First 1
    if (-not $entry -or -not $entry.versionInfo) { Fail "$spdx names no version for $port" }
    return $entry.versionInfo
}
# X11: the submodule's work tree must be its commit (the zip and the notice name that commit)
$commonlibDirty = git -C $commonlib status --porcelain
if ($LASTEXITCODE -ne 0) { Fail "git status failed in $commonlib" }
if ($commonlibDirty) { Fail "plugins\external\commonlibsse-ng has uncommitted changes: the binaries would not be its commit (git -C plugins\external\commonlibsse-ng status)" }
$commonlibVersion = (git -C $commonlib describe --tags --exact-match 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $commonlibVersion) { Fail "plugins\external\commonlibsse-ng is not at a tag (git submodule update --init --recursive)" }
$commonlibVersion = $commonlibVersion.Trim().TrimStart('v')
# G6: each binary must be newer than everything it is compiled from (the DLL: its sources, its CMakeLists, the shared plugin
# headers and the spelllearning files it compiles; the builder: its sources and tools\CMakeLists.txt). README, data and the
# tools folder are not compiled into either and are packaged as they are.
$dllSources = @('plugins\BlackFlame\src', 'plugins\BlackFlame\CMakeLists.txt', 'plugins\Common.h', 'plugins\PCH.h',
    'plugins\WideFileSink.h', 'plugins\spelllearning\src\EncodingUtils.cpp', 'plugins\spelllearning\include\EncodingUtils.h',
    'plugins\spelllearning\include\PluginNames.h', 'plugins\spelllearning\include\AsciiText.h')
# The builder: its folder, its CMake target and every plugin header it compiles (tools\CMakeLists.txt's sources and their
# includes: the copy contract, its identity resource header and test table, the throttle signal, the path rule)
$builderSources = @('tools\blackflame-greybuild', 'tools\CMakeLists.txt', 'plugins\BlackFlame\src\CopyContract*.h',
    'plugins\BlackFlame\src\BuilderSignal.h', 'plugins\BlackFlame\src\DataPathRule.h')
# W7: a file the build compiles that git does not track would be in neither the commit nor the source zip
$untracked = @(git -C $repo ls-files --others --exclude-standard -- @($dllSources + $builderSources | ForEach-Object { $_ -replace '\\', '/' }))
if ($untracked) { Fail "untracked file(s) under the DLL's or the builder's sources (commit or remove them): $($untracked -join ', ')" }
foreach ($pair in @(@((Join-Path $dllDir 'HeartOfMagic_BlackFlame.dll'), $dllSources), @($builder, $builderSources))) {
    $binary = Get-Item $pair[0]
    $newest = Newest $pair[1]
    if ($newest -and $newest.LastWriteTime -gt $binary.LastWriteTime) {
        Fail "$($binary.Name) ($($binary.LastWriteTime)) is older than $($newest.FullName) ($($newest.LastWriteTime)): run .\BuildRelease.ps1 -preset Release-2026 first"
    }
}
# The builder's output identity: a release one ("<n>", no "+dev"), and n the kBuilderOutputVersion the DLL was built with
$identity = (& $builder --identity 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $identity) { Fail "$builder --identity failed (exit $LASTEXITCODE): build it again (.\BuildRelease.ps1 -preset Release-2026)" }
$keysHeader = Join-Path $repo 'plugins\BlackFlame\src\CopyContractKeys.h'
$versionMatch = [regex]::Match((Get-Content $keysHeader -Raw), 'kBuilderOutputVersion = ([0-9]+);')
if (-not $versionMatch.Success) { Fail "no 'kBuilderOutputVersion = <n>;' in $keysHeader" }
$outputVersion = $versionMatch.Groups[1].Value
if ($identity -match '\+dev') {
    Fail "the builder's output identity is '$identity', a dev build (not Release, or builder sources that differ from git HEAD): commit them and build Release"
}
if ($identity -ne $outputVersion) {
    Fail "the builder's output identity '$identity' is not kBuilderOutputVersion $outputVersion of CopyContractKeys.h: build the builder again"
}
# Its whole --self-test, the golden test included (the repository's fixtures): any FAIL line or a non-zero exit stops here
$golden = Join-Path $repo 'tools\blackflame-greybuild\golden'
$selfTest = @(& $builder --self-test --golden-dir $golden 2>$null)
$selfTestExit = $LASTEXITCODE
$selfTestFails = @($selfTest | Where-Object { $_ -match '^FAIL [^ ]' })
$selfTestSummary = $selfTest | Where-Object { $_ -match '^SELF-TEST ' } | Select-Object -Last 1
if ($selfTestExit -ne 0 -or $selfTestFails -or -not $selfTestSummary) {
    Fail "the builder's --self-test failed (exit $selfTestExit, $(if ($selfTestSummary) { $selfTestSummary } else { 'no summary line' })): $($selfTestFails -join ' | ')"
}
Write-Host "builder: output identity $identity, --self-test $selfTestSummary"
# A package ships only a builder whose output version is pinned (GreyGoldenTable.h kReleasedPins: the self-test's
# "PASS golden pin: version <n> is pinned" line), so its golden table can never be regenerated in place afterwards
$pinned = @($selfTest | Where-Object { $_ -match "^PASS golden pin: version $outputVersion is pinned " })
if (-not $pinned) {
    $note = $selfTest | Where-Object { $_ -match "^NOTE golden table v$outputVersion fnv64 ([0-9a-f]{16})" } | Select-Object -First 1
    $row = if ($note -and $note -match "fnv64 ([0-9a-f]{16})") { "{ $outputVersion, `"$($Matches[1])`" }," } else { "(the self-test printed no NOTE golden table v$outputVersion line)" }
    Fail ("output version $outputVersion has no released pin: add this row to kReleasedPins in tools\blackflame-greybuild\GreyGoldenTable.h: " +
        "$row then commit, build Release again (.\BuildRelease.ps1 -preset Release-2026) and run this again")
}
Write-Host "  $($pinned[0])"
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
) else if "%CODE%"=="-1073741515" (
    echo The builder could not start: a DLL is missing ^(code 0xC0000135^). It needs VCOMP140.DLL from the
    echo Microsoft Visual C++ Redistributable 2015-2022 ^(x64^): install it from Microsoft, then run this again.
) else (
    echo The builder stopped with code %CODE%: read the lines above for the next step.
)
pause
exit /b %CODE%
'@
Set-Content -Encoding ASCII (Join-Path $tools 'Build Black Flame copies.bat') $bat
Copy-Item $readme (Join-Path $stage 'README_TESTER.md')
# U2: licence texts from their origins, the notice filled in for this commit
$stageLicenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force (Join-Path $stageLicenses 'CommonLibSSE-NG') | Out-Null
foreach ($name in $commonlibLicenses) { Copy-Item (Join-Path $commonlib $name) (Join-Path $stageLicenses 'CommonLibSSE-NG') }
function Rows($ports) {
    ($ports.Keys | ForEach-Object {
        Copy-Item (Join-Path $vcpkgShare "$_\copyright") (Join-Path $stageLicenses "$_.txt") -Force
        '| {0} | {1} | {2} | `licenses/{3}.txt` |' -f $ports[$_][0], (PortVersion $_), $ports[$_][1], $_
    }) -join "`n"
}
$commonlibCommit = (git -C $commonlib rev-parse HEAD).Trim()
$sourceLine = if ($withSource) {
    $kind = if ($dirty) { 'and the uncommitted changes of its tracked files' } else { 'which no public branch or tag holds yet' }
    'That commit ({0}) is in this package as `source/HeartOfMagic-{1}-source.zip` (git archive of the repository).' -f $kind, $Version
} else { 'The commit is public in that repository.' }
# W5: the submodule is in the zip when the zip is the source; otherwise the repository's submodule names it
$commonlibSource = if ($withSource) {
    ('CommonLibSSE-NG {0} is in the same zip at `plugins/external/commonlibsse-ng` (its commit `{1}`, upstream ' +
        '<https://github.com/alandtse/CommonLibVR>, branch `ng`).') -f "v$commonlibVersion", $commonlibCommit
} else {
    ('CommonLibSSE-NG is the repository''s git submodule `plugins/external/commonlibsse-ng` at tag `v{0}` (commit `{1}`, ' +
        '<https://github.com/alandtse/CommonLibVR>, branch `ng`); `git submodule update --init --recursive` fetches it.') -f
        $commonlibVersion, $commonlibCommit
}
$text = (Get-Content $noticesTemplate -Raw).Replace('@VERSION@', $Version).Replace('@COMMIT@', $commit).
    Replace('@COMMONLIB_VERSION@', $commonlibVersion).Replace('@SOURCE@', $sourceLine).Replace('@COMMONLIB_SOURCE@', $commonlibSource).
    Replace('@DLL_ROWS@', (Rows $dllPorts)).Replace('@BUILDER_ROWS@', (Rows $builderPorts))
if ($text -match '@[A-Z_]+@') { Fail "THIRD-PARTY-NOTICES-TESTER.md.in has a placeholder the script does not fill: $($Matches[0])" }
Set-Content -Encoding UTF8 (Join-Path $stage 'THIRD-PARTY-NOTICES.md') $text
foreach ($file in $licenceSources | Where-Object { $_ -ne $noticesTemplate }) {
    $name = if ($file.StartsWith($commonlib)) { "CommonLibSSE-NG\$(Split-Path $file -Leaf)" } else { "$(Split-Path (Split-Path $file -Parent) -Leaf).txt" }
    $landed = Join-Path $stageLicenses $name
    if (-not (Test-Path $landed) -or (Get-FileHash $landed).Hash -ne (Get-FileHash $file).Hash) {
        Fail "licenses\$name in the package is not the current $file"
    }
}
if ($withSource) {
    # W7: git stash create prints nothing when there is nothing to stash (untracked files only): the commit then
    $stashed = if ($dirty) { git -C $repo stash create } else { $null }
    $tree = if ($stashed) { "$stashed".Trim() } else { $commit }
    $sourceDir = Join-Path $stage 'source'
    New-Item -ItemType Directory -Force $sourceDir | Out-Null
    # W5: the repository's zip (git archive), then the CommonLibSSE-NG submodule's (git archive leaves submodules out)
    # copied into it entry by entry under plugins/external/commonlibsse-ng/. Zip to zip: nothing is unpacked on disk (the
    # submodule has symbolic links that need a privilege to create; in the zip they are entries holding their target)
    $sourceZip = Join-Path $sourceDir "HeartOfMagic-$Version-source.zip"
    $libZip = Join-Path $dist "HeartOfMagic-BlackFlame-Tester_$Version-commonlib.zip"
    git -C $repo archive --format=zip --prefix="HeartOfMagic-$Version/" -o $sourceZip $tree
    if ($LASTEXITCODE -ne 0) { Fail "git archive of $tree failed: no source zip, no package" }
    git -C $commonlib archive --format=zip --prefix="HeartOfMagic-$Version/plugins/external/commonlibsse-ng/" -o $libZip $commonlibCommit
    if ($LASTEXITCODE -ne 0) { Fail "git archive of the CommonLibSSE-NG submodule failed: no source zip, no package" }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    try {
        $target = $null
        $library = $null
        try {
            # Z15: both opened inside the try, so a failed second open still disposes the first
            $target = [System.IO.Compression.ZipFile]::Open($sourceZip, [System.IO.Compression.ZipArchiveMode]::Update)
            $library = [System.IO.Compression.ZipFile]::OpenRead($libZip)
            foreach ($entry in $library.Entries) {
                # X10: the repository's archive already has the submodule's folder entry (its gitlink): never twice
                if ($target.GetEntry($entry.FullName)) { continue }
                $copy = $target.CreateEntry($entry.FullName)
                $copy.LastWriteTime = $entry.LastWriteTime
                # Z15: copied, but .NET writes "made on Windows", so extractors ignore the unix bits: a symbolic link comes
                # out as a small file holding its target (README, Y20)
                $copy.ExternalAttributes = $entry.ExternalAttributes
                if ($entry.FullName.EndsWith('/')) { continue }
                $in = $entry.Open()
                $out = $copy.Open()
                try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
            }
        } finally {
            if ($library) { $library.Dispose() }
            if ($target) { $target.Dispose() }
        }
    } finally {
        Remove-Item -Force -ErrorAction SilentlyContinue $libZip
    }
}

# Last guard: nothing outside the expected tree
$unexpected = Get-ChildItem -Recurse -File $stage | Where-Object { $_.FullName -match '\\copies\\' }
if ($unexpected) { Fail "generated copies found in the package: $($unexpected[0].FullName)" }

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
$count = (Get-ChildItem -Recurse -File $stage).Count
Write-Host "wrote $zip ($count files, version $Version)"
