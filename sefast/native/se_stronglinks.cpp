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
#include <bitset>
#include <string>

namespace sefast {

/*
 * Strong Links of degree 2, 3 and 4, ported from
 * diuf.sudoku.solver.rules.StrongLinks and StrongLinksHint. Degree 2 also
 * stands in for TurbotFish, which the live registration block replaced.
 *
 * A hint is a chain of `degree` strong links on one digit joined end to end by
 * weak links: consecutive link ends must share a region, the "bridge", and the
 * two free ends then eliminate the digit from every cell they both see. When
 * the free ends share a region too the chain closes into a ring, which
 * eliminates around every bridge as well.
 *
 * A link is either an ordinary conjugate pair -- exactly two positions for the
 * digit in a region -- or a *grouped* link: up to six positions confined to two
 * "blades", so that one blade being empty forces the other. For a block the
 * blades are the row-mates and column-mates of a heart cell with the remaining
 * rectangle empty (the Empty Rectangle pattern: nine heart configurations plus
 * six whole-line ones); for a row or column they are two of its three thirds
 * with the remaining third empty.
 *
 * The rating build reaches only blocks, rows and columns. getHints permutes
 * region types drawn from Settings' variant list and SeFastEntry.configure
 * leaves every variant off, so linkSet entries are always 0, 1 or 2.
 *
 * Like the wing family this collects every hint and reports the sort minimum
 * rather than the first found, but its comparator is difficulty ascending,
 * eliminations descending, then suffix in *forward* lexicographic order -- the
 * opposite of the wings' reverse order. Ties go to the earliest generated
 * because Collections.sort is stable, so the search order here has to match the
 * Java exactly: region-type set, then digit, then the nested region loops, then
 * link permutation and direction.
 */

namespace {

constexpr int kMaxLinks = 4;

/** Region positions that must be empty of the digit, per configuration. */
constexpr int kBlockEmpty[15][4] = {
    {4, 5, 7, 8}, {3, 5, 6, 8}, {3, 4, 6, 7},
    {1, 2, 7, 8}, {0, 2, 6, 8}, {0, 1, 6, 7},
    {1, 2, 4, 5}, {0, 2, 3, 5}, {0, 1, 3, 4},
    {6, 7, 8, -1}, {3, 4, 5, -1}, {0, 1, 2, -1},
    {2, 5, 8, -1}, {1, 4, 7, -1}, {0, 3, 6, -1},
};

/** The two blades of each block configuration: positions 0-2 and 3-5. */
constexpr int kBlockGrouped[15][6] = {
    {3, 6, -1, 1, 2, -1}, {4, 7, -1, 0, 2, -1}, {5, 8, -1, 0, 1, -1},
    {0, 6, -1, 4, 5, -1}, {1, 7, -1, 3, 5, -1}, {2, 8, -1, 3, 4, -1},
    {0, 3, -1, 7, 8, -1}, {1, 4, -1, 6, 8, -1}, {2, 5, -1, 6, 7, -1},
    {0, 1, 2, 3, 4, 5}, {0, 1, 2, 6, 7, 8}, {3, 4, 5, 6, 7, 8},
    {0, 3, 6, 1, 4, 7}, {0, 3, 6, 2, 5, 8}, {1, 4, 7, 2, 5, 8},
};

constexpr int kLineEmpty[3][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
constexpr int kLineGrouped[3][6] = {
    {3, 4, 5, 6, 7, 8}, {0, 1, 2, 6, 7, 8}, {0, 1, 2, 3, 4, 5}};

/** Grid.Region.Rectangle / lineEmptyCells, as a nine-bit position mask. */
uint16_t emptyMask(int regionType, int config) {
    uint16_t mask = 0;
    if (regionType == 0) {
        for (int at = 0; at < 4; ++at)
            if (kBlockEmpty[config][at] >= 0)
                mask |= static_cast<uint16_t>(1u << kBlockEmpty[config][at]);
    } else {
        for (int at = 0; at < 3; ++at)
            mask |= static_cast<uint16_t>(1u << kLineEmpty[config][at]);
    }
    return mask;
}

/** crossBlade1 / crossBlade2 for a block, lineBlade1 / lineBlade2 for a line. */
uint16_t bladeMask(int regionType, int config, int blade) {
    uint16_t mask = 0;
    for (int at = blade * 3; at < blade * 3 + 3; ++at) {
        int position = regionType == 0 ? kBlockGrouped[config][at]
                                       : kLineGrouped[config][at];
        if (position >= 0) mask |= static_cast<uint16_t>(1u << position);
    }
    return mask;
}

int nextSetBit(uint16_t mask, int from) {
    for (int bit = from; bit < 16; ++bit)
        if ((mask >> bit) & 1u) return bit;
    return -1;
}

/** Grid.Region.toFullNumber: region type times ten plus the one-based index. */
int fullNumber(int regionType, int index) { return regionType * 10 + index + 1; }

int blockOf(int cell) { return cell % 9 / 3 + cell / 27 * 3; }

const std::bitset<kCells>& visibleMask(int cell) {
    static const auto table = [] {
        std::array<std::bitset<kCells>, kCells> result{};
        for (int source = 0; source < kCells; ++source)
            for (int other : visibleCells(source)) result[source].set(other);
        return result;
    }();
    return table[cell];
}

const std::bitset<kCells>& regionMask(int type, int index) {
    static const auto table = [] {
        std::array<std::array<std::bitset<kCells>, 9>, kRegionTypes> result{};
        for (int type = 0; type < kRegionTypes; ++type)
            for (int index = 0; index < 9; ++index)
                for (int cell : regionCells(type, index))
                    result[type][index].set(cell);
        return result;
    }();
    return table[type][index];
}

struct RegionRef {
    int type = -1;
    int index = -1;
    bool valid() const { return type >= 0; }
};

/**
 * StrongLinks.shareRegionOf: the region holding both link ends and every
 * grouped support they carry. Column is tried first, then row, then block --
 * the order decides which region the hint reports, so it is not cosmetic.
 */
RegionRef shareRegionOf(int bridge1, int bridge1Support, int bridge2,
                        int bridge2Support, int bridge1Support2,
                        int bridge2Support2) {
    int first = bridge1 >= 0 ? bridge1 : bridge1Support;
    int second = bridge2 >= 0 ? bridge2 : bridge2Support;
    for (int type = 2; type >= 0; --type) {
        auto coordinate = [type](int cell) {
            return type == 2 ? cell % 9 : type == 1 ? cell / 9 : blockOf(cell);
        };
        int home = coordinate(first);
        if (home != coordinate(second)) continue;
        bool same = true;
        if (bridge1Support >= 0) {
            same = coordinate(bridge1Support) == home;
            if (bridge1Support2 >= 0 && same)
                same = coordinate(bridge1Support2) == home;
        }
        if (bridge2Support >= 0 && same) {
            same = coordinate(bridge2Support) == home;
            if (bridge2Support2 >= 0 && same)
                same = coordinate(bridge2Support2) == home;
        }
        if (same) return {type, home};
    }
    return {};
}

bool isSameLine(int first, int second) {
    return first % 9 == second % 9 || first / 9 == second / 9;
}

/** StrongLinks.isLex: the digit string is no greater than its reverse. */
bool isLex(const int* values, int count) {
    for (int at = 0; at < count; ++at) {
        int mirrored = values[count - 1 - at];
        if (values[at] != mirrored) return values[at] < mirrored;
    }
    return true;
}

/**
 * StrongLinksHint.rLineName: the min-lex name of the link-type sequence, with
 * the two line types interchangeable so an isomorphic pattern keeps its name.
 */
std::string rLineName(const int* set, int count) {
    std::string original, lines, reverseSwap, currentSwap;
    bool isThereOne = false;
    for (int at = 0; at < count; ++at) {
        std::string digit = std::to_string(set[at]);
        if (set[at] == 1) {
            isThereOne = true;
            reverseSwap = "2" + reverseSwap;
            currentSwap += "2";
        }
        original += digit;
        if (set[at] == 2) {
            lines += "1";
            reverseSwap = "1" + reverseSwap;
            currentSwap += "1";
        } else {
            lines += digit;
            if (set[at] < 1 || set[at] > 2) {
                reverseSwap = digit + reverseSwap;
                currentSwap += digit;
            }
        }
    }
    if (reverseSwap > currentSwap) reverseSwap = currentSwap;
    if (isThereOne) return original > reverseSwap ? reverseSwap : original;
    return lines;
}

/** StrongLinksHint.baseRatings, in tenths. */
constexpr int kBaseRatings[8] = {0, 40, 54, 58, 62, 66, 70, 74};

struct Hint {
    bool found = false;
    int rating = 0;
    int eliminations = 0;
    std::string suffix;
    std::array<uint16_t, kCells> removals{};
};

/** The getHints comparator: difficulty, then eliminations, then suffix. */
bool precedes(const Hint& candidate, const Hint& incumbent) {
    if (candidate.rating != incumbent.rating)
        return candidate.rating < incumbent.rating;
    if (candidate.eliminations != incumbent.eliminations)
        return candidate.eliminations > incumbent.eliminations;
    return candidate.suffix < incumbent.suffix;
}

struct Search {
    explicit Search(const Board& board) : board(board) {}

    const Board& board;
    int links = 0;
    int digit = 0;
    std::array<int, kMaxLinks> linkSet{};

    // cells[2d] and cells[2d + 1] are link d's two ends; the same offsets plus
    // 2 * links hold their grouped supports and plus 4 * links their second
    // supports. -1 stands for the Java null.
    std::array<int, kMaxLinks * 6> cells{};
    std::array<int, kMaxLinks * 6> baseRegionCells{};
    std::array<int, kMaxLinks * 6> emptyRegionCells{};
    int baseCount = 0;
    int emptyCount = 0;
    std::array<int, kMaxLinks> baseLinkRegion{};
    std::array<bool, kMaxLinks> baseLinkEmptyRegion{};
    std::array<int, kMaxLinks> heartConfig{};

    Hint best;

    uint16_t potentialPositions(int type, int index) const {
        uint16_t mask = 0;
        const auto& members = regionCells(type, index);
        for (int at = 0; at < 9; ++at)
            if ((board.candidates[members[at]] >> digit) & 1u)
                mask |= static_cast<uint16_t>(1u << at);
        return mask;
    }

    bool holdsDigit(int cell) const {
        return ((board.candidates[cell] >> digit) & 1u) != 0;
    }

    int cellAt(int depth, int position) const {
        return regionCells(linkSet[depth], baseLinkRegion[depth])[position];
    }

    void buildLinks(int depth, int previousIndex);
    void emitHints();
    void emitFor(const int* order, const int* direction);
    void addHint(const int* order, int start, int end, const int* bridge1,
                 const int* bridge1Support, const int* bridge1Support2,
                 const int* bridge2, const int* bridge2Support,
                 const int* bridge2Support2, int startSupport,
                 int startSupport2, int endSupport, int endSupport2,
                 const RegionRef* shareRegion, RegionRef ringRegion);
};

void Search::buildLinks(int depth, int previousIndex) {
    if (depth >= links) return;
    int from = depth > 0 && linkSet[depth - 1] == linkSet[depth]
            ? previousIndex + 1 : 0;
    int type = linkSet[depth];
    for (int index = from; index < 9; ++index) {
        baseLinkEmptyRegion[depth] = false;
        baseLinkRegion[depth] = index;
        uint16_t positions = potentialPositions(type, index);
        int count = bitCount(positions);
        heartConfig[depth] = 0;
        if (count <= 1) continue;
        if (count > 6) continue;

        uint16_t blade1 = positions, blade2 = positions, heart = positions;
        if (count > 2) {
            int limit = type < 1 ? 15 : 3;
            for (heartConfig[depth] = 0; heartConfig[depth] < limit;
                 ++heartConfig[depth]) {
                int config = heartConfig[depth];
                // Configurations 9 to 14 confine the digit to two whole lines,
                // which needs four positions to say anything.
                if (config > 8 && count < 4) continue;
                blade1 = positions;
                blade2 = positions;
                heart = positions;
                if ((positions & emptyMask(type, config)) != 0) continue;
                blade1 &= bladeMask(type, config, 0);
                blade2 &= bladeMask(type, config, 1);
                if (type == 0) heart &= static_cast<uint16_t>(1u << config);
                if (blade1 != 0 && blade2 != 0) baseLinkEmptyRegion[depth] = true;
                break;
            }
            if (!baseLinkEmptyRegion[depth]) continue;
        }

        for (int ordinal = 0; ordinal < 2; ++ordinal) {
            cells[depth * 2 + links * 2 + 0] = -1;
            cells[depth * 2 + links * 4 + 0] = -1;
            cells[depth * 2 + links * 2 + 1] = -1;
            cells[depth * 2 + links * 4 + 1] = -1;
            bool reducedEmptyLine = false;
            if (baseLinkEmptyRegion[depth]) {
                int blade1Count = bitCount(blade1);
                int blade2Count = bitCount(blade2);
                if (blade1Count == 1 || blade2Count == 1) {
                    if (ordinal == 0) {
                        if (blade1Count != 1) continue;
                        int p1;
                        if (type == 0) {
                            cells[depth * 2 + 0] =
                                    cellAt(depth, nextSetBit(blade1, 0));
                            cells[links * 2 + depth * 2 + 1] =
                                    cellAt(depth, heartConfig[depth]);
                            cells[links * 2 + depth * 2 + 0] = -1;
                            cells[depth * 2 + links * 4 + 0] = -1;
                            cells[depth * 2 + links * 4 + 1] = -1;
                            cells[depth * 2 + 1] =
                                    cellAt(depth, p1 = nextSetBit(blade2, 0));
                            if (blade2Count > 1)
                                cells[depth * 2 + links * 4 + 1] =
                                        cellAt(depth, nextSetBit(blade2, p1 + 1));
                        } else {
                            int p2;
                            cells[depth * 2 + 0] =
                                    cellAt(depth, nextSetBit(blade1, 0));
                            cells[depth * 2 + 1] =
                                    cellAt(depth, p1 = nextSetBit(blade2, 0));
                            cells[links * 2 + depth * 2 + 0] = -1;
                            cells[depth * 2 + links * 4 + 0] = -1;
                            cells[depth * 2 + links * 4 + 1] = -1;
                            cells[links * 2 + depth * 2 + 1] =
                                    cellAt(depth, p2 = nextSetBit(blade2, p1 + 1));
                            if (blade2Count > 2)
                                cells[depth * 2 + links * 4 + 1] =
                                        cellAt(depth, nextSetBit(blade2, p2 + 1));
                        }
                        // Both blades single: the heart is dropped here and the
                        // ordinal-1 branch below rewrites the pair.
                        if (blade2Count == 1) {
                            cells[depth * 2 + 0] =
                                    cellAt(depth, nextSetBit(blade1, 0));
                            cells[depth * 2 + 1] =
                                    cellAt(depth, nextSetBit(blade2, 0));
                            cells[links * 2 + depth * 2 + 1] = -1;
                            cells[links * 2 + depth * 2 + 0] = -1;
                            cells[depth * 2 + links * 4 + 0] = -1;
                            cells[depth * 2 + links * 4 + 1] = -1;
                            ordinal = 1;
                        }
                    }
                    if (ordinal == 1) {
                        if (blade2Count != 1) continue;
                        int p1;
                        if (type == 0) {
                            cells[depth * 2 + 0] =
                                    cellAt(depth, nextSetBit(blade2, 0));
                            cells[links * 2 + depth * 2 + 1] =
                                    cellAt(depth, heartConfig[depth]);
                            cells[depth * 2 + 1] =
                                    cellAt(depth, p1 = nextSetBit(blade1, 0));
                            cells[links * 2 + depth * 2 + 0] = -1;
                            cells[depth * 2 + links * 4 + 0] = -1;
                            cells[depth * 2 + links * 4 + 1] = -1;
                            if (blade1Count > 1)
                                cells[depth * 2 + links * 4 + 1] =
                                        cellAt(depth, nextSetBit(blade1, p1 + 1));
                        } else {
                            int p2;
                            cells[depth * 2 + 0] =
                                    cellAt(depth, nextSetBit(blade2, 0));
                            cells[depth * 2 + 1] =
                                    cellAt(depth, p2 = nextSetBit(blade1, 0));
                            cells[links * 2 + depth * 2 + 1] =
                                    cellAt(depth, p1 = nextSetBit(blade1, p2 + 1));
                            cells[links * 2 + depth * 2 + 0] = -1;
                            cells[depth * 2 + links * 4 + 0] = -1;
                            cells[depth * 2 + links * 4 + 1] = -1;
                            if (blade1Count > 2)
                                cells[depth * 2 + links * 4 + 1] =
                                        cellAt(depth, nextSetBit(blade1, p1 + 1));
                        }
                    }
                } else {
                    int p1, p2, p3, p4;
                    cells[depth * 2 + links * 4 + 0] = -1;
                    cells[depth * 2 + links * 4 + 1] = -1;
                    cells[depth * 2 + 0] = cellAt(depth, p1 = nextSetBit(blade1, 0));
                    cells[links * 2 + depth * 2 + 0] =
                            cellAt(depth, p3 = nextSetBit(blade1, p1 + 1));
                    if (blade1Count > 2)
                        cells[depth * 2 + links * 4 + 0] =
                                cellAt(depth, nextSetBit(blade1, p3 + 1));
                    cells[depth * 2 + 1] = cellAt(depth, p2 = nextSetBit(blade2, 0));
                    cells[links * 2 + depth * 2 + 1] =
                            cellAt(depth, p4 = nextSetBit(blade2, p2 + 1));
                    if (blade2Count > 2)
                        cells[depth * 2 + links * 4 + 1] =
                                cellAt(depth, nextSetBit(blade2, p4 + 1));
                    // The reduced, equivalent form of the whole-line block
                    // configurations, where both pairs already share a line.
                    if (heartConfig[depth] > 8 && count == 4
                            && isSameLine(cells[links * 2 + depth * 2 + 0],
                                          cells[links * 2 + depth * 2 + 1])
                            && isSameLine(cells[depth * 2 + 0],
                                          cells[depth * 2 + 1]))
                        reducedEmptyLine = true;
                    if (reducedEmptyLine && ordinal == 1
                            && cells[depth * 2 + 1]
                                    == cellAt(depth, nextSetBit(blade2, 0))) {
                        cells[depth * 2 + 1] =
                                cellAt(depth, nextSetBit(blade1, p1 + 1));
                        cells[links * 2 + depth * 2 + 0] =
                                cellAt(depth, nextSetBit(blade2, 0));
                    }
                    if (!reducedEmptyLine) {
                        ordinal = 1;
                        if (heartConfig[depth] < 9 && bitCount(heart) == 1) {
                            if (blade1Count == 2)
                                cells[depth * 2 + links * 4 + 0] =
                                        cellAt(depth, heartConfig[depth]);
                            if (blade2Count == 2)
                                cells[depth * 2 + links * 4 + 1] =
                                        cellAt(depth, heartConfig[depth]);
                        }
                    }
                }
            } else {
                ordinal = 1;
                int p2;
                cells[depth * 2 + 0] = cellAt(depth, p2 = nextSetBit(positions, 0));
                cells[depth * 2 + 1] = cellAt(depth, nextSetBit(positions, p2 + 1));
                cells[depth * 2 + links * 2 + 0] = -1;
                cells[depth * 2 + links * 2 + 1] = -1;
                cells[depth * 2 + links * 4 + 0] = -1;
                cells[depth * 2 + links * 4 + 1] = -1;
                if (type == 0
                        && isSameLine(cells[depth * 2 + 0], cells[depth * 2 + 1]))
                    continue;
            }

            int before = baseCount;
            for (int at = 0; at < 9; ++at) {
                int cell = cellAt(depth, at);
                if (!holdsDigit(cell)) continue;
                baseRegionCells[baseCount++] = cell;
                if (baseLinkEmptyRegion[depth]) emptyRegionCells[emptyCount++] = cell;
            }
            // No base region may share a candidate cell with an earlier one.
            bool overlaps = false;
            for (int at = 0; at < before && !overlaps; ++at)
                for (int other = before; other < baseCount; ++other)
                    if (baseRegionCells[at] == baseRegionCells[other]) {
                        overlaps = true;
                        break;
                    }
            if (!overlaps) {
                buildLinks(depth + 1, index);
                if (depth == links - 1) emitHints();
            }
            for (int at = 0; at < 9; ++at) {
                if (!holdsDigit(cellAt(depth, at))) continue;
                baseRegionCells[--baseCount] = -1;
                if (baseLinkEmptyRegion[depth]) emptyRegionCells[--emptyCount] = -1;
            }
        }
    }
}

/**
 * Every ordering of the links and every direction of each: QuickPerm over the
 * order keeping only the min-lex half, and a plain binary count over the
 * directions with the first link most significant.
 */
void Search::emitHints() {
    int order[kMaxLinks];
    int control[kMaxLinks + 1];
    int direction[kMaxLinks];
    for (int at = 0; at < links; ++at) {
        order[at] = at;
        control[at] = at;
    }
    control[links] = links;
    auto allDirections = [&] {
        for (int bits = 0; bits < (1 << links); ++bits) {
            for (int at = 0; at < links; ++at)
                direction[at] = (bits >> (links - 1 - at)) & 1;
            emitFor(order, direction);
        }
    };
    if (isLex(order, links)) allDirections();
    int upper = 1;
    while (upper < links) {
        control[upper]--;
        int lower = upper % 2 * control[upper];
        std::swap(order[lower], order[upper]);
        if (isLex(order, links)) allDirections();
        upper = 1;
        while (control[upper] == 0) {
            control[upper] = upper;
            upper++;
        }
    }
}

void Search::emitFor(const int* order, const int* direction) {
    int bridge1[kMaxLinks], bridge1Support[kMaxLinks], bridge1Support2[kMaxLinks];
    int bridge2[kMaxLinks], bridge2Support[kMaxLinks], bridge2Support2[kMaxLinks];
    RegionRef shareRegion[kMaxLinks];
    for (int at = 0; at < links - 1; ++at) {
        int head = 2 * order[at] + (1 - direction[at]);
        int tail = 2 * order[at + 1] + direction[at + 1];
        bridge1[at] = cells[head];
        bridge1Support[at] = cells[head + 2 * links];
        bridge1Support2[at] = cells[head + 4 * links];
        bridge2[at] = cells[tail];
        bridge2Support[at] = cells[tail + 2 * links];
        bridge2Support2[at] = cells[tail + 4 * links];
        shareRegion[at] = shareRegionOf(bridge1[at], bridge1Support[at],
                                        bridge2[at], bridge2Support[at],
                                        bridge1Support2[at], bridge2Support2[at]);
        if (!shareRegion[at].valid()) return;
    }

    int head = 2 * order[0] + direction[0];
    int tail = 2 * order[links - 1] + (1 - direction[links - 1]);
    int start = cells[head];
    int startSupport = cells[head + 2 * links];
    int startSupport2 = cells[head + 4 * links];
    int end = cells[tail];
    int endSupport = cells[tail + 2 * links];
    int endSupport2 = cells[tail + 4 * links];

    RegionRef ringRegion = shareRegionOf(start, startSupport, end, endSupport,
                                         startSupport2, endSupport2);
    if (ringRegion.valid()) {
        // isRegionMinLex: a ring is reported once, from its lowest numbered
        // base region and running forwards.
        int lowest = fullNumber(linkSet[0], baseLinkRegion[0]);
        for (int at = 1; at < links; ++at)
            lowest = std::min(lowest, fullNumber(linkSet[at], baseLinkRegion[at]));
        if (fullNumber(linkSet[order[0]], baseLinkRegion[order[0]]) != lowest
                || direction[0] != 0)
            return;
    }
    addHint(order, start, end, bridge1, bridge1Support, bridge1Support2,
            bridge2, bridge2Support, bridge2Support2, startSupport,
            startSupport2, endSupport, endSupport2, shareRegion, ringRegion);
}

void Search::addHint(const int* order, int start, int end, const int* bridge1,
                     const int* bridge1Support, const int* bridge1Support2,
                     const int* bridge2, const int* bridge2Support,
                     const int* bridge2Support2, int startSupport,
                     int startSupport2, int endSupport, int endSupport2,
                     const RegionRef* shareRegion, RegionRef ringRegion) {
    bool ring = ringRegion.valid();
    std::bitset<kCells> victims = visibleMask(start) & visibleMask(end);
    if (baseLinkEmptyRegion[order[0]] && startSupport >= 0) {
        victims &= visibleMask(startSupport);
        if (startSupport2 >= 0) victims &= visibleMask(startSupport2);
    }
    if (baseLinkEmptyRegion[order[links - 1]] && endSupport >= 0) {
        victims &= visibleMask(endSupport);
        if (endSupport2 >= 0) victims &= visibleMask(endSupport2);
    }
    victims.reset(start);
    victims.reset(end);
    for (int at = 0; at < links; ++at)
        victims &= ~regionMask(linkSet[order[at]], baseLinkRegion[order[at]]);
    // A ring keeps its shared regions eligible; an open chain does not.
    if (!ring)
        for (int at = 0; at < links - 1; ++at)
            victims &= ~regionMask(shareRegion[at].type, shareRegion[at].index);

    Hint hint;
    // eliminationsTotal counts every map write, so a cell reached from more
    // than one ring bridge is counted more than once. That count feeds the
    // sort, so the double count is reproduced rather than cleaned up.
    for (int cell = 0; cell < kCells; ++cell) {
        if (!victims.test(cell) || !holdsDigit(cell)) continue;
        hint.removals[cell] = static_cast<uint16_t>(1u << digit);
        hint.eliminations++;
    }
    if (ring) {
        for (int at = 0; at < links - 1; ++at) {
            std::bitset<kCells> around =
                    visibleMask(bridge1[at]) & visibleMask(bridge2[at]);
            // Unlike the chain ends these look only at whether a support
            // exists, not at whether that link was grouped.
            if (bridge1Support[at] >= 0) {
                around &= visibleMask(bridge1Support[at]);
                if (bridge1Support2[at] >= 0)
                    around &= visibleMask(bridge1Support2[at]);
            }
            if (bridge2Support[at] >= 0) {
                around &= visibleMask(bridge2Support[at]);
                if (bridge2Support2[at] >= 0)
                    around &= visibleMask(bridge2Support2[at]);
            }
            around.reset(bridge1[at]);
            around.reset(bridge2[at]);
            for (int link = 0; link < links; ++link)
                around &= ~regionMask(linkSet[order[link]],
                                      baseLinkRegion[order[link]]);
            for (int cell = 0; cell < kCells; ++cell) {
                if (!around.test(cell) || !holdsDigit(cell)) continue;
                hint.removals[cell] = static_cast<uint16_t>(1u << digit);
                hint.eliminations++;
            }
        }
    }
    bool worth = false;
    for (uint16_t mask : hint.removals)
        if (mask != 0) worth = true;
    if (!worth) return;

    int grouped = 0;
    for (int at = 0; at < links; ++at)
        if (baseLinkEmptyRegion[at]) grouped++;

    int actual[kMaxLinks];
    for (int at = 0; at < links; ++at) actual[at] = linkSet[order[at]];
    hint.suffix = std::to_string(grouped);
    if (isLex(actual, links)) {
        hint.suffix += rLineName(actual, links);
    } else {
        int reversed[kMaxLinks];
        for (int at = 0; at < links; ++at) reversed[at] = actual[links - 1 - at];
        hint.suffix += rLineName(reversed, links);
    }

    // getDifficulty searches the suffix from index 1, so the leading
    // grouped-link count never takes part.
    const std::string& suffix = hint.suffix;
    bool hasBlock = suffix.find('0', 1) != std::string::npos;
    bool hasSecondLine = suffix.find('2', 1) != std::string::npos;
    bool variant = false;
    for (char type = '3'; type <= '9'; ++type)
        if (suffix.find(type, 1) != std::string::npos) variant = true;
    int base = kBaseRatings[links - 1];
    if (grouped > 0 || variant) hint.rating = base + 3;
    else if (!hasBlock && !hasSecondLine) hint.rating = base;
    else if (hasBlock && hasSecondLine) hint.rating = base + 2;
    else hint.rating = base + 1;

    hint.found = true;
    if (!best.found || precedes(hint, best)) best = std::move(hint);
}

/**
 * The top-level getHints: every non-decreasing region-type sequence, in the
 * order Permutations yields it, then every digit.
 */
bool findStrongLinks(const Board& board, int degree, Hint& best) {
    if (degree < 2 || degree > kMaxLinks) return false;
    static const int kVariants[3] = {0, 1, 2};
    unsigned width = static_cast<unsigned>(3 * degree);
    Search search{board};
    search.links = degree;
    // Permutations(degree, 3 * degree): all combinations in increasing binary
    // order. Only those whose bit numbers are congruent to their own position
    // modulo the degree survive, which leaves the non-decreasing type sets.
    for (unsigned value = (1u << degree) - 1; value < (1u << width);) {
        int indexes[kMaxLinks];
        int at = 0;
        for (unsigned bit = 0; bit < width; ++bit)
            if ((value >> bit) & 1u) indexes[at++] = static_cast<int>(bit);
        int accepted = 0;
        for (; accepted < degree; ++accepted) {
            if (indexes[accepted] % degree != accepted) break;
            search.linkSet[accepted] = kVariants[indexes[accepted] / degree];
        }
        if (accepted == degree) {
            for (int digit = 1; digit <= 9; ++digit) {
                search.digit = digit;
                search.cells.fill(-1);
                search.baseRegionCells.fill(-1);
                search.emptyRegionCells.fill(-1);
                search.baseCount = 0;
                search.emptyCount = 0;
                search.baseLinkEmptyRegion.fill(false);
                search.heartConfig.fill(0);
                search.buildLinks(0, 0);
            }
        }
        unsigned smallest = value & (~value + 1u);
        unsigned ripple = value + smallest;
        unsigned ones = value ^ ripple;
        ones = (ones >> 2) / smallest;
        value = ripple | ones;
    }
    best = std::move(search.best);
    return best.found;
}

}  // namespace

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_strong_links(
        const char* state, int degree) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::Hint hint;
        if (!sefast::findStrongLinks(board, degree, hint)) return "";
        sefast::RuleHint key;
        key.rating = hint.rating;
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
