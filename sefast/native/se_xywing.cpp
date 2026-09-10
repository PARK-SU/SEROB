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
 * XY-Wing and XYZ-Wing, ported from diuf.sudoku.solver.rules.XYWing and
 * XYWingHint.
 *
 * The hinge cell is scanned in index order and both wings are scanned over
 * its peers in ascending cell-index order, which is what fixes which of
 * several possible wings is reported first.
 */
struct XYWingHint {
    std::array<uint16_t, kCells> removals{};
};

inline int xyWingRating(bool isXYZ) { return isXYZ ? 44 : 42; }  // 4.4, 4.2

namespace {

/** XYWing.createHint: the eliminations shared by the two wings. */
bool buildXYWingHint(const Board& board, bool isXYZ, int xyCell, int xzCell,
                     int yzCell, XYWingHint& hint) {
    uint16_t common = static_cast<uint16_t>(
            board.candidates[xzCell] & board.candidates[yzCell]);
    int zValue = __builtin_ctz(static_cast<unsigned>(common));
    uint16_t bit = static_cast<uint16_t>(1u << zValue);

    std::array<uint16_t, kCells> removals{};
    bool worth = false;
    for (int victim : visibleCells(xzCell)) {
        if (victim == xyCell || victim == xzCell || victim == yzCell) continue;
        if (!isPeer(yzCell, victim)) continue;
        if (isXYZ && !isPeer(xyCell, victim)) continue;
        if ((board.candidates[victim] & bit) == 0) continue;
        removals[victim] = bit;
        worth = true;
    }
    if (!worth) return false;

    hint.removals = removals;
    return true;
}

/** XYWing.isXYWing / isXYZWing on candidate masks. */
inline bool isWingPattern(bool isXYZ, uint16_t xy, uint16_t xz, uint16_t yz) {
    if (bitCount(xy) != (isXYZ ? 3 : 2)) return false;
    if (bitCount(xz) != 2 || bitCount(yz) != 2) return false;
    if (bitCount(static_cast<uint16_t>(xy | xz | yz)) != 3) return false;
    return bitCount(static_cast<uint16_t>(xy & xz & yz)) == (isXYZ ? 1 : 0);
}

}  // namespace

/** XYWing.getHints under SingleHintAccumulator semantics. */
inline bool findXYWing(const Board& board, bool isXYZ, XYWingHint& hint) {
    int targetCardinality = isXYZ ? 3 : 2;
    for (int xyCell = 0; xyCell < kCells; ++xyCell) {
        uint16_t xy = board.candidates[xyCell];
        if (bitCount(xy) != targetCardinality) continue;
        for (int xzCell : visibleCells(xyCell)) {
            uint16_t xz = board.candidates[xzCell];
            if (bitCount(xz) != 2) continue;
            // Cheap pre-test: the hinge must keep exactly one value the first
            // wing does not carry.
            if (bitCount(static_cast<uint16_t>(xy & ~xz)) != 1) continue;
            for (int yzCell : visibleCells(xyCell)) {
                uint16_t yz = board.candidates[yzCell];
                if (bitCount(yz) != 2) continue;
                if (!isWingPattern(isXYZ, xy, xz, yz)) continue;
                if (buildXYWingHint(board, isXYZ, xyCell, xzCell, yzCell, hint))
                    return true;
            }
        }
    }
    return false;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_xy_wing(
        const char* state, int isXYZ) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::XYWingHint hint;
        if (!sefast::findXYWing(board, isXYZ != 0, hint)) return "";
        sefast::RuleHint key;
        key.rating = sefast::xyWingRating(isXYZ != 0);
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
