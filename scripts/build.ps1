param([string]$QtRoot = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$QtRoot) { $QtRoot = Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64' }
if (!(Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) { throw 'Qt 6 SDK not found. See README.md.' }
$buildDir = Join-Path $projectRoot 'build'
cmake -S $projectRoot -B $buildDir -G 'Visual Studio 18 2026' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build $buildDir --config Release --parallel 8
if ($LASTEXITCODE) { throw 'Build failed' }
if (!$SkipTests) {
    $savedPath = $env:PATH
    try {
        $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
        $testOutput = (Join-Path $buildDir 'test-results.txt') + ',txt'
        & (Join-Path $buildDir 'Release\zex_tests.exe') -o $testOutput
        if ($LASTEXITCODE) { Get-Content (Join-Path $buildDir 'test-results.txt'); throw 'Tests failed' }
    } finally {
        $env:PATH = $savedPath
        Remove-Item Env:\QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
    }
    Get-Content (Join-Path $buildDir 'test-results.txt')
}
& (Join-Path $PSScriptRoot 'package.ps1') -QtRoot $QtRoot
