/*
 * SEROB rating core, derived from SukakuExplainer / Sudoku Explainer.
 * Copyright (C) 2006-2009 Nicolas Juillerat
 * C++ port and modifications by clubDS
 *
 * Licensed under the GNU Lesser General Public License v2.1 only.
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "se_rules.h"

namespace sefast {

/*
 * Hidden Pair / Triplet / Quad and their direct forms, ported from
 * diuf.sudoku.solver.rules.HiddenSet, HiddenSetHint and DirectHiddenSetHint.
 *
 * The dual of NakedSet: the permutation runs over values, and the common
 * tuple is over positions within the region. Region types are visited in
 * HiddenSet.getHints order, blocks then columns then rows; the variant passes
 * are guarded by !isVLatin(), which never holds in the rating build.
 */
constexpr int kHiddenSetRegionTypes[3] = {0, 2, 1};

struct HiddenSetHint {
    int placementCell = -1;          // direct mode only
    int placementValue = 0;
    std::array<uint16_t, kCells> removals{};
};

/** HiddenSetHint / DirectHiddenSetHint getDifficulty, revisedRating off. */
inline int hiddenSetRating(int degree, bool direct) {
    if (direct) {
        if (degree == 2) return 20;   // 2.0
        if (degree == 3) return 25;   // 2.5
        return 43;                    // 4.3
    }
    if (degree == 2) return 34;       // 3.4
    if (degree == 3) return 40;       // 4.0
    return 54;                        // 5.4
}

namespace {

/** Region positions holding `value`, as a 9-bit mask over region indexes. */
inline unsigned positionsOf(const Board& board, const std::array<int, 9>& cells,
                            int value) {
    uint16_t bit = static_cast<uint16_t>(1u << value);
    unsigned result = 0;
    for (int at = 0; at < 9; ++at)
        if ((board.candidates[cells[at]] & bit) != 0) result |= 1u << at;
    return result;
}

/**
 * HiddenSet.createHiddenSetHint. In direct mode the hint only counts when the
 * squeeze leaves some other value with a single position left in the region,
 * and it then carries that placement instead of eliminations.
 */
bool buildHiddenSetHint(const Board& board, int type, int index, int degree,
                        bool direct, unsigned valueMask, unsigned common,
                        HiddenSetHint& hint) {
    const std::array<int, 9>& cells = regionCells(type, index);
    std::array<uint16_t, kCells> removals{};
    std::vector<int> tuple;
    bool worth = false;
    for (int at = 0; at < 9; ++at) {
        if ((common & (1u << at)) == 0) continue;
        int cell = cells[at];
        uint16_t removable = static_cast<uint16_t>(
                board.candidates[cell] & ~static_cast<uint16_t>(valueMask));
        if (removable != 0) {
            removals[cell] = removable;
            worth = true;
        }
        tuple.push_back(cell);
    }

    if (direct) {
        for (int value = 1; value <= 9; ++value) {
            if ((valueMask & (1u << value)) != 0) continue;
            unsigned positions = positionsOf(board, cells, value);
            if (__builtin_popcount(positions) <= 1) continue;
            unsigned outside = positions & ~common;
            if (__builtin_popcount(outside) != 1) continue;
            int at = __builtin_ctz(outside);
            hint.placementCell = cells[at];
            hint.placementValue = value;
            return true;
        }
        return false;   // createHiddenSetHint returns null
    }

    if (!worth) return false;
    hint.removals = removals;
    return true;
}

}  // namespace

/**
 * HiddenSet.getHints under SingleHintAccumulator semantics.
 *
 * The empty-cell guard is `empty > degree * 2 || (isDirect && empty > degree)`,
 * which reduces to `empty > degree` in direct mode and `empty > degree * 2`
 * otherwise. Note it is a strict comparison, unlike NakedSet's.
 */
inline bool findHiddenSet(const Board& board, int degree, bool direct,
                          HiddenSetHint& hint) {
    if (degree < 2 || degree > 4) return false;
    for (int type : kHiddenSetRegionTypes) {
        for (int index = 0; index < 9; ++index) {
            const std::array<int, 9>& cells = regionCells(type, index);
            int empty = 0;
            for (int cell : cells)
                if (board.values[cell] == 0) ++empty;
            if (!(empty > degree * 2 || (direct && empty > degree))) continue;

            // Permutations(degree, 9) over value indexes 0..8, shifted to 1..9.
            unsigned value = (1u << degree) - 1;
            const unsigned carry = (1u << (9 - degree)) - 1;
            bool isLast = false;
            for (;;) {
                bool hasNext = !isLast;
                isLast = ((value & (0u - value)) & carry) == 0;
                if (!hasNext) break;
                unsigned current = value;
                if (!isLast) {
                    unsigned smallest = value & (0u - value);
                    unsigned ripple = value + smallest;
                    unsigned ones = value ^ ripple;
                    ones = (ones >> 2) / smallest;
                    value = ripple | ones;
                }

                // CommonTuples.searchCommonTuple over position sets: each
                // value needs at least two positions, and the union must hold
                // exactly `degree` of them.
                unsigned valueMask = 0;
                unsigned common = 0;
                bool viable = true;
                for (int bit = 0; bit < 9 && viable; ++bit) {
                    if ((current & (1u << bit)) == 0) continue;
                    int candidate = bit + 1;
                    unsigned positions = positionsOf(board, cells, candidate);
                    if (__builtin_popcount(positions) <= 1) viable = false;
                    else {
                        common |= positions;
                        valueMask |= 1u << candidate;
                    }
                }
                if (!viable || __builtin_popcount(common) != degree) continue;
                if (buildHiddenSetHint(board, type, index, degree, direct,
                                       valueMask, common, hint))
                    return true;
            }
        }
    }
    return false;
}

}  // namespace sefast

namespace {

/** `param` packs the degree and the direct flag: degree + (direct ? 10 : 0). */
inline void unpack(int param, int& degree, bool& direct) {
    direct = param >= 10;
    degree = direct ? param - 10 : param;
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_hidden_set(
        const char* state, int param) {
    static std::string result;
    try {
        int degree;
        bool direct;
        unpack(param, degree, direct);
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::HiddenSetHint hint;
        if (!sefast::findHiddenSet(board, degree, direct, hint)) return "";
        sefast::RuleHint key;
        key.rating = sefast::hiddenSetRating(degree, direct);
        key.placementCell = hint.placementCell;
        key.placementValue = hint.placementValue;
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
