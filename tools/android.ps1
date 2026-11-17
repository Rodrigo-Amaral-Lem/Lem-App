# SPDX-License-Identifier: GPL-3.0-only
# Usage: .\tools\android.ps1 build | deploy [--options]
# Arguments intentionally pass through unchanged to the shared Python driver.
$ErrorActionPreference = 'Stop'
$forwardArgs = @($args)

function Find-LemPython {
    $candidates = @($env:LEM_PYTHON)
    foreach ($name in @('python', 'python3')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { $candidates += $command.Source }
    }
    $roots = @("$env:LOCALAPPDATA\Programs\Python", "$env:ProgramFiles\Python")
    foreach ($root in $roots) {
        if (Test-Path -LiteralPath $root) {
            $candidates += @(Get-ChildItem -LiteralPath $root -Filter 'python.exe' -Recurse -ErrorAction SilentlyContinue | ForEach-Object FullName)
        }
    }
    foreach ($candidate in $candidates) {
        if (-not $candidate -or -not (Test-Path -LiteralPath $candidate)) { continue }
        # Do not launch Windows Store aliases, which can open an interactive Store window.
        if ($candidate -match '\\WindowsApps\\') { continue }
        try {
            & $candidate -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' 2>$null
            if ($LASTEXITCODE -eq 0) { return $candidate }
        } catch { }
    }
    $launcher = Get-Command py -ErrorAction SilentlyContinue
    if ($launcher) {
        try {
            $path = & $launcher.Source -3 -c 'import sys; print(sys.executable if sys.version_info >= (3,10) else "")' 2>$null
            if ($LASTEXITCODE -eq 0 -and $path -and (Test-Path -LiteralPath $path)) { return $path }
        } catch { }
    }
    return $null
}

$python = Find-LemPython
if (-not $python) {
    if ($forwardArgs -contains '--no-bootstrap') { throw 'Python 3.10+ is missing. Set LEM_PYTHON to an installed interpreter.' }
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) { throw 'Install Microsoft App Installer (winget) or Python 3.10+, then rerun. You can set LEM_PYTHON to python.exe.' }
    & $winget.Source install --id Python.Python.3.12 --exact --source winget --accept-source-agreements --accept-package-agreements --disable-interactivity
    if ($LASTEXITCODE -ne 0) { throw "Python installation failed ($LASTEXITCODE)." }
    $python = Find-LemPython
    if (-not $python) { throw 'Python was installed but could not be found. Open a fresh terminal or set LEM_PYTHON.' }
}
& $python (Join-Path $PSScriptRoot 'android.py') @forwardArgs
exit $LASTEXITCODE
