# Stop hook (see .claude/settings.json): runs when Claude finishes a response.
# Builds AutoDraft silently if sources changed since the last good build and refreshes the
# stand-alone run folder (AutoDraft\). Output of this script is read by Claude, so the
# messages are English; Claude reports to the user in Korean.
#
# Exit codes:  0 = nothing to say (built, or nothing to do)
#              2 = Claude must continue (build error to fix, or app running so the new
#                  version could not be applied) - stderr is shown to Claude.

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$buildDir = Join-Path $root 'build'
New-Item -ItemType Directory -Force $buildDir | Out-Null
$stamp    = Join-Path $buildDir '.last_ok'
$pending  = Join-Path $buildDir '.deploy_pending'
$notified = Join-Path $buildDir '.deploy_notified'
$attempts = Join-Path $buildDir '.auto_build_attempts'
$log      = Join-Path $buildDir 'auto_build.log'
$exe      = Join-Path $root 'AutoDraft\AutoDraft.exe'

# The hook receives JSON on stdin; we do not need it.
try { [Console]::In.ReadToEnd() | Out-Null } catch { }

function Fail([string]$message) {
    [Console]::Error.WriteLine($message)
    exit 2
}

# 1) No Qt yet (setup.bat not run): do nothing.
cmd.exe /c "`"$root\scripts\env.bat`"" | Out-Null
if ($LASTEXITCODE -ne 0) { exit 0 }

# 2) Give up retrying after repeated failures (reset after 30 minutes or on success).
$failCount = 0
if (Test-Path $attempts) {
    if (((Get-Date) - (Get-Item $attempts).LastWriteTime).TotalMinutes -gt 30) {
        Remove-Item $attempts -Force
    } else {
        $failCount = [int](Get-Content $attempts -ErrorAction SilentlyContinue)
    }
}
if ($failCount -ge 3) { exit 0 }

# 3) Did the sources change since the last good build?
$files = @()
foreach ($pattern in '*.cpp', '*.h', '*.pro', '*.qrc', '*.rc') {
    $files += Get-ChildItem -Path $root -Filter $pattern -File
}
foreach ($dir in 'icons', 'help', 'translations') {
    if (Test-Path (Join-Path $root $dir)) { $files += Get-ChildItem (Join-Path $root $dir) -Recurse -File }
}
$newest = ($files | Measure-Object LastWriteTime -Maximum).Maximum
$lastOk = [datetime]::MinValue
if (Test-Path $stamp) { $lastOk = (Get-Item $stamp).LastWriteTime }
$sourcesChanged = ($newest -gt $lastOk) -or -not (Test-Path $exe)

# 4) Nothing changed: only finish a postponed deploy if the app has been closed.
if (-not $sourcesChanged) {
    if (Test-Path $pending) {
        cmd.exe /c "`"$root\scripts\deploy.bat`"" | Out-Null
        if ($LASTEXITCODE -eq 0) { Remove-Item $notified -Force -ErrorAction SilentlyContinue }
    }
    exit 0
}

# 5) Build (release + run folder).
cmd.exe /c "`"$root\build.bat`"" 2>&1 | Out-File -Encoding utf8 $log
$rc = $LASTEXITCODE

if ($rc -eq 0) {
    (Get-Date) | Out-File $stamp
    (Get-Item $stamp).LastWriteTime = Get-Date
    Remove-Item $attempts, $notified -Force -ErrorAction SilentlyContinue
    exit 0
}

if ($rc -eq 3) {
    # Compiled fine, but AutoDraft.exe is running so the run folder was not updated.
    (Get-Date) | Out-File $stamp
    if (-not (Test-Path $notified)) {
        'notified' | Out-File $notified
        Fail ("The build succeeded, but AutoDraft is currently running, so the new version was NOT applied yet. " +
              "Tell the user (in Korean, without technical terms) to close AutoDraft; the new version is applied automatically the next time you finish a task.")
    }
    exit 0
}

# Build error: send the error lines to Claude so it can fix them.
$failCount++
"$failCount" | Out-File $attempts
$lines = Get-Content $log
$errors = $lines | Where-Object { $_ -match 'error' -and $_ -notmatch '-Werror' } | Select-Object -Last 40
if (-not $errors) { $errors = $lines | Select-Object -Last 20 }
$text = ($errors | ForEach-Object { $_.Substring(0, [Math]::Min(300, $_.Length)) }) -join "`n"
if ($failCount -ge 3) {
    Fail ("The automatic build failed 3 times; automatic retries stop now. Do not hide this: tell the user in plain Korean that the latest change could not be built and what you tried.`n" + $text)
}
Fail ("The automatic build FAILED (attempt $failCount of 3). Fix the cause in the source, then finish your answer; the build runs again automatically. Do not mention build details to the user unless it keeps failing.`n" + $text)
