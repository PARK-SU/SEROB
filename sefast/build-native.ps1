param(
    [string]$Compiler = 'em++'
)

$ErrorActionPreference = 'Stop'
$nativeSource = Join-Path $PSScriptRoot 'native'
$output = Join-Path $PSScriptRoot 'build/native'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sources = @(Get-ChildItem -LiteralPath $nativeSource -Filter '*.cpp' | Sort-Object Name | ForEach-Object FullName)
$exports = @(
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
& $Compiler @sources '-std=c++17' '-O3' '-msimd128' '-DSEFAST_NO_MAIN' `
    '-sMODULARIZE=1' '-sEXPORT_NAME=createSeFastNative' '-sENVIRONMENT=web,worker,node' `
    "-sEXPORTED_FUNCTIONS=$($exports -join ',')" '-sEXPORTED_RUNTIME_METHODS=ccall,HEAPU16' `
    '-sALLOW_MEMORY_GROWTH=1' '-sFILESYSTEM=0' '-sDISABLE_EXCEPTION_CATCHING=0' `
    '-o' (Join-Path $output 'sefast_native.js')
if ($LASTEXITCODE -ne 0) { throw "Emscripten failed with exit code $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $nativeSource 'sefast_runtime.js') -Destination $output
