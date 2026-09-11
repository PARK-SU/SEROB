/*
 * Derived from Sudoku Explainer Solver, Grid and Cell.
 * Copyright (C) 2006-2009 Nicolas Juillerat.
 * C++ port and modifications by ClubDS, 2026.
 * SPDX-License-Identifier: LGPL-2.1-only
 */
#include "se_solver.h"

#include <algorithm>

extern "C" {
const char* sefast_best_hidden_single(const char*, int);
const char* sefast_best_naked_single(const char*, int);
const char* sefast_best_locking(const char*, int);
const char* sefast_best_hidden_set(const char*, int);
const char* sefast_best_naked_set(const char*, int);
const char* sefast_best_fisherman(const char*, int);
const char* sefast_best_strong_links(const char*, int);
const char* sefast_best_xy_wing(const char*, int);
const char* sefast_best_unique_loop(const char*, int);
const char* sefast_best_wing(const char*, int);
const char* sefast_best_bug(const char*, int);
const char* sefast_best_aligned_pair(const char*, int);
const char* sefast_best_aligned_exclusion(const char*, int);
const char* sefast_best_static(const char*);
const char* sefast_best_chain(const char*, int, int, int, int, int);
void sefast_reset_rating_diagnostics();
const char* sefast_rating_diagnostics();
}

