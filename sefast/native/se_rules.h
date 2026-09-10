/*
 * SEROB rating core, derived from SukakuExplainer / Sudoku Explainer.
 * Copyright (C) 2006-2009 Nicolas Juillerat
 * C++ port and modifications by clubDS
 *
 * Licensed under the GNU Lesser General Public License v2.1 only.
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * The hint-key format shared across the Java/C++ seam, and the rule-layer
 * conventions every ported technique follows.
 */
#ifndef SEFAST_SE_RULES_H
#define SEFAST_SE_RULES_H

#include "se_core.h"

#include <sstream>
#include <utility>
#include <vector>

namespace sefast {

/**
 * One technique result in the 87-field form Chaining.decodeFastHint parses:
 * bestCell, rating, complexity, sortKey, placement cell, placement value,
 * then one elimination mask per cell.
 *
 * Ratings are integer tenths throughout. SE difficulties are exact
 * one-decimal constants, so the tenths are what Java's
 * Math.round(difficulty * 10.0) yields, and no technique needs to touch a
 * double. complexity and sortKey are chaining tie-breaks; techniques that
 * have none leave them at zero.
 */
struct RuleHint {
    int bestCell = -1;
    int rating = 0;
    int complexity = 0;
    int sortKey = 0;
    int placementCell = -1;
    int placementValue = 0;
    std::array<uint16_t, kCells> removals{};

    /** IndirectHint.isWorth: a hint that eliminates nothing is discarded. */
    bool worth() const {
        for (uint16_t mask : removals)
            if (mask != 0) return true;
        return false;
    }
};

inline std::string formatHintKey(const RuleHint& hint) {
    std::ostringstream out;
    out << hint.bestCell << ',' << hint.rating << ',' << hint.complexity << ','
        << hint.sortKey << ',' << hint.placementCell << ','
        << hint.placementValue;
    for (uint16_t mask : hint.removals) out << ',' << mask;
    return out.str();
}

}  // namespace sefast

#endif  // SEFAST_SE_RULES_H
