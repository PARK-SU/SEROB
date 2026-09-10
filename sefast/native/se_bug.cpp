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
 * Bivalue Universal Grave, types 1 to 4, ported from
 * diuf.sudoku.solver.rules.unique.BivalueUniversalGrave and its four hints.
 *
 * Region types are visited as block, row, column — note that this is the
 * plain 0, 1, 2 order, not the block, column, row order NakedSet uses.
 *
 * Two behaviours are selected by Settings.islkSudokuBUG, which
 * SeFastEntry.configure turns on for the current mode and off for se121:
 * the extra type-2 cell gathering in the scan, and whether type 3 iterates
 * degree outside region or the other way round. Under SingleHintAccumulator
 * the first hint added wins, so that nesting decides which type-3 hint is
 * reported.
 *
 * The isRestricted guard and the generalized naked set branch are both
 * unreachable here: they need forbidden pairs or a non-vLatin variant, and the
 * rating build enables neither.
 */
constexpr int kBugRegionTypes[3] = {0, 1, 2};

struct BugHint {
    int rating = 0;
    std::array<uint16_t, kCells> removals{};
};

namespace {

/** A cell set with the intersect/subtract behaviour of Java's CellSet. */
struct CellMask {
    bool seeded = false;
    std::array<bool, kCells> in{};

    void intersectVisible(int cell) {
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

    void remove(const std::vector<int>& cells) {
        for (int cell : cells) in[cell] = false;
    }

    bool empty() const {
        if (!seeded) return true;
        for (bool member : in)
            if (member) return false;
        return true;
    }
};

inline unsigned positionsOf(const Board& board, int type, int index, int value) {
    const std::array<int, 9>& cells = regionCells(type, index);
    uint16_t bit = static_cast<uint16_t>(1u << value);
    unsigned result = 0;
    for (int at = 0; at < 9; ++at)
        if ((board.candidates[cells[at]] & bit) != 0) result |= 1u << at;
    return result;
}

/** The region of the given type holding every one of the cells, or -1. */
int sharedRegion(int type, const std::vector<int>& cells) {
    if (cells.empty()) return -1;
    for (int index = 0; index < 9; ++index) {
        bool all = true;
        for (int cell : cells)
            if (!regionContains(type, index, cell)) { all = false; break; }
        if (all) return index;
    }
    return -1;
}

struct BugState {
    Board temp;                                  // grid with the bug values gone
    std::vector<int> bugCells;                   // insertion order
    std::array<uint16_t, kCells> bugValues{};
    uint16_t allBugValues = 0;
    CellMask commonCells;

