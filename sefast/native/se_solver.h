/*
 * Derived from Sudoku Explainer, Copyright (C) 2006-2009 Nicolas Juillerat.
 * C++ port and modifications by ClubDS, 2026.
 * SPDX-License-Identifier: LGPL-2.1-only
 */
#ifndef SEFAST_SE_SOLVER_H
#define SEFAST_SE_SOLVER_H

#include "se_rules.h"

namespace sefast {

struct SolverHint {
    RuleHint hint;
    std::string key;
    int producerIndex = -1;
};

struct Rating {
    int difficulty = 0;
    int pearl = 0;
    int diamond = 0;
    std::string csv() const;
};

SolverHint nextHint(const Board& grid, int mode, bool low = false);
void applyHint(Board& grid, const RuleHint& hint);
// Java restores its input in finally; taking a copy preserves that contract.
Rating rateBoard(Board grid, int mode, bool low = false, char want = 0);

}  // namespace sefast
#endif
