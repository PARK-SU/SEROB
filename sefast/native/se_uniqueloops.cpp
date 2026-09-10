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
 * Unique Rectangles and Loops, types 1 to 4, ported from
 * diuf.sudoku.solver.rules.unique.UniqueLoops and its hint classes.
 *
 * Like the wing family this collects every hint and sorts — difficulty
 * ascending then type ascending, ties to the earliest generated — so under
 * SingleHintAccumulator the minimum under that comparator is what gets
 * reported. Hints are also de-duplicated by hint class plus loop cell set.
 *
 * Settings.islkSudokuURUL, which configure ties to the mode, changes one
 * thing that matters here: the loop search either scopes the accumulated
 * extra values per branch or lets them leak across sibling candidates. Its
 * other branch, in the type 3 search, differs only in where the degree loop
 * starts and reaches the same (degree, region) pairs either way.
 *
 * The isRestricted guard and the generalized naked set branch need forbidden
 * pairs or a non-vLatin variant, and the rating build enables neither.
 */
constexpr int kLoopRegionTypes[3] = {0, 1, 2};

enum class LoopHintKind { Type1, Type2, Type3Naked, Type3Hidden, Type4 };

struct LoopHint {
    LoopHintKind kind = LoopHintKind::Type1;
    int type = 0;                    // getType(), 1 to 4
    int rating = 0;
    std::vector<int> loop;
    std::array<uint16_t, kCells> removals{};
    int order = 0;
};

namespace {

inline int regionOf(int type, int cell) {
    int row = cell / 9, column = cell % 9;
    if (type == 0) return row / 3 * 3 + column / 3;
    return type == 1 ? row : column;
}

inline int regionIndexOf(int type, int index, int cell) {
    const std::array<int, 9>& cells = regionCells(type, index);
    for (int at = 0; at < 9; ++at)
        if (cells[at] == cell) return at;
    return -1;
}

inline uint16_t positionsOf(const Board& board, int type, int index, int value) {
    const std::array<int, 9>& cells = regionCells(type, index);
    uint16_t bit = static_cast<uint16_t>(1u << value);
    uint16_t result = 0;
    for (int at = 0; at < 9; ++at)
        if ((board.candidates[cells[at]] & bit) != 0)
            result = static_cast<uint16_t>(result | (1u << at));
    return result;
}

/** UniqueLoopHint.getDifficulty, revisedRating off: 4.5 for a rectangle,
 *  rising with the loop length. */
inline int loopRating(size_t size) {
    int rating = 45;
    if (size >= 10) rating += 3;
    if (size >= 8) rating += 2;
    else if (size >= 6) rating += 1;
    return rating;
}

/**
 * UniqueLoops.checkForLoops. A loop alternates region types, so a step never
 * reuses the type it arrived on, and closes when it returns to the first cell
 * after at least four cells.
 */
void checkForLoops(const Board& board, int cell, int v1, int v2, bool lkUrul,
                   std::vector<int>& loop, int allowedEx, uint16_t exValues,
                   int lastRegionType, std::vector<std::vector<int>>& results) {
    loop.push_back(cell);
    const uint16_t loopBits = static_cast<uint16_t>((1u << v1) | (1u << v2));
    // The non-lk branch clones the extra values once on entry and then mutates
    // that copy across sibling candidates, so state leaks between them.
    uint16_t running = exValues;

    for (int type : kLoopRegionTypes) {
        if (type == lastRegionType) continue;
        int index = regionOf(type, cell);
        const std::array<int, 9>& cells = regionCells(type, index);
        for (int at = 0; at < 9; ++at) {
            int next = cells[at];
            if (next == loop[0] && loop.size() >= 4) {
                results.push_back(loop);
                continue;
            }
            if (std::find(loop.begin(), loop.end(), next) != loop.end()) continue;
            uint16_t values = board.candidates[next];
            if ((values & loopBits) != loopBits) continue;

            uint16_t nextEx;
            if (lkUrul) {
                nextEx = static_cast<uint16_t>((exValues | values) & ~loopBits);
            } else {
                running = static_cast<uint16_t>((running | values) & ~loopBits);
                nextEx = running;
            }
            int count = bitCount(values);
            if (count == 2 || bitCount(nextEx) == 1 || allowedEx > 0)
                checkForLoops(board, next, v1, v2, lkUrul, loop,
                              allowedEx - (count > 2 ? 1 : 0), nextEx, type,
                              results);
        }
    }
    loop.pop_back();
}

/** Every region must be entered once at each parity, or never. */
bool isValidLoop(const std::vector<int>& loop) {
    std::array<bool, 27> odd{}, even{};
    bool isOdd = false;
    for (int cell : loop) {
        for (int type : kLoopRegionTypes) {
            int slot = type * 9 + regionOf(type, cell);
            std::array<bool, 27>& seen = isOdd ? odd : even;
            if (seen[slot]) return false;
            seen[slot] = true;
        }
        isOdd = !isOdd;
    }
    return odd == even;
}

struct Collector {
    const Board& board;
    bool lkUrul;
    std::vector<LoopHint> hints;
    int order = 0;