namespace sefast {
namespace {

using RuleFn = const char* (*)(const char*, int);
struct Producer {
    RuleFn run;
    int param;
};

const char* staticHint(const char* state, int) {
    return sefast_best_static(state);
}

const char* chainHint(const char* state, int param) {
    return sefast_best_chain(state, param / 10000 % 10, param / 1000 % 10,
            param / 100 % 10, param / 10 % 10, param % 10);
}

void checkMode(int mode) {
    if (mode != 0 && mode != 1)
        throw std::invalid_argument("mode must be 0 (current) or 1 (SE 1.2.1)");
}

std::vector<Producer> producers(int mode, bool low) {
    checkMode(mode);
    // Solver.getSingleHint: list order, not a global sort by hint difficulty.
    std::vector<Producer> result{
        {sefast_best_hidden_single, 0}, {sefast_best_locking, 1},
        {sefast_best_hidden_set, 12}, {sefast_best_naked_single, 0},
        {sefast_best_hidden_set, 13}, {sefast_best_locking, 0},
        {sefast_best_naked_set, 2}, {sefast_best_fisherman, 2},
        {sefast_best_hidden_set, 2}, {sefast_best_naked_set, 3},
        {sefast_best_fisherman, 3}, {sefast_best_hidden_set, 3}
    };
    if (mode == 0) result.push_back({sefast_best_strong_links, 2});
    result.insert(result.end(), {
        {sefast_best_xy_wing, 0}, {sefast_best_xy_wing, 1},
        {sefast_best_unique_loop, mode == 0 ? 1 : 0},
        {sefast_best_naked_set, 4}, {sefast_best_fisherman, 4},
        {sefast_best_hidden_set, 4}
    });
    if (mode == 0) result.insert(result.end(), {
        {sefast_best_strong_links, 3}, {sefast_best_wing, 3}
    });
    result.push_back({sefast_best_bug, mode == 0 ? 1 : 0});
    if (mode == 0) result.insert(result.end(), {
        {sefast_best_strong_links, 4}, {sefast_best_wing, 4}
    });
    result.push_back({sefast_best_aligned_pair, 0});
    if (mode == 0) result.push_back({sefast_best_wing, 5});
    result.push_back({staticHint, 0});
    if (mode == 0) result.push_back({sefast_best_wing, 6});
    result.push_back({sefast_best_aligned_exclusion, 3});
    if (!low) {
        for (int param : {1100, 10000, 11000, 11010, 11020, 11030,
                          11040, 11041, 11042, 11043})
            result.push_back({chainHint, param});
    }
    return result;
}

RuleHint decode(const std::string& key) {
    std::istringstream input(key);
    auto field = [&]() {
        std::string value;
        std::getline(input, value, ',');
        return std::stoi(value);
    };
    RuleHint hint;
    hint.bestCell = field();
    hint.rating = field();
    hint.complexity = field();
    hint.sortKey = field();
    hint.placementCell = field();
    hint.placementValue = field();
    for (uint16_t& mask : hint.removals) mask = static_cast<uint16_t>(field());
    return hint;
}

SolverHint findHint(const Board& grid, const std::vector<Producer>& rules) {
    std::string state = grid.toRatingState();
    for (size_t index = 0; index < rules.size(); ++index) {
        std::string key = rules[index].run(state.c_str(), rules[index].param);
        if (key.empty()) continue;
        if (key == "-1" || key.compare(0, 6, "ERROR,") == 0)
            throw std::runtime_error(key);
        return {decode(key), key, static_cast<int>(index)};
    }
    return {};
}

bool isPuzzle(const char* puzzle) {
    if (!puzzle) return false;
    std::string input(puzzle);
    return input.size() == kCells && std::all_of(input.begin(), input.end(),
            [](char ch) { return ch == '.' || (ch >= '1' && ch <= '9'); });
}

}  // namespace

SolverHint nextHint(const Board& grid, int mode, bool low) {
    return findHint(grid, producers(mode, low));
}

void applyHint(Board& grid, const RuleHint& hint) {
    for (int cell = 0; cell < kCells; ++cell)
        grid.candidates[cell] &= static_cast<uint16_t>(~hint.removals[cell]);
    if (hint.placementCell >= 0) {
        int cell = hint.placementCell;
        grid.values[cell] = static_cast<uint8_t>(hint.placementValue);
        grid.candidates[cell] = 0;
        for (int peer : Board::peers(cell))
            grid.candidates[peer] &= static_cast<uint16_t>(~(1u << hint.placementValue));
    }
}

std::string Rating::csv() const {
    return std::to_string(difficulty) + ',' + std::to_string(pearl)
            + ',' + std::to_string(diamond);
}

Rating rateBoard(Board grid, int mode, bool low, char want) {
    const auto rules = producers(mode, low);
    Rating rating;
    while (std::find(grid.values.begin(), grid.values.end(), 0) != grid.values.end()) {
        SolverHint next = findHint(grid, rules);
        if (next.key.empty()) {
            rating.difficulty = 200;
            break;
        }
        rating.difficulty = std::max(rating.difficulty, next.hint.rating);
        applyHint(grid, next.hint);
        if (rating.pearl == 0) {
            if (rating.diamond == 0) rating.diamond = rating.difficulty;
            if (next.hint.placementCell >= 0) {
                if (want == 'd' && rating.difficulty > rating.diamond) {
                    rating.difficulty = 200;
                    break;
                }
                rating.pearl = rating.difficulty;
            }
        } else if (want != 0 && rating.difficulty > rating.pearl) {
            rating.difficulty = 200;
            break;
        }
    }
    return rating;
}

}  // namespace sefast

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_rate(const char* puzzle, int mode) {
    static std::string result;
    try {
        if (!sefast::isPuzzle(puzzle))
            throw std::invalid_argument("puzzle must contain 81 characters from . and 1-9");
        sefast::checkMode(mode);
        result = sefast::rateBoard(sefast::Board::fromPuzzle(puzzle), mode).csv();
    } catch (const std::invalid_argument& error) {
        result = std::string("ERROR,java.lang.IllegalArgumentException,") + error.what();
    } catch (const std::exception& error) {
        result = std::string("ERROR,java.lang.RuntimeException,") + error.what();
    }
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_rate_low_current(const char* puzzle) {
    static std::string result;
    if (!sefast::isPuzzle(puzzle)) return "ERROR,input";
    try {
        auto rating = sefast::rateBoard(sefast::Board::fromPuzzle(puzzle), 0, true);
        result = rating.difficulty >= 199 ? "" : rating.csv();
    } catch (const std::exception& error) {
        result = std::string("ERROR,java.lang.RuntimeException,") + error.what();
    }
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_rate_diag(const char* puzzle, int mode) {
    static std::string result;
    sefast_reset_rating_diagnostics();
    result = sefast_rate(puzzle, mode);
    result += ',';
    result += sefast_rating_diagnostics();
    return result.c_str();
}
