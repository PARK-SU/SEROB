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
 * Pointing and Claiming, ported from diuf.sudoku.solver.rules.Locking,
 * LockingHint and DirectLockingHint.
 *
 * Locking.getHints walks four ordered region-type pairs. The Disjoint-Group
 * and Windows pairs it also contains are unreachable from the rating build,
 * where isDG and isWindows stay false.
 */
struct LockingPair {
    int first;
    int second;
};
constexpr LockingPair kLockingPairs[4] = {
    {0, 2},  // block, column
    {0, 1},  // block, row
    {2, 0},  // column, block
    {1, 0},  // row, block
};

struct LockingHint {
    int firstType = -1;
    int secondType = -1;
    int value = 0;
    int placementCell = -1;          // direct mode only
    std::array<uint16_t, kCells> removals{};
};

/**
 * LockingHint.getDifficulty and getName with regions.length == 2, so the
 * degree is 1 and the split is on whether the second region is a line.
 * revisedRating is always 0 here, so the original ratings apply.
 */
inline int lockingRating(int secondType) {
    return secondType == 1 || secondType == 2 ? 26 : 28;  // 2.6, 2.8
}

/** DirectLockingHint.getDifficulty split on the first region. */
inline int directLockingRating(int firstType) {
    return firstType == 0 ? 17 : 19;  // 1.7, 1.9
}

namespace {

/** Region positions holding `value`, as a 9-bit mask over region indexes. */
inline unsigned potentialPositions(const Board& board, int type, int index,
                                   int value) {
    const std::array<int, 9>& cells = regionCells(type, index);
    uint16_t bit = static_cast<uint16_t>(1u << value);
    unsigned result = 0;
    for (int at = 0; at < 9; ++at)
        if ((board.candidates[cells[at]] & bit) != 0) result |= 1u << at;
    return result;
}

/**
 * Locking.createLockingHint for the indirect mode: eliminate the value from
 * the cells of the second region that lie outside the first. isWorth rejects
 * an empty elimination set.
 */
bool buildLockingHint(const Board& board, const LockingPair& pair, int index1,
                      int index2, int value, LockingHint& hint) {
    const std::array<int, 9>& cells2 = regionCells(pair.second, index2);
    uint16_t bit = static_cast<uint16_t>(1u << value);
    std::array<uint16_t, kCells> removals{};
    bool worth = false;
    for (int at = 0; at < 9; ++at) {
        int cell = cells2[at];
        if ((board.candidates[cell] & bit) == 0) continue;
        if (!regionContains(pair.first, index1, cell)) {
            removals[cell] = bit;
            worth = true;
        }
    }
    if (!worth) return false;
    hint.firstType = pair.first;
    hint.secondType = pair.second;
    hint.value = value;
    hint.removals = removals;
    return true;
}

/**
 * Locking.lookForFollowingHiddenSingles: once the value is locked into the
 * intersection, a third region of the first type may be left with a single
 * remaining position for it.
 */
bool findFollowingHiddenSingle(const Board& board, const LockingPair& pair,
                               int index1, int index2, int value,
                               LockingHint& hint) {
    for (int index3 = 0; index3 < 9; ++index3) {
        if (index3 == index1) continue;
        if (!regionsCross(pair.first, index3, pair.second, index2)) continue;
        unsigned positions = potentialPositions(board, pair.first, index3, value);
        if (__builtin_popcount(positions) <= 1) continue;
        const std::array<int, 9>& cells3 = regionCells(pair.first, index3);
        int remaining = 0;
        int target = -1;
        for (int at = 0; at < 9; ++at) {
            if ((positions & (1u << at)) == 0) continue;
            int cell = cells3[at];
            if (regionContains(pair.second, index2, cell)) continue;
            ++remaining;
            target = cell;
        }
        if (remaining != 1) continue;
        hint.firstType = pair.first;
        hint.secondType = pair.second;
        hint.value = value;
        hint.placementCell = target;
        return true;
    }
    return false;
}

}  // namespace

/**
 * Locking.getHints under SingleHintAccumulator semantics. The loop nesting is
 * region-type pair, then value, then first region, then second region, exactly
 * as in the Java.
 */
inline bool findLocking(const Board& board, bool direct, LockingHint& hint) {
    for (const LockingPair& pair : kLockingPairs) {
        for (int value = 1; value <= 9; ++value) {
            for (int index1 = 0; index1 < 9; ++index1) {
                unsigned positions =
                        potentialPositions(board, pair.first, index1, value);
                if (__builtin_popcount(positions) < 2) continue;
                const std::array<int, 9>& cells1 = regionCells(pair.first, index1);
                for (int index2 = 0; index2 < 9; ++index2) {
                    if (!regionsCross(pair.first, index1, pair.second, index2))
                        continue;
                    bool inCommon = true;
                    for (int at = 0; at < 9 && inCommon; ++at) {
                        if ((positions & (1u << at)) == 0) continue;
                        if (!regionContains(pair.second, index2, cells1[at]))
                            inCommon = false;
                    }
                    if (!inCommon) continue;
                    if (direct) {
                        if (findFollowingHiddenSingle(board, pair, index1, index2,
                                                      value, hint))
                            return true;
                    } else if (buildLockingHint(board, pair, index1, index2,
                                                value, hint)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

}  // namespace sefast

namespace {

const char* lockingKey(const char* state, bool direct) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::LockingHint hint;
        if (!sefast::findLocking(board, direct, hint)) return "";
        sefast::RuleHint key;
        if (direct) {
            key.rating = sefast::directLockingRating(hint.firstType);
            key.placementCell = hint.placementCell;
            key.placementValue = hint.value;
        } else {
            key.rating = sefast::lockingRating(hint.secondType);
            key.removals = hint.removals;
        }
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_locking(
        const char* state, int direct) {
    return lockingKey(state, direct != 0);
}
