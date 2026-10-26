#Requires -Version 7
<#
.SYNOPSIS
    Builds the public release zips dist\BlackFlame-<version>\HeartOfMagic-BlackFlame_<version>_KO.zip and _EN.zip from the
    tested binaries of the last Release build (no build here).

.DESCRIPTION
    Each zip is one mod folder for MO2 or Vortex:
      HeartOfMagic_BlackFlame.esp (KO: plugins\BlackFlame\data, EN: plugins\BlackFlame\localization\en; same FormIDs)
      HeartOfMagic_BlackFlame_Slots.esp
      SKSE\Plugins\HeartOfMagic_BlackFlame.dll
      SKSE\Plugins\HeartOfMagic_BlackFlame.json            (the tested single-launch config, lanternLog false, every test
                                                          and automation switch off: $releaseConfig below)
      SKSE\Plugins\HeartOfMagic_BlackFlame_greymap.json
      SKSE\Plugins\HeartOfMagic_BlackFlame\blackflame-greybuild.exe
      meshes\heartofmagic\blackflame\..., textures\HeartOfMagic\BlackFlame\... (vanilla-derived, as shipped)
      README_KO.md or README_EN.md                         (plugins\BlackFlame\docs\README_<lang>.md, the player guide)
      LICENSE, THIRD-PARTY-NOTICES.md, licenses\           (licence texts copied from their origins now)
      PACKAGE-MANIFEST.json                                (version, commits, every file's size and sha256)
    Never in it: generated copies, a source snapshot, lab\, requests\ (load-order dumps), PDBs. The notices point to the
    public repository and the exact commit instead of a source zip, so that commit must be on origin before upload.

    Refusals: a working tree with changes; any shipped binary or ESP whose sha256 is not the tested one ($tested); a
    compiled input of either binary that differs between the build commit and HEAD; a builder whose output identity is a
    dev one or not kBuilderOutputVersion; a failed builder --self-test or an unpinned output version; a commit no branch or
    tag on origin reaches (after git fetch), unless -AllowUnpushed (then it warns: push before uploading).
    A new build means new $tested hashes and a new $buildCommit: run the in-game test on that build first.

.EXAMPLE
    .\plugins\BlackFlame\tools\Make-ReleasePackage.ps1
    .\plugins\BlackFlame\tools\Make-ReleasePackage.ps1 -Language EN -AllowUnpushed
#>
[CmdletBinding()]
param(
    [ValidateSet('KO', 'EN', 'Both')]
    [string]$Language = 'Both',
    [switch]$AllowUnpushed
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$addon = Join-Path $repo 'plugins\BlackFlame'
$data = Join-Path $addon 'data'
$dll = Join-Path $repo 'build\plugins\BlackFlame\Release\HeartOfMagic_BlackFlame.dll'
$builder = Join-Path $repo 'build\tools\Release\blackflame-greybuild.exe'

# The build the user tested in game (2026-10-26, AE 1.6.1170, disk trim 6e968385): its commit and the sha256 of every binary and plugin shipped
$buildCommit = '6e96838530e351c54343f2124abe426632ffdc1c'
$tested = @{
    'dll'     = 'e95fa1fccdfbfec2f23c94bf5300b1aa1b3d305aed3b8a893823010fb234799b'
    'builder' = '00d23fe5f60e6b8774d49fadb123b62ffae35f553b35874f5bd45b15309f29f2'
    'slots'   = '368a2c5acff9d1cdef0c3b178da392ebd62047b88e1496f7efba37744b4b3972'
    'KO'      = '39972f63dd22f1e4d5dbb19e714efdde57c8933c394a9d80840a5b2795698595'
    'EN'      = 'f86fb13f098e5acc9576d8864fb2f4535fb9cd27a531ea0d0f74f9fc14ec6021'
}
$espByLanguage = @{
    'KO' = Join-Path $data 'HeartOfMagic_BlackFlame.esp'
    'EN' = Join-Path $addon 'localization\en\HeartOfMagic_BlackFlame.esp'
}
# The shipped config: the tested Dev mod's json with lanternLog off (one line per burning hit, never capped)
$releaseConfig = [ordered]@{
    'singleLaunch' = $true; 'launchBuilder' = $true; 'slotTxst' = $true; 'writeRequest' = $true
    'selfTest' = $false; 'testerMode' = $false; 'selfTestSlots' = $false; 'slotPreflight' = $false
    'devAutoE3' = $false; 'devAutoQuit' = $false; 'lanternLog' = $false
}
# Every file either binary is compiled from (and the build inputs that set its version and libraries)
$compiledInputs = @('plugins/BlackFlame/src', 'plugins/BlackFlame/CMakeLists.txt', 'plugins/Common.h', 'plugins/PCH.h',
    'plugins/WideFileSink.h', 'plugins/spelllearning', 'plugins/cmake', 'plugins/external', 'tools/blackflame-greybuild',
    'tools/CMakeLists.txt', 'CMakeLists.txt', 'CMakePresets.json', 'cmake', 'vcpkg.json', 'VERSION')

$noticesTemplate = Join-Path $PSScriptRoot 'THIRD-PARTY-NOTICES-RELEASE.md.in'
$commonlib = Join-Path $repo 'plugins\external\commonlibsse-ng'
$vcpkgShare = Join-Path $repo 'build\vcpkg_installed\x64-windows-static\share'
# port -> shown name, licence. DLL: CommonLib's own links (spdlog, DirectXTK) and the DLL's (nlohmann-json), spdlog's fmt
# (no xbyak and no zlib: neither one's strings are in HeartOfMagic_BlackFlame.dll)
$dllPorts = [ordered]@{ 'fmt' = @('{fmt}', 'MIT'); 'spdlog' = @('spdlog', 'MIT'); 'nlohmann-json' = @('JSON for Modern C++ (nlohmann-json)', 'MIT');
    'directxtk' = @('DirectX Tool Kit (DirectXTK)', 'MIT'); 'directxmath' = @('DirectXMath', 'MIT') }
$builderPorts = [ordered]@{ 'nifly' = @('nifly', 'GPL-3.0'); 'directxtex' = @('DirectXTex', 'MIT'); 'fmt' = @('{fmt}', 'MIT');
    'nlohmann-json' = @('JSON for Modern C++ (nlohmann-json)', 'MIT') }
$commonlibLicenses = @('COPYING.txt', 'EXCEPTIONS.md', 'licenses\LICENSE-MIT.txt')

function Fail([string]$why) {
    [Console]::Error.WriteLine("Make-ReleasePackage: $why")
    exit 1
}

function Sha256([string]$path) { (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant() }

function CheckHash([string]$path, [string]$expected, [string]$what) {
    if (-not (Test-Path $path)) { Fail "$what is missing: $path" }
    $actual = Sha256 $path
    if ($actual -ne $expected) { Fail "$what is not the tested file: $path has sha256 $actual, the tested one is $expected" }
}

# Clean tree: the notices name HEAD as the package's commit
$dirty = git -C $repo status --porcelain
if ($LASTEXITCODE -ne 0) { Fail "git status failed in $repo" }
if ($dirty) { Fail "the working tree has uncommitted changes: commit them first (the notices name the commit)" }
$commit = (git -C $repo rev-parse HEAD).Trim()
$version = (Get-Content (Join-Path $addon 'VERSION') -Raw).Trim()
if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { Fail "plugins\BlackFlame\VERSION holds '$version', not <major>.<minor>.<patch>" }

# The tested binaries and plugins, by hash (no timestamp heuristics: nothing is rebuilt for a release)
CheckHash $dll $tested.dll 'the plugin DLL'
CheckHash $builder $tested.builder 'the builder'
CheckHash (Join-Path $data 'HeartOfMagic_BlackFlame_Slots.esp') $tested.slots 'HeartOfMagic_BlackFlame_Slots.esp'
$languages = if ($Language -eq 'Both') { @('KO', 'EN') } else { @($Language) }
foreach ($lang in $languages) { CheckHash $espByLanguage[$lang] $tested[$lang] "the $lang HeartOfMagic_BlackFlame.esp" }

# The binaries' sources at HEAD are the build commit's
git -C $repo cat-file -e "$buildCommit^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) { Fail "the build commit $buildCommit is not in this repository" }
git -C $repo merge-base --is-ancestor $buildCommit $commit
if ($LASTEXITCODE -ne 0) { Fail "the build commit $buildCommit is not an ancestor of HEAD $commit" }
$changed = @(git -C $repo diff --name-only $buildCommit $commit -- @compiledInputs)
if ($LASTEXITCODE -ne 0) { Fail "git diff $buildCommit $commit failed" }
if ($changed) { Fail "compiled inputs changed since the build commit $buildCommit (the binaries would not match HEAD's source): $($changed -join ', ')" }

# Published: reachable from a branch or tag origin has now (fetched first; offline counts as not published)
$published = $false
$promptBefore = $env:GIT_TERMINAL_PROMPT
try {
    $env:GIT_TERMINAL_PROMPT = '0'
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
        Write-Host "Make-ReleasePackage: git fetch origin failed (offline or no access): the commit counts as not published"
    }
} finally {
    $env:GIT_TERMINAL_PROMPT = $promptBefore
}
if (-not $published -and -not $AllowUnpushed) {
    Fail "commit $commit is on no branch or tag of origin: push it first (the notices point to it), or pass -AllowUnpushed and push before uploading"
}

# The builder: a release identity of the DLL's kBuilderOutputVersion, its whole self-test passing, its version pinned
$identity = (& $builder --identity 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $identity) { Fail "$builder --identity failed (exit $LASTEXITCODE)" }
$keysHeader = Join-Path $addon 'src\CopyContractKeys.h'
$versionMatch = [regex]::Match((Get-Content $keysHeader -Raw), 'kBuilderOutputVersion = ([0-9]+);')
if (-not $versionMatch.Success) { Fail "no 'kBuilderOutputVersion = <n>;' in $keysHeader" }
$outputVersion = $versionMatch.Groups[1].Value
if ($identity -match '\+dev') { Fail "the builder's output identity is '$identity', a dev build" }
if ($identity -ne $outputVersion) { Fail "the builder's output identity '$identity' is not kBuilderOutputVersion $outputVersion" }
$golden = Join-Path $repo 'tools\blackflame-greybuild\golden'
$selfTest = @(& $builder --self-test --golden-dir $golden 2>$null)
$selfTestExit = $LASTEXITCODE
$selfTestFails = @($selfTest | Where-Object { $_ -match '^FAIL [^ ]' })
$selfTestSummary = $selfTest | Where-Object { $_ -match '^SELF-TEST ' } | Select-Object -Last 1
if ($selfTestExit -ne 0 -or $selfTestFails -or -not $selfTestSummary) {
    Fail "the builder's --self-test failed (exit $selfTestExit, $(if ($selfTestSummary) { $selfTestSummary } else { 'no summary line' })): $($selfTestFails -join ' | ')"
}
$pinned = @($selfTest | Where-Object { $_ -match "^PASS golden pin: version $outputVersion is pinned " })
if (-not $pinned) { Fail "output version $outputVersion has no released pin (GreyGoldenTable.h kReleasedPins)" }
Write-Host "builder: output identity $identity, --self-test $selfTestSummary"

# Licence sources and the CommonLibSSE-NG submodule at its tag
$licenceSources = @($noticesTemplate, (Join-Path $repo 'LICENSE')) + @($commonlibLicenses | ForEach-Object { Join-Path $commonlib $_ }) +
    @(@($dllPorts.Keys) + @($builderPorts.Keys) | Select-Object -Unique | ForEach-Object { Join-Path $vcpkgShare "$_\copyright" })
foreach ($file in $licenceSources) {
    if (-not (Test-Path $file)) { Fail "$file is missing (git submodule update --init --recursive; the vcpkg tree of the Release build)" }
}
function PortVersion([string]$port) {
    $spdx = Join-Path $vcpkgShare "$port\vcpkg.spdx.json"
    if (-not (Test-Path $spdx)) { Fail "$spdx is missing" }
    $entry = (Get-Content $spdx -Raw | ConvertFrom-Json).packages | Where-Object { $_.name -eq $port } | Select-Object -First 1
    if (-not $entry -or -not $entry.versionInfo) { Fail "$spdx names no version for $port" }
    return $entry.versionInfo
}
$commonlibDirty = git -C $commonlib status --porcelain
if ($LASTEXITCODE -ne 0) { Fail "git status failed in $commonlib" }
if ($commonlibDirty) { Fail "plugins\external\commonlibsse-ng has uncommitted changes" }
$commonlibVersion = (git -C $commonlib describe --tags --exact-match 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $commonlibVersion) { Fail "plugins\external\commonlibsse-ng is not at a tag (git submodule update --init --recursive)" }
$commonlibVersion = $commonlibVersion.Trim().TrimStart('v')
$commonlibCommit = (git -C $commonlib rev-parse HEAD).Trim()

# Only shipped, vanilla-derived files
if (Test-Path (Join-Path $data 'meshes\heartofmagic\blackflame\copies')) { Fail "plugins\BlackFlame\data holds generated copies (meshes\...\copies): remove them" }
if (Test-Path (Join-Path $data 'SKSE\Plugins\HeartOfMagic_BlackFlame_copies.json')) { Fail "plugins\BlackFlame\data holds a generated copies map" }

$out = Join-Path $repo "dist\BlackFlame-$version"
New-Item -ItemType Directory -Force $out | Out-Null
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

foreach ($lang in $languages) {
    $name = "HeartOfMagic-BlackFlame_${version}_$lang"
    $stage = Join-Path $out $name
    $zip = "$stage.zip"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    if (Test-Path $zip) { Remove-Item -Force $zip }
    New-Item -ItemType Directory -Force $stage | Out-Null

    Copy-Item $espByLanguage[$lang] (Join-Path $stage 'HeartOfMagic_BlackFlame.esp')
    Copy-Item (Join-Path $data 'HeartOfMagic_BlackFlame_Slots.esp') $stage
    foreach ($folder in 'SKSE', 'meshes', 'textures') { Copy-Item -Recurse (Join-Path $data $folder) $stage }
    $plugins = Join-Path $stage 'SKSE\Plugins'
    Copy-Item $dll $plugins
    $builderDir = Join-Path $plugins 'HeartOfMagic_BlackFlame'
    New-Item -ItemType Directory -Force $builderDir | Out-Null
    Copy-Item $builder $builderDir

    # The config: the repository's json with the release keys set, then read back key by key
    $configPath = Join-Path $plugins 'HeartOfMagic_BlackFlame.json'
    $config = Get-Content $configPath -Raw | ConvertFrom-Json
    foreach ($key in $releaseConfig.Keys) { $config | Add-Member -NotePropertyName $key -NotePropertyValue $releaseConfig[$key] -Force }
    $config | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8NoBOM $configPath
    $readBack = Get-Content $configPath -Raw | ConvertFrom-Json
    foreach ($key in $releaseConfig.Keys) {
        if ($readBack.$key -ne $releaseConfig[$key]) { Fail "the staged config's $key is '$($readBack.$key)', not '$($releaseConfig[$key])'" }
    }

    $readme = Join-Path $addon "docs\README_$lang.md"
    if (-not (Test-Path $readme)) { Fail "$readme is missing" }
    Copy-Item $readme $stage
    Copy-Item (Join-Path $repo 'LICENSE') $stage

    # Licence texts from their origins, the notice filled in for this commit
    $stageLicenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force (Join-Path $stageLicenses 'CommonLibSSE-NG') | Out-Null
    foreach ($file in $commonlibLicenses) { Copy-Item (Join-Path $commonlib $file) (Join-Path $stageLicenses 'CommonLibSSE-NG') }
    $rows = {
        param($ports)
        ($ports.Keys | ForEach-Object {
            Copy-Item (Join-Path $vcpkgShare "$_\copyright") (Join-Path $stageLicenses "$_.txt") -Force
            '| {0} | {1} | {2} | `licenses/{3}.txt` |' -f $ports[$_][0], (PortVersion $_), $ports[$_][1], $_
        }) -join "`n"
    }
    $commonlibSource = ('CommonLibSSE-NG is the repository''s git submodule `plugins/external/commonlibsse-ng` at tag `v{0}` ' +
        '(commit `{1}`, <https://github.com/alandtse/CommonLibVR>, branch `ng`); `git submodule update --init --recursive` fetches it.') -f
        $commonlibVersion, $commonlibCommit
    $text = (Get-Content $noticesTemplate -Raw).Replace('@VERSION@', $version).Replace('@LANGUAGE@', $lang).
        Replace('@COMMIT@', $commit).Replace('@BUILD_COMMIT@', $buildCommit).Replace('@COMMONLIB_VERSION@', $commonlibVersion).
        Replace('@COMMONLIB_SOURCE@', $commonlibSource).Replace('@DLL_ROWS@', (& $rows $dllPorts)).Replace('@BUILDER_ROWS@', (& $rows $builderPorts))
    if ($text -match '@[A-Z_]+@') { Fail "THIRD-PARTY-NOTICES-RELEASE.md.in has a placeholder the script does not fill: $($Matches[0])" }
    Set-Content -Encoding utf8NoBOM (Join-Path $stage 'THIRD-PARTY-NOTICES.md') $text

    # Guards: only the expected top level, nothing generated, private or debug, the tested hashes in the stage
    $expectedTop = @('HeartOfMagic_BlackFlame.esp', 'HeartOfMagic_BlackFlame_Slots.esp', 'SKSE', 'meshes', 'textures',
        "README_$lang.md", 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'licenses')
    $extra = @(Get-ChildItem $stage | Where-Object { $_.Name -notin $expectedTop })
    if ($extra) { Fail "unexpected entries in the package: $($extra.Name -join ', ')" }
    $files = @(Get-ChildItem -Recurse -File $stage)
    $bad = @($files | Where-Object { $_.FullName -match '\\(copies|source|lab|requests)\\' -or $_.Name -match '(?i)takealook|\.pdb$|\.zip$|_copies\.json$' })
    if ($bad) { Fail "files that must not ship are in the package: $($bad.FullName -join ', ')" }
    CheckHash (Join-Path $plugins 'HeartOfMagic_BlackFlame.dll') $tested.dll 'the staged DLL'
    CheckHash (Join-Path $builderDir 'blackflame-greybuild.exe') $tested.builder 'the staged builder'
    CheckHash (Join-Path $stage 'HeartOfMagic_BlackFlame_Slots.esp') $tested.slots 'the staged Slots ESP'
    CheckHash (Join-Path $stage 'HeartOfMagic_BlackFlame.esp') $tested[$lang] "the staged $lang ESP"

    # The manifest: what is in the zip, with sizes and hashes
    $manifestFiles = [ordered]@{}
    foreach ($file in $files | Sort-Object FullName) {
        $rel = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
        $manifestFiles[$rel] = [ordered]@{ 'bytes' = $file.Length; 'sha256' = (Sha256 $file.FullName) }
    }
    $manifest = [ordered]@{ 'name' = 'Heart of Magic - Black Flame'; 'version' = $version; 'language' = $lang; 'commit' = $commit;
        'buildCommit' = $buildCommit; 'source' = "https://github.com/Iwillbewarlock/HeartOfMagic/tree/$commit"; 'files' = $manifestFiles }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8NoBOM (Join-Path $stage 'PACKAGE-MANIFEST.json')

    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $zipHash = Sha256 $zip
    Set-Content -Encoding ascii "$zip.sha256" "$zipHash  $(Split-Path $zip -Leaf)"
    Write-Host "wrote $zip ($($files.Count + 1) files, sha256 $zipHash)"
}
if (-not $published) {
    Write-Host ""
    Write-Host "WARNING: commit $commit is not on origin yet. The notices point to it: push it (and tag it) before uploading."
}
