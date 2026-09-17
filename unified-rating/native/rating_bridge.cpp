/*
 * Unified rating entry point over the two engines in this module.
 *
 * skfr: Copyright (c) 2011, OWNER: Gerard Penet. Distributed under the BSD
 * licence reproduced in the skfr source distribution.
 * SEROB: derived from Sudoku Explainer, Copyright (C) 2006-2009 Nicolas
 * Juillerat. SPDX-License-Identifier: LGPL-2.1-only
 *
 * Both engines report ER/EP/ED in tenths, so one "er,ep,ed" reply serves all
 * three modes and the host only has to pick a mode.
 */
#include <cstring>
#include <string>

#include "ratingengine.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

extern "C" {
const char* sefast_rate(const char* puzzle, int mode);
const char* sefast_rate_low_current(const char* puzzle);
}

namespace {

// 0 and 1 carry the same meaning they have in sefast_rate, so a module built
// without skfr and one built with it agree; skfr is the mode added on the end.
constexpr int kSe = 0;
constexpr int kSe121 = 1;
constexpr int kSkfr = 2;

bool isPuzzle(const char* puzzle) {
    if (!puzzle || std::strlen(puzzle) != 81) return false;
    for (int cell = 0; cell < 81; ++cell) {
        const char digit = puzzle[cell];
        if (digit != '.' && digit != '0' && (digit < '1' || digit > '9'))
            return false;
    }
    return true;
}

/*
 * skfr reads a mutable, NUL-terminated board that spells empty cells '0', and
 * declines a puzzle it could not rate by reporting ER 0. An empty reply is how
 * this module says "not rated", so that case crosses unchanged.
 */
std::string rateSkfr(const char* puzzle) {
    char board[82];
    for (int cell = 0; cell < 81; ++cell)
        board[cell] = puzzle[cell] == '.' ? '0' : puzzle[cell];
    board[81] = '\0';

    int er = 0, ep = 0, ed = 0, stopped = 0;
    skfr::ratePuzzleC(board, &er, &ep, &ed, &stopped);
    if (er == 0) return std::string();
    return std::to_string(er) + ',' + std::to_string(ep) + ',' + std::to_string(ed);
}

/* The low-rule pass answers most puzzles without paying for the chain rules. */
std::string rateSeCurrent(const char* puzzle) {
    std::string rating = sefast_rate_low_current(puzzle);
    if (rating.empty()) rating = sefast_rate(puzzle, 0);
    return rating;
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char* rating_rate(const char* puzzle, int mode) {
    static std::string result;
    if (!isPuzzle(puzzle)) {
        result = "ERROR,java.lang.IllegalArgumentException,"
                "puzzle must contain 81 characters from ., 0 and 1-9";
        return result.c_str();
    }
    switch (mode) {
        case kSe: result = rateSeCurrent(puzzle); break;
        case kSe121: result = sefast_rate(puzzle, 1); break;
        case kSkfr: result = rateSkfr(puzzle); break;
        default:
            result = "ERROR,java.lang.IllegalArgumentException,"
                    "mode must be 0 (SE), 1 (SE 1.2.1) or 2 (skfr)";
    }
    return result.c_str();
}
