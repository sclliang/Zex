param([Parameter(Mandatory=$true)][string]$QtRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageDir = Join-Path $projectRoot 'package'
$binDir = Join-Path $packageDir 'bin'
New-Item -ItemType Directory -Force $binDir | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\Release\Zex.exe') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\Release\zex_app.dll') -Destination $binDir -Force
& (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --no-opengl-sw --no-system-d3d-compiler --dir $binDir --libdir $binDir --plugindir (Join-Path $binDir 'plugins') (Join-Path $binDir 'zex_app.dll')
if ($LASTEXITCODE) { throw 'Qt deployment failed' }
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -property installationPath
$crtDir = Get-ChildItem -Path (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory | Where-Object Name -Match '^\d' | Sort-Object Name -Descending | Select-Object -First 1
$crt = Join-Path $crtDir.FullName 'x64\Microsoft.VC145.CRT'
if (!(Test-Path -LiteralPath $crt)) { $crt = Join-Path $crtDir.FullName 'x64\Microsoft.VC143.CRT' }
if (!(Test-Path -LiteralPath $crt)) { throw 'MSVC app-local CRT not found' }
Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $binDir -Force
$licensesDir = Join-Path $binDir 'licenses'
New-Item -ItemType Directory -Force $licensesDir | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'vendor\scintilla\License.txt') -Destination (Join-Path $licensesDir 'Scintilla.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'vendor\lexilla\License.txt') -Destination (Join-Path $licensesDir 'Lexilla.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'vendor\pcre2\LICENCE.md') -Destination (Join-Path $licensesDir 'PCRE2.md') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.txt') -Destination $licensesDir -Force
if (Test-Path (Join-Path $QtRoot 'LICENSES')) { Copy-Item -LiteralPath (Join-Path $QtRoot 'LICENSES') -Destination (Join-Path $licensesDir 'Qt') -Recurse -Force }
$qtNotices = Join-Path $projectRoot 'vendor\qt-notices'
if (!(Test-Path -LiteralPath $qtNotices)) { throw 'Qt notices are missing. Run scripts/collect-qt-notices.py first.' }
$qtLicenseDir = Join-Path $licensesDir 'Qt'
New-Item -ItemType Directory -Force $qtLicenseDir | Out-Null
Get-ChildItem -LiteralPath $qtNotices | Copy-Item -Destination $qtLicenseDir -Recurse -Force
$iconNotices = Join-Path $projectRoot 'vendor\icon-notices'
if (!(Test-Path -LiteralPath $iconNotices)) { throw 'Icon library notices are missing.' }
$iconLicenseDir = Join-Path $licensesDir 'FileIcons'
New-Item -ItemType Directory -Force $iconLicenseDir | Out-Null
Get-ChildItem -LiteralPath $iconNotices | Copy-Item -Destination $iconLicenseDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'USER_GUIDE.txt') -Destination $packageDir -Force
Write-Output "Package ready: $packageDir"
