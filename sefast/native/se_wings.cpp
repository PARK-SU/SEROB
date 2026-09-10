/*
 * SEROB rating core, derived from SukakuExplainer / Sudoku Explainer.
 * Copyright (C) 2006-2009 Nicolas Juillerat
 * C++ port and modifications by clubDS
 *
 * Licensed under the GNU Lesser General Public License v2.1 only.
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "se_rules.h"

#include <algorithm>

namespace sefast {

/*
 * WXYZ-Wing, VWXYZ-Wing, UVWXYZ-Wing and TUVWXYZ-Wing (ALS-XZ with a bivalue
 * cell), ported from diuf.sudoku.solver.rules.WXYZWing, VWXYZWing, UVWXYZWing
 * and TUVWXYZWing plus their hints.
 *
 * The four Java classes are the same algorithm at four sizes, so this is one
 * implementation parameterised by the number of wing cells. With W wing cells
 * plus the bivalue cell: every wing cell holds at least two candidates, the
 * running union stays below W + 2 and must land on exactly W + 1, and the
 * bivalue cell's two values must both lie in that union.
 *
 * Unlike the techniques ported before these do not report the first hint they
 * find. They collect every hint, sort by difficulty ascending, then
 * eliminations descending, then suffix in *reverse* lexicographic order, and
 * only then feed the accumulator — so under SingleHintAccumulator the reported
 * hint is the minimum under that comparator, ties going to the earliest
 * generated because Collections.sort is stable.
 *
 * Every CellSet iteration in the Java is backed by a BitSet over cell indexes,
 * so all of them are plain ascending index order.
 */
struct WingSpec {
    int wingCells;
    int baseRating;      // tenths
    int sizeSpan;        // 0 when the rating is fixed at the base
    const char* name;
};

/**
 * getDifficulty per hint class. The two smaller wings adjust by pilot size,
 * middle sizes being the harder ones; the two larger ones have that term
 * commented out and sit at the base rating.
 */
constexpr WingSpec kWingSpecs[4] = {
    {3, 55, 1, "WXYZ-Wing"},      // 5.5 + (1 - |3 - biggest|) * 0.1
    {4, 62, 2, "VWXYZ-Wing"},     // 6.2 + (2 - |3 - biggest|) * 0.1
    {5, 66, 0, "UVWXYZ-Wing"},    // 6.6 fixed
    {6, 75, 0, "TUVWXYZ-Wing"},   // 7.5 fixed
};

inline const WingSpec* wingSpec(int wingCells) {
    for (const WingSpec& spec : kWingSpecs)
        if (spec.wingCells == wingCells) return &spec;
    return nullptr;
}

struct WingHint {
    int rating = 0;
    int eliminations = 0;
    std::string suffix;
    int order = 0;                   // generation index, for stable ties
    std::array<uint16_t, kCells> removals{};
    bool found = false;
};

namespace {

inline int wingRating(const WingSpec& spec, int biggestCardinality) {
    if (spec.sizeSpan == 0) return spec.baseRating;
    return spec.baseRating + spec.sizeSpan - std::abs(3 - biggestCardinality);
}

/** getSuffix, and getName is the wing's name plus it. */
inline std::string wingSuffix(bool doubleLink, int biggestCardinality,
                              int wingSize) {
    return std::to_string(doubleLink ? 2 : 1)
            + std::to_string(biggestCardinality) + std::to_string(wingSize);
}

/** isWXYZWing and friends: a value a wing cell holds must be visible from the
 *  bivalue cell. The Java tests the wing cells last-to-first, but every test
 *  must pass, so the order does not matter. */
bool linksBack(uint16_t bit, const Board& board, const std::vector<int>& wing,
               int yzCell) {
    for (int cell : wing)
        if ((board.candidates[cell] & bit) != 0 && !isPeer(yzCell, cell))
            return false;
    return true;
}

/** The cells a value may be eliminated from: the visible cells common to
 *  every wing cell holding it, optionally seeded with the bivalue cell's. */
struct Victims {
    bool seeded = false;
    std::array<bool, kCells> in{};

