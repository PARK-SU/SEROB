# SEROB corresponding source

This directory contains the complete corresponding source for the SEROB
WebAssembly difficulty-rating engine distributed with fsrs Daily Sudoku.

## License

The SE-derived engine is licensed under the GNU Lesser General Public License
version 2.1 only.

Copyright (C) 2006-2009 Nicolas Juillerat. Additional SukakuExplainer
contributors are credited in `SukakuExplainer/README.md`.

The SEROB WebAssembly and C++ adaptations were modified in 2026 by ClubDS.

## Contents

- `SukakuExplainer/`: the exact modified Java source compiled into
  `sefast.wasm`, with original notices preserved and modified files marked.
- `sefast/teavm/`: the TeaVM entry point and Maven build configuration.
- `sefast/native/`: the LGPL-2.1-only C++ forcing-chain acceleration source.

## Rebuilding

Requirements:

- JDK 17
- Maven
- TeaVM dependencies resolved by Maven
- Emscripten with `em++` on `PATH`

From the repository root, build the Java engine with:

```powershell
mvn -f ./sefast/teavm/pom.xml clean package -Pwasmgc
```

This writes the TeaVM output under `./sefast/build/teavm-wasmgc`.
Build the native module with the command documented in
`sefast/native/README.md`. The six runtime files installed in the application
are:

- `sefast.wasm`
- `sefast_runtime.js`
- `sefast_native.js`
- `sefast_native.wasm`
- `sefast_worker.js`
- `sefast_runner.js`
