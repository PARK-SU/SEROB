# SEROB native core

This directory contains the C++ implementation of SE rating primitives.

Build with Emscripten from the repository root:

```powershell
em++ ".\sefast\native\se_closure.cpp" -std=c++17 -O3 -msimd128 `
  -DSEFAST_NO_MAIN "-sMODULARIZE=1" `
  "-sEXPORT_NAME=createSeFastNative" "-sENVIRONMENT=worker,node" `
  "-sEXPORTED_FUNCTIONS=_sefast_closure,_sefast_closure_packed,_sefast_closure_length,_sefast_best_level0,_sefast_best_chain,_sefast_best_chain_cells,_sefast_best_static,_sefast_diagnostics" `
  "-sEXPORTED_RUNTIME_METHODS=ccall,HEAPU16" `
  "-sALLOW_MEMORY_GROWTH=1" "-sFILESYSTEM=0" `
  -o ".\sefast\build\native\sefast_native.js"
```