    /** UniqueLoopHint.equals: same hint class and the same set of loop cells. */
    bool alreadyHave(const LoopHint& hint) const {
        for (const LoopHint& other : hints) {
            if (other.kind != hint.kind) continue;
            if (other.loop.size() != hint.loop.size()) continue;
            bool same = true;
            for (int cell : hint.loop)
                if (std::find(other.loop.begin(), other.loop.end(), cell)
                        == other.loop.end()) { same = false; break; }
            if (same) return true;
        }
        return false;
    }

    void add(LoopHint hint) {
        bool worth = false;
        for (uint16_t mask : hint.removals)
            if (mask != 0) { worth = true; break; }
        if (!worth) return;
        if (alreadyHave(hint)) return;
        hint.order = order++;
        hints.push_back(std::move(hint));
    }

    LoopHint make(const std::vector<int>& loop, LoopHintKind kind, int type,
                  int extraRating) const {
        LoopHint hint;
        hint.kind = kind;
        hint.type = type;
        hint.rating = loopRating(loop.size()) + extraRating;
        hint.loop = loop;
        return hint;
    }

    void type1(const std::vector<int>& loop, int rescue, int v1, int v2) {
        LoopHint hint = make(loop, LoopHintKind::Type1, 1, 0);
        hint.removals[rescue] = static_cast<uint16_t>((1u << v1) | (1u << v2));
        add(std::move(hint));
    }

    void type2(const std::vector<int>& loop, const std::vector<int>& extraCells,
               int v1, int v2) {
        uint16_t common = static_cast<uint16_t>(
                board.candidates[extraCells[0]] & ~((1u << v1) | (1u << v2)));
        if (common == 0) return;
        int value = __builtin_ctz(static_cast<unsigned>(common));
        uint16_t bit = static_cast<uint16_t>(1u << value);

        std::array<bool, kCells> victims{};
        bool seeded = false;
        for (int cell : extraCells) {
            if (!seeded) {
                seeded = true;
                for (int peer : visibleCells(cell)) victims[peer] = true;
            } else {
                std::array<bool, kCells> next{};
                for (int peer : visibleCells(cell))
                    if (victims[peer]) next[peer] = true;
                victims = next;
            }
        }
        LoopHint hint = make(loop, LoopHintKind::Type2, 2, 0);
        for (int cell = 0; cell < kCells; ++cell) {
            if (!victims[cell]) continue;
            if (std::find(extraCells.begin(), extraCells.end(), cell)
                    != extraCells.end())
                continue;
            if ((board.candidates[cell] & bit) != 0) hint.removals[cell] = bit;
        }
        add(std::move(hint));
    }

    void type4(const std::vector<int>& loop, int c1, int c2, int v1, int v2) {
        int lockedType = -1, lockedIndex = -1, removeValue = -1;
        int type1Region = -1, type1Index = -1, type2Region = -1, type2Index = -1;
        for (int type : kLoopRegionTypes) {
            int index = regionOf(type, c1);
            if (index != regionOf(type, c2)) continue;
            bool hasValue1 = false, hasValue2 = false;
            for (int cell : regionCells(type, index)) {
                if (cell == c1 || cell == c2) continue;
                if ((board.candidates[cell] & (1u << v1)) != 0) hasValue1 = true;
                if ((board.candidates[cell] & (1u << v2)) != 0) hasValue2 = true;
            }
            if (!hasValue1) { type1Region = type; type1Index = index; }
            if (!hasValue2) { type2Region = type; type2Index = index; }
        }
        // The Java keeps the last region found for each value, then prefers v1.
        if (type1Region >= 0) {
            lockedType = type1Region; lockedIndex = type1Index; removeValue = v2;
        } else if (type2Region >= 0) {
            lockedType = type2Region; lockedIndex = type2Index; removeValue = v1;
        } else {
            return;
        }
        LoopHint hint = make(loop, LoopHintKind::Type4, 4, 0);
        uint16_t bit = static_cast<uint16_t>(1u << removeValue);
        hint.removals[c1] = bit;
        hint.removals[c2] = bit;
        add(std::move(hint));
    }