    void intersect(int cell) {
        if (!seeded) {
            seeded = true;
            for (int peer : visibleCells(cell)) in[peer] = true;
            return;
        }
        std::array<bool, kCells> next{};
        for (int peer : visibleCells(cell))
            if (in[peer]) next[peer] = true;
        in = next;
    }
};

Victims gatherVictims(const Board& board, const std::vector<int>& wing,
                      int value, int seedCell) {
    Victims victims;
    if (seedCell >= 0) victims.intersect(seedCell);
    uint16_t bit = static_cast<uint16_t>(1u << value);
    // The Java walks the wing cells from the far end back to the pilot; a set
    // intersection does not care, but the seeding branch does, and seeding is
    // handled above.
    for (auto cell = wing.rbegin(); cell != wing.rend(); ++cell)
        if ((board.candidates[*cell] & bit) != 0) victims.intersect(*cell);
    return victims;
}

void applyVictims(const Board& board, const Victims& victims, int value,
                  const std::vector<int>& excluded,
                  std::array<uint16_t, kCells>& removals, int& eliminations,
                  bool& any) {
    if (!victims.seeded) return;
    uint16_t bit = static_cast<uint16_t>(1u << value);
    for (int cell = 0; cell < kCells; ++cell) {
        if (!victims.in[cell]) continue;
        if (std::find(excluded.begin(), excluded.end(), cell) != excluded.end())
            continue;
        if ((board.candidates[cell] & bit) == 0) continue;
        if ((removals[cell] & bit) == 0) ++eliminations;
        removals[cell] |= bit;
        any = true;
    }
}

/**
 * createHint. In the doubly linked case the wing values the bivalue cell does
 * not carry are eliminated as weak links too, and the hint falls back to a
 * plain wing when that yields nothing.
 */
bool buildWingHint(const Board& board, const WingSpec& spec,
                   const std::vector<int>& wing, int yzCell, uint16_t wingSet,
                   int xValue, int zValue, bool doubleLink,
                   int biggestCardinality, int wingSize, int order,
                   WingHint& hint) {
    std::vector<int> all = wing;
    all.push_back(yzCell);
    std::array<uint16_t, kCells> removals{};
    int eliminations = 0;
    bool weak = false, strongX = false, strongZ = false;

    if (doubleLink) {
        uint16_t rest = static_cast<uint16_t>(
                wingSet & ~((1u << xValue) | (1u << zValue)));
        while (rest != 0) {
            int value = __builtin_ctz(static_cast<unsigned>(rest));
            rest = static_cast<uint16_t>(rest & ~(1u << value));
            applyVictims(board, gatherVictims(board, wing, value, -1), value,
                         all, removals, eliminations, weak);
        }
        applyVictims(board, gatherVictims(board, wing, xValue, yzCell), xValue,
                     all, removals, eliminations, strongX);
    }
    applyVictims(board, gatherVictims(board, wing, zValue, yzCell), zValue,
                 all, removals, eliminations, strongZ);

    if (doubleLink && !weak && (!strongZ || !strongX)) doubleLink = false;

    if (eliminations == 0) return false;   // isWorth
    hint.rating = wingRating(spec, biggestCardinality);
    hint.eliminations = eliminations;
    hint.suffix = wingSuffix(doubleLink, biggestCardinality, wingSize);
    hint.order = order;
    hint.removals = removals;
    hint.found = true;
    return true;
}

/** The comparator the wing classes sort with, as "a is strictly better". */
bool better(const WingHint& a, const WingHint& b) {
    if (a.rating != b.rating) return a.rating < b.rating;
    if (a.eliminations != b.eliminations) return a.eliminations > b.eliminations;
    if (a.suffix != b.suffix) return a.suffix > b.suffix;   // reverse lexicographic
    return a.order < b.order;                               // stable sort
}

struct Search {
    const Board& board;
    const WingSpec& spec;
    WingHint best;
    int order = 0;

