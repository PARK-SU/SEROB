# Combined rating module

This directory contains the source that links the SEROB engine together with
the separate skfr engine into one WebAssembly module, selected by mode.

## License

This module combines skfr (BSD-3-Clause) with an SE-derived engine
(LGPL-2.1-only). Distribution of the combined module must comply with
LGPL-2.1 section 6 and retain the skfr BSD notices.

The corresponding source and build material are provided in `sefast/native/`,
this directory, and the pinned skfr source identified below. The LGPL-2.1
license text is available in the repository root `LICENSE`.

## Contents

- `native/rating_bridge.cpp`: the entry point that selects between the engines.
- `native/rating_runtime.js`: the JavaScript runtime adapter.
- `build-rating.ps1`: the PowerShell build script for the combined module.

## Modes

| Mode | Engine            |
| ---- | ----------------- |
| 0    | SE, current rules |
| 1    | SE 1.2.1          |
| 2    | skfr              |

0 and 1 are the numbers `sefast_rate` already uses, so they mean the same thing
in a module built without skfr; skfr is the mode added on the end.

`rating_rate(puzzle, mode)` answers `"er,ep,ed"` in tenths, or an empty string
when the engine declined the puzzle. `ERROR,` prefixes a rejected input. The SE
engine's own entry points stay exported, under `se` in the runtime adapter.

## Rebuilding

Requirements:

- PowerShell
- Emscripten with `em++` on `PATH`
- The skfr source, which is not vendored here

The current output was built with Emscripten 6.0.9 and [skfr](https://github.com/dobrichev/skfr).

From the repository root, build the combined WebAssembly module with:

```powershell
.\unified-rating\build-rating.ps1
```

Pass `-SkfrSource` to point at skfr's `src` directory, which defaults to a
sibling checkout at `../skfr/src`, and `-Compiler` to use a compiler that is not
on `PATH`. skfr predates C++11 and is compiled with four extra flags rather than
edited; `build-rating.ps1` records which and why.

This writes the three runtime files under `./unified-rating/build/native`:

- `rating.js`
- `rating.wasm`
- `rating_runtime.js`

## Usage

Put the generated files and this worker in the same public directory:

```text
unified-rating/
  rating.js
  rating.wasm
  rating_runtime.js
  rating_worker.js
```

`rating_worker.js`:

```js
importScripts("./rating.js", "./rating_runtime.js");

let enginePromise;

function loadEngine() {
  if (!enginePromise) {
    enginePromise = createRating().catch((error) => {
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
    const raw = engine.rate(puzzle, mode);
    if (raw.startsWith("ERROR,")) throw new Error(raw);

    const [er = null, ep = null, ed = null] = raw
      ? raw.split(",").map(Number)
      : [];
    self.postMessage({ id, result: { er, ep, ed } });
  } catch (error) {
    self.postMessage({ id, error: String(error?.message || error) });
  }
};
```

`puzzle` must contain 81 characters from `.`, `0` and `1`-`9`; both `.` and `0`
spell an empty cell. Give each mode its own worker: an SE rating can run for
minutes on a hard puzzle while skfr answers in milliseconds.
