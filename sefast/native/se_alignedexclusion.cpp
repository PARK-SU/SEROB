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
 * Aligned Triplet Exclusion and the general n-cell form, ported from
 * diuf.sudoku.solver.rules.AlignedExclusion and AlignedExclusionHint.
 *
 * AlignedPairExclusion overrides getHints with its own simpler search, so it
 * lives in se_alignedpair.cpp; this is the general algorithm.
 *
 * The first two base cells come from a Twomutations walk. The remaining ones
 * are drawn from the "twin area", the excluders of those two that are
 * themselves base cells. That set is a LinkedHashSet in the Java, so its
 * insertion order decides which triplet is reported first and has to be
 * reproduced exactly.
 */
struct AlignedExclusionHint {
    std::array<uint16_t, kCells> removals{};
};

/** AlignedExclusionHint.getDifficulty; degrees above 3 are unsupported there. */
inline int alignedExclusionRating(int degree) {
    return degree == 2 ? 62 : 75;   // 6.2, 7.5
}

namespace {

/**
 * Whether a value assignment survives: no two base cells that see each other
 * may take the same value, and every common excluder must keep a value of its
 * own.
 */
bool combinationAllowed(const Board& board, const std::vector<int>& cells,
                        const std::vector<int>& values,
                        const std::vector<int>& commonExcluders) {
    const int degree = static_cast<int>(cells.size());
    for (int i = 0; i < degree; ++i)
        for (int j = i + 1; j < degree; ++j)
            if (values[i] == values[j] && isPeer(cells[i], cells[j]))
                return false;
    uint16_t taken = 0;
    for (int value : values) taken |= static_cast<uint16_t>(1u << value);
    for (int excluder : commonExcluders)
        if ((board.candidates[excluder] & ~taken) == 0) return false;
    return true;
}

/**
 * The value assignments the base cells may still take. The Java walks its
 * odometer downwards, but every assignment is visited exactly once either way
 * and only the resulting set is used, so the direction does not matter.
 */
bool buildAlignedExclusionHint(const Board& board, const std::vector<int>& cells,
                               const std::vector<int>& commonExcluders,
                               AlignedExclusionHint& hint) {
    const int degree = static_cast<int>(cells.size());
    std::vector<std::vector<int>> choices(degree);
    for (int i = 0; i < degree; ++i)
        for (int value = 1; value <= 9; ++value)
            if ((board.candidates[cells[i]] & (1u << value)) != 0)
                choices[i].push_back(value);

    std::vector<uint16_t> allowed(degree, 0);
    std::vector<int> at(degree, 0);
    std::vector<int> values(degree, 0);
    for (;;) {
        for (int i = 0; i < degree; ++i) values[i] = choices[i][at[i]];
        if (combinationAllowed(board, cells, values, commonExcluders))
            for (int i = 0; i < degree; ++i)
                allowed[i] |= static_cast<uint16_t>(1u << values[i]);

        int carry = 0;
        while (carry < degree) {
            if (++at[carry] < static_cast<int>(choices[carry].size())) break;
            at[carry] = 0;
            ++carry;
        }
        if (carry == degree) break;
    }

    std::array<uint16_t, kCells> removals{};
    bool worth = false;
    for (int i = 0; i < degree; ++i) {
        uint16_t removable = static_cast<uint16_t>(
                board.candidates[cells[i]] & ~allowed[i]);
        if (removable != 0) {
            removals[cells[i]] = removable;
            worth = true;
        }
    }
    if (!worth) return false;
    hint.removals = removals;
    return true;
}

}  // namespace

/** AlignedExclusion.getHints under SingleHintAccumulator semantics. */
inline bool findAlignedExclusion(const Board& board, int degree,
                                 AlignedExclusionHint& hint) {
    if (degree < 3) return false;

    // Base cells, each with the peers that could exclude for them.
    std::vector<int> candidates;
    std::vector<std::vector<int>> excluders;
    std::array<int, kCells> candidateAt{};
    candidateAt.fill(-1);
    for (int cell = 0; cell < kCells; ++cell) {
        if (bitCount(board.candidates[cell]) < 2) continue;
        bool hasNakedSingle = false;
        std::vector<int> cellExcluders;
        for (int peer : visibleCells(cell)) {
            int count = bitCount(board.candidates[peer]);
            if (count == 1) hasNakedSingle = true;
            else if (count >= 2 && count <= degree) cellExcluders.push_back(peer);
        }
        // The technique is skipped entirely while a naked single remains.
        if (hasNakedSingle || cellExcluders.empty()) continue;
        candidateAt[cell] = static_cast<int>(candidates.size());
        candidates.push_back(cell);
        excluders.push_back(std::move(cellExcluders));
    }
    if (static_cast<int>(candidates.size()) < degree) return false;

    // Twomutations(2, n): (0,1), (0,2), (1,2), (0,3), (1,3), (2,3), ...
    const int count = static_cast<int>(candidates.size());
    for (int second = 1; second < count; ++second) {
        for (int first = 0; first < second; ++first) {
            int cell0 = candidates[first];
            int cell1 = candidates[second];

            // twinArea: the two cells' excluders, in LinkedHashSet insertion
            // order, keeping only other base cells.
            std::vector<int> tail;
            std::array<bool, kCells> seen{};
            for (int source : {first, second})
                for (int excluder : excluders[source]) {
                    if (seen[excluder]) continue;
                    seen[excluder] = true;
                    if (candidateAt[excluder] < 0) continue;
                    if (excluder == cell0 || excluder == cell1) continue;
                    tail.push_back(excluder);
                }
            const int tailCount = static_cast<int>(tail.size());
            if (tailCount < degree - 2) continue;

            // Permutations(degree - 2, tailCount) over indexes into the tail.
            // 64-bit, like the Java: the twin area can hold up to 40 cells,
            // so a 32-bit shift would be undefined here.
            const int pick = degree - 2;
            uint64_t permValue = (uint64_t(1) << pick) - 1;
            const uint64_t permCarry = (uint64_t(1) << (tailCount - pick)) - 1;
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

                std::vector<int> cells = {cell0, cell1};
                for (int bit = 0; bit < tailCount; ++bit)
                    if ((current & (uint64_t(1) << bit)) != 0)
                        cells.push_back(tail[bit]);

                // Common excluders, in the first base cell's order.
                std::vector<int> common;
                for (int excluder : excluders[first]) {
                    bool everywhere = true;
                    for (size_t i = 1; i < cells.size() && everywhere; ++i) {
                        const std::vector<int>& other =
                                excluders[candidateAt[cells[i]]];
                        everywhere = false;
                        for (int candidate : other)
                            if (candidate == excluder) { everywhere = true; break; }
                    }
                    if (everywhere) common.push_back(excluder);
                }
                if (common.size() < 2) continue;

                if (buildAlignedExclusionHint(board, cells, common, hint))
                    return true;
            }
        }
    }
    return false;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_aligned_exclusion(
        const char* state, int degree) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::AlignedExclusionHint hint;
        if (!sefast::findAlignedExclusion(board, degree, hint)) return "";
        sefast::RuleHint key;
        key.rating = sefast::alignedExclusionRating(degree);
        key.removals = hint.removals;
        result = sefast::formatHintKey(key);
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}