    /** Record a bug cell and narrow the cells that see all of them. */
    bool addBugCell(int cell, int value) {
        if (std::find(bugCells.begin(), bugCells.end(), cell) == bugCells.end())
            bugCells.push_back(cell);
        bugValues[cell] |= static_cast<uint16_t>(1u << value);
        allBugValues |= static_cast<uint16_t>(1u << value);
        temp.candidates[cell] &= static_cast<uint16_t>(~(1u << value));
        commonCells.intersectVisible(cell);
        commonCells.remove(bugCells);
        // None of type 1, 2 or 3 can follow.
        return !(bugCells.size() > 1 && bitCount(allBugValues) > 1
                 && commonCells.empty());
    }
};

/**
 * The scan: every value with a position count other than zero or two must
 * pin down exactly one cell of three or more candidates.
 */
bool scanBug(const Board& board, bool lkBug, BugState& state) {
    CellMask extraCells;
    int onlyValue = 0;
    bool oneValue = true;
    bool extraSeen = false;
    std::vector<int> extraOrder;

    for (int type : kBugRegionTypes) {
        for (int index = 0; index < 9; ++index) {
            for (int value = 1; value <= 9; ++value) {
                unsigned positions = positionsOf(board, type, index, value);
                int count = __builtin_popcount(positions);
                if (count == 0 || count == 2) continue;

                const std::array<int, 9>& cells = regionCells(type, index);
                std::vector<int> newBugCells;
                for (int at = 0; at < 9; ++at) {
                    if ((positions & (1u << at)) == 0) continue;
                    if (bitCount(board.candidates[cells[at]]) >= 3)
                        newBugCells.push_back(cells[at]);
                }

                if (lkBug) {
                    if (!extraSeen) {
                        extraSeen = true;
                        onlyValue = value;
                        for (int cell : newBugCells)
                            if (!extraCells.in[cell]) {
                                extraCells.in[cell] = true;
                                extraOrder.push_back(cell);
                            }
                        extraCells.seeded = true;
                    } else if (oneValue) {
                        if (onlyValue == value) {
                            for (int cell : newBugCells)
                                if (!extraCells.in[cell]) {
                                    extraCells.in[cell] = true;
                                    extraOrder.push_back(cell);
                                }
                        } else {
                            oneValue = false;
                        }
                    }
                }

                if (newBugCells.size() == 1)
                    if (!state.addBugCell(newBugCells[0], value)) return false;
                // A value appears more than twice with no cell holding more
                // than two values, so this is not a BUG pattern.
                if (newBugCells.empty()) return false;
            }
        }
    }

    if (lkBug && oneValue && extraSeen
            && extraOrder.size() > state.bugCells.size()) {
        std::vector<int> ascending = extraOrder;   // CellSet iterates ascending
        std::sort(ascending.begin(), ascending.end());
        for (int cell : ascending) {
            if (std::find(state.bugCells.begin(), state.bugCells.end(), cell)
                    != state.bugCells.end())
                continue;
            if (!state.addBugCell(cell, onlyValue)) return false;
        }
    }
    return true;
}

/** With the bug values gone every cell must be bivalue and every value must
 *  have exactly zero or two positions in each region. */
bool isBugPattern(const BugState& state) {
    for (int cell = 0; cell < kCells; ++cell)
        if (state.temp.values[cell] == 0
                && bitCount(state.temp.candidates[cell]) != 2)
            return false;
    for (int type : kBugRegionTypes)
        for (int index = 0; index < 9; ++index)
            for (int value = 1; value <= 9; ++value) {
                int count = __builtin_popcount(
                        positionsOf(state.temp, type, index, value));
                if (count != 0 && count != 2) return false;
            }
    return true;
}

/** Bug1Hint. Note the Java adds it without an isWorth test. */
bool addBug1(const Board& board, const BugState& state, BugHint& hint) {
    int cell = state.bugCells[0];
    hint.rating = 56;   // 5.6
    hint.removals.fill(0);
    hint.removals[cell] = static_cast<uint16_t>(
            board.candidates[cell] & ~state.allBugValues);
    return true;
}

/** Bug2Hint: the single bug value goes from every cell seeing all bug cells. */
bool addBug2(const Board& board, const BugState& state, BugHint& hint) {
    if (!state.commonCells.seeded) return false;
    int value = __builtin_ctz(static_cast<unsigned>(state.allBugValues));
    uint16_t bit = static_cast<uint16_t>(1u << value);
    std::array<uint16_t, kCells> removals{};
    bool worth = false;
    for (int cell = 0; cell < kCells; ++cell) {
        if (!state.commonCells.in[cell]) continue;
        if ((board.candidates[cell] & bit) == 0) continue;
        removals[cell] = bit;
        worth = true;
    }
    if (!worth) return false;
    hint.rating = 57;   // 5.7
    hint.removals = removals;
    return true;
}

/** Bug4Hint: the two bug cells share a region and one non-bug value. */
bool addBug4(const Board& board, const BugState& state, BugHint& hint) {
    int c1 = state.bugCells[0], c2 = state.bugCells[1];
    uint16_t common = static_cast<uint16_t>(
            board.candidates[c1] & board.candidates[c2] & ~state.allBugValues);
    if (bitCount(common) != 1) return false;
    for (int type : kBugRegionTypes) {
        int index = sharedRegion(type, state.bugCells);
        if (index < 0) continue;
        int value = __builtin_ctz(static_cast<unsigned>(common));
        uint16_t bit = static_cast<uint16_t>(1u << value);
        hint.rating = 57;   // 5.7
        hint.removals.fill(0);
        hint.removals[c1] = static_cast<uint16_t>(
                board.candidates[c1] & ~state.bugValues[c1] & ~bit);
        hint.removals[c2] = static_cast<uint16_t>(
                board.candidates[c2] & ~state.bugValues[c2] & ~bit);
        return true;
    }
    return false;
}

/** One (region, degree) pair of Bug3Hint's search. */
bool tryBug3(const Board& board, const BugState& state, int type, int degree,
             BugHint& hint) {
    int index = sharedRegion(type, state.bugCells);
    if (index < 0) return false;

    std::vector<int> regionCellList;      // common cells inside that region
    for (int cell = 0; cell < kCells; ++cell)
        if (state.commonCells.in[cell] && regionContains(type, index, cell))
            regionCellList.push_back(cell);
    const int count = static_cast<int>(regionCellList.size());
    if (count < degree) return false;

    const int pick = degree - 1;
    uint64_t permValue = (uint64_t(1) << pick) - 1;
    const uint64_t permCarry = (uint64_t(1) << (count - pick)) - 1;
    bool isLast = false;
    for (;;) {
        bool hasNext = !isLast;
        isLast = ((permValue & (0 - permValue)) & permCarry) == 0;
        if (!hasNext) break;
        uint64_t current = permValue;
        if (!isLast) {
            uint64_t smallest = permValue & (0 - permValue);
            uint64_t ripple = permValue + smallest;
            uint64_t ones = permValue ^ ripple;
            ones = (ones >> 2) / smallest;
            permValue = ripple | ones;
        }

        std::vector<int> nakedCells;
        uint16_t otherCommon = 0;
        bool viable = true;
        for (int bit = 0; bit < count; ++bit) {
            if ((current & (uint64_t(1) << bit)) == 0) continue;
            int cell = regionCellList[bit];
            nakedCells.push_back(cell);
            otherCommon |= board.candidates[cell];
        }
        // Every value of the naked set must be covered by the non-bug cells.
        if (bitCount(otherCommon) != degree) continue;

        // CommonTuples.searchCommonTuple over the naked cells plus the bug
        // values: each needs at least two candidates, the union exactly degree.
        uint16_t nakedSet = 0;
        for (int cell : nakedCells) {
            if (bitCount(board.candidates[cell]) <= 1) { viable = false; break; }
            nakedSet |= board.candidates[cell];
        }
        if (!viable) continue;
        if (bitCount(state.allBugValues) <= 1) continue;
        nakedSet |= state.allBugValues;
        if (bitCount(nakedSet) != degree) continue;

        std::array<uint16_t, kCells> removals{};
        bool worth = false;
        for (int cell : regionCellList) {
            if (std::find(nakedCells.begin(), nakedCells.end(), cell)
                    != nakedCells.end())
                continue;
            if (std::find(state.bugCells.begin(), state.bugCells.end(), cell)
                    != state.bugCells.end())
                continue;
            uint16_t removable = static_cast<uint16_t>(
                    board.candidates[cell] & nakedSet);
            if (removable == 0) continue;
            removals[cell] = removable;
            worth = true;
        }
        if (!worth) continue;

        hint.rating = 57 + (bitCount(nakedSet) - 1);   // 5.7 plus 0.1 per size
        hint.removals = removals;
        return true;
    }
    return false;
}

/** Bug3Hint. lksudoku iterates degree outside region; the original does the
 *  opposite, and the first hint added is the one reported. */
bool addBug3(const Board& board, const BugState& state, bool lkBug,
             BugHint& hint) {
    if (lkBug) {
        for (int degree = 2; degree <= 6; ++degree)
            for (int type : kBugRegionTypes)
                if (tryBug3(board, state, type, degree, hint)) return true;
    } else {
        for (int type : kBugRegionTypes)
            for (int degree = 2; degree <= 6; ++degree)
                if (tryBug3(board, state, type, degree, hint)) return true;
    }
    return false;
}

}  // namespace

/** BivalueUniversalGrave.getHints under SingleHintAccumulator semantics. */
inline bool findBug(const Board& board, bool lkBug, BugHint& hint) {
    BugState state;
    state.temp = board;
    if (!scanBug(board, lkBug, state)) return false;
    if (state.bugCells.empty()) return false;
    if (!isBugPattern(state)) return false;

    if (state.bugCells.size() == 1) return addBug1(board, state, hint);
    if (bitCount(state.allBugValues) == 1) {
        if (addBug2(board, state, hint)) return true;
        if (state.bugCells.size() == 2) return addBug4(board, state, hint);
        return false;
    }
    if (state.commonCells.seeded && !state.commonCells.empty()) {
        if (state.bugCells.size() == 2 && addBug4(board, state, hint))
            return true;
        return addBug3(board, state, lkBug, hint);
    }
    return false;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_bug(
        const char* state, int lkBug) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::BugHint hint;
        if (!sefast::findBug(board, lkBug != 0, hint)) return "";
        sefast::RuleHint key;
        key.rating = hint.rating;
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