    /** The bivalue cell and the two hint shapes it can produce. */
    void tryBivalueCells(const std::vector<int>& wing, uint16_t wingSet,
                         int biggest, int wingSize) {
        for (int yzCell = 0; yzCell < kCells; ++yzCell) {
            if (std::find(wing.begin(), wing.end(), yzCell) != wing.end())
                continue;
            bool visible = false;
            for (int cell : wing)
                if (isPeer(cell, yzCell)) { visible = true; break; }
            if (!visible) continue;
            uint16_t yz = board.candidates[yzCell];
            if (bitCount(yz) != 2) continue;
            if (bitCount(static_cast<uint16_t>(yz & wingSet)) != 2) continue;

            int xValue = __builtin_ctz(static_cast<unsigned>(yz));
            int zValue = __builtin_ctz(
                    static_cast<unsigned>(yz & ~(1u << xValue)));
            uint16_t xBit = static_cast<uint16_t>(1u << xValue);
            uint16_t zBit = static_cast<uint16_t>(1u << zValue);

            bool doubleLink = linksBack(zBit, board, wing, yzCell);
            WingHint candidate;
            bool made = false;
            if (linksBack(xBit, board, wing, yzCell)) {
                made = buildWingHint(board, spec, wing, yzCell, wingSet, xValue,
                                     zValue, doubleLink, biggest, wingSize,
                                     order, candidate);
            } else if (doubleLink) {
                // The x link failed, so retry with the roles swapped.
                made = buildWingHint(board, spec, wing, yzCell, wingSet, zValue,
                                     xValue, false, biggest, wingSize, order,
                                     candidate);
            }
            if (!made) continue;
            ++order;
            if (!best.found || better(candidate, best)) best = candidate;
        }
    }

    /**
     * Extend the wing. Each cell after the first must be forward-visible from
     * every cell already chosen, which is what the chained CellSet
     * intersections in the Java compute.
     */
    void extend(std::vector<int>& wing, const std::array<bool, kCells>& allowed,
                uint16_t wingSet, int biggest, int wingSize) {
        const int depth = static_cast<int>(wing.size());
        const bool last = depth == spec.wingCells - 1;
        for (int cell = 0; cell < kCells; ++cell) {
            if (!allowed[cell]) continue;
            uint16_t values = board.candidates[cell];
            int count = bitCount(values);
            if (count <= 1) continue;
            uint16_t nextSet = static_cast<uint16_t>(wingSet | values);
            int size = bitCount(nextSet);
            if (last ? size != spec.wingCells + 1 : size >= spec.wingCells + 2)
                continue;

            wing.push_back(cell);
            int nextBiggest = std::max(biggest, count);
            int nextSize = wingSize + count;
            if (last) {
                tryBivalueCells(wing, nextSet, nextBiggest, nextSize);
            } else {
                std::array<bool, kCells> nextAllowed{};
                for (int other = cell + 1; other < kCells; ++other)
                    if (allowed[other] && isPeer(cell, other))
                        nextAllowed[other] = true;
                extend(wing, nextAllowed, nextSet, nextBiggest, nextSize);
            }
            wing.pop_back();
        }
    }
};

}  // namespace

/** getHints: collect every hint, then report the sort minimum. */
inline bool findWing(const Board& board, int wingCells, WingHint& best) {
    const WingSpec* spec = wingSpec(wingCells);
    if (spec == nullptr) return false;
    Search search{board, *spec, WingHint(), 0};
    for (int pilot = 0; pilot < kCells; ++pilot) {
        uint16_t values = board.candidates[pilot];
        int count = bitCount(values);
        if (count <= 1 || count >= spec->wingCells + 2) continue;
        std::array<bool, kCells> allowed{};
        for (int other = pilot + 1; other < kCells; ++other)
            if (isPeer(pilot, other)) allowed[other] = true;
        std::vector<int> wing = {pilot};
        search.extend(wing, allowed, values, count, count);
    }
    best = search.best;
    return best.found;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_wing(
        const char* state, int wingCells) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::WingHint hint;
        if (!sefast::findWing(board, wingCells, hint)) return "";
        sefast::RuleHint key;
        key.rating = hint.rating;
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
