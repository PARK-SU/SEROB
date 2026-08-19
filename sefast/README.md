# SE Rating-Only Build

SEROB is a rating-only build of the SukakuExplainer source. It keeps
SukakuExplainer's technique order, implication parents, hint complexity, and
hint ordering.

## Modes

- `current` / `0`: current SukakuExplainer rules
- `se121` / `1`: Explainer 1.2.1 compatibility mode

The browser API is asynchronous because difficult nested forcing chains must
not block the UI thread:

```js
const { er, ep, ed } = await SeFast.rate(puzzle, "se121");
const results = await SeFast.ratePuzzles(puzzles, "current");
```

Ratings are returned as integers in tenths (`99` means `9.9`).
