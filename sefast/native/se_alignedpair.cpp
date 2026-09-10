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
 * Aligned Pair Exclusion, ported from
 * diuf.sudoku.solver.rules.AlignedPairExclusion and AlignedExclusionHint.
 *
 * Base cells are those with at least two candidates, no naked single among
 * their peers, and at least one bivalue peer to exclude with. Pairs of base
 * cells are then walked in Twomutations order, and a candidate combination
 * survives only if some common excluder keeps a value of its own.
 */
struct AlignedPairHint {
    std::array<uint16_t, kCells> removals{};
};

inline int alignedPairRating() { return 62; }  // 6.2

/** AlignedPairExclusion.getHints under SingleHintAccumulator semantics. */
inline bool findAlignedPair(const Board& board, AlignedPairHint& hint) {
    // Base cells, in ascending index order, each with its bivalue excluders.
    std::vector<int> candidates;
    std::vector<std::vector<int>> excluders;
    for (int cell = 0; cell < kCells; ++cell) {
        if (bitCount(board.candidates[cell]) < 2) continue;
        bool hasNakedSingle = false;
        std::vector<int> cellExcluders;
        for (int peer : visibleCells(cell)) {
            int count = bitCount(board.candidates[peer]);
            if (count == 1) hasNakedSingle = true;
            else if (count == 2) cellExcluders.push_back(peer);
        }
        // The technique is skipped entirely while a naked single remains.
        if (hasNakedSingle || cellExcluders.empty()) continue;
        candidates.push_back(cell);
        excluders.push_back(std::move(cellExcluders));
    }
    if (candidates.size() < 2) return false;

    // Twomutations(2, n): (0,1), (0,2), (1,2), (0,3), (1,3), (2,3), ...
    const int count = static_cast<int>(candidates.size());
    for (int second = 1; second < count; ++second) {
        for (int first = 0; first < second; ++first) {
            int cellA = candidates[first];
            int cellB = candidates[second];

            std::vector<int> common;
            for (int excluder : excluders[first]) {
                for (int other : excluders[second])
                    if (other == excluder) { common.push_back(excluder); break; }
            }
            if (common.size() < 2) continue;

            uint16_t valuesA = board.candidates[cellA];
            uint16_t valuesB = board.candidates[cellB];
            bool peers = isPeer(cellA, cellB);

            // Values that survive in at least one allowed combination.
            uint16_t allowedA = 0;
            uint16_t allowedB = 0;
            for (int a = 1; a <= 9; ++a) {
                if ((valuesA & (1u << a)) == 0) continue;
                for (int b = 1; b <= 9; ++b) {
                    if ((valuesB & (1u << b)) == 0) continue;
                    // Hidden single rule: two peers cannot hold one value.
                    if (a == b && peers) continue;
                    uint16_t taken = static_cast<uint16_t>((1u << a) | (1u << b));
                    bool allowed = true;
                    for (int excluder : common) {
                        if ((board.candidates[excluder] & ~taken) == 0) {
                            allowed = false;
                            break;
                        }
                    }
                    if (!allowed) continue;
                    allowedA |= static_cast<uint16_t>(1u << a);
                    allowedB |= static_cast<uint16_t>(1u << b);
                }
            }

            uint16_t removeA = static_cast<uint16_t>(valuesA & ~allowedA);
            uint16_t removeB = static_cast<uint16_t>(valuesB & ~allowedB);
            if (removeA == 0 && removeB == 0) continue;

            std::array<uint16_t, kCells> removals{};
            removals[cellA] = removeA;
            removals[cellB] = removeB;
            hint.removals = removals;
            return true;
        }
    }
    return false;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_aligned_pair(
        const char* state, int) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::AlignedPairHint hint;
        if (!sefast::findAlignedPair(board, hint)) return "";
        sefast::RuleHint key;
        key.rating = sefast::alignedPairRating();
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
