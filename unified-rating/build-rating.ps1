param(
    [string]$Compiler = 'em++',
    [string]$SkfrSource = (Join-Path $PSScriptRoot '../../skfr/src')
)

$ErrorActionPreference = 'Stop'
$ratingSource = Join-Path $PSScriptRoot 'native'
$sefastSource = Join-Path $PSScriptRoot '../sefast/native'
$output = Join-Path $PSScriptRoot 'build/native'
$objects = Join-Path $output 'obj'
New-Item -ItemType Directory -Force -Path $objects | Out-Null

if (-not (Test-Path -LiteralPath $SkfrSource)) {
    throw "skfr source not found at $SkfrSource; pass -SkfrSource <path to skfr/src>"
}

$common = @('-std=c++17', '-O3', '-msimd128')

# skfr predates C++11. It reaches SSE2 through <emmintrin.h>, calls memalign
# and free without including their headers, and narrows ints inside brace
# initializers; these four flags carry it as-is, with no edit to its source.
$skfrFlags = @('-msse2', '-include', 'malloc.h', '-include', 'cstdlib', '-Wno-c++11-narrowing')
# skfr.cpp is the command-line front end and skfrdll.cpp is a Windows DLL entry
# point. Neither belongs in a WebAssembly module.
$skfrUnits = @('bitfields', 'flog', 'fsss', 'opsudo', 'puzzle', 'ratingengine',
    't_128', 'utilities')

function Compile([string]$source, [string]$name, [string[]]$flags) {
    $object = Join-Path $objects "$name.o"
    & $Compiler $source '-c' @common @flags '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Emscripten failed on $name with exit code $LASTEXITCODE" }
    return $object
}

$compiled = @()
foreach ($unit in $skfrUnits) {
    $compiled += Compile (Join-Path $SkfrSource "$unit.cpp") "skfr_$unit" $skfrFlags
}
foreach ($file in Get-ChildItem -LiteralPath $sefastSource -Filter '*.cpp' | Sort-Object Name) {
    $compiled += Compile $file.FullName $file.BaseName @('-DSEFAST_NO_MAIN')
}
$compiled += Compile (Join-Path $ratingSource 'rating_bridge.cpp') 'rating_bridge' @("-I$SkfrSource")

$exports = @(
    '_rating_rate',
    '_sefast_rate', '_sefast_rate_diag', '_sefast_rate_low_current',
    '_sefast_closure', '_sefast_closure_packed', '_sefast_closure_length',
    '_sefast_best_level0', '_sefast_best_chain', '_sefast_best_chain_cells',
    '_sefast_best_static', '_sefast_diagnostics',
    '_sefast_best_naked_single', '_sefast_best_hidden_single',
    '_sefast_best_locking', '_sefast_best_naked_set', '_sefast_best_hidden_set',
    '_sefast_best_fisherman', '_sefast_best_xy_wing', '_sefast_best_aligned_pair',
    '_sefast_best_aligned_exclusion', '_sefast_best_wing', '_sefast_best_bug',
    '_sefast_best_unique_loop', '_sefast_best_strong_links'
)
& $Compiler @compiled @common `
    '-sMODULARIZE=1' '-sEXPORT_NAME=createRatingNative' '-sENVIRONMENT=web,worker,node' `
    "-sEXPORTED_FUNCTIONS=$($exports -join ',')" '-sEXPORTED_RUNTIME_METHODS=ccall,HEAPU16' `
    '-sALLOW_MEMORY_GROWTH=1' '-sFILESYSTEM=0' '-sDISABLE_EXCEPTION_CATCHING=0' `
    '-o' (Join-Path $output 'rating.js')
if ($LASTEXITCODE -ne 0) { throw "Emscripten failed with exit code $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $ratingSource 'rating_runtime.js') -Destination $output
