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
 * X-Wing, Swordfish and Jellyfish, ported from
 * diuf.sudoku.solver.rules.Fisherman, which reports through LockingHint.
 *
 * Two passes: base columns covered by rows, then base rows covered by columns.
 * A base line's potential positions are indexes into that line, so for a
 * column they are row numbers and vice versa, which is what makes the cover
 * lines fall out of the common tuple.
 */
struct FishermanHint {
    std::array<uint16_t, kCells> removals{};
};

/**
 * LockingHint.getDifficulty and getName with regions.length == degree * 2.
 * No block ever takes part here, so degree 2 is a plain X-Wing.
 */
inline int fishermanRating(int degree) {
    if (degree == 2) return 32;   // 3.2
    if (degree == 3) return 38;   // 3.8
    return 52;                    // 5.2
}

namespace {

inline unsigned linePositions(const Board& board, int type, int index,
                              int value) {
    const std::array<int, 9>& cells = regionCells(type, index);
    uint16_t bit = static_cast<uint16_t>(1u << value);
    unsigned result = 0;
    for (int at = 0; at < 9; ++at)
        if ((board.candidates[cells[at]] & bit) != 0) result |= 1u << at;
    return result;
}

/**
 * Fisherman.createFishHint. The regions array interleaves base and cover
 * lines, and the highlighted cells are the intersections that still hold the
 * value, walked cover-line first. Eliminations are the value's positions in
 * the cover lines that fall outside the base lines.
 */
bool buildFishermanHint(const Board& board, int baseType, int coverType,
                        unsigned baseMask, unsigned coverMask, int value,
                        FishermanHint& hint) {
    uint16_t bit = static_cast<uint16_t>(1u << value);
    std::array<uint16_t, kCells> removals{};
    bool worth = false;
    for (int cover = 0; cover < 9; ++cover) {
        if ((coverMask & (1u << cover)) == 0) continue;
        unsigned outside = linePositions(board, coverType, cover, value) & ~baseMask;
        if (outside == 0) continue;
        const std::array<int, 9>& cells = regionCells(coverType, cover);
        for (int at = 0; at < 9; ++at)
            if ((outside & (1u << at)) != 0) {
                removals[cells[at]] = bit;
                worth = true;
            }
    }
    if (!worth) return false;

    hint.removals = removals;
    return true;
}

}  // namespace

/**
 * Fisherman.getHints under SingleHintAccumulator semantics. The permutation
 * over base lines is the outer loop and the value the inner one, matching the
 * Java nesting.
 */
inline bool findFisherman(const Board& board, int degree, FishermanHint& hint) {
    if (degree < 2 || degree > 4) return false;
    static const int passes[2][2] = {{2, 1}, {1, 2}};   // column/row, row/column
    for (const int* pass : passes) {
        int baseType = pass[0];
        int coverType = pass[1];

        std::array<int, 10> occurrences{};
        for (int cell = 0; cell < kCells; ++cell)
            if (board.values[cell] != 0) ++occurrences[board.values[cell]];

        unsigned value = (1u << degree) - 1;
        const unsigned carry = (1u << (9 - degree)) - 1;
        bool isLast = false;
        for (;;) {
            bool hasNext = !isLast;
            isLast = ((value & (0u - value)) & carry) == 0;
            if (!hasNext) break;
            unsigned baseMask = value;
            if (!isLast) {
                unsigned smallest = value & (0u - value);
                unsigned ripple = value + smallest;
                unsigned ones = value ^ ripple;
                ones = (ones >> 2) / smallest;
                value = ripple | ones;
            }

            for (int candidate = 1; candidate <= 9; ++candidate) {
                // The pattern needs at least degree * 2 missing occurrences.
                if (occurrences[candidate] + degree * 2 > 9) continue;

                unsigned common = 0;
                bool viable = true;
                for (int base = 0; base < 9 && viable; ++base) {
                    if ((baseMask & (1u << base)) == 0) continue;
                    unsigned positions =
                            linePositions(board, baseType, base, candidate);
                    if (__builtin_popcount(positions) <= 1) viable = false;
                    else common |= positions;
                }
                if (!viable || __builtin_popcount(common) != degree) continue;
                if (buildFishermanHint(board, baseType, coverType, baseMask,
                                       common, candidate, hint))
                    return true;
            }
        }
    }
    return false;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_fisherman(
        const char* state, int degree) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::FishermanHint hint;
        if (!sefast::findFisherman(board, degree, hint)) return "";
        sefast::RuleHint key;
        key.rating = sefast::fishermanRating(degree);
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
