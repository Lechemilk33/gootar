<#
.SYNOPSIS
    Sets up Gootar on Windows: installs what's missing, builds, and reports.

.DESCRIPTION
    Uses winget, which ships with Windows 11 - nothing exotic to install first.
    Everything is idempotent: run it again after changing anything and it only
    does the parts that are still needed.

.PARAMETER SkipInstall
    Check and build, but never install anything.

.PARAMETER Clean
    Delete the build directory first.

.EXAMPLE
    .\setup.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipInstall,
    [switch]$Clean,
    # Never prompt, never launch. Set automatically when nothing can answer.
    [switch]$NonInteractive
)

# A prompt with nobody there to answer it hangs forever, which is exactly what
# happened the first time this ran in CI. Detect that rather than trusting the
# caller to pass the switch.
if ($env:CI -or -not [Environment]::UserInteractive) {
    $NonInteractive = $true
}

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- output helpers --------------------------------------------------------

function Write-Head($text) {
    Write-Host ''
    Write-Host "  $text" -ForegroundColor White
    Write-Host "  $('-' * $text.Length)" -ForegroundColor DarkGray
}
function Write-Ok($text)   { Write-Host "  [ok]   $text" -ForegroundColor Green }
function Write-Info($text) { Write-Host "  [..]   $text" -ForegroundColor Cyan }
function Write-Warn($text) { Write-Host "  [!]    $text" -ForegroundColor Yellow }
function Write-Fail($text) { Write-Host "  [x]    $text" -ForegroundColor Red }

function Test-Command($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

Write-Host ''
Write-Host '   GOOTAR SETUP' -ForegroundColor White
Write-Host '   NAM rig: librarian + player' -ForegroundColor DarkGray

# --- prerequisites ---------------------------------------------------------

Write-Head 'Checking what you already have'

$hasWinget = Test-Command 'winget'
if (-not $hasWinget -and -not $SkipInstall) {
    Write-Fail 'winget not found.'
    Write-Host '         winget ships with Windows 11 and recent Windows 10.'
    Write-Host '         Install "App Installer" from the Microsoft Store, then re-run this.'
    exit 1
}

# name, winget id, the command that proves it is there
$tools = @(
    @{ Name = 'Git';    Id = 'Git.Git';          Probe = 'git' }
    @{ Name = 'CMake';  Id = 'Kitware.CMake';    Probe = 'cmake' }
    @{ Name = 'Node.js';Id = 'OpenJS.NodeJS.LTS';Probe = 'node' }
)

$missing = @()
foreach ($tool in $tools) {
    if (Test-Command $tool.Probe) {
        $version = (& $tool.Probe --version 2>&1 | Select-Object -First 1)
        Write-Ok "$($tool.Name)  $version"
    } else {
        Write-Warn "$($tool.Name) is missing"
        $missing += $tool
    }
}

# Visual Studio's C++ toolset is the big one, and it is not a command on PATH.
# vswhere is the supported way to ask whether it is present.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasCppToolset = $false
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    if ($vsPath) {
        $hasCppToolset = $true
        Write-Ok "MSVC C++ toolset  ($(Split-Path -Leaf $vsPath))"
    }
}
if (-not $hasCppToolset) {
    Write-Warn 'MSVC C++ toolset is missing (this is the big download, ~6-8 GB)'
}

# --- install ---------------------------------------------------------------

