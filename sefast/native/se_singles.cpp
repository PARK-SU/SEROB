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
 * Naked Single and Hidden Single, ported from
 * diuf.sudoku.solver.rules.NakedSingle / HiddenSingle and their hints.
 *
 * Both are DirectHintProducers: the hint places a value and eliminates
 * nothing, so only the placement fields of the hint key are filled in.
 */

struct SingleHint {
    int cell = -1;
    int value = 0;
    int rating = 0;
};

/**
 * NakedSingle.getHints: the first empty cell, in index order, whose candidate
 * set has exactly one member. NakedSingleHint carries a null region.
 *
 * NakedSingleHint.getDifficulty returns 2.3 unless revisedRating is on, and
 * SeFastEntry.configure always sets revisedRating to 0.
 */
inline bool findNakedSingle(const Board& board, SingleHint& hint) {
    for (int cell = 0; cell < kCells; ++cell) {
        if (board.values[cell] != 0) continue;
        uint16_t candidates = board.candidates[cell];
        if (bitCount(candidates) != 1) continue;
        hint.cell = cell;
        hint.value = __builtin_ctz(static_cast<unsigned>(candidates));
        hint.rating = 23;   // 2.3
        return true;
    }
    return false;
}

/*
 * HiddenSingle.getHints makes two passes over blocks, columns, rows: the
 * first accepts only "alone" cells (the region's last empty cell), the second
 * only the rest. The variant region passes are guarded by !isVLatin(), which
 * is false in both rating modes.
 */
constexpr int kHiddenSingleRegionTypes[3] = {0, 2, 1};

/** HiddenSingleHint.getDifficulty: 1.0 alone, 1.2 in a block, else 1.5. */
inline int hiddenSingleRating(bool alone, int regionType) {
    if (alone) return 10;
    return regionType == 0 ? 12 : 15;
}

inline bool findHiddenSingle(const Board& board, SingleHint& hint) {
    for (int pass = 0; pass < 2; ++pass) {
        bool aloneOnly = pass == 0;
        for (int type : kHiddenSingleRegionTypes) {
            for (int index = 0; index < 9; ++index) {
                const std::array<int, 9>& cells = regionCells(type, index);
                int empty = 0;
                for (int cell : cells)
                    if (board.values[cell] == 0) ++empty;
                for (int value = 1; value <= 9; ++value) {
                    uint16_t bit = static_cast<uint16_t>(1u << value);
                    int count = 0;
                    int only = -1;
                    for (int at = 0; at < 9; ++at) {
                        if ((board.candidates[cells[at]] & bit) == 0) continue;
                        ++count;
                        only = cells[at];
                    }
                    if (count != 1) continue;
                    bool alone = empty == 1;
                    if (alone != aloneOnly) continue;
                    hint.cell = only;
                    hint.value = value;
                    hint.rating = hiddenSingleRating(alone, type);
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace sefast

namespace {

const char* singleKey(const char* state, bool naked) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::SingleHint hint;
        bool found = naked ? sefast::findNakedSingle(board, hint)
                           : sefast::findHiddenSingle(board, hint);
        if (!found) return "";
        sefast::RuleHint key;
        key.rating = hint.rating;
        key.placementCell = hint.cell;
        key.placementValue = hint.value;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_naked_single(
        const char* state, int) {
    return singleKey(state, true);
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_hidden_single(
        const char* state, int) {
    return singleKey(state, false);
}
