$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $projectRoot 'package'
$artifactsDir = Join-Path $projectRoot 'artifacts'
$portableDir = Join-Path $artifactsDir 'portable-check\中文 路径'
New-Item -ItemType Directory -Force $portableDir | Out-Null
Get-ChildItem -LiteralPath $packageDir | Copy-Item -Destination $portableDir -Recurse -Force
$fixturesDir = Join-Path $artifactsDir 'fixtures'
New-Item -ItemType Directory -Force $fixturesDir | Out-Null
$utf16 = Join-Path $fixturesDir '中文 文件.txt'
[System.IO.File]::WriteAllText($utf16, "中文搜索测试`r`nhello Zex`r`n中文", [System.Text.Encoding]::Unicode)
$large = Join-Path $fixturesDir 'large.log'
[System.IO.File]::WriteAllText($large, ('2026-10-30 INFO Zex sample log entry with enough text for a long file.' + "`n") * 150000, [System.Text.UTF8Encoding]::new($false))
function Invoke-PackageCheck([string]$name, [string[]]$files, [string]$find = '', [switch]$Capture, [string]$Session = '', [int]$Line = 0, [string]$Theme = '', [switch]$Appearance, [string]$TextEncoding = '', [switch]$Integration) {
    $report = Join-Path $artifactsDir ($name + '.json')
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $portableDir 'Zex.exe'
    $arguments = @('--verify-package', ('"' + $report + '"'))
    if ($Capture) { $arguments += @('--capture', ('"' + (Join-Path $artifactsDir ($name + '.png')) + '"')) }
    if ($find) { $arguments += @('--find', ('"' + $find + '"')) }
    if ($Session) { $arguments += @('--verify-session', ('"' + $Session + '"')) }
    if ($Line -gt 0) { $arguments += @('--verify-line', [string]$Line) }
    if ($Theme) { $arguments += @('--verify-theme', $Theme) }
    if ($Appearance) { $arguments += @('--capture-appearance', ('"' + (Join-Path $artifactsDir ("appearance-$Theme.png")) + '"')) }
    if ($TextEncoding) { $arguments += @('--verify-encoding', $TextEncoding) }
    if ($Integration) { $arguments += @('--verify-integration-capture', ('"' + (Join-Path $artifactsDir ("integration-$Theme.png")) + '"')) }
    foreach ($path in $files) { $arguments += '"' + $path + '"' }
    $start.Arguments = $arguments -join ' '
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $env:TEMP
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $start.EnvironmentVariables.Remove('QT_PLUGIN_PATH')
    $start.EnvironmentVariables.Remove('QT_QPA_PLATFORM_PLUGIN_PATH')
    $process = [System.Diagnostics.Process]::Start($start)
    if (!$process.WaitForExit(35000)) { $process.Kill(); throw "Timed out: $name" }
    if ($process.ExitCode -ne 0) { throw "Failed: $name (exit $($process.ExitCode))" }
    $result = Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$result.ready) { throw "Not ready: $name" }
    return $result
}
$results = @()
$results += Invoke-PackageCheck -name 'welcome' -files @() -Capture
$results += Invoke-PackageCheck -name 'editor' -files @((Join-Path $projectRoot 'examples\settings.json'),(Join-Path $projectRoot 'examples\hello.cpp')) -find 'document' -Capture
if ($results[-1].tabs -ne 2 -or $results[-1].search_matches -ne 11) { throw 'Packaged tab/search check failed' }
$results += Invoke-PackageCheck -name 'unicode-path' -files @($utf16) -find '中文'
if ($results[-1].encoding -ne 'UTF-16 LE' -or $results[-1].search_matches -ne 2) { throw 'Unicode file check failed' }
$results += Invoke-PackageCheck -name 'large-file' -files @($large)
if ($results[-1].language -notlike '*大文件模式*') { throw 'Large-file mode check failed' }
# Cross-process persistence uses an isolated session, never the real user's preferences.
$readingFile = Join-Path $fixturesDir '阅读 位置.cpp'
[System.IO.File]::WriteAllText($readingFile, ("// 中文 reading position, line with enough columns`n" * 300), [System.Text.UTF8Encoding]::new($false))
$sessionFile = Join-Path $fixturesDir 'restart-session.json'
[System.IO.File]::WriteAllText($sessionFile, '{"version":1,"tabs":[],"activePath":""}', [System.Text.UTF8Encoding]::new($false))
$sessionFiles = @((Join-Path $projectRoot 'examples\settings.json'), $large, $readingFile)
$results += Invoke-PackageCheck -name 'session-save' -files $sessionFiles -Session $sessionFile -Line 120
if (!$results[-1].session_saved -or $results[-1].loaded_tabs -ne 1) { throw 'Session save/lazy loading failed' }
$results += Invoke-PackageCheck -name 'session-restore' -files @() -Session $sessionFile -Capture
$restored = $results[-1]
if ($restored.tabs -ne 3 -or $restored.loaded_tabs -ne 1 -or $restored.session.tabs[2].position.topLine -ne 119 -or $restored.session.tabs[2].position.caretLine -ne 119) { throw 'Cross-process reading position failed' }
for ($index = 0; $index -lt $sessionFiles.Count; ++$index) {
    if ($restored.session.tabs[$index].path -ne $sessionFiles[$index].Replace('\','/')) { throw 'Restored file order mismatch' }
}
$results += Invoke-PackageCheck -name 'session-explicit-file' -files @($sessionFiles[0]) -Session $sessionFile
if ($results[-1].tabs -ne 3 -or $results[-1].loaded_tabs -ne 1 -or $results[-1].file -ne $sessionFiles[0].Replace('\','/')) { throw 'Explicit file focus/deduplication failed' }

# Same foreground document, with/without 31 dormant log files. Repeat to reduce timing noise.
$manySession = Join-Path $fixturesDir 'many-tabs-session.json'
$manyTabs = @()
for ($index = 0; $index -lt 31; ++$index) {
    $background = Join-Path $fixturesDir ('background-{0:D2}.log' -f $index)
    [System.IO.File]::WriteAllText($background, ("Background log entry for deferred session loading.`n" * 21000), [System.Text.UTF8Encoding]::new($false))
    $manyTabs += @{path=$background.Replace('\','/');position=@{topLine=100+$index;caretLine=110+$index}}
}
$manyTabs += @{path=$readingFile.Replace('\','/');position=@{topLine=119;caretLine=119}}
@{version=1;activePath=$readingFile.Replace('\','/');tabs=$manyTabs} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manySession -Encoding UTF8
for ($iteration = 1; $iteration -le 3; ++$iteration) {
    $results += Invoke-PackageCheck -name "startup-single-$iteration" -files @($readingFile)
    $results += Invoke-PackageCheck -name "startup-32-tabs-$iteration" -files @() -Session $manySession
    if ($results[-1].tabs -ne 32 -or $results[-1].loaded_tabs -ne 1) { throw 'Background restored files were eagerly loaded' }
    if ($results[-1].session.tabs[0].position.topLine -ne 100) { throw 'Dormant reading position was lost' }
}
foreach ($themeId in @('graphite','midnight','north','violet','paper','sand')) {
    $results += Invoke-PackageCheck -name "theme-$themeId" -files @((Join-Path $projectRoot 'examples\settings.json'),(Join-Path $projectRoot 'examples\hello.cpp')) -find 'document' -Theme $themeId -Capture -Appearance
    if ($results[-1].theme -ne $themeId -or $results[-1].search_matches -ne 11 -or !$results[-1].screenshot_saved -or !$results[-1].appearance_saved) { throw "Theme verification failed: $themeId" }
}
$results | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $artifactsDir 'package-verification.json') -Encoding UTF8
foreach ($themeId in @('graphite','paper')) {
    $results += Invoke-PackageCheck -name "integration-$themeId" -files @() -Theme $themeId -Integration
    if (!$results[-1].integration_saved) { throw 'Integration dialog capture failed' }
}
$manualFile = Join-Path $fixturesDir '无 BOM 编码.txt'
[System.IO.File]::WriteAllText($manualFile, ("中文 encoding test`n" * 60), [System.Text.UnicodeEncoding]::new($false,$false))
$beforeHash = (Get-FileHash -LiteralPath $manualFile -Algorithm SHA256).Hash
$encodingSession = Join-Path $fixturesDir 'encoding-session.json'
[System.IO.File]::WriteAllText($encodingSession, '{"version":1,"tabs":[],"activePath":""}', [System.Text.UTF8Encoding]::new($false))
$results += Invoke-PackageCheck -name 'manual-encoding-save' -files @($manualFile) -TextEncoding 'utf-16le' -Session $encodingSession -find '中文' -Capture
if ($results[-1].encoding -ne 'UTF-16 LE' -or $results[-1].search_matches -ne 60 -or $results[-1].session.tabs[0].encoding -ne 'utf-16le') { throw 'Manual decoding failed' }
$results += Invoke-PackageCheck -name 'manual-encoding-restore' -files @() -Session $encodingSession -find '中文'
if ($results[-1].encoding -ne 'UTF-16 LE' -or $results[-1].search_matches -ne 60) { throw 'Manual encoding was not restored across processes' }
if ((Get-FileHash -LiteralPath $manualFile -Algorithm SHA256).Hash -ne $beforeHash) { throw 'Encoding switch modified the file' }
& (Join-Path $PSScriptRoot 'verify-integration.ps1') -PackageDir $portableDir
if (!$?) { throw 'Single-instance verification failed' }
$results | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $artifactsDir 'package-verification.json') -Encoding UTF8
$results | Select-Object file,tabs,encoding,language,search_matches,startup_to_ready_ms | Format-Table -AutoSize
$version = (Get-Item -LiteralPath (Join-Path $packageDir 'Zex.exe')).VersionInfo
if ($version.CompanyName -ne 'sixunited' -or $version.FileVersion -ne '0.5.1.0' -or $version.OriginalFilename -ne 'Zex.exe' -or $version.LegalCopyright -ne 'Copyright (c) 2026 sixunited. Author: sclliang') { throw 'VERSIONINFO mismatch' }
if ($version.ProductName -ne 'Zex' -or $version.InternalName -ne 'Zex' -or $version.FileDescription -ne 'Zex File and Code Viewer' -or $version.ProductVersion -ne '0.5.1.0' -or $version.FileMajorPart -ne 0 -or $version.FileMinorPart -ne 5 -or $version.FileBuildPart -ne 1 -or $version.FilePrivatePart -ne 0) { throw 'Product or numeric version mismatch' }
$distDir = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Force $distDir | Out-Null
$zip = Join-Path $distDir 'Zex-0.5.1-win-x64.zip'
Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Format-List | Out-String | Set-Content -LiteralPath ($zip + '.sha256.txt')
Write-Output "Verified portable ZIP: $zip"
