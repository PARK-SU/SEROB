/*
 * SEROB rating core, derived from SukakuExplainer / Sudoku Explainer.
 * Copyright (C) 2006-2009 Nicolas Juillerat
 * C++ port and modifications by clubDS
 *
 * Licensed under the GNU Lesser General Public License v2.1 only.
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Grid representation and board geometry, shared by the chaining core and by
 * every ported solving technique.
 */
#ifndef SEFAST_SE_CORE_H
#define SEFAST_SE_CORE_H

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace sefast {

constexpr int kCells = 81;
constexpr int kPotentialIds = 1460;
constexpr uint16_t kAll = 0x3fe;

struct Board {
    std::array<uint8_t, kCells> values{};
    std::array<uint16_t, kCells> candidates{};

    static Board fromPuzzle(const std::string& puzzle) {
        if (puzzle.size() != kCells)
            throw std::invalid_argument("puzzle must have 81 characters");
        Board result;
        for (int cell = 0; cell < kCells; ++cell) {
            char ch = puzzle[cell];
            if (ch == '.' || ch == '0') {
                result.candidates[cell] = kAll;
            } else if (ch >= '1' && ch <= '9') {
                result.values[cell] = static_cast<uint8_t>(ch - '0');
            } else {
                throw std::invalid_argument("invalid puzzle character");
            }
        }
        for (int cell = 0; cell < kCells; ++cell) {
            if (result.values[cell] == 0) continue;
            int value = result.values[cell];
            for (int peer : peers(cell))
                result.candidates[peer] &= static_cast<uint16_t>(~(1u << value));
            result.candidates[cell] = 0;
        }
        return result;
    }

    static Board fromInput(const std::string& input) {
        if (input.size() == kCells) return fromPuzzle(input);
        if (input.size() != 325 || input[81] != ':')
            throw std::invalid_argument("input must be a puzzle or rating-state hex");
        Board result;
        for (int cell = 0; cell < kCells; ++cell) {
            char ch = input[cell];
            if (ch == '.') result.values[cell] = 0;
            else if (ch >= '1' && ch <= '9')
                result.values[cell] = static_cast<uint8_t>(ch - '0');
            else throw std::invalid_argument("invalid state value");
            int mask = 0;
            for (int digit = 0; digit < 3; ++digit) {
                char hex = input[82 + cell * 3 + digit];
                int value = hex >= '0' && hex <= '9' ? hex - '0'
                    : hex >= 'a' && hex <= 'f' ? hex - 'a' + 10
                    : hex >= 'A' && hex <= 'F' ? hex - 'A' + 10 : -1;
                if (value < 0) throw std::invalid_argument("invalid state candidate mask");
                mask = (mask << 4) | value;
            }
            result.candidates[cell] = static_cast<uint16_t>(mask);
        }
        return result;
    }

    std::string toRatingState() const {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(325);
        for (uint8_t value : values)
            result += value ? static_cast<char>('0' + value) : '.';
        result += ':';
        for (uint16_t mask : candidates) {
            result += hex[(mask >> 8) & 15];
            result += hex[(mask >> 4) & 15];
            result += hex[mask & 15];
        }
        return result;
    }

    static const std::array<int, 20>& peers(int cell) {
        static const auto table = [] {
            std::array<std::array<int, 20>, kCells> result{};
            for (int source = 0; source < kCells; ++source) {
                std::array<bool, kCells> seen{};
                int at = 0;
                int row = source / 9, col = source % 9;
                int boxRow = row / 3 * 3, boxCol = col / 3 * 3;
                auto append = [&](int other) {
                    if (other != source && !seen[other]) {
                        seen[other] = true;
                        result[source][at++] = other;
                    }
                };
                for (int y = boxRow; y < boxRow + 3; ++y)
                    for (int x = boxCol; x < boxCol + 3; ++x)
                        append(y * 9 + x);
                for (int x = 0; x < 9; ++x) append(row * 9 + x);
                for (int y = 0; y < 9; ++y) append(y * 9 + col);
            }
            return result;
        }();
        return table[cell];
    }
};

inline int potentialId(int cell, int value, bool on) {
    return (cell * 9 + value) * 2 + (on ? 1 : 0);
}

inline int potentialCell(int id) {
    return ((id >> 1) - 1) / 9;
}

inline int potentialValue(int id) {
    return ((id >> 1) - 1) % 9 + 1;
}

inline bool potentialOn(int id) {
    return (id & 1) != 0;
}

inline int bitCount(uint16_t value) {
    return __builtin_popcount(static_cast<unsigned>(value));
}

/** Region cells in Grid.Block / Grid.Row / Grid.Column construction order. */
inline const std::array<int, 9>& regionCells(int type, int index) {
    static const auto table = [] {
        std::array<std::array<std::array<int, 9>, 9>, 3> result{};
        for (int at = 0; at < 9; ++at) {
            int vNum = at / 3, hNum = at % 3;
            for (int i = 0; i < 9; ++i) {
                result[0][at][i] = 9 * (vNum * 3 + i / 3) + (hNum * 3 + i % 3);
                result[1][at][i] = 9 * at + i;
                result[2][at][i] = 9 * i + at;
            }
        }
        return result;
    }();
    return table[type][index];
}

/**
 * Grid.visibleCellIndex: a cell's peers in ascending cell-index order.
 *
 * This is not Board::peers, which lists the box first and which the chaining
 * core depends on. Several techniques iterate peers and stop at the first
 * match, so they see hints in this order and no other.
 */
inline const std::array<int, 20>& visibleCells(int cell) {
    static const auto table = [] {
        std::array<std::array<int, 20>, kCells> result{};
        for (int source = 0; source < kCells; ++source) {
            int at = 0;
            for (int other = 0; other < kCells; ++other) {
                if (other == source) continue;
                bool sameRow = other / 9 == source / 9;
                bool sameColumn = other % 9 == source % 9;
                bool sameBox = (other / 9) / 3 == (source / 9) / 3
                        && (other % 9) / 3 == (source % 9) / 3;
                if (sameRow || sameColumn || sameBox) result[source][at++] = other;
            }
        }
        return result;
    }();
    return table[cell];
}

/** Whether `other` is among `cell`'s visible cells. */
inline bool isPeer(int cell, int other) {
    static const auto table = [] {
        std::array<std::array<bool, kCells>, kCells> result{};
        for (int source = 0; source < kCells; ++source)
            for (int other : visibleCells(source))
                result[source][other] = true;
        return result;
    }();
    return table[cell][other];
}

/** Region type count: 0 block, 1 row, 2 column. The rating build reaches no
 *  others, because SeFastEntry.configure leaves every variant region off. */
constexpr int kRegionTypes = 3;

/** Grid.Region.regionCellsBitSet membership. */
inline bool regionContains(int type, int index, int cell) {
    static const auto table = [] {
        std::array<std::array<std::array<bool, kCells>, 9>, kRegionTypes> result{};
        for (int at = 0; at < kRegionTypes; ++at)
            for (int index = 0; index < 9; ++index)
                for (int member : regionCells(at, index))
                    result[at][index][member] = true;
        return result;
    }();
    return table[type][index][cell];
}

/** Grid.Region.crosses: the two regions share at least one cell. */
inline bool regionsCross(int type1, int index1, int type2, int index2) {
    static const auto table = [] {
        std::array<std::array<std::array<std::array<bool, 9>, kRegionTypes>, 9>,
                   kRegionTypes> result{};
        for (int a = 0; a < kRegionTypes; ++a)
            for (int i = 0; i < 9; ++i)
                for (int b = 0; b < kRegionTypes; ++b)
                    for (int j = 0; j < 9; ++j)
                        for (int cell : regionCells(a, i))
                            if (regionContains(b, j, cell)) {
                                result[a][i][b][j] = true;
                                break;
                            }
        return result;
    }();
    return table[type1][index1][type2][index2];
}

}  // namespace sefast

#endif  // SEFAST_SE_CORE_H
