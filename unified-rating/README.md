# Combined rating module

Links SEROB (SE) and skfr into one WebAssembly module.

## License

SE is LGPL-2.1-only (root `LICENSE`). skfr is BSD-3-Clause; its changes are in
`skfr.patch`.

## API

| Mode | Engine   |
| ---- | -------- |
| 0    | SE       |
| 1    | SE 1.2.1 |
| 2    | skfr     |

- `rating_rate(puzzle, mode)` returns `"er,ep,ed"` in tenths, `""` when the
  engine declines, or `ERROR,...`.
- `rating_rate_one_cell(puzzle, mode)` rates Only one cell puzzles: several
  solutions allowed, no uniqueness rules, stops at the first placement. Use EP
  and ED only. Puzzles that overflow skfr's 320 candidates go to a second skfr
  copy built for 729.

`puzzle` is 81 characters; `.` and `0` are empty cells.

## Build

Requires PowerShell, Emscripten 6.0.9 and [skfr](https://github.com/dobrichev/skfr)
at `d9c587f` with `skfr.patch` applied (`git apply`).

```powershell
.\unified-rating\build-rating.ps1 [-SkfrSource ..\skfr\src] [-Compiler em++]
```

Writes `rating.js`, `rating.wasm` and `rating_runtime.js` to `unified-rating/build/native`.

## Usage

Put the generated files and this worker in the same public directory:

```text
rating/
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
  const { id, puzzle, mode = 0, onlyOneCell = false } = data;

  try {
    const engine = await loadEngine();
    const raw = engine.rate(puzzle, mode, { onlyOneCell });
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

Call it from the page:

```js
const worker = new Worker("/rating/rating_worker.js");
let requestId = 0;

function rate(puzzle, mode = 0, onlyOneCell = false) {
  const id = ++requestId;

  return new Promise((resolve, reject) => {
    const receive = ({ data }) => {
      if (data.id !== id) return;
      worker.removeEventListener("message", receive);
      if (data.error) reject(new Error(data.error));
      else resolve(data.result);
    };

    worker.addEventListener("message", receive);
    worker.postMessage({ id, puzzle, mode, onlyOneCell });
  });
}

const result = await rate(puzzle, 2);
// result: { er, ep, ed }, each null when the engine declined
```
