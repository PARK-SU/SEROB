/*
 * SEROB rating core, derived from SukakuExplainer / Sudoku Explainer.
 * Copyright (C) 2006-2009 Nicolas Juillerat
 * C++ port and modifications by clubDS
 *
 * Licensed under the GNU Lesser General Public License v2.1 only.
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace sefast {

constexpr int kCells = 81;
constexpr int kPotentialIds = 1460;
constexpr uint16_t kAll = 0x3fe;

enum Cause : int8_t {
    None = -1,
    NakedSingle = 0,
    HiddenBlock = 1,
    HiddenRow = 2,
    HiddenColumn = 3,
    Advanced = 11,
};

struct Node {
    int16_t id = -1;
    int8_t cause = None;
    std::array<int16_t, 4> parents{};
    std::vector<int16_t> extraParents;
    uint8_t parentCount = 0;
    int16_t nestedComplexity = 0;
    uint64_t nestedSignature = 0;

    int parentAt(int index) const {
        return index < static_cast<int>(parents.size())
                ? parents[index] : extraParents[index - parents.size()];
    }

    void appendParent(int parent) {
        if (parentCount < parents.size())
            parents[parentCount] = static_cast<int16_t>(parent);
        else
            extraParents.push_back(static_cast<int16_t>(parent));
        ++parentCount;
    }
};

struct Board {
    std::array<uint8_t, kCells> values{};
    std::array<uint16_t, kCells> candidates{};

    static Board fromPuzzle(const std::string& puzzle) {
        if (puzzle.size() != kCells)
            throw std::invalid_argument("puzzle must have 81 characters");
        Board result;
        for (int cell = 0; cell < kCells; ++cell) {
            char ch = puzzle[cell];
            if (ch == '.') {
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

struct RemovalHash {
    size_t operator()(const std::array<uint16_t, kCells>& removals) const {
        uint64_t hash = 1469598103934665603ull;
        for (uint16_t mask : removals) {
            hash ^= mask;
            hash *= 1099511628211ull;
        }
        return static_cast<size_t>(hash);
    }
};

std::vector<Node> staticAdvancedPotentials(const Board& current,
                                           const Board& initial);
std::vector<Node> multipleStaticAdvancedPotentials(const Board& current,
                                                   const Board& initial);
std::vector<Node> dynamicAdvancedPotentials(const Board& current,
                                            const Board& initial,
                                            int nestingLevel);
using StaticAdvancedCache = std::unordered_map<std::string, std::vector<Node>>;
uint64_t diagnosticAdvancedCalls = 0;
uint64_t diagnosticStaticCacheHits = 0;
uint64_t diagnosticStaticCacheMisses = 0;
uint64_t diagnosticStaticHints = 0;

class Closure {
public:
    Closure(const Board& board, bool dynamic, bool nishio, int level = 0,
            StaticAdvancedCache* staticCache = nullptr, int nestingLimit = 0)
        : grid_(board), source_(board), dynamic_(dynamic), nishio_(nishio),
          level_(level), staticCache_(staticCache), nestingLimit_(nestingLimit) {
        onIndex_.fill(-1);
        offIndex_.fill(-1);
    }

    std::string run(int sourceId) {
        std::vector<int> on;
        std::vector<int> off;
        (potentialOn(sourceId) ? on : off).push_back(sourceId);
        return run(on, off);
    }

    void compute(int sourceId) {
        std::vector<int> on;
        std::vector<int> off;
        (potentialOn(sourceId) ? on : off).push_back(sourceId);
        execute(on, off);
    }

    const std::vector<Node>& nodes(bool on) const {
        return on ? onNodes_ : offNodes_;
    }

    const Node* find(int id) const {
        bool on = potentialOn(id);
        int index = on ? onIndex_[id] : offIndex_[id];
        if (index < 0) return nullptr;
        return on ? &onNodes_[index] : &offNodes_[index];
    }

    bool contradicted() const { return contradicted_; }
    const Node& contradictionOn() const { return contradictionOn_; }
    const Node& contradictionOff() const { return contradictionOff_; }

    int ancestorCount(const Node& target) const {
        std::array<bool, kPotentialIds> seen{};
        std::vector<const Node*> pending{&target};
        int result = 0;
        while (!pending.empty()) {
            const Node* node = pending.back();
            pending.pop_back();
            if (seen[node->id]) continue;
            seen[node->id] = true;
            ++result;
            for (int i = 0; i < node->parentCount; ++i) {
                const Node* parent = find(node->parentAt(i));
                if (parent == nullptr)
                    throw std::runtime_error("ancestor parent not found");
                pending.push_back(parent);
            }
        }
        return result;
    }

    void collectNested(const Node& target,
            std::unordered_map<uint64_t, int>& nested) const {
        std::array<bool, kPotentialIds> seen{};
        std::vector<const Node*> pending{&target};
        while (!pending.empty()) {
            const Node* node = pending.back();
            pending.pop_back();
            if (seen[node->id]) continue;
            seen[node->id] = true;
            if (node->nestedSignature != 0)
                nested.emplace(node->nestedSignature, node->nestedComplexity);
            for (int i = 0; i < node->parentCount; ++i) {
                const Node* parent = find(node->parentAt(i));
                if (parent == nullptr)
                    throw std::runtime_error("nested parent not found");
                pending.push_back(parent);
            }
        }
    }

    std::vector<int> chainIds(const Node& target) const {
        std::array<bool, kPotentialIds> seen{};
        std::vector<const Node*> pending{&target};
        std::vector<int> result;
        while (!pending.empty()) {
            std::vector<const Node*> next;
            for (const Node* node : pending) {
                if (seen[node->id]) continue;
                seen[node->id] = true;
                result.push_back(node->id);
                for (int index = 0; index < node->parentCount; ++index) {
                    const Node* parent = find(node->parentAt(index));
                    if (parent == nullptr)
                        throw std::runtime_error("chain parent not found");
                    next.push_back(parent);
                }
            }
            pending = std::move(next);
        }
        return result;
    }

    void collectRuleParents(const Node& target, const Board& initial,
            const Board& current, Cause rootCause, int rootRegionType,
            std::vector<int>& result,
            std::array<bool, kPotentialIds>& added) const {
        std::array<bool, kPotentialIds> seen{};
        std::vector<const Node*> pending{&target};
        while (!pending.empty()) {
            std::vector<const Node*> next;
            for (const Node* node : pending) {
                if (seen[node->id]) continue;
                seen[node->id] = true;
                Cause cause = static_cast<Cause>(node->cause);
                int regionType = cause == HiddenBlock ? 0
                        : cause == HiddenRow ? 1 : cause == HiddenColumn ? 2 : -1;
                if (cause == None && node->parentCount == 0) {
                    cause = rootCause;
                    regionType = rootRegionType;
                }
                if (potentialOn(node->id) && cause != None && cause != Advanced) {
                    int cell = potentialCell(node->id);
                    int value = potentialValue(node->id);
                    if (cause == NakedSingle) {
                        for (int other = 1; other <= 9; ++other) {
                            int parent = potentialId(cell, other, false);
                            uint16_t bit = static_cast<uint16_t>(1u << other);
                            if ((initial.candidates[cell] & bit)
                                    && !(current.candidates[cell] & bit)
                                    && !added[parent]) {
                                added[parent] = true;
                                result.push_back(parent);
                            }
                        }
                    } else if (regionType >= 0) {
                        for (int other : regionCells(regionType, cell)) {
                            int parent = potentialId(other, value, false);
                            uint16_t bit = static_cast<uint16_t>(1u << value);
                            if ((initial.candidates[other] & bit)
                                    && !(current.candidates[other] & bit)
                                    && !added[parent]) {
                                added[parent] = true;
                                result.push_back(parent);
                            }
                        }
                    }
                }
                for (int index = 0; index < node->parentCount; ++index) {
                    const Node* parent = find(node->parentAt(index));
                    if (parent == nullptr)
                        throw std::runtime_error("rule parent chain not found");
                    next.push_back(parent);
                }
            }
            pending = std::move(next);
        }
    }

    std::string run(const std::vector<int>& initialOn,
                    const std::vector<int>& initialOff) {
        execute(initialOn, initialOff);
        return dump();
    }

    std::vector<uint16_t> runPacked(const std::vector<int>& initialOn,
                                    const std::vector<int>& initialOff) {
        execute(initialOn, initialOff);
        std::vector<uint16_t> result;
        result.reserve(4 + (onNodes_.size() + offNodes_.size()) * 5);
        result.push_back(1);
        result.push_back(static_cast<uint16_t>(onNodes_.size()));
        result.push_back(static_cast<uint16_t>(offNodes_.size()));
        result.push_back(contradicted_ ? 1 : 0);
        for (const Node& node : onNodes_) appendPacked(result, node);
        for (const Node& node : offNodes_) appendPacked(result, node);
        if (contradicted_) {
            appendPacked(result, contradictionOn_);
            appendPacked(result, contradictionOff_);
        }
        return result;
    }

private:
    void execute(const std::vector<int>& initialOn,
                 const std::vector<int>& initialOff) {
        for (int id : initialOn) addInitial(id, true);
        for (int id : initialOff) addInitial(id, false);

        size_t onHead = 0;
        size_t offHead = 0;
        while (true) {
            if (onHead < onOrder_.size()) {
                int id = onOrder_[onHead++];
                if (processOn(id)) break;
                continue;
            }
            if (offHead < offOrder_.size()) {
                int id = offOrder_[offHead++];
                if (processOff(id)) break;
                continue;
            }
            if (level_ > 0) {
                bool added = false;
                for (const Node& node : advancedPotentials()) {
                    if (offIndex_[node.id] < 0) {
                        addOff(node);
                        added = true;
                    }
                }
                if (added) continue;
            }
            break;
        }
    }
    Board grid_;
    Board source_;
    bool dynamic_;
    bool nishio_;
    int level_;
    StaticAdvancedCache* staticCache_;
    int nestingLimit_;
    std::array<int16_t, kPotentialIds> onIndex_{};
    std::array<int16_t, kPotentialIds> offIndex_{};
    std::vector<Node> onNodes_;
    std::vector<Node> offNodes_;
    std::vector<int16_t> onOrder_;
    std::vector<int16_t> offOrder_;
    Node contradictionOn_;
    Node contradictionOff_;
    bool contradicted_ = false;

    void addInitial(int id, bool on) {
        if (id < 2 || id >= kPotentialIds || potentialOn(id) != on)
            throw std::invalid_argument("invalid initial potential id");
        Node source;
        source.id = static_cast<int16_t>(id);
        if (on) addOn(source);
        else addOff(source);
    }

    static std::array<int, 9> regionCells(int type, int cell) {
        std::array<int, 9> result{};
        int row = cell / 9;
        int col = cell % 9;
        if (type == 0) {
            int boxRow = row / 3 * 3;
            int boxCol = col / 3 * 3;
            int at = 0;
            for (int y = boxRow; y < boxRow + 3; ++y)
                for (int x = boxCol; x < boxCol + 3; ++x)
                    result[at++] = y * 9 + x;
        } else if (type == 1) {
            for (int x = 0; x < 9; ++x) result[x] = row * 9 + x;
        } else {
            for (int y = 0; y < 9; ++y) result[y] = y * 9 + col;
        }
        return result;
    }

    static std::array<int, 9> regionByIndex(int type, int index) {
        std::array<int, 9> result{};
        if (type == 0) {
            int boxRow = index / 3 * 3;
            int boxCol = index % 3 * 3;
            int at = 0;
            for (int y = boxRow; y < boxRow + 3; ++y)
                for (int x = boxCol; x < boxCol + 3; ++x)
                    result[at++] = y * 9 + x;
        } else if (type == 1) {
            for (int x = 0; x < 9; ++x) result[x] = index * 9 + x;
        } else {
            for (int y = 0; y < 9; ++y) result[y] = y * 9 + index;
        }
        return result;
    }

    static bool contains(const std::array<int, 9>& region, int cell) {
        for (int item : region) if (item == cell) return true;
        return false;
    }

    uint16_t positions(int type, int region, int value) const {
        uint16_t result = 0;
        auto cells = regionByIndex(type, region);
        for (int pos = 0; pos < 9; ++pos)
            if (grid_.candidates[cells[pos]] & (1u << value)) result |= 1u << pos;
        return result;
    }

    int emptyCount(int type, int region) const {
        int result = 0;
        for (int cell : regionByIndex(type, region))
            if (grid_.values[cell] == 0) ++result;
        return result;
    }

    static std::vector<int> hashMapCells(const std::vector<int>& inserted) {
        std::vector<int> unique;
        std::array<bool, kCells> seen{};
        for (int cell : inserted) {
            if (!seen[cell]) {
                seen[cell] = true;
                unique.push_back(cell);
            }
        }
        int capacity = 16;
        while (unique.size() > static_cast<size_t>(capacity * 3 / 4)) capacity *= 2;
        std::stable_sort(unique.begin(), unique.end(), [capacity](int left, int right) {
            return (left & (capacity - 1)) < (right & (capacity - 1));
        });
        return unique;
    }

    std::vector<Node> emitAdvanced(const std::vector<int>& parents,
            const std::vector<std::pair<int, uint16_t>>& removals) const {
        if (parents.empty()) return {};
        std::vector<int> inserted;
        std::array<uint16_t, kCells> masks{};
        for (const auto& removal : removals) {
            if (masks[removal.first] == 0) inserted.push_back(removal.first);
            masks[removal.first] |= removal.second;
        }
        std::vector<Node> result;
        for (int cell : hashMapCells(inserted)) {
            for (int value = 1; value <= 9; ++value) {
                if (!(masks[cell] & (1u << value))) continue;
                Node node;
                node.id = static_cast<int16_t>(potentialId(cell, value, false));
                node.cause = Advanced;
                for (int parent : parents) {
                    if (offIndex_[parent] < 0)
                        throw std::runtime_error("advanced parent not found");
                    node.appendParent(parent);
                }
                result.push_back(node);
            }
        }
        return result;
    }

    static void append(std::vector<Node>& target, std::vector<Node> source) {
        target.insert(target.end(), source.begin(), source.end());
    }

    std::vector<Node> lockingPotentials() const {
        std::vector<Node> result;
        const int calls[4][2] = {{0, 2}, {0, 1}, {2, 0}, {1, 0}};
        for (const auto& call : calls) {
            int type1 = call[0], type2 = call[1];
            for (int value = 1; value <= 9; ++value) {
                for (int index1 = 0; index1 < 9; ++index1) {
                    auto region1 = regionByIndex(type1, index1);
                    std::vector<int> candidates;
                    for (int cell : region1)
                        if (grid_.candidates[cell] & (1u << value)) candidates.push_back(cell);
                    if (candidates.size() < 2) continue;
                    for (int index2 = 0; index2 < 9; ++index2) {
                        auto region2 = regionByIndex(type2, index2);
                        bool crosses = false;
                        for (int cell : region1) if (contains(region2, cell)) crosses = true;
                        if (!crosses) continue;
                        bool common = true;
                        for (int cell : candidates)
                            if (!contains(region2, cell)) { common = false; break; }
                        if (!common) continue;
                        std::vector<int> parents;
                        for (int cell : region1) {
                            if ((source_.candidates[cell] & (1u << value))
                                    && !(grid_.candidates[cell] & (1u << value))
                                    && !contains(region2, cell))
                                parents.push_back(potentialId(cell, value, false));
                        }
                        std::vector<std::pair<int, uint16_t>> removals;
                        for (int cell : region2) {
                            if (!contains(region1, cell)
                                    && (grid_.candidates[cell] & (1u << value)))
                                removals.push_back({cell, static_cast<uint16_t>(1u << value)});
                        }
                        if (!removals.empty()) append(result, emitAdvanced(parents, removals));
                    }
                }
            }
        }
        return result;
    }

    std::vector<Node> hiddenPairPotentials() const {
        std::vector<Node> result;
        const int types[3] = {0, 2, 1};
        for (int type : types) {
            for (int region = 0; region < 9; ++region) {
                if (emptyCount(type, region) <= 4) continue;
                auto cells = regionByIndex(type, region);
                for (int first = 1; first <= 8; ++first) {
                    for (int second = first + 1; second <= 9; ++second) {
                        uint16_t firstPos = positions(type, region, first);
                        uint16_t secondPos = positions(type, region, second);
                        if (bitCount(firstPos) <= 1 || bitCount(secondPos) <= 1) continue;
                        uint16_t common = firstPos | secondPos;
                        if (bitCount(common) != 2) continue;
                        std::vector<int> parents;
                        for (int pos = 0; pos < 9; ++pos) {
                            if (common & (1u << pos)) continue;
                            int cell = cells[pos];
                            if (source_.candidates[cell] & (1u << first))
                                parents.push_back(potentialId(cell, first, false));
                            if (source_.candidates[cell] & (1u << second))
                                parents.push_back(potentialId(cell, second, false));
                        }
                        std::vector<std::pair<int, uint16_t>> removals;
                        uint16_t pair = static_cast<uint16_t>((1u << first) | (1u << second));
                        for (int pos = 0; pos < 9; ++pos) {
                            if (!(common & (1u << pos))) continue;
                            uint16_t mask = grid_.candidates[cells[pos]] & ~pair;
                            if (mask) removals.push_back({cells[pos], mask});
                        }
                        if (!removals.empty()) append(result, emitAdvanced(parents, removals));
                    }
                }
            }
        }
        return result;
    }

    std::vector<Node> nakedPairPotentials() const {
        std::vector<Node> result;
        const int types[3] = {0, 2, 1};
        for (int type : types) {
            for (int region = 0; region < 9; ++region) {
                if (emptyCount(type, region) < 4) continue;
                auto cells = regionByIndex(type, region);
                for (int first = 0; first < 8; ++first) {
                    for (int second = first + 1; second < 9; ++second) {
                        uint16_t one = grid_.candidates[cells[first]];
                        uint16_t two = grid_.candidates[cells[second]];
                        if (bitCount(one) <= 1 || bitCount(two) <= 1) continue;
                        uint16_t pair = one | two;
                        if (bitCount(pair) != 2) continue;
                        std::vector<int> parents;
                        for (int pos : {first, second}) {
                            int cell = cells[pos];
                            for (int value = 1; value <= 9; ++value) {
                                if ((source_.candidates[cell] & (1u << value))
                                        && !(pair & (1u << value)))
                                    parents.push_back(potentialId(cell, value, false));
                            }
                        }
                        std::vector<std::pair<int, uint16_t>> removals;
                        for (int pos = 0; pos < 9; ++pos) {
                            if (pos == first || pos == second) continue;
                            uint16_t mask = grid_.candidates[cells[pos]] & pair;
                            if (mask) removals.push_back({cells[pos], mask});
                        }
                        if (!removals.empty()) append(result, emitAdvanced(parents, removals));
                    }
                }
            }
        }
        return result;
    }

    std::vector<Node> xWingPotentials() const {
        std::vector<Node> result;
        const int calls[2][2] = {{2, 1}, {1, 2}};
        std::array<int, 10> occurrences{};
        for (uint8_t value : grid_.values) if (value) ++occurrences[value];
        for (const auto& call : calls) {
            int primary = call[0], secondary = call[1];
            for (int first = 0; first < 8; ++first) {
                for (int second = first + 1; second < 9; ++second) {
                    for (int value = 1; value <= 9; ++value) {
                        if (occurrences[value] + 4 > 9) continue;
                        uint16_t one = positions(primary, first, value);
                        uint16_t two = positions(primary, second, value);
                        if (bitCount(one) <= 1 || bitCount(two) <= 1) continue;
                        uint16_t common = one | two;
                        if (bitCount(common) != 2) continue;
                        std::vector<int> parents;
                        for (int line : {first, second}) {
                            auto region = regionByIndex(primary, line);
                            for (int cell : region) {
                                if ((source_.candidates[cell] & (1u << value))
                                        && !(grid_.candidates[cell] & (1u << value))) {
                                    bool inSecondary = false;
                                    for (int index = 0; index < 9; ++index)
                                        if ((common & (1u << index))
                                                && contains(regionByIndex(secondary, index), cell))
                                            inSecondary = true;
                                    if (!inSecondary)
                                        parents.push_back(potentialId(cell, value, false));
                                }
                            }
                        }
                        std::vector<std::pair<int, uint16_t>> removals;
                        for (int line = 0; line < 9; ++line) {
                            if (!(common & (1u << line))) continue;
                            auto region = regionByIndex(secondary, line);
                            for (int pos = 0; pos < 9; ++pos) {
                                if (pos == first || pos == second) continue;
                                int cell = region[pos];
                                if (grid_.candidates[cell] & (1u << value))
                                    removals.push_back({cell, static_cast<uint16_t>(1u << value)});
                            }
                        }
                        if (!removals.empty()) append(result, emitAdvanced(parents, removals));
                    }
                }
            }
        }
        return result;
    }

    std::vector<Node> advancedPotentials() const {
        ++diagnosticAdvancedCalls;
        std::vector<Node> result = lockingPotentials();
        if (!result.empty()) return result;
        result = hiddenPairPotentials();
        if (!result.empty()) return result;
        result = nakedPairPotentials();
        if (!result.empty()) return result;
        result = xWingPotentials();
        if (!result.empty() || level_ < 2) return result;
        if (staticCache_ != nullptr) {
            std::string key;
            key.resize(kCells * 2);
            for (int cell = 0; cell < kCells; ++cell) {
                key[cell * 2] = static_cast<char>(grid_.candidates[cell] & 255);
                key[cell * 2 + 1] = static_cast<char>(grid_.candidates[cell] >> 8);
            }
            auto found = staticCache_->find(key);
            if (found != staticCache_->end()) {
                ++diagnosticStaticCacheHits;
                return found->second;
            }
            ++diagnosticStaticCacheMisses;
            if (level_ >= 4)
                result = dynamicAdvancedPotentials(grid_, source_, nestingLimit_);
            else {
                result = staticAdvancedPotentials(grid_, source_);
                if (result.empty() && level_ >= 3)
                    result = multipleStaticAdvancedPotentials(grid_, source_);
            }
            staticCache_->emplace(std::move(key), result);
        } else {
            if (level_ >= 4)
                result = dynamicAdvancedPotentials(grid_, source_, nestingLimit_);
            else {
                result = staticAdvancedPotentials(grid_, source_);
                if (result.empty() && level_ >= 3)
                    result = multipleStaticAdvancedPotentials(grid_, source_);
            }
        }
        for (const Node& node : result)
            for (int i = 0; i < node.parentCount; ++i)
                if (offIndex_[node.parentAt(i)] < 0)
                    throw std::runtime_error("nested static parent not found: "
                            + std::to_string(node.parentAt(i)));
        return result;
    }

    static Cause regionCause(int type) {
        return type == 0 ? HiddenBlock : type == 1 ? HiddenRow : HiddenColumn;
    }

    int addOn(const Node& node) {
        int index = static_cast<int>(onNodes_.size());
        onNodes_.push_back(node);
        onIndex_[node.id] = static_cast<int16_t>(index);
        onOrder_.push_back(node.id);
        return index;
    }

    int addOff(const Node& node) {
        int index = static_cast<int>(offNodes_.size());
        offNodes_.push_back(node);
        offIndex_[node.id] = static_cast<int16_t>(index);
        offOrder_.push_back(node.id);
        return index;
    }

    Node withParent(int cell, int value, bool on, Cause cause, int parent) const {
        Node result;
        result.id = static_cast<int16_t>(potentialId(cell, value, on));
        result.cause = cause;
        result.appendParent(parent);
        return result;
    }

    void addParent(Node& node, int parent) const {
        for (int i = 0; i < node.parentCount; ++i)
            if (node.parentAt(i) == parent) return;
        node.appendParent(parent);
    }

    bool acceptOff(const Node& node) {
        int conjugate = node.id | 1;
        if (onIndex_[conjugate] >= 0) {
            contradictionOn_ = onNodes_[onIndex_[conjugate]];
            contradictionOff_ = node;
            contradicted_ = true;
            return true;
        }
        if (offIndex_[node.id] < 0) addOff(node);
        return false;
    }

    bool acceptOn(const Node& node) {
        int conjugate = node.id & ~1;
        if (offIndex_[conjugate] >= 0) {
            contradictionOn_ = node;
            contradictionOff_ = offNodes_[offIndex_[conjugate]];
            contradicted_ = true;
            return true;
        }
        if (onIndex_[node.id] < 0) addOn(node);
        return false;
    }

    bool processOn(int parent) {
        int cell = potentialCell(parent);
        int value = potentialValue(parent);
        std::array<bool, kPotentialIds> emitted{};

        if (!nishio_) {
            uint16_t values = grid_.candidates[cell];
            for (int other = 1; other <= 9; ++other) {
                if (other != value && (values & (1u << other))) {
                    Node node = withParent(cell, other, false, NakedSingle, parent);
                    emitted[node.id] = true;
                    if (acceptOff(node)) return true;
                }
            }
        }

        for (int type = 0; type < 3; ++type) {
            auto cells = regionCells(type, cell);
            for (int other : cells) {
                if (other == cell) continue;
                int id = potentialId(other, value, false);
                if (!emitted[id] && (grid_.candidates[other] & (1u << value))) {
                    Node node = withParent(other, value, false, regionCause(type), parent);
                    emitted[id] = true;
                    if (acceptOff(node)) return true;
                }
            }
        }
        return false;
    }

    void addHiddenCellParents(Node& node, int cell) const {
        for (int value = 1; value <= 9; ++value) {
            uint16_t bit = static_cast<uint16_t>(1u << value);
            if ((source_.candidates[cell] & bit) && !(grid_.candidates[cell] & bit)) {
                int id = potentialId(cell, value, false);
                if (offIndex_[id] < 0)
                    throw std::runtime_error("hidden cell parent not found");
                addParent(node, id);
            }
        }
    }

    void addHiddenRegionParents(Node& node, int type, int cell, int value) const {
        for (int other : regionCells(type, cell)) {
            uint16_t bit = static_cast<uint16_t>(1u << value);
            if ((source_.candidates[other] & bit) && !(grid_.candidates[other] & bit)) {
                int id = potentialId(other, value, false);
                if (offIndex_[id] < 0)
                    throw std::runtime_error("hidden region parent not found");
                addParent(node, id);
            }
        }
    }

    bool processOff(int parent) {
        int cell = potentialCell(parent);
        int value = potentialValue(parent);
        std::array<bool, kPotentialIds> emitted{};

        if (!nishio_ && bitCount(grid_.candidates[cell]) == 2) {
            for (int other = 1; other <= 9; ++other) {
                if (other != value && (grid_.candidates[cell] & (1u << other))) {
                    Node node = withParent(cell, other, true, NakedSingle, parent);
                    if (dynamic_) addHiddenCellParents(node, cell);
                    emitted[node.id] = true;
                    if (acceptOn(node)) return true;
                    break;
                }
            }
        }

        for (int type = 0; type < 3; ++type) {
            int only = -1;
            for (int other : regionCells(type, cell)) {
                if (other == cell) continue;
                if (grid_.candidates[other] & (1u << value)) {
                    if (only >= 0) {
                        only = -1;
                        break;
                    }
                    only = other;
                }
            }
            if (only >= 0) {
                Node node = withParent(only, value, true, regionCause(type), parent);
                if (!emitted[node.id]) {
                    if (dynamic_) addHiddenRegionParents(node, type, cell, value);
                    emitted[node.id] = true;
                    if (acceptOn(node)) return true;
                }
            }
        }

        if (dynamic_)
            grid_.candidates[cell] &= static_cast<uint16_t>(~(1u << value));
        return false;
    }

    static void appendNode(std::ostringstream& out, const Node& node) {
        out << node.id << '@' << static_cast<int>(node.cause) << '@';
        for (int i = 0; i < node.parentCount; ++i) {
            if (i) out << '.';
            out << node.parentAt(i);
        }
    }

    static void appendPacked(std::vector<uint16_t>& out, const Node& node) {
        out.push_back(static_cast<uint16_t>(node.id));
        out.push_back(static_cast<uint16_t>(node.cause + 1));
        out.push_back(node.parentCount);
        for (int i = 0; i < node.parentCount; ++i)
            out.push_back(static_cast<uint16_t>(node.parentAt(i)));
    }

    std::string dump() const {
        std::ostringstream out;
        out << "N:";
        for (size_t i = 0; i < onNodes_.size(); ++i) {
            if (i) out << ';';
            appendNode(out, onNodes_[i]);
        }
        out << "|F:";
        for (size_t i = 0; i < offNodes_.size(); ++i) {
            if (i) out << ';';
            appendNode(out, offNodes_[i]);
        }
        out << "|C:";
        if (contradicted_) {
            appendNode(out, contradictionOn_);
            out << ';';
            appendNode(out, contradictionOff_);
        }
        return out.str();
    }
};

}  // namespace sefast

namespace sefast {

struct StaticHint {
    enum Kind { Cycle, Forcing } kind = Forcing;
    struct PathNode {
        int16_t id = -1;
        int16_t parent = -1;
        int8_t cause = None;
    };
    bool y = false;
    bool x = false;
    int rating = 0;
    int complexity = 0;
    int sortKey = 0;
    int targetId = -1;
    std::vector<PathNode> path;
    std::array<uint16_t, kCells> removals{};
    uint64_t signature = 0;
};

class StaticChains {
public:
    explicit StaticChains(const Board& board) : board_(board) {
        arena_.reserve(4096);
        onCellY_ = std::make_unique<OnCellTable>();
        onPeers_ = std::make_unique<OnPeersTable>();
        offY_ = std::make_unique<OffYTable>();
        offX_ = std::make_unique<OffXTable>();
        pendingOn_.reserve(1024);
        pendingOff_.reserve(4096);
        cycleOff_.reserve(4096);
        buildDirectEdges();
    }

    std::vector<StaticHint> hints() {
        std::vector<StaticHint> result;
        result.reserve(4096);
        appendConfig(result, false, true);
        appendConfig(result, true, false);
        appendConfig(result, true, true);
        std::stable_sort(result.begin(), result.end(), [](const StaticHint& a,
                                                         const StaticHint& b) {
            if (a.rating != b.rating) return a.rating < b.rating;
            if (a.complexity != b.complexity) return a.complexity < b.complexity;
            return a.sortKey < b.sortKey;
        });
        std::vector<StaticHint> unique;
        std::unordered_set<std::array<uint16_t, kCells>, RemovalHash> seen;
        seen.reserve(result.size());
        for (StaticHint& hint : result)
            if (seen.insert(hint.removals).second)
                unique.push_back(std::move(hint));
        diagnosticStaticHints += unique.size();
        return unique;
    }

    std::vector<int> ruleParents(const StaticHint& hint,
                                 const Board& initial) const {
        std::vector<int> result;
        std::array<bool, kPotentialIds> added{};
        collectPathParents(hint.path, initial, result, added, false);
        if (hint.kind == StaticHint::Cycle)
            collectPathParents(hint.path, initial, result, added, true);
        return result;
    }

private:
    struct SNode {
        int16_t id = -1;
        int16_t parent = -1;
        int8_t cause = None;
        uint64_t ancestorBloom = 0;
    };

    struct DirectEdge {
        int16_t id = -1;
        int8_t cause = None;
    };

    struct CycleOff {
        int16_t id = -1;
        int16_t parent = -1;
        int16_t node = -1;
        int8_t cause = None;
        bool first = false;
    };

    struct EdgeList {
        std::array<int, 32> nodes{};
        uint8_t count = 0;

        int* begin() { return nodes.data(); }
        int* end() { return nodes.data() + count; }

        bool containsId(const std::vector<SNode>& arena, int id) const {
            for (int index = 0; index < count; ++index)
                if (arena[nodes[index]].id == id) return true;
            return false;
        }

        void add(int node) { nodes[count++] = node; }
    };

    template<size_t Capacity>
    struct DirectEdgeList {
        std::array<DirectEdge, Capacity> edges{};
        uint8_t count = 0;

        bool contains(int id) const {
            for (int index = 0; index < count; ++index)
                if (edges[index].id == id) return true;
            return false;
        }

        void addUnique(int id, Cause cause) {
            if (!contains(id))
                edges[count++] = DirectEdge{static_cast<int16_t>(id), cause};
        }
    };

    using OnCellTable = std::array<DirectEdgeList<8>, kPotentialIds>;
    using OnPeersTable = std::array<DirectEdgeList<24>, kPotentialIds>;
    using OffYTable = std::array<DirectEdgeList<1>, kPotentialIds>;
    using OffXTable = std::array<DirectEdgeList<3>, kPotentialIds>;

    const Board& board_;
    std::vector<SNode> arena_;
    std::array<int16_t, kPotentialIds> onIndex_{};
    std::array<int16_t, kPotentialIds> offIndex_{};
    std::unique_ptr<OnCellTable> onCellY_;
    std::unique_ptr<OnPeersTable> onPeers_;
    std::unique_ptr<OffYTable> offY_;
    std::unique_ptr<OffXTable> offX_;
    std::vector<int> pendingOn_;
    std::vector<int> pendingOff_;
    std::vector<CycleOff> cycleOff_;

    static int lengthRating(int base, int complexity) {
        int length = complexity - 2;
        int ceiling = 4;
        bool odd = false;
        while (length > ceiling) {
            ++base;
            ceiling = odd ? ceiling * 4 / 3 : ceiling * 3 / 2;
            odd = !odd;
        }
        return base;
    }

    int addNode(int id, int parent, Cause cause) {
        uint64_t bloom = parent < 0 ? 0 : arena_[parent].ancestorBloom;
        bloom |= 1ull << (static_cast<unsigned>(id) & 63u);
        arena_.push_back(SNode{static_cast<int16_t>(id),
                              static_cast<int16_t>(parent), cause, bloom});
        return static_cast<int>(arena_.size() - 1);
    }

    void buildDirectEdges() {
        for (int cell = 0; cell < kCells; ++cell) {
            uint16_t mask = board_.candidates[cell];
            if (!mask) continue;
            int cardinality = bitCount(mask);
            for (int value = 1; value <= 9; ++value) {
                if (!(mask & (1u << value))) continue;
                int on = potentialId(cell, value, true);
                int off = on - 1;
                for (int other = 1; other <= 9; ++other)
                    if (other != value && (mask & (1u << other)))
                        (*onCellY_)[on].addUnique(
                                potentialId(cell, other, false), NakedSingle);
                for (int type = 0; type < 3; ++type) {
                    Cause cause = type == 0 ? HiddenBlock
                            : type == 1 ? HiddenRow : HiddenColumn;
                    for (int other : region(type, cell)) {
                        if (other == cell || !(board_.candidates[other] & (1u << value)))
                            continue;
                        int target = potentialId(other, value, false);
                        (*onPeers_)[on].addUnique(target, cause);
                    }
                }
                if (cardinality == 2) {
                    for (int other = 1; other <= 9; ++other) {
                        if (other != value && (mask & (1u << other))) {
                            (*offY_)[off].addUnique(potentialId(cell, other, true), NakedSingle);
                            break;
                        }
                    }
                }
                for (int type = 0; type < 3; ++type) {
                    int only = -1;
                    for (int other : region(type, cell)) {
                        if (other == cell) continue;
                        if (board_.candidates[other] & (1u << value)) {
                            if (only >= 0) { only = -1; break; }
                            only = other;
                        }
                    }
                    if (only >= 0)
                        (*offX_)[off].addUnique(potentialId(only, value, true),
                                type == 0 ? HiddenBlock
                                : type == 1 ? HiddenRow : HiddenColumn);
                }
            }
        }
    }

    bool isParent(int child, int parentId) const {
        if (!(arena_[child].ancestorBloom
                & (1ull << (static_cast<unsigned>(parentId) & 63u))))
            return false;
        int current = child;
        while (arena_[current].parent >= 0) {
            current = arena_[current].parent;
            if (arena_[current].id == parentId) return true;
        }
        return false;
    }

    EdgeList onToOff(int parent, bool y) {
        EdgeList result;
        int id = arena_[parent].id;
        if (y) {
            const auto& cellEdges = (*onCellY_)[id];
            for (int index = 0; index < cellEdges.count; ++index)
                result.add(addNode(cellEdges.edges[index].id, parent,
                        static_cast<Cause>(cellEdges.edges[index].cause)));
        }
        const auto& peerEdges = (*onPeers_)[id];
        for (int index = 0; index < peerEdges.count; ++index)
            result.add(addNode(peerEdges.edges[index].id, parent,
                    static_cast<Cause>(peerEdges.edges[index].cause)));
        return result;
    }

    EdgeList offToOn(int parent, bool y, bool x) {
        EdgeList result;
        int id = arena_[parent].id;
        if (y)
            for (int index = 0; index < (*offY_)[id].count; ++index)
                result.add(addNode((*offY_)[id].edges[index].id, parent,
                        static_cast<Cause>((*offY_)[id].edges[index].cause)));
        if (x)
            for (int index = 0; index < (*offX_)[id].count; ++index) {
                const DirectEdge& edge = (*offX_)[id].edges[index];
                if (!result.containsId(arena_, edge.id))
                    result.add(addNode(edge.id, parent, static_cast<Cause>(edge.cause)));
            }
        return result;
    }

    static std::array<int, 9> region(int type, int cell) {
        std::array<int, 9> result{};
        int row = cell / 9, col = cell % 9;
        if (type == 0) {
            int at = 0;
            for (int y = row / 3 * 3; y < row / 3 * 3 + 3; ++y)
                for (int x = col / 3 * 3; x < col / 3 * 3 + 3; ++x)
                    result[at++] = y * 9 + x;
        } else if (type == 1) {
            for (int x = 0; x < 9; ++x) result[x] = row * 9 + x;
        } else {
            for (int y = 0; y < 9; ++y) result[y] = y * 9 + col;
        }
        return result;
    }

    void reset(int source, bool on) {
        arena_.clear();
        onIndex_.fill(-1);
        offIndex_.fill(-1);
        int root = addNode(source, -1, None);
        (on ? onIndex_ : offIndex_)[source] = static_cast<int16_t>(root);
    }

    std::vector<StaticHint::PathNode> path(int target) const {
        std::vector<StaticHint::PathNode> result;
        int current = target;
        while (current >= 0) {
            const SNode& item = arena_[current];
            result.push_back(StaticHint::PathNode{
                    item.id,
                    static_cast<int16_t>(item.parent < 0
                            ? -1 : arena_[item.parent].id),
                    item.cause});
            current = item.parent;
        }
        return result;
    }

    static int pathComplexity(const std::vector<StaticHint::PathNode>& path) {
        std::array<bool, kPotentialIds> seen{};
        int result = 0;
        for (const StaticHint::PathNode& node : path) {
            if (!seen[node.id]) {
                seen[node.id] = true;
                ++result;
            }
        }
        return result;
    }

    StaticHint forcingHint(int target, bool y, bool x) const {
        StaticHint result;
        result.kind = StaticHint::Forcing;
        result.y = y;
        result.x = x;
        result.targetId = arena_[target].id;
        result.path = path(target);
        result.complexity = pathComplexity(result.path);
        result.rating = lengthRating(y && x ? 70 : 66, result.complexity);
        result.sortKey = y && x ? 4 : y ? 3 : 2;
        int cell = potentialCell(result.targetId);
        int value = potentialValue(result.targetId);
        result.removals[cell] = potentialOn(result.targetId)
                ? static_cast<uint16_t>(board_.candidates[cell] & ~(1u << value))
                : static_cast<uint16_t>(board_.candidates[cell] & (1u << value));
        result.signature = signature(result);
        return result;
    }

    StaticHint cycleHint(int target, bool y, bool x) const {
        StaticHint result;
        result.kind = StaticHint::Cycle;
        result.y = y;
        result.x = x;
        result.targetId = arena_[target].id;
        result.path = path(target);
        result.complexity = pathComplexity(result.path);
        result.rating = lengthRating(y && x ? 70 : 65, result.complexity);
        result.sortKey = y && x ? 4 : y ? 3 : 2;
        std::array<bool, kCells> chainCells{};
        for (const StaticHint::PathNode& node : result.path)
            chainCells[potentialCell(node.id)] = true;
        std::array<uint16_t, kCells> forward{}, backward{};
        for (const StaticHint::PathNode& node : result.path) {
            int cell = potentialCell(node.id), value = potentialValue(node.id);
            for (int peer : Board::peers(cell)) {
                if (!chainCells[peer] && (board_.candidates[peer] & (1u << value))) {
                    (potentialOn(node.id) ? forward : backward)[peer] |= 1u << value;
                }
            }
        }
        for (int cell = 0; cell < kCells; ++cell)
            result.removals[cell] = forward[cell] & backward[cell];
        result.signature = signature(result);
        return result;
    }

    static bool worth(const StaticHint& hint) {
        for (uint16_t mask : hint.removals) if (mask) return true;
        return false;
    }

    void cycles(std::vector<StaticHint>& hints, int source, bool y, bool x) {
        reset(source, true);
        pendingOn_.clear();
        cycleOff_.clear();
        pendingOn_.push_back(0);
        std::vector<int>& pendingOn = pendingOn_;
        std::vector<CycleOff>& pendingOff = cycleOff_;
        size_t onHead = 0, offHead = 0;
        int length = 0;
        int forcingTarget = -1;
        while (onHead < pendingOn.size() || offHead < pendingOff.size()) {
            ++length;
            size_t onEnd = pendingOn.size();
            while (onHead < onEnd) {
                int parent = pendingOn[onHead++];
                auto appendOff = [&](const DirectEdge& edge) {
                    int node = -1;
                    if (x && forcingTarget < 0 && edge.id == (source ^ 1)) {
                        node = addNode(edge.id, parent,
                                static_cast<Cause>(edge.cause));
                        forcingTarget = node;
                    }
                    if (!isParent(parent, edge.id)) {
                        bool first = offIndex_[edge.id] < 0;
                        if (first && node < 0)
                            node = addNode(edge.id, parent,
                                    static_cast<Cause>(edge.cause));
                        pendingOff.push_back(CycleOff{
                                edge.id, static_cast<int16_t>(parent),
                                static_cast<int16_t>(node), edge.cause, first});
                        if (first)
                            offIndex_[edge.id] = static_cast<int16_t>(node);
                    }
                };
                int id = arena_[parent].id;
                if (y) {
                    const auto& edges = (*onCellY_)[id];
                    for (int index = 0; index < edges.count; ++index)
                        appendOff(edges.edges[index]);
                }
                const auto& edges = (*onPeers_)[id];
                for (int index = 0; index < edges.count; ++index)
                    appendOff(edges.edges[index]);
            }
            ++length;
            size_t offEnd = pendingOff.size();
            while (offHead < offEnd) {
                const CycleOff pending = pendingOff[offHead++];
                int parent = pending.node;
                int firstTarget = -1;
                auto appendOn = [&](const DirectEdge& edge) {
                    if (edge.id == firstTarget) return;
                    if (!pending.first && edge.id != source) return;
                    if (parent < 0)
                        parent = addNode(pending.id, pending.parent,
                                static_cast<Cause>(pending.cause));
                    int node = -1;
                    if (length >= 4 && edge.id == source) {
                        node = addNode(edge.id, parent,
                                static_cast<Cause>(edge.cause));
                        StaticHint hint = cycleHint(node, y, x);
                        if (worth(hint)) hints.push_back(std::move(hint));
                    }
                    if (onIndex_[edge.id] < 0) {
                        if (node < 0)
                            node = addNode(edge.id, parent,
                                    static_cast<Cause>(edge.cause));
                        onIndex_[edge.id] = static_cast<int16_t>(node);
                        pendingOn.push_back(node);
                    }
                };
                int id = pending.id;
                if (y) {
                    const auto& edges = (*offY_)[id];
                    for (int index = 0; index < edges.count; ++index) {
                        appendOn(edges.edges[index]);
                        firstTarget = edges.edges[index].id;
                    }
                }
                if (x) {
                    const auto& edges = (*offX_)[id];
                    for (int index = 0; index < edges.count; ++index)
                        appendOn(edges.edges[index]);
                }
            }
        }
        if (forcingTarget >= 0) {
            StaticHint hint = forcingHint(forcingTarget, y, true);
            if (worth(hint)) hints.push_back(std::move(hint));
        }
    }

    void forcing(std::vector<StaticHint>& hints, int source, bool y) {
        reset(source, potentialOn(source));
        pendingOn_.clear();
        pendingOff_.clear();
        std::vector<int>& pendingOn = pendingOn_;
        std::vector<int>& pendingOff = pendingOff_;
        (potentialOn(source) ? pendingOn : pendingOff).push_back(0);
        size_t onHead = 0, offHead = 0;
        std::array<bool, kPotentialIds> targets{};
        while (onHead < pendingOn.size() || offHead < pendingOff.size()) {
            while (onHead < pendingOn.size()) {
                int parent = pendingOn[onHead++];
                auto appendOff = [&](const DirectEdge& edge) {
                    bool target = (edge.id | 1) == source && !targets[edge.id];
                    bool unseen = offIndex_[edge.id] < 0;
                    if (!target && !unseen) return;
                    int node = addNode(edge.id, parent,
                            static_cast<Cause>(edge.cause));
                    if (target) {
                        targets[edge.id] = true;
                        StaticHint hint = forcingHint(node, y, true);
                        if (worth(hint)) hints.push_back(std::move(hint));
                    }
                    if (unseen) {
                        offIndex_[edge.id] = static_cast<int16_t>(node);
                        pendingOff.push_back(node);
                    }
                };
                int id = arena_[parent].id;
                if (y) {
                    const auto& edges = (*onCellY_)[id];
                    for (int index = 0; index < edges.count; ++index)
                        appendOff(edges.edges[index]);
                }
                const auto& edges = (*onPeers_)[id];
                for (int index = 0; index < edges.count; ++index)
                    appendOff(edges.edges[index]);
            }
            while (offHead < pendingOff.size()) {
                int parent = pendingOff[offHead++];
                int firstTarget = -1;
                auto appendOn = [&](const DirectEdge& edge) {
                    if (edge.id == firstTarget) return;
                    bool target = (edge.id & ~1) == source && !targets[edge.id];
                    bool unseen = onIndex_[edge.id] < 0;
                    if (!target && !unseen) return;
                    int node = addNode(edge.id, parent,
                            static_cast<Cause>(edge.cause));
                    if (target) {
                        targets[edge.id] = true;
                        StaticHint hint = forcingHint(node, y, true);
                        if (worth(hint)) hints.push_back(std::move(hint));
                    }
                    if (unseen) {
                        onIndex_[edge.id] = static_cast<int16_t>(node);
                        pendingOn.push_back(node);
                    }
                };
                int id = arena_[parent].id;
                if (y) {
                    const auto& edges = (*offY_)[id];
                    for (int index = 0; index < edges.count; ++index) {
                        appendOn(edges.edges[index]);
                        firstTarget = edges.edges[index].id;
                    }
                }
                const auto& edges = (*offX_)[id];
                for (int index = 0; index < edges.count; ++index)
                    appendOn(edges.edges[index]);
            }
        }
    }

    void appendConfig(std::vector<StaticHint>& result, bool y, bool x) {
        for (int cell = 0; cell < kCells; ++cell) {
            int cardinality = bitCount(board_.candidates[cell]);
            if (board_.values[cell] != 0 || cardinality <= 1 || (!x && cardinality > 2))
                continue;
            for (int value = 1; value <= 9; ++value) {
                if (!(board_.candidates[cell] & (1u << value))) continue;
                int on = potentialId(cell, value, true);
                cycles(result, on, y, x);
                if (x) {
                    forcing(result, on - 1, y);
                }
            }
        }
    }

    static uint64_t signature(const StaticHint& hint) {
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        mix(hint.kind);
        for (const StaticHint::PathNode& node : hint.path) mix(node.id);
        if (hint.kind == StaticHint::Cycle)
            for (auto it = hint.path.rbegin(); it != hint.path.rend(); ++it) mix(it->id ^ 1);
        return hash == 0 ? 1 : hash;
    }

    void collectPathParents(const std::vector<StaticHint::PathNode>& original,
            const Board& initial,
            std::vector<int>& result, std::array<bool, kPotentialIds>& added,
            bool reversed) const {
        std::array<bool, kPotentialIds> done{};
        for (size_t offset = 0; offset < original.size(); ++offset) {
            size_t index = reversed ? original.size() - 1 - offset : offset;
            int id = reversed ? original[index].id ^ 1 : original[index].id;
            if (done[id]) continue;
            done[id] = true;
            Cause cause = static_cast<Cause>(original[index].cause);
            if (!potentialOn(id) || cause == None) continue;
            int cell = potentialCell(id), value = potentialValue(id);
            if (cause == NakedSingle) {
                for (int other = 1; other <= 9; ++other) {
                    int parent = potentialId(cell, other, false);
                    if ((initial.candidates[cell] & (1u << other))
                            && !(board_.candidates[cell] & (1u << other)) && !added[parent]) {
                        added[parent] = true;
                        result.push_back(parent);
                    }
                }
            } else {
                int type = cause == HiddenBlock ? 0 : cause == HiddenRow ? 1 : 2;
                for (int other : region(type, cell)) {
                    int parent = potentialId(other, value, false);
                    if ((initial.candidates[other] & (1u << value))
                            && !(board_.candidates[other] & (1u << value)) && !added[parent]) {
                        added[parent] = true;
                        result.push_back(parent);
                    }
                }
            }
        }
    }
};

std::vector<Node> staticAdvancedPotentials(const Board& current,
                                           const Board& initial) {
    StaticChains engine(current);
    std::vector<Node> result;
    std::vector<StaticHint> hints = engine.hints();
    std::array<bool, kPotentialIds> emitted{};
    for (const StaticHint& hint : hints) {
        bool useful = false;
        for (int cell = 0; cell < kCells && !useful; ++cell)
            for (int value = 1; value <= 9; ++value)
                if ((hint.removals[cell] & (1u << value))
                        && !emitted[potentialId(cell, value, false)]) {
                    useful = true;
                    break;
                }
        if (!useful) continue;
        std::vector<int> parents = engine.ruleParents(hint, initial);
        if (parents.empty()) continue;
        std::vector<int> cells;
        for (int cell = 0; cell < kCells; ++cell)
            if (hint.removals[cell]) cells.push_back(cell);
        int capacity = 16;
        while (cells.size() > static_cast<size_t>(capacity * 3 / 4)) capacity *= 2;
        std::stable_sort(cells.begin(), cells.end(), [capacity](int left, int right) {
            return (left & (capacity - 1)) < (right & (capacity - 1));
        });
        for (int cell : cells) {
            for (int value = 1; value <= 9; ++value) {
                if (!(hint.removals[cell] & (1u << value))) continue;
                int id = potentialId(cell, value, false);
                if (emitted[id]) continue;
                Node node;
                node.id = static_cast<int16_t>(id);
                node.cause = Advanced;
                node.nestedComplexity = static_cast<int16_t>(hint.complexity);
                node.nestedSignature = hint.signature;
        for (int parent : parents)
            if (node.parentCount == 255)
                throw std::runtime_error("too many nested static parents");
        for (int parent : parents)
            node.appendParent(parent);
                result.push_back(node);
                emitted[id] = true;
            }
        }
    }
    return result;
}

struct MultipleChainHint {
    int rating = 0;
    int complexity = 0;
    int sortKey = 0;
    int targetId = -1;
    std::array<uint16_t, kCells> removals{};
    std::vector<int> parents;
    uint64_t signature = 0;
};

static int chainingLengthRating(int base, int complexity) {
    int length = complexity - 2;
    int ceiling = 4;
    bool odd = false;
    while (length > ceiling) {
        ++base;
        ceiling = odd ? ceiling * 4 / 3 : ceiling * 3 / 2;
        odd = !odd;
    }
    return base;
}

static uint64_t fullChainSignature(
        const std::vector<std::pair<const Closure*, const Node*>>& targets) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(targets.size());
    for (const auto& target : targets) {
        std::vector<int> chain = target.first->chainIds(*target.second);
        mix(0x10000u + chain.size());
        for (int id : chain) mix(static_cast<uint64_t>(id));
    }
    return hash == 0 ? 1 : hash;
}

class MultipleChains {
public:
    MultipleChains(const Board& current, const Board& initial,
            bool multiple, bool dynamic, bool nishio, int level)
        : current_(current), initial_(initial), multiple_(multiple),
          dynamic_(dynamic), nishio_(nishio), level_(level) {}

    std::vector<MultipleChainHint> hints() {
        std::vector<MultipleChainHint> result;
        for (int cell = 0; cell < kCells; ++cell) {
            int cardinality = bitCount(current_.candidates[cell]);
            if (current_.values[cell] != 0
                    || !(cardinality > 2 || (cardinality > 1 && dynamic_)))
                continue;
            appendCell(result, cell);
        }
        std::stable_sort(result.begin(), result.end(),
                [](const MultipleChainHint& left, const MultipleChainHint& right) {
            if (left.rating != right.rating) return left.rating < right.rating;
            if (left.complexity != right.complexity)
                return left.complexity < right.complexity;
            return left.sortKey < right.sortKey;
        });
        std::vector<MultipleChainHint> unique;
        std::unordered_set<std::array<uint16_t, kCells>, RemovalHash> seen;
        seen.reserve(result.size());
        for (MultipleChainHint& hint : result) {
            if (seen.insert(hint.removals).second)
                unique.push_back(std::move(hint));
        }
        return unique;
    }

private:
    struct ValueBranch {
        int value = 0;
        std::shared_ptr<Closure> on;
        std::shared_ptr<Closure> off;
    };

    const Board& current_;
    const Board& initial_;
    bool multiple_;
    bool dynamic_;
    bool nishio_;
    int level_;
    mutable StaticAdvancedCache advancedCache_;
    mutable std::array<std::shared_ptr<Closure>, kPotentialIds> branchCache_{};

    static std::array<int, 9> region(int type, int cell) {
        std::array<int, 9> result{};
        int row = cell / 9, column = cell % 9;
        if (type == 0) {
            int at = 0;
            for (int y = row / 3 * 3; y < row / 3 * 3 + 3; ++y)
                for (int x = column / 3 * 3; x < column / 3 * 3 + 3; ++x)
                    result[at++] = y * 9 + x;
        } else if (type == 1) {
            for (int x = 0; x < 9; ++x) result[x] = row * 9 + x;
        } else {
            for (int y = 0; y < 9; ++y) result[y] = y * 9 + column;
        }
        return result;
    }

    std::shared_ptr<Closure> branch(int sourceId) const {
        if (branchCache_[sourceId]) return branchCache_[sourceId];
        auto result = std::make_shared<Closure>(current_, dynamic_, nishio_, level_,
                                                &advancedCache_);
        result->compute(sourceId);
        branchCache_[sourceId] = result;
        return result;
    }

    static std::vector<int> intersection(
            const std::vector<const Closure*>& branches, bool on) {
        std::vector<int> result;
        if (branches.empty()) return result;
        for (const Node& node : branches.front()->nodes(on)) {
            bool common = true;
            for (size_t index = 1; index < branches.size(); ++index) {
                if (branches[index]->find(node.id) == nullptr) {
                    common = false;
                    break;
                }
            }
            if (common) result.push_back(node.id);
        }
        return result;
    }

    int complexity(const std::vector<std::pair<const Closure*, const Node*>>& targets) const {
        int result = 0;
        std::unordered_map<uint64_t, int> nested;
        for (const auto& target : targets) {
            result += target.first->ancestorCount(*target.second);
            target.first->collectNested(*target.second, nested);
        }
        for (const auto& item : nested) result += item.second;
        return result;
    }

    int baseRating() const {
        if (level_ > 0) return 85 + level_ * 5;
        if (nishio_) return 75;
        if (dynamic_) return 85;
        return 80;
    }

    void appendHint(std::vector<MultipleChainHint>& result,
            const std::vector<const Closure*>& branches, int targetId,
            int sortKey, Cause rootCause, int rootRegionType) const {
        std::vector<std::pair<const Closure*, const Node*>> targets;
        for (const Closure* closure : branches) {
            const Node* target = closure->find(targetId);
            if (target == nullptr)
                throw std::runtime_error("multiple chain target not found");
            targets.push_back({closure, target});
        }
        MultipleChainHint hint;
        hint.targetId = targetId;
        hint.complexity = complexity(targets);
        hint.rating = chainingLengthRating(baseRating(), hint.complexity);
        hint.sortKey = sortKey;
        int cell = potentialCell(targetId);
        int value = potentialValue(targetId);
        hint.removals[cell] = potentialOn(targetId)
                ? static_cast<uint16_t>(current_.candidates[cell] & ~(1u << value))
                : static_cast<uint16_t>(current_.candidates[cell] & (1u << value));
        if (hint.removals[cell] == 0) return;
        std::array<bool, kPotentialIds> added{};
        for (const auto& target : targets)
            target.first->collectRuleParents(*target.second, initial_, current_,
                    rootCause, rootRegionType, hint.parents, added);
        if (hint.parents.empty()) return;
        hint.signature = fullChainSignature(targets);
        result.push_back(std::move(hint));
    }

    void appendContradiction(std::vector<MultipleChainHint>& result,
            int sourceId, const Closure& closure) const {
        std::vector<std::pair<const Closure*, const Node*>> targets{
            {&closure, &closure.contradictionOn()},
            {&closure, &closure.contradictionOff()}
        };
        MultipleChainHint hint;
        hint.targetId = sourceId ^ 1;
        hint.complexity = complexity(targets);
        hint.rating = chainingLengthRating(baseRating(), hint.complexity);
        hint.sortKey = 7;
        int cell = potentialCell(hint.targetId);
        int value = potentialValue(hint.targetId);
        hint.removals[cell] = potentialOn(hint.targetId)
                ? static_cast<uint16_t>(current_.candidates[cell] & ~(1u << value))
                : static_cast<uint16_t>(current_.candidates[cell] & (1u << value));
        if (hint.removals[cell] == 0) return;
        std::array<bool, kPotentialIds> added{};
        for (const auto& target : targets)
            target.first->collectRuleParents(*target.second, initial_, current_,
                    None, -1, hint.parents, added);
        if (hint.parents.empty()) return;
        hint.signature = fullChainSignature(targets);
        result.push_back(std::move(hint));
    }

    void appendDoubleReductions(std::vector<MultipleChainHint>& result,
            const Closure& fromOn, const Closure& fromOff) const {
        std::vector<const Closure*> branches{&fromOn, &fromOff};
        for (const Node& node : fromOn.nodes(true))
            if (fromOff.find(node.id) != nullptr)
                appendHint(result, branches, node.id, 1, None, -1);
        for (const Node& node : fromOn.nodes(false))
            if (fromOff.find(node.id) != nullptr)
                appendHint(result, branches, node.id, 1, None, -1);
    }

    void appendRegion(std::vector<MultipleChainHint>& result, int sourceCell,
            int value, const Closure& firstBranch) const {
        for (int type = 0; type < 3; ++type) {
            std::vector<int> positions;
            for (int cell : region(type, sourceCell))
                if (current_.candidates[cell] & (1u << value))
                    positions.push_back(cell);
            if (positions.size() < 2 || (!multiple_ && positions.size() > 2)
                    || positions.front() != sourceCell)
                continue;
            std::vector<const Closure*> branches{&firstBranch};
            for (size_t index = 1; index < positions.size(); ++index) {
                branches.push_back(branch(
                        potentialId(positions[index], value, true)).get());
            }
            Cause root = type == 0 ? HiddenBlock
                    : type == 1 ? HiddenRow : HiddenColumn;
            for (int target : intersection(branches, true))
                appendHint(result, branches, target, 6, root, type);
            for (int target : intersection(branches, false))
                appendHint(result, branches, target, 6, root, type);
        }
    }

    void appendCell(std::vector<MultipleChainHint>& result, int cell) const {
        std::vector<ValueBranch> values;
        int cardinality = bitCount(current_.candidates[cell]);
        for (int value = 1; value <= 9; ++value) {
            if (!(current_.candidates[cell] & (1u << value))) continue;
            int onId = potentialId(cell, value, true);
            ValueBranch entry;
            entry.value = value;
            entry.on = branch(onId);
            if (dynamic_ || nishio_)
                entry.off = branch(onId - 1);
            if ((dynamic_ || nishio_) && entry.on->contradicted())
                appendContradiction(result, onId, *entry.on);
            if ((dynamic_ || nishio_) && entry.off->contradicted())
                appendContradiction(result, onId - 1, *entry.off);
            if (dynamic_ && !nishio_ && cardinality >= 3)
                appendDoubleReductions(result, *entry.on, *entry.off);
            if (!nishio_)
                appendRegion(result, cell, value, *entry.on);
            values.push_back(std::move(entry));
        }
        if (nishio_ || !(cardinality == 2 || (multiple_ && cardinality > 2)))
            return;
        std::vector<const Closure*> branches;
        for (const ValueBranch& value : values) branches.push_back(value.on.get());
        for (int target : intersection(branches, true))
            appendHint(result, branches, target, 5, NakedSingle, -1);
        for (int target : intersection(branches, false))
            appendHint(result, branches, target, 5, NakedSingle, -1);
    }
};

static std::vector<Node> emitChainAdvanced(
        const std::vector<MultipleChainHint>& hints) {
    std::vector<Node> result;
    std::array<bool, kPotentialIds> emitted{};
    for (const MultipleChainHint& hint : hints) {
        std::vector<int> cells;
        for (int cell = 0; cell < kCells; ++cell)
            if (hint.removals[cell]) cells.push_back(cell);
        int capacity = 16;
        while (cells.size() > static_cast<size_t>(capacity * 3 / 4)) capacity *= 2;
        std::stable_sort(cells.begin(), cells.end(), [capacity](int left, int right) {
            return (left & (capacity - 1)) < (right & (capacity - 1));
        });
        for (int cell : cells) {
            for (int value = 1; value <= 9; ++value) {
                if (!(hint.removals[cell] & (1u << value))) continue;
                int id = potentialId(cell, value, false);
                if (emitted[id]) continue;
                Node node;
                node.id = static_cast<int16_t>(id);
                node.cause = Advanced;
                node.nestedComplexity = static_cast<int16_t>(hint.complexity);
                node.nestedSignature = hint.signature;
                for (int parent : hint.parents) node.appendParent(parent);
                result.push_back(std::move(node));
                emitted[id] = true;
            }
        }
    }
    return result;
}

std::vector<Node> multipleStaticAdvancedPotentials(const Board& current,
                                                   const Board& initial) {
    return emitChainAdvanced(MultipleChains(
            current, initial, true, false, false, 0).hints());
}

std::vector<Node> dynamicAdvancedPotentials(const Board& current,
                                            const Board& initial,
                                            int nestingLevel) {
    return emitChainAdvanced(MultipleChains(
            current, initial, true, true, false, nestingLevel).hints());
}

struct BestHint {
    bool found = false;
    int cell = -1;
    int rating = 0;
    int complexity = 0;
    int sortKey = 0;
    int resultCell = -1;
    int resultValue = 0;
    std::array<uint16_t, kCells> removals{};
};

class Level0Dynamic {
public:
    Level0Dynamic(const Board& board, bool multiple, bool nishio, int level,
            int nestingLimit = 0)
        : board_(board), multiple_(multiple), nishio_(nishio), level_(level),
          nestingLimit_(nestingLimit) {}

    BestHint rate() {
        for (int cell = 0; cell < kCells; ++cell) {
            rateOne(cell);
        }
        return best_;
    }

    BestHint rateCells(const std::vector<int>& cells) {
        for (int cell : cells) {
            if (cell < 0 || cell >= kCells)
                throw std::invalid_argument("invalid chain cell");
            rateOne(cell);
        }
        return best_;
    }

private:
    void rateOne(int cell) {
            int cardinality = bitCount(board_.candidates[cell]);
            if (board_.values[cell] != 0 || cardinality <= 1) return;
            rateCell(cell, cardinality);
    }
    struct ValueBranch {
        int value;
        std::shared_ptr<Closure> on;
        std::shared_ptr<Closure> off;
    };

    const Board& board_;
    bool multiple_;
    bool nishio_;
    int level_;
    int nestingLimit_;
    BestHint best_;
    mutable StaticAdvancedCache staticCache_;
    mutable std::array<std::shared_ptr<Closure>, kPotentialIds> branchCache_{};

    int chainRating(int complexity) const {
        int result = level_ > 0 ? 85 + level_ * 5 : nishio_ ? 75 : 85;
        int length = complexity - 2;
        int ceiling = 4;
        bool odd = false;
        while (length > ceiling) {
            ++result;
            ceiling = odd ? ceiling * 4 / 3 : ceiling * 3 / 2;
            odd = !odd;
        }
        return result;
    }

    void consider(int sourceCell, int complexity, int sortKey, int targetId) {
        int targetCell = potentialCell(targetId);
        int targetValue = potentialValue(targetId);
        uint16_t removals = potentialOn(targetId)
                ? static_cast<uint16_t>(board_.candidates[targetCell]
                    & ~(1u << targetValue))
                : static_cast<uint16_t>(board_.candidates[targetCell]
                    & (1u << targetValue));
        if (removals == 0) return;
        BestHint incoming;
        incoming.found = true;
        incoming.cell = sourceCell;
        incoming.rating = chainRating(complexity);
        incoming.complexity = complexity;
        incoming.sortKey = sortKey;
        incoming.removals[targetCell] = removals;
        if (potentialOn(targetId)) {
            incoming.resultCell = targetCell;
            incoming.resultValue = targetValue;
        }
        accept(incoming);
    }

    void considerContradiction(int sourceCell, int sourceId,
            const Closure& closure) {
        int complexity = hintComplexity({
                {&closure, &closure.contradictionOn()},
                {&closure, &closure.contradictionOff()}
        });
        consider(sourceCell, complexity, 7, sourceId ^ 1);
    }

    void accept(const BestHint& incoming) {
        if (!best_.found || incoming.rating < best_.rating
                || (incoming.rating == best_.rating
                    && (incoming.complexity < best_.complexity
                        || (incoming.complexity == best_.complexity
                            && incoming.sortKey < best_.sortKey))))
            best_ = incoming;
    }

    std::shared_ptr<Closure> branch(int sourceId) const {
        if (branchCache_[sourceId]) return branchCache_[sourceId];
        auto result = std::make_shared<Closure>(board_, true, nishio_, level_,
                                                &staticCache_, nestingLimit_);
        try {
            result->compute(sourceId);
        } catch (const std::exception& error) {
            throw std::runtime_error("branch " + std::to_string(sourceId)
                    + ": " + error.what());
        }
        branchCache_[sourceId] = result;
        return result;
    }

    std::vector<int> intersection(const std::vector<const Closure*>& branches,
                                  bool on) const {
        std::vector<int> result;
        if (branches.empty()) return result;
        for (const Node& node : branches[0]->nodes(on)) {
            bool common = true;
            for (size_t i = 1; i < branches.size(); ++i) {
                if (branches[i]->find(node.id) == nullptr) {
                    common = false;
                    break;
                }
            }
            if (common) result.push_back(node.id);
        }
        return result;
    }

    int combinedComplexity(int targetId,
                           const std::vector<const Closure*>& branches) const {
        std::vector<std::pair<const Closure*, const Node*>> targets;
        for (const Closure* branch : branches) {
            const Node* target = branch->find(targetId);
            if (target == nullptr)
                throw std::runtime_error("common chain target not found");
            targets.push_back({branch, target});
        }
        return hintComplexity(targets);
    }

    int hintComplexity(const std::vector<std::pair<const Closure*, const Node*>>& targets) const {
        int result = 0;
        std::unordered_map<uint64_t, int> nested;
        for (const auto& target : targets) {
            result += target.first->ancestorCount(*target.second);
            target.first->collectNested(*target.second, nested);
        }
        for (const auto& item : nested) result += item.second;
        return result;
    }

    void reductions(int sourceCell, const Closure& fromOn,
                    const Closure& fromOff) {
        for (const Node& node : fromOn.nodes(true)) {
            const Node* other = fromOff.find(node.id);
            if (other != nullptr) {
                int complexity = hintComplexity({{&fromOn, &node}, {&fromOff, other}});
                consider(sourceCell, complexity, 1, node.id);
            }
        }
        for (const Node& node : fromOn.nodes(false)) {
            const Node* other = fromOff.find(node.id);
            if (other != nullptr) {
                int complexity = hintComplexity({{&fromOn, &node}, {&fromOff, other}});
                consider(sourceCell, complexity, 1, node.id);
            }
        }
    }

    void regionReductions(int sourceCell, int value,
                          const Closure& firstBranch) {
        for (int type = 0; type < 3; ++type) {
            auto cells = ClosureRegion::cells(type, sourceCell);
            std::vector<int> positions;
            for (int cell : cells)
                if (board_.candidates[cell] & (1u << value)) positions.push_back(cell);
            if (positions.size() < 2 || (!multiple_ && positions.size() > 2)
                    || positions.front() != sourceCell)
                continue;
            std::vector<const Closure*> branches;
            branches.push_back(&firstBranch);
            for (size_t i = 1; i < positions.size(); ++i) {
                branches.push_back(branch(
                        potentialId(positions[i], value, true)).get());
            }
            for (int target : intersection(branches, true))
                consider(sourceCell, combinedComplexity(target, branches), 6, target);
            for (int target : intersection(branches, false))
                consider(sourceCell, combinedComplexity(target, branches), 6, target);
        }
    }

    void rateCell(int cell, int cardinality) {
        std::vector<ValueBranch> values;
        for (int value = 1; value <= 9; ++value) {
            if (!(board_.candidates[cell] & (1u << value))) continue;
            int onId = potentialId(cell, value, true);
            int offId = onId - 1;
            ValueBranch entry{value, branch(onId), branch(offId)};
            if (entry.on->contradicted())
                considerContradiction(cell, onId, *entry.on);
            if (entry.off->contradicted())
                considerContradiction(cell, offId, *entry.off);
            if (cardinality >= 3 && !nishio_)
                reductions(cell, *entry.on, *entry.off);
            if (!nishio_)
                regionReductions(cell, value, *entry.on);
            values.push_back(std::move(entry));
        }
        if (nishio_ || (cardinality > 2 && !multiple_)) return;
        std::vector<const Closure*> branches;
        for (const ValueBranch& value : values) branches.push_back(value.on.get());
        for (int target : intersection(branches, true))
            consider(cell, combinedComplexity(target, branches), 5, target);
        for (int target : intersection(branches, false))
            consider(cell, combinedComplexity(target, branches), 5, target);
    }

    // Keeps region ordering in one small public adapter without exposing
    // Closure's implication internals.
    struct ClosureRegion {
        static std::array<int, 9> cells(int type, int cell) {
            std::array<int, 9> result{};
            int row = cell / 9;
            int col = cell % 9;
            if (type == 0) {
                int at = 0;
                for (int y = row / 3 * 3; y < row / 3 * 3 + 3; ++y)
                    for (int x = col / 3 * 3; x < col / 3 * 3 + 3; ++x)
                        result[at++] = y * 9 + x;
            } else if (type == 1) {
                for (int x = 0; x < 9; ++x) result[x] = row * 9 + x;
            } else {
                for (int y = 0; y < 9; ++y) result[y] = y * 9 + col;
            }
            return result;
        }
    };
};

}  // namespace sefast

static std::vector<int> parseIds(const char* text) {
    std::vector<int> result;
    if (text == nullptr || *text == '\0') return result;
    std::stringstream input(text);
    std::string item;
    while (std::getline(input, item, ',')) result.push_back(std::atoi(item.c_str()));
    return result;
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_closure(
        const char* state, const char* onIds, const char* offIds,
        int dynamic, int nishio) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::Closure closure(board, dynamic != 0, nishio != 0);
        result = closure.run(parseIds(onIds), parseIds(offIds));
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

static std::vector<uint16_t> packedResult;

extern "C" EMSCRIPTEN_KEEPALIVE const uint16_t* sefast_closure_packed(
        const char* state, const char* onIds, const char* offIds,
        int dynamic, int nishio) {
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::Closure closure(board, dynamic != 0, nishio != 0);
        packedResult = closure.runPacked(parseIds(onIds), parseIds(offIds));
    } catch (...) {
        packedResult.assign(1, 0);
    }
    return packedResult.data();
}

extern "C" EMSCRIPTEN_KEEPALIVE int sefast_closure_length() {
    return static_cast<int>(packedResult.size());
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_level0(
        const char* state, int multiple, int dynamic, int nishio, int level) {
    static std::string result;
    if (!dynamic || level < 0 || level > 3) return "-1";
    try {
        sefast::diagnosticAdvancedCalls = 0;
        sefast::diagnosticStaticCacheHits = 0;
        sefast::diagnosticStaticCacheMisses = 0;
        sefast::diagnosticStaticHints = 0;
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::BestHint best = sefast::Level0Dynamic(
                board, multiple != 0, nishio != 0, level).rate();
        if (!best.found) return "";
        std::ostringstream out;
        out << best.cell << ',' << best.rating << ',' << best.complexity << ','
            << best.sortKey << ',' << best.resultCell << ',' << best.resultValue;
        for (uint16_t mask : best.removals) out << ',' << mask;
        result = out.str();
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_chain(
        const char* state, int multiple, int dynamic, int nishio, int level,
        int nestingLimit) {
    static std::string result;
    if (!dynamic || level < 0 || level > 4
            || nestingLimit < 0 || nestingLimit > 3)
        return "-1";
    try {
        sefast::diagnosticAdvancedCalls = 0;
        sefast::diagnosticStaticCacheHits = 0;
        sefast::diagnosticStaticCacheMisses = 0;
        sefast::diagnosticStaticHints = 0;
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::BestHint best = sefast::Level0Dynamic(
                board, multiple != 0, nishio != 0, level, nestingLimit).rate();
        if (!best.found) return "";
        std::ostringstream out;
        out << best.cell << ',' << best.rating << ',' << best.complexity << ','
            << best.sortKey << ',' << best.resultCell << ',' << best.resultValue;
        for (uint16_t mask : best.removals) out << ',' << mask;
        result = out.str();
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_chain_cells(
        const char* state, const char* cells, int multiple, int dynamic,
        int nishio, int level, int nestingLimit) {
    static std::string result;
    if (!dynamic || level < 0 || level > 4
            || nestingLimit < 0 || nestingLimit > 3)
        return "-1";
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        sefast::BestHint best = sefast::Level0Dynamic(
                board, multiple != 0, nishio != 0, level, nestingLimit)
                .rateCells(parseIds(cells));
        if (!best.found) return "";
        std::ostringstream out;
        out << best.cell << ',' << best.rating << ',' << best.complexity << ','
            << best.sortKey << ',' << best.resultCell << ',' << best.resultValue;
        for (uint16_t mask : best.removals) out << ',' << mask;
        result = out.str();
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_diagnostics() {
    static std::string result;
    result = std::to_string(sefast::diagnosticAdvancedCalls) + ","
            + std::to_string(sefast::diagnosticStaticCacheHits) + ","
            + std::to_string(sefast::diagnosticStaticCacheMisses) + ","
            + std::to_string(sefast::diagnosticStaticHints);
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sefast_best_static(const char* state) {
    static std::string result;
    try {
        auto board = sefast::Board::fromInput(state == nullptr ? "" : state);
        std::vector<sefast::StaticHint> hints = sefast::StaticChains(board).hints();
        if (hints.empty()) return "";
        const sefast::StaticHint& best = hints.front();
        int resultCell = -1;
        int resultValue = 0;
        if (best.kind == sefast::StaticHint::Forcing && sefast::potentialOn(best.targetId)) {
            resultCell = sefast::potentialCell(best.targetId);
            resultValue = sefast::potentialValue(best.targetId);
        }
        std::ostringstream out;
        out << -1 << ',' << best.rating << ',' << best.complexity << ','
            << best.sortKey << ',' << resultCell << ',' << resultValue;
        for (uint16_t mask : best.removals) out << ',' << mask;
        result = out.str();
    } catch (const std::exception& error) {
        result = std::string("ERROR,") + error.what();
    }
    return result.c_str();
}

#ifndef SEFAST_NO_MAIN
int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: se_closure PUZZLE SOURCE_ID DYNAMIC NISHIO\n";
        return 2;
    }
    try {
        auto board = sefast::Board::fromInput(argv[1]);
        std::stringstream sources(argv[2]);
        std::string item;
        while (std::getline(sources, item, ',')) {
            int source = std::atoi(item.c_str());
            sefast::Closure closure(board, std::atoi(argv[3]) != 0,
                                    std::atoi(argv[4]) != 0);
            std::cout << source << '=' << closure.run(source) << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
