param([string]$PackageDir = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$PackageDir) { $PackageDir = Join-Path $projectRoot 'package' }
$exe = Join-Path $PackageDir 'Zex.exe'
$artifacts = Join-Path $projectRoot 'artifacts'
$fixtureDir = Join-Path $artifacts 'instance-fixtures'
New-Item -ItemType Directory -Force $fixtureDir | Out-Null
$utf8 = [System.Text.UTF8Encoding]::new($false)
$names = @('first.txt', '中文 文件.cpp', 'third.json')
foreach ($name in $names) { [System.IO.File]::WriteAllText((Join-Path $fixtureDir $name), "// 中文 file`nint value = 42;`n", $utf8) }
function Start-Zex([string[]]$Arguments, [string]$Directory = $fixtureDir) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $exe
    $start.Arguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $start.WorkingDirectory = $Directory
    $start.UseShellExecute = $false
    $start.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $start.EnvironmentVariables.Remove('QT_PLUGIN_PATH')
    $start.EnvironmentVariables.Remove('QT_QPA_PLATFORM_PLUGIN_PATH')
    return [System.Diagnostics.Process]::Start($start)
}
$channel = [guid]::NewGuid().ToString('N')
$report = Join-Path $artifacts "instance-$channel.json"
$ready = Join-Path $artifacts "instance-$channel.ready"
$primary = Start-Zex @('--verify-instance',$channel,'--verify-package',$report,'--verify-ready',$ready,'--verify-wait-ms','4500')
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $ready) -and [DateTime]::UtcNow -lt $deadline -and !$primary.HasExited) { Start-Sleep -Milliseconds 40 }
    if (!(Test-Path -LiteralPath $ready)) { throw 'Receiver did not become ready' }
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $batch = Start-Zex (@('--verify-instance',$channel,'--') + $names)
    if (!$batch.WaitForExit(12000) -or $batch.ExitCode -ne 0) { throw 'Batch forwarding failed' }
    $initialForwardMs = $watch.ElapsedMilliseconds
    $watch.Restart()
    $duplicate = Start-Zex @('--verify-instance',$channel,'--','中文 文件.cpp','FIRST.TXT')
    if (!$duplicate.WaitForExit(5000) -or $duplicate.ExitCode -ne 0) { throw 'Duplicate forwarding failed' }
    $warmForwardMs = $watch.ElapsedMilliseconds
    $activate = Start-Zex @('--verify-instance',$channel)
    if (!$activate.WaitForExit(5000) -or $activate.ExitCode -ne 0) { throw 'Window activation failed' }
    if (!$primary.WaitForExit(15000) -or $primary.ExitCode -ne 0) { throw 'Primary process failed' }
    $result = Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($result.pid -ne $primary.Id -or !$result.single_instance -or $result.tabs -ne 3 -or !$result.ready) { throw 'Single-instance report mismatch' }
    for ($i=0; $i -lt 3; ++$i) {
        if ($result.session.tabs[$i].path -ne (Join-Path $fixtureDir $names[$i]).Replace('\','/')) { throw 'Forwarded path/order mismatch' }
    }
    if ($result.file -ne (Join-Path $fixtureDir $names[0]).Replace('\','/')) { throw 'Duplicate did not focus existing tab' }
    # The channel becomes reusable as soon as the owner exits.
    $restartReport = Join-Path $artifacts "instance-restart-$channel.json"
    $restart = Start-Zex @('--verify-instance',$channel,'--verify-package',$restartReport,'--',$names[2])
    if (!$restart.WaitForExit(10000) -or $restart.ExitCode -ne 0) { throw 'Instance restart failed' }
    $restarted = Get-Content -LiteralPath $restartReport -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($restarted.tabs -ne 1 -or !$restarted.single_instance) { throw 'Instance ownership was not released' }
    # Any one of simultaneous launches may become the owner; every path must arrive there.
    $racers = @(); $raceReports = @(); $raceChannel = [guid]::NewGuid().ToString('N')
    try {
        for ($i=0; $i -lt 3; ++$i) {
            $raceReport = Join-Path $artifacts "race-$raceChannel-$i.json"; $raceReports += $raceReport
            $racers += Start-Zex @('--verify-instance',$raceChannel,'--verify-package',$raceReport,'--verify-wait-ms','3000','--',$names[$i])
        }
        foreach ($racer in $racers) { if (!$racer.WaitForExit(15000) -or $racer.ExitCode -ne 0) { throw 'Concurrent launch failed' } }
        $written = @($raceReports | Where-Object { Test-Path -LiteralPath $_ })
        if ($written.Count -ne 1) { throw 'Concurrent launches created multiple owners' }
        $race = Get-Content -LiteralPath $written[0] -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($race.tabs -ne 3 -or !$race.ready) { throw 'Concurrent launch lost a file' }
    } finally { foreach ($racer in $racers) { if (!$racer.HasExited) { $racer.Kill(); $racer.WaitForExit() } } }
    $crashChannel = [guid]::NewGuid().ToString('N'); $crashReady = Join-Path $artifacts "crash-$crashChannel.ready"
    $crash = Start-Zex @('--verify-instance',$crashChannel,'--verify-ready',$crashReady)
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while (!(Test-Path -LiteralPath $crashReady) -and [DateTime]::UtcNow -lt $deadline -and !$crash.HasExited) { Start-Sleep -Milliseconds 40 }
        if (!(Test-Path -LiteralPath $crashReady)) { throw 'Crash-recovery fixture did not become ready' }
    } finally { if (!$crash.HasExited) { $crash.Kill(); $crash.WaitForExit() } }
    $recoveryReport = Join-Path $artifacts "recovery-$crashChannel.json"
    $recovery = Start-Zex @('--verify-instance',$crashChannel,'--verify-package',$recoveryReport,'--',$names[0])
    if (!$recovery.WaitForExit(10000) -or $recovery.ExitCode -ne 0) { throw 'Abandoned instance lock was not recovered' }
    [pscustomobject]@{initial_forward_ms=$initialForwardMs;warm_forward_ms=$warmForwardMs;tabs=$result.tabs;restart=$true;simultaneous_launches=3;crash_recovery=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'instance-verification.json') -Encoding UTF8
    Write-Output "Single-instance checks passed; warm forwarding: $warmForwardMs ms"
} finally {
    if (!$primary.HasExited) { $primary.Kill(); $primary.WaitForExit() }
}