    void type3(const std::vector<int>& loop, int c1, int c2, int v1, int v2) {
        const uint16_t loopBits = static_cast<uint16_t>((1u << v1) | (1u << v2));
        uint16_t extra = static_cast<uint16_t>(
                (board.candidates[c1] | board.candidates[c2]) & ~loopBits);
        const int extraCount = bitCount(extra);

        // The lk branch runs the degree loop from 2 and guards only the naked
        // search with degree >= extra; the other starts the loop at extra, so
        // it never reaches the smaller degrees for the hidden search either.
        for (int degree = lkUrul ? 2 : extraCount; degree <= 7; ++degree) {
            for (int type : kLoopRegionTypes) {
                int index = regionOf(type, c1);
                if (index != regionOf(type, c2)) continue;
                const std::array<int, 9>& cells = regionCells(type, index);
                int empty = 0;
                for (int cell : cells)
                    if (board.values[cell] == 0) ++empty;
                int index1 = regionIndexOf(type, index, c1);
                int index2 = regionIndexOf(type, index, c2);

                if (degree * 2 <= empty && degree >= extraCount)
                    type3Naked(loop, type, index, index1, index2, c1, c2, extra,
                               degree);
                if (degree * 2 < empty)
                    type3Hidden(loop, type, index, index1, index2, c1, c2, extra,
                                v1, v2, degree);
            }
        }
    }

    void type3Naked(const std::vector<int>& loop, int type, int index,
                    int index1, int index2, int c1, int c2, uint16_t extra,
                    int degree) {
        const std::array<int, 9>& cells = regionCells(type, index);
        uint64_t permValue = (uint64_t(1) << degree) - 1;
        const uint64_t permCarry = (uint64_t(1) << (9 - degree)) - 1;
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
            // containsFirst: the tuple must hold c1 and must not hold c2.
            if ((current & (uint64_t(1) << index1)) == 0) continue;
            if ((current & (uint64_t(1) << index2)) != 0) continue;

            uint16_t nakedSet = static_cast<uint16_t>(
                    extra & board.candidates[c1] & board.candidates[c2]);
            std::vector<int> otherCells;
            uint16_t unionAll = 0;
            bool viable = true;
            for (int at = 0; at < 9; ++at) {
                if ((current & (uint64_t(1) << at)) == 0) continue;
                uint16_t values = at == index1 ? extra : board.candidates[cells[at]];
                if (bitCount(values) <= 1) viable = false;
                unionAll = static_cast<uint16_t>(unionAll | values);
                if (at != index1) {
                    nakedSet = static_cast<uint16_t>(nakedSet | values);
                    otherCells.push_back(cells[at]);
                }
            }
            if (bitCount(nakedSet) != degree) continue;
            // CommonTuples.searchCommonTuple over the tuple's value sets.
            if (!viable || bitCount(unionAll) != degree) continue;

            LoopHint hint = make(loop, LoopHintKind::Type3Naked, 3,
                                 bitCount(unionAll) - 1);
            for (int at = 0; at < 9; ++at) {
                int cell = cells[at];
                if (cell == c1 || cell == c2) continue;
                if (std::find(otherCells.begin(), otherCells.end(), cell)
                        != otherCells.end())
                    continue;
                uint16_t removable = static_cast<uint16_t>(
                        board.candidates[cell] & unionAll);
                if (removable != 0) hint.removals[cell] = removable;
            }
            add(std::move(hint));
        }
    }