if (($missing.Count -gt 0 -or -not $hasCppToolset) -and -not $SkipInstall) {
    Write-Head 'Installing what is missing'
    Write-Host '  Windows may ask for permission for each one.' -ForegroundColor DarkGray

    foreach ($tool in $missing) {
        Write-Info "installing $($tool.Name)..."
        winget install --id $tool.Id --exact --silent `
            --accept-package-agreements --accept-source-agreements | Out-Null
        if ($LASTEXITCODE -eq 0) { Write-Ok "$($tool.Name) installed" }
        else { Write-Fail "$($tool.Name) failed (winget exit $LASTEXITCODE)" }
    }

    if (-not $hasCppToolset) {
        Write-Info 'installing Visual Studio Build Tools with the C++ workload...'
        Write-Host '         This is several GB and takes a while. Leave it running.' -ForegroundColor DarkGray
        winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent `
            --accept-package-agreements --accept-source-agreements `
            --override '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' | Out-Null
        if ($LASTEXITCODE -eq 0) { Write-Ok 'Build Tools installed' }
        else { Write-Fail "Build Tools failed (winget exit $LASTEXITCODE)" }
    }

    Write-Warn 'Close this window and open a NEW terminal so PATH updates, then re-run.'
    Write-Host ''
    exit 0
}

if ($missing.Count -gt 0 -or -not $hasCppToolset) {
    Write-Fail 'Missing prerequisites and -SkipInstall was set. Nothing to do.'
    exit 1
}

# --- submodules ------------------------------------------------------------

Write-Head 'Dependencies'

$juce = Join-Path $root 'native\libs\JUCE\CMakeLists.txt'
if (Test-Path $juce) {
    Write-Ok 'JUCE, NeuralAudio and AudioDSPTools are present'
} else {
    Write-Info 'fetching submodules (about 2 GB, mostly JUCE)...'
    Push-Location $root
    git submodule update --init --recursive
    Pop-Location
    if (Test-Path $juce) { Write-Ok 'submodules ready' }
    else { Write-Fail 'submodule checkout failed'; exit 1 }
}

# --- audio interface -------------------------------------------------------

Write-Head 'Your audio hardware'

# ASIO drivers register themselves here; this is how hosts enumerate them.
$asioDrivers = @()
if (Test-Path 'HKLM:\SOFTWARE\ASIO') {
    # @() so a single driver still reports a Count rather than being a scalar.
    $asioDrivers = @(Get-ChildItem 'HKLM:\SOFTWARE\ASIO' -ErrorAction SilentlyContinue | ForEach-Object {
        $desc = (Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue).Description
        if ($desc) { $desc } else { $_.PSChildName }
    })
}

if ($asioDrivers.Count -gt 0) {
    Write-Ok "$($asioDrivers.Count) ASIO driver$(if($asioDrivers.Count -ne 1){'s'}) found:"
    foreach ($d in $asioDrivers) { Write-Host "           - $d" -ForegroundColor Gray }
} else {
    Write-Warn 'No ASIO drivers registered.'
    Write-Host '         Install your interface driver. Without ASIO you get WASAPI,'
    Write-Host '         which works but is too laggy to actually play through.'
}

# The SDK is a separate download and cannot be shipped here.
$asioSdk = Join-Path $root 'native\libs\asiosdk\common\iasiodrv.h'
$enableAsio = Test-Path $asioSdk
if ($enableAsio) {
    Write-Ok 'ASIO SDK found - building WITH ASIO support'
} else {
    Write-Warn 'ASIO SDK not present - building with WASAPI only'
    Write-Host '         To enable ASIO: download the ASIO SDK from Steinberg, unzip to'
    Write-Host "         $root\native\libs\asiosdk"
    Write-Host '         (so that common\iasiodrv.h exists), then run this script again.'
}

# --- where your captures are ----------------------------------------------

Write-Head 'Your captures'

$candidates = @(
    "$env:USERPROFILE\Documents\NAM Models",
    "$env:USERPROFILE\Documents\NAM",
    "$env:USERPROFILE\Documents\Neural Amp Modeler",
    "$env:USERPROFILE\Documents\Tone3000",
    "$env:USERPROFILE\Music\NAM",
    "$env:USERPROFILE\Downloads\NAM"
)
$found = $false
foreach ($dir in $candidates) {
    if (Test-Path $dir) {
        $count = @(Get-ChildItem $dir -Filter *.nam -Recurse -ErrorAction SilentlyContinue).Count
        if ($count -gt 0) {
            Write-Ok "$count capture$(if($count -ne 1){'s'}) in $dir"
            Write-Host '           The app will pick this up on first launch.' -ForegroundColor DarkGray
            $found = $true
            break
        }
    }
}
if (-not $found) {
    Write-Info 'No capture folder found yet - that is fine.'
    Write-Host '         Put your .nam files anywhere and point the app at them with'
    Write-Host '         "Model folder", or drop them in Documents\NAM Models and they'
    Write-Host '         will be found automatically next launch.'
}

# --- build -----------------------------------------------------------------

Write-Head 'Building'

$buildDir = Join-Path $root 'native\build'
if ($Clean -and (Test-Path $buildDir)) {
    Write-Info 'removing the old build directory...'
    Remove-Item $buildDir -Recurse -Force
}

# The generator is deliberately not pinned: naming a Visual Studio version
# breaks the moment a newer one is installed.
$cmakeArgs = @(
    '-S', (Join-Path $root 'native'),
    '-B', $buildDir,
    '-A', 'x64',
    "-DGOOTAR_ENABLE_ASIO=$(if ($enableAsio) { 'ON' } else { 'OFF' })"
)

Write-Info 'configuring...'
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Fail 'CMake configure failed'; exit 1 }

Write-Info 'compiling (first build takes a while - JUCE is large)...'
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { Write-Fail 'Build failed'; exit 1 }
Write-Ok 'built'

Write-Info 'running the engine tests...'
& ctest --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { Write-Warn 'Tests failed - the app may still run, but something is off' }
else { Write-Ok 'tests passed' }

# --- web librarian ---------------------------------------------------------

Write-Head 'Librarian UI'

Push-Location $root
if (-not (Test-Path (Join-Path $root 'node_modules'))) {
    Write-Info 'installing UI dependencies...'
    & npm install --no-audit --no-fund
}
if ($LASTEXITCODE -eq 0) { Write-Ok 'UI dependencies ready' }

# Next.js reports anonymous usage by default. This is a local tool; it should
# not be talking to anyone. The npm scripts set this too - belt and braces.
& npx next telemetry disable 2>&1 | Out-Null
Write-Ok 'telemetry disabled'
Pop-Location

# --- done ------------------------------------------------------------------

$exe = Join-Path $buildDir 'GootarPlayer_artefacts\Release\Standalone\Gootar Player.exe'
$vst3 = Join-Path $buildDir 'GootarPlayer_artefacts\Release\VST3'

Write-Head 'Ready'
Write-Host ''
Write-Host '  Player:     ' -NoNewline; Write-Host $exe -ForegroundColor Cyan
if (Test-Path $vst3) {
    Write-Host '  VST3:       ' -NoNewline; Write-Host $vst3 -ForegroundColor Cyan
}
Write-Host '  Librarian:  ' -NoNewline; Write-Host 'npm run dev   (then open http://localhost:3000)' -ForegroundColor Cyan
Write-Host ''
Write-Host '  First run: Options -> Audio/MIDI Settings, pick your interface,' -ForegroundColor DarkGray
Write-Host '  set 48000 Hz and a 64 or 128 sample buffer.' -ForegroundColor DarkGray
if (-not $enableAsio) {
    Write-Host ''
    Write-Warn 'Built without ASIO - fine for testing, too laggy to play through.'
}
Write-Host ''

if ((Test-Path $exe) -and -not $NonInteractive) {
    $answer = Read-Host '  Launch the player now? [y/N]'
    if ($answer -match '^[Yy]') { Start-Process $exe }
}
