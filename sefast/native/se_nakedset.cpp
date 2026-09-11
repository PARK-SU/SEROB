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
 * Naked Pair / Triplet / Quad, ported from
 * diuf.sudoku.solver.rules.NakedSet and NakedSetHint.
 *
 * NakedSet.getHints visits region types in the order blocks, columns, rows.
 * The Disjoint-Group and Windows passes it also contains are unreachable from
 * the rating build: SeFastEntry.configure leaves isDG and isWindows at their
 * false defaults in both modes, so they are not ported.
 */
constexpr int kNakedSetRegionTypes[3] = {0, 2, 1};

struct NakedSetHint {
    std::array<uint16_t, kCells> removals{};
};

/**
 * NakedSetHint.getDifficulty in integer tenths, matching how the chaining port
 * already carries ratings. SE difficulties are exact one-decimal constants, so
 * the tenths are the value Java's Math.round(difficulty * 10.0) produces.
 */
inline int nakedSetRating(int degree) {
    if (degree == 2) return 30;   // 3.0
    if (degree == 3) return 36;   // 3.6
    return 50;                    // 5.0
}

/**
 * NakedSet.getHints under SingleHintAccumulator semantics: the first tuple
 * whose eliminations are non-empty wins and the search stops there.
 */
inline bool findNakedSet(const Board& board, int degree, NakedSetHint& hint) {
    if (degree < 2 || degree > 4) return false;
    for (int type : kNakedSetRegionTypes) {
        for (int index = 0; index < 9; ++index) {
            const std::array<int, 9>& cells = regionCells(type, index);
            int empty = 0;
            for (int cell : cells)
                if (board.values[cell] == 0) ++empty;
            if (empty < degree * 2) continue;

            // Permutations(degree, 9): every 9-bit pattern with `degree` bits
            // set, in ascending numeric order, driven exactly as the Java
            // hasNext()/next() pair drives it.
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

                // CommonTuples.searchCommonTuple: every cell of the tuple must
                // hold at least two candidates, and their union must have
                // exactly `degree` values.
                std::array<int, 4> tuple{};
                uint16_t common = 0;
                bool viable = true;
                int at = 0;
                for (int bit = 0; bit < 9 && viable; ++bit) {
                    if ((current & (1u << bit)) == 0) continue;
                    int cell = cells[bit];
                    uint16_t candidates = board.candidates[cell];
                    if (bitCount(candidates) <= 1) viable = false;
                    else {
                        common |= candidates;
                        tuple[at++] = cell;
                    }
                }
                if (!viable || bitCount(common) != degree) continue;

                // createValueUniquenessHint: eliminate the tuple's values from
                // the rest of the region. isWorth() rejects an empty result.
                std::array<uint16_t, kCells> removals{};
                bool worth = false;
                for (int i = 0; i < 9; ++i) {
                    int other = cells[i];
                    bool inTuple = false;
                    for (int j = 0; j < degree; ++j)
                        if (tuple[j] == other) { inTuple = true; break; }
                    if (inTuple) continue;
                    uint16_t removable =
                            static_cast<uint16_t>(common & board.candidates[other]);
                    if (removable != 0) {
                        removals[other] = removable;
                        worth = true;
                    }
                }
                if (!worth) continue;

                hint.removals = removals;
                return true;
            }
        }
    }
    return false;
}


}  // namespace sefast

/**
 * Naked Pair / Triplet / Quad as a hint key. Naked sets are elimination-only
 * and carry no chaining tie-breaks, so only the rating and the elimination
 * masks are filled in. Returns "" when the technique does not fire.
 */
extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_naked_set(
        const char* state, int degree) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::NakedSetHint hint;
        if (!sefast::findNakedSet(board, degree, hint)) return "";
        sefast::RuleHint key;
        key.rating = sefast::nakedSetRating(degree);
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
