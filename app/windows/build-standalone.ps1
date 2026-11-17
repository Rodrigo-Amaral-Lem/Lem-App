# SPDX-License-Identifier: GPL-3.0-only
# Works from any directory. Generated files default to app/build/windows-static.
[CmdletBinding()]
param(
    [string]$QtPrefix, [switch]$BuildQt, [string]$QtSource, [string]$QtBuild,
    [string]$QtRoot,
    [ValidateSet('MinGW','MSVC')][string]$Compiler = 'MinGW',
    [string]$ToolchainBin, [string]$BuildDirectory,
    [ValidateRange(1,128)][int]$Jobs = 4, [switch]$Plan
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This script must run on Windows.' }
# Never launch a Windows Store alias while looking for Python.
$candidates = @($env:LEM_PYTHON)
foreach ($name in @('python.exe', 'python3.exe')) {
    $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $candidates += $command.Source }
}
foreach ($root in @("$env:LOCALAPPDATA\Programs\Python", "$env:LOCALAPPDATA\Python", "$env:USERPROFILE\AppData\Local\Programs\Python", "$env:USERPROFILE\AppData\Local\Python", "$env:ProgramFiles\Python")) {
    if (Test-Path -LiteralPath $root) {
        $candidates += @(Get-ChildItem -LiteralPath $root -Filter python.exe -Recurse -ErrorAction SilentlyContinue | ForEach-Object FullName)
    }
}
$python = $null
foreach ($candidate in $candidates) {
    if (-not $candidate -or $candidate -match '\\WindowsApps\\' -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    try {
        & $candidate -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)'
        if ($LASTEXITCODE -eq 0) { $python = $candidate; break }
    } catch { }
}
if (-not $python) {
    $launcher = Get-Command py.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($launcher -and $launcher.Source -notmatch '\\WindowsApps\\') {
        try {
            $candidate = & $launcher.Source -3 -c 'import sys; print(sys.executable if sys.version_info >= (3,10) else "")'
            if ($LASTEXITCODE -eq 0 -and $candidate -and (Test-Path -LiteralPath $candidate)) { $python = $candidate }
        } catch { }
    }
}
if (-not $python) { throw 'Python 3.10+ is required. Set LEM_PYTHON to an installed python.exe.' }
$forward = @('--compiler', $Compiler, '--jobs', "$Jobs")
foreach ($entry in @(
    @('--qt-prefix', $QtPrefix), @('--qt-source', $QtSource), @('--qt-build', $QtBuild),
    @('--qt-root', $QtRoot), @('--toolchain-bin', $ToolchainBin), @('--build-directory', $BuildDirectory)
)) {
    if ($entry[1]) { $forward += $entry }
}
if ($BuildQt) { $forward += '--build-qt' }
if ($Plan) { $forward += '--plan' }
& $python (Join-Path $PSScriptRoot 'build_standalone.py') @forward
if ($LASTEXITCODE -ne 0) { throw "Windows static build failed (exit $LASTEXITCODE). See the reported build log." }