    void type3Hidden(const std::vector<int>& loop, int type, int index,
                     int index1, int index2, int c1, int c2, uint16_t extra,
                     int v1, int v2, int degree) {
        std::vector<int> remValues;
        for (int value = 1; value <= 9; ++value)
            if (value != v1 && value != v2 && (extra & (1u << value)) == 0)
                remValues.push_back(value);
        const int pick = degree - 2;
        if (pick > static_cast<int>(remValues.size())) return;

        const std::array<int, 9>& cells = regionCells(type, index);
        uint64_t permValue = pick == 0 ? 0 : (uint64_t(1) << pick) - 1;
        const uint64_t permCarry =
                (uint64_t(1) << (static_cast<int>(remValues.size()) - pick)) - 1;
        bool isLast = pick == 0;
        bool first = true;
        for (;;) {
            uint64_t current;
            if (pick == 0) {
                if (!first) break;
                first = false;
                current = 0;
            } else {
                bool hasNext = !isLast;
                isLast = ((permValue & (0 - permValue)) & permCarry) == 0;
                if (!hasNext) break;
                current = permValue;
                if (!isLast) {
                    uint64_t smallest = permValue & (0 - permValue);
                    uint64_t ripple = permValue + smallest;
                    uint64_t ones = permValue ^ ripple;
                    ones = (ones >> 2) / smallest;
                    permValue = ripple | ones;
                }
            }

            uint16_t hiddenValues = static_cast<uint16_t>(
                    (1u << v1) | (1u << v2));
            uint16_t common = 0;
            bool viable = true;
            for (int bit = 0; bit < static_cast<int>(remValues.size()); ++bit)
                if ((current & (uint64_t(1) << bit)) != 0)
                    hiddenValues = static_cast<uint16_t>(
                            hiddenValues | (1u << remValues[bit]));
            for (int value = 1; value <= 9 && viable; ++value) {
                if ((hiddenValues & (1u << value)) == 0) continue;
                uint16_t positions = static_cast<uint16_t>(
                        positionsOf(board, type, index, value) & ~(1u << index2));
                // searchCommonTupleLight: every value needs a position left.
                if (positions == 0) viable = false;
                common = static_cast<uint16_t>(common | positions);
            }
            if (!viable || bitCount(common) != degree) continue;

            uint16_t remaining = static_cast<uint16_t>(
                    common & ~(1u << index1) & ~(1u << index2));
            LoopHint hint = make(loop, LoopHintKind::Type3Hidden, 3,
                                 bitCount(remaining) - 1);
            for (int at = 0; at < 9; ++at) {
                if ((remaining & (1u << at)) == 0) continue;
                int cell = cells[at];
                if (cell == c1 || cell == c2) continue;
                uint16_t removable = static_cast<uint16_t>(
                        board.candidates[cell] & ~hiddenValues);
                if (removable != 0) hint.removals[cell] = removable;
            }
            add(std::move(hint));
        }
    }
};

/** The comparator UniqueLoops sorts with, as "a is strictly better". */
bool better(const LoopHint& a, const LoopHint& b) {
    if (a.rating != b.rating) return a.rating < b.rating;
    if (a.type != b.type) return a.type < b.type;
    return a.order < b.order;
}

}  // namespace

/** UniqueLoops.getHints: collect every hint, then report the sort minimum. */
inline bool findUniqueLoop(const Board& board, bool lkUrul, LoopHint& best) {
    Collector collector{board, lkUrul, {}, 0};
    for (int cell = 0; cell < kCells; ++cell) {
        uint16_t values = board.candidates[cell];
        if (bitCount(values) != 2) continue;
        int v1 = __builtin_ctz(static_cast<unsigned>(values));
        int v2 = __builtin_ctz(static_cast<unsigned>(values & ~(1u << v1)));

        std::vector<int> loop;
        std::vector<std::vector<int>> results;
        checkForLoops(board, cell, v1, v2, lkUrul, loop, 2, 0, -1, results);

        for (const std::vector<int>& found : results) {
            if (!isValidLoop(found)) continue;
            std::vector<int> extraCells;
            for (int loopCell : found)
                if (bitCount(board.candidates[loopCell]) > 2)
                    extraCells.push_back(loopCell);

            if (extraCells.size() == 1) {
                collector.type1(found, extraCells[0], v1, v2);
            } else if (extraCells.size() > 2) {
                collector.type2(found, extraCells, v1, v2);
            } else if (extraCells.size() == 2) {
                int c1 = extraCells[0], c2 = extraCells[1];
                uint16_t rest = static_cast<uint16_t>(
                        (board.candidates[c1] | board.candidates[c2])
                        & ~((1u << v1) | (1u << v2)));
                if (bitCount(rest) == 1)
                    collector.type2(found, extraCells, v1, v2);
                else if (bitCount(rest) >= 2)
                    collector.type3(found, c1, c2, v1, v2);
                collector.type4(found, c1, c2, v1, v2);
            }
        }
    }
    if (collector.hints.empty()) return false;
    best = collector.hints[0];
    for (const LoopHint& hint : collector.hints)
        if (better(hint, best)) best = hint;
    return true;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_unique_loop(
        const char* state, int lkUrul) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::LoopHint hint;
        if (!sefast::findUniqueLoop(board, lkUrul != 0, hint)) return "";
        sefast::RuleHint key;
        key.rating = hint.rating;
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
