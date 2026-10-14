# Build with the *official* Expert Sleepers SDK and GNU ARM toolchain.
# Examples:
#   .\build.ps1
#   .\build.ps1 -ApiPath "C:\my-synth-code\distingNT_API"
param([string]$ApiPath = "")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ApiPath)) {
    $inside = Join-Path $here "distingNT_API"
    $beside = Join-Path (Split-Path -Parent $here) "distingNT_API"
    if (Test-Path (Join-Path $inside "include\distingnt\api.h")) {
        $ApiPath = $inside
    } else {
        $ApiPath = $beside
    }
}
$header = Join-Path $ApiPath "include\distingnt\api.h"
if (-not (Test-Path $header)) {
    throw "Official distingNT_API header not found at '$header'. Clone https://github.com/expertsleepersltd/distingNT_API and pass -ApiPath to its folder."
}
$compiler = Get-Command "arm-none-eabi-g++" -ErrorAction SilentlyContinue
if (-not $compiler) { throw "arm-none-eabi-g++ not found. Use the GNU ARM compiler you used for the working Clocked Wavetable plugin." }
$out = Join-Path $here "shape_lfo.o"
if (Test-Path $out) { Remove-Item $out }
$inc = Join-Path $ApiPath "include"
Write-Host "Building against official SDK: $header"
Write-Host "Compiler: $($compiler.Source)"
& $compiler.Source -std=c++17 -O2 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard `
    -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-unwind-tables -fno-asynchronous-unwind-tables `
    -ffunction-sections -fdata-sections "-I$inc" -c (Join-Path $here "shape_lfo.cpp") -o $out
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) { throw "Build failed (exit $LASTEXITCODE); no valid object generated." }
Write-Host "Built: $out ($((Get-Item $out).Length) bytes)"
$nm = Get-Command "arm-none-eabi-nm" -ErrorAction SilentlyContinue
if ($nm) {
    $undef = & $nm.Source --undefined-only $out
    if ($LASTEXITCODE -ne 0) { throw "nm inspection failed" }
    Write-Host "Host-import symbols:"
    $undef | ForEach-Object { Write-Host "  $_" }
    $bad = $undef | Where-Object { $_ -match '__aeabi_|\bmemcpy\b|\bmemset\b|\bmalloc\b|\bfree\b|\boperator new\b' }
    if ($bad) { throw "Unexpected runtime helper(s) detected. Do not install this build: $bad" }
}
Write-Host "Copy shape_lfo.o into /programs/plug-ins/ on the disting NT MicroSD card."
