# Build native static Qt once, then link and validate a single portable Lem.exe.
# Run from a Windows build shell with CMake, Ninja, Python and the compiler on PATH.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$QtPrefix,
    [switch]$BuildQt,
    [string]$QtSource = 'C:\Qt\6.12.0\Src',
    [string]$QtBuild = 'C:\Qt\build-6.12.0-static',
    [ValidateSet('MinGW','MSVC')][string]$Compiler = 'MinGW',
    [string]$ToolchainBin,
    [string]$BuildDirectory,
    [ValidateRange(1,128)][int]$Jobs = 4
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This script must run on Windows.' }
$appSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $appSource 'build\windows-static' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$QtPrefix = [IO.Path]::GetFullPath($QtPrefix)
$QtBuild = [IO.Path]::GetFullPath($QtBuild)
$originalPath = $env:PATH
function Invoke-Checked([string]$File, [string[]]$Arguments) {
    Write-Host "[command] $File $($Arguments -join ' ')"
    # Windows PowerShell 5.1 wraps native stderr as ErrorRecord. A harmless
    # diagnostic must not terminate the script before we inspect the exit code.
    $ErrorActionPreference = 'Continue'
    $PSNativeCommandUseErrorActionPreference = $false
    & $File @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }
    $nativeExitCode = $LASTEXITCODE
    if ($nativeExitCode -ne 0) { throw "$File failed (exit $nativeExitCode). See the preceding output and app/windows-static-transcript.log." }
}
function Assert-NinjaBuild([string]$Directory, [string]$Stage) {
    $buildFile = Join-Path $Directory 'build.ninja'
    if (-not (Test-Path -LiteralPath $buildFile -PathType Leaf)) {
        throw "$Stage did not generate $buildFile. Cannot start compilation for this stage. See the configure output in app/windows-static-transcript.log; if this is an old/partial build, use a NEW build directory."
    }
}
function Require-Tool([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $command) { throw "Missing $Name on PATH. See docs/windows-single-exe.md." }
    return $command.Source
}
$transcriptStarted = $false
try {
    $logPath = Join-Path $appSource 'windows-static-transcript.log'
    Start-Transcript -LiteralPath $logPath -Force | Out-Null
    $transcriptStarted = $true
    Write-Host "Build log: $logPath"
    if ($ToolchainBin) {
        $ToolchainBin = (Resolve-Path -LiteralPath $ToolchainBin).Path
        $env:PATH = "$ToolchainBin;$env:PATH"
    }
    $cmake = Require-Tool 'cmake.exe'
    $null = Require-Tool 'ninja.exe'
    $python = Require-Tool 'python.exe'
    if ($Compiler -eq 'MinGW') {
        $compilerPath = Require-Tool 'g++.exe'
        $null = Require-Tool 'gcc.exe'
        $null = Require-Tool 'windres.exe'
        $platform = 'win32-g++'
    } else {
        $compilerPath = Require-Tool 'cl.exe'
        $null = Require-Tool 'rc.exe'
        if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'Use the x64 Native Tools / Developer PowerShell for Visual Studio 2022.' }
        $platform = 'win32-msvc'
    }
    if ($BuildQt) {
        $QtSource = (Resolve-Path -LiteralPath $QtSource).Path
        if ($QtSource -match '\s' -or $QtBuild -match '\s' -or $QtPrefix -match '\s') {
            throw 'Use short paths without spaces for Qt source/build/install directories.'
        }
        $configure = Join-Path $QtSource 'configure.bat'
        if (-not (Test-Path -LiteralPath $configure)) { throw "Missing Qt source: $configure" }
        if (Test-Path -LiteralPath (Join-Path $QtPrefix 'bin\Qt6Core.dll')) { throw 'QtPrefix points to a shared Qt kit. Choose a separate static installation.' }
        if ($QtBuild -eq $QtSource -or $QtBuild -eq $QtPrefix -or $QtPrefix -eq $QtSource) { throw 'Qt source, build and prefix must be different directories.' }
        $arguments = @('-static','-static-runtime','-release','-cmake-generator','Ninja','-platform',$platform,
            '-prefix',$QtPrefix,'-submodules','qtbase,qtshadertools,qtdeclarative',
            '-nomake','tests','-nomake','examples','-schannel','-no-openssl','-no-icu',
            '-opengl','desktop','-qt-zlib','-qt-libpng','-qt-libjpeg','-qt-pcre',
            '-qt-freetype','-qt-harfbuzz','-qt-doubleconversion')
        $signature = (@($QtSource,$Compiler,$compilerPath) + $arguments | ConvertTo-Json -Compress)
        $marker = Join-Path $QtBuild 'lem-static-config.json'
        New-Item -ItemType Directory -Path $QtBuild -Force | Out-Null
        if (Test-Path -LiteralPath (Join-Path $QtBuild 'CMakeCache.txt')) {
            if (-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $signature.Trim()) {
                throw 'Qt build directory has another or incomplete configuration. Select a NEW -QtBuild directory.'
            }
        }
        # A cache/marker is not proof that the generate step completed. Resume a
        # matching partial configuration only after configure creates build.ninja.
        if (-not (Test-Path -LiteralPath (Join-Path $QtBuild 'build.ninja') -PathType Leaf) -or
            -not (Test-Path -LiteralPath $marker)) {
            Write-Host '[1/6] Configure static Qt'
            Push-Location $QtBuild
            try { Invoke-Checked -File $configure -Arguments $arguments }
            finally { Pop-Location }
            Assert-NinjaBuild -Directory $QtBuild -Stage 'Qt configure'
            Set-Content -LiteralPath $marker -Value $signature -Encoding UTF8
        }
        Assert-NinjaBuild -Directory $QtBuild -Stage 'Qt configure'
        Write-Host '[2/6] Build static Qt'
        Invoke-Checked -File $cmake -Arguments @('--build',$QtBuild,'--parallel',"$Jobs")
        Write-Host '[3/6] Install static Qt'
        Invoke-Checked -File $cmake -Arguments @('--install',$QtBuild)
    }
    $qtConfig = Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'
    if (-not (Test-Path -LiteralPath $qtConfig)) { throw 'Static Qt is not installed. First use -BuildQt or provide an existing static -QtPrefix.' }
    $appArguments = @('-S',$appSource,'-B',$BuildDirectory,'-G','Ninja',
        "-DCMAKE_PREFIX_PATH=$QtPrefix",'-DCMAKE_BUILD_TYPE=Release',
        '-DLEM_WINDOWS_STANDALONE=ON','-DLEM_BUILD_TESTS=OFF')
    if ($Compiler -eq 'MSVC') { $appArguments += '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded' }
    else { $appArguments += @('-DCMAKE_C_COMPILER=gcc','-DCMAKE_CXX_COMPILER=g++','-DCMAKE_RC_COMPILER=windres') }
    Write-Host '[4/6] Configure Lem'
    Invoke-Checked -File $cmake -Arguments $appArguments
    Assert-NinjaBuild -Directory $BuildDirectory -Stage 'Lem configure'
    Write-Host '[5/6] Build Lem'
    Invoke-Checked -File $cmake -Arguments @('--build',$BuildDirectory,'--target','Lem','--parallel',"$Jobs")
    Write-Host '[6/6] Verify single-file executable'
    $exe = Join-Path $BuildDirectory 'Lem.exe'
    Invoke-Checked -File $python -Arguments @((Join-Path $PSScriptRoot 'verify-pe.py'),$exe,'--report',(Join-Path $BuildDirectory 'dll-imports.json'))
    $portable = Join-Path $BuildDirectory 'portable'
    New-Item -ItemType Directory -Path $portable -Force | Out-Null
    $otherFiles = @(Get-ChildItem -LiteralPath $portable -Force | Where-Object { $_.Name -ne 'Lem.exe' })
    if ($otherFiles.Count) { throw 'The portable test directory must contain only Lem.exe. Choose a new -BuildDirectory.' }
    $portableExe = Join-Path $portable 'Lem.exe'
    Copy-Item -LiteralPath $exe -Destination $portableExe -Force
    # Hide installed Qt/QML/plugin paths during the test. No account/storage is used.
    $keys = @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORM','QT_QUICK_BACKEND')
    $saved = @{}
    foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key,'Process') }
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        foreach ($key in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')) {
            [Environment]::SetEnvironmentVariable($key,$null,'Process')
        }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_QUICK_BACKEND = 'software'
        $process = Start-Process -FilePath $portableExe -ArgumentList '--smoke-test' -WorkingDirectory $portable -PassThru
        if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Standalone smoke test timed out.' }
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "Standalone startup/TLS/JPEG test failed ($($process.ExitCode))." }
    } finally {
        foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key,$saved[$key],'Process') }
    }
    Write-Host "Ready: $portableExe"
    Write-Host 'Copy only Lem.exe to the destination PC. A final check on a PC without Qt is recommended.'
} catch {
    Write-Host "FAILED: $($_.Exception.Message)"
    throw
} finally {
    $env:PATH = $originalPath
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
