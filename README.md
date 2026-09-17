# SEROB corresponding source

This directory contains the complete corresponding source for the SEROB
C++ WebAssembly difficulty-rating engine distributed with fsrs Daily Sudoku.

## License

The SE-derived engine is licensed under the GNU Lesser General Public License
version 2.1 only.

Copyright (C) 2006-2009 Nicolas Juillerat. Additional SukakuExplainer
contributors are credited in `SukakuExplainer/README.md`.

The SEROB WebAssembly and C++ adaptations were modified in 2026 by ClubDS.

## Contents

- `SukakuExplainer/`: the Java reference source, with original notices
  preserved and modified files marked.
- `sefast/native/`: the LGPL-2.1-only C++ rating-engine source and JavaScript
  runtime adapter.
- `sefast/build-native.ps1`: the PowerShell build script for the C++ WebAssembly
  module.
- `unified-rating/`: a second build that links this engine together with the
  separate skfr engine into one module, selected by mode. See
  `unified-rating/README.md`.

## Rebuilding

Requirements:

- PowerShell
- Emscripten with `em++` on `PATH`

The current output was built with Emscripten 6.0.9.

From the repository root, build the C++ WebAssembly module with:

```powershell
.\sefast\build-native.ps1
```

To use a compiler that is not on `PATH`, pass its executable with the
`-Compiler` parameter.

This writes the three runtime files under `./sefast/build/native`:

- `sefast_native.js`
- `sefast_native.wasm`
- `sefast_runtime.js`

## Usage

Put the generated files and this worker in the same public directory:

```text
serob/
  sefast_native.js
  sefast_native.wasm
  sefast_runtime.js
  sefast_worker.js
```

`sefast_worker.js`:

```js
importScripts("./sefast_native.js", "./sefast_runtime.js");

let enginePromise;

function loadEngine() {
  if (!enginePromise) {
    enginePromise = createSeFast().catch((error) => {
      enginePromise = null;
      throw error;
    });
  }
  return enginePromise;
}

self.onmessage = async ({ data }) => {
  const { id, puzzle, mode = 0 } = data;

  try {
    const engine = await loadEngine();
    let raw = mode === 0 ? engine.rateLowCurrent(puzzle) : "";
    if (raw === "") raw = engine.rate(puzzle, mode);
    if (raw.startsWith("ERROR,")) throw new Error(raw);

    const [er, ep, ed] = raw.split(",").map(Number);
    self.postMessage({ id, result: { er, ep, ed } });
  } catch (error) {
    self.postMessage({ id, error: String(error?.message || error) });
  }
};
```

Call it from the page:

```js
const worker = new Worker("/serob/sefast_worker.js");
let requestId = 0;

function rate(puzzle, mode = 0) {
  const id = ++requestId;

  return new Promise((resolve, reject) => {
    const receive = ({ data }) => {
      if (data.id !== id) return;
      worker.removeEventListener("message", receive);
      if (data.error) reject(new Error(data.error));
      else resolve(data.result);
    };

    worker.addEventListener("message", receive);
    worker.postMessage({ id, puzzle, mode });
  });
}

const result = await rate(puzzle, 0);
// result: { er, ep, ed }
```

`puzzle` must contain 81 characters from `.` and `1`-`9`.
Use mode `0` for current rules or `1` for SE 1.2.1 compatibility.
