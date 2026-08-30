/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "t9_engine.h"

#include <algorithm>
#include <cstring>

#include "py_normalize.h"

namespace pime {

namespace {

bool validSyllable(const char* s, size_t max_len)
{
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    size_t len = 0;
    for (const char* p = s; *p != '\0'; ++p, ++len) {
        if (*p < 'a' || *p > 'z' || len >= max_len) {
            return false;
        }
    }
    return true;
}

// Length of the first unit of a syllable: zh/ch/sh are whole units, any
// other syllable contributes its first letter (bare vowels included).
size_t unitLen(const char* syllable)
{
    if ((syllable[0] == 'z' || syllable[0] == 'c' || syllable[0] == 's') && syllable[1] == 'h') {
        return 2;
    }
    return 1;
}

}  // namespace

void T9Engine::clear()
{
    _syllables.clear();
    _ids.clear();
    _units.clear();
    _unit_syllables.clear();
    _seen.clear();
    _max_id = 0;
}

bool T9Engine::build(const Entry* entries, size_t count)
{
    clear();
    if (entries == nullptr || count == 0) {
        return false;
    }

    struct Item {
        char text[kMaxSyllable + 1];
        uint16_t id;
    };
    std::vector<Item> items;
    items.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (!validSyllable(entries[i].syllable, kMaxSyllable)) {
            clear();
            return false;
        }
        Item item{};
        std::strncpy(item.text, entries[i].syllable, kMaxSyllable);
        item.id = entries[i].id;
        items.push_back(item);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const int c = std::strcmp(a.text, b.text);
        return c != 0 ? c < 0 : a.id < b.id;
    });
    items.erase(
        std::unique(items.begin(), items.end(),
                    [](const Item& a, const Item& b) { return a.id == b.id && std::strcmp(a.text, b.text) == 0; }),
        items.end());

    for (const Item& item : items) {
        if (_syllables.empty() || std::strcmp(_syllables.back().text, item.text) != 0) {
            Syllable syl{};
            std::strncpy(syl.text, item.text, kMaxSyllable);
            for (size_t i = 0; syl.text[i] != '\0'; ++i) {
                syl.digits[i] = pyDigitOf(syl.text[i]);
            }
            syl.first = static_cast<uint32_t>(_ids.size());
            _syllables.push_back(syl);
        }
        _syllables.back().count++;
        _ids.push_back(item.id);
        _max_id = std::max(_max_id, item.id);
    }
    _seen.assign((static_cast<size_t>(_max_id) + 8) / 8, 0);

    // Unit table. "c" and "ch" interleave in the text-sorted syllable list
    // (ca < cha < ci), so group via a stable sort on unit text: syllable
    // indices stay ascending within a unit, which keeps suffixes sorted.
    std::vector<uint16_t> order(_syllables.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<uint16_t>(i);
    }
    auto unitText = [this](uint16_t idx, char(&buf)[3]) {
        const char* text = _syllables[idx].text;
        const size_t ul  = unitLen(text);
        std::memcpy(buf, text, ul);
        buf[ul] = '\0';
    };
    std::stable_sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) {
        char ua[3], ub[3];
        unitText(a, ua);
        unitText(b, ub);
        return std::strcmp(ua, ub) < 0;
    });
    for (uint16_t idx : order) {
        const char* text = _syllables[idx].text;
        const size_t ul  = unitLen(text);
        if (_units.empty() || std::strlen(_units.back().text) != ul ||
            std::strncmp(_units.back().text, text, ul) != 0) {
            Unit unit{};
            std::memcpy(unit.text, text, ul);
            unit.first = static_cast<uint32_t>(_unit_syllables.size());
            _units.push_back(unit);
        }
        _units.back().count++;
        _unit_syllables.push_back(idx);
    }
    return true;
}

uint16_t T9Engine::unitCount() const
{
    return static_cast<uint16_t>(_units.size());
}

const char* T9Engine::unitAt(uint16_t unit) const
{
    return unit < _units.size() ? _units[unit].text : "";
}

uint16_t T9Engine::suffixCount(uint16_t unit) const
{
    return unit < _units.size() ? _units[unit].count : 0;
}

const char* T9Engine::suffixAt(uint16_t unit, uint16_t suffix) const
{
    if (unit >= _units.size() || suffix >= _units[unit].count) {
        return "";
    }
    const char* text = _syllables[_unit_syllables[_units[unit].first + suffix]].text;
    return text + unitLen(text);
}

bool T9Engine::locate(const char* syllable, uint16_t& unit, uint16_t& suffix) const
{
    if (!validSyllable(syllable, kMaxSyllable)) {
        return false;
    }
    for (uint16_t u = 0; u < _units.size(); ++u) {
        const size_t ul = std::strlen(_units[u].text);
        if (std::strncmp(_units[u].text, syllable, ul) != 0 || unitLen(syllable) != ul) {
            continue;
        }
        for (uint16_t s = 0; s < _units[u].count; ++s) {
            if (std::strcmp(suffixAt(u, s), syllable + ul) == 0) {
                unit   = u;
                suffix = s;
                return true;
            }
        }
        return false;
    }
    return false;
}

uint16_t T9Engine::queryExact(const char* syllable, uint16_t* out, uint16_t cap, uint16_t offset) const
{
    if (!validSyllable(syllable, kMaxSyllable)) {
        return 0;
    }
    // _syllables is text-sorted; binary search for the exact entry.
    size_t lo = 0, hi = _syllables.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (std::strcmp(_syllables[mid].text, syllable) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= _syllables.size() || std::strcmp(_syllables[lo].text, syllable) != 0) {
        return 0;
    }
    const Syllable& syl = _syllables[lo];
    if (out != nullptr) {
        for (uint16_t i = offset; i < syl.count && static_cast<uint32_t>(i - offset) < cap; ++i) {
            out[i - offset] = _ids[syl.first + i];
        }
    }
    return syl.count;
}

uint16_t T9Engine::interpretations(const char* digits, const char** out, uint16_t cap) const
{
    if (digits == nullptr || digits[0] == '\0') {
        return 0;
    }
    const size_t len = std::strlen(digits);
    if (len > kMaxSyllable) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        if (digits[i] < '2' || digits[i] > '9') {
            return 0;
        }
    }

    // Distinct letter prefixes of the matching syllables, each carrying the
    // best (lowest) id found underneath. First digits differ in at most four
    // letters and later positions only narrow the set, so the count stays
    // far below kMaxInterps in practice.
    char texts[kMaxInterps][kMaxSyllable + 1];
    uint16_t weights[kMaxInterps];
    uint16_t n = 0;
    for (const Syllable& syl : _syllables) {
        if (std::strlen(syl.text) < len || std::strncmp(syl.digits, digits, len) != 0) {
            continue;
        }
        const uint16_t weight = _ids[syl.first];
        uint16_t slot         = n;
        for (uint16_t i = 0; i < n; ++i) {
            if (std::strncmp(texts[i], syl.text, len) == 0) {
                slot = i;
                break;
            }
        }
        if (slot == n) {
            if (n >= kMaxInterps) {
                continue;
            }
            std::memcpy(texts[n], syl.text, len);
            texts[n][len] = '\0';
            weights[n]    = weight;
            n++;
        } else {
            weights[slot] = std::min(weights[slot], weight);
        }
    }

    // Insertion sort by (weight, text); n is tiny.
    for (uint16_t i = 1; i < n; ++i) {
        char text[kMaxSyllable + 1];
        std::memcpy(text, texts[i], sizeof(text));
        const uint16_t weight = weights[i];
        int j                 = i - 1;
        while (j >= 0 && (weights[j] > weight || (weights[j] == weight && std::strcmp(texts[j], text) > 0))) {
            std::memcpy(texts[j + 1], texts[j], sizeof(text));
            weights[j + 1] = weights[j];
            j--;
        }
        std::memcpy(texts[j + 1], text, sizeof(text));
        weights[j + 1] = weight;
    }

    const uint16_t write = std::min<uint16_t>(n, cap);
    for (uint16_t i = 0; i < write; ++i) {
        std::memcpy(_interp_text[i], texts[i], sizeof(texts[i]));
        if (out != nullptr) {
            out[i] = _interp_text[i];
        }
    }
    return n;
}

uint16_t T9Engine::query(const char* interp, uint16_t* out, uint16_t cap, uint16_t offset) const
{
    if (!validSyllable(interp, kMaxSyllable) || _seen.empty()) {
        return 0;
    }
    const size_t len = std::strlen(interp);
    std::fill(_seen.begin(), _seen.end(), 0);

    uint16_t total = 0;
    auto emit      = [&](uint16_t id) {
        uint8_t& byte     = _seen[id / 8];
        const uint8_t bit = static_cast<uint8_t>(1u << (id % 8));
        if ((byte & bit) != 0) {
            return;
        }
        byte |= bit;
        if (out != nullptr && total >= offset && static_cast<uint32_t>(total - offset) < cap) {
            out[total - offset] = id;
        }
        total++;
    };

    // Exact-syllable group first: these are the characters whose whole
    // reading the child has typed, which beats any longer continuation.
    struct Cursor {
        uint32_t pos;
        uint32_t end;
    };
    std::vector<Cursor> cursors;
    for (const Syllable& syl : _syllables) {
        const int cmp = std::strncmp(syl.text, interp, len);
        if (cmp < 0) {
            continue;
        }
        if (cmp > 0) {
            break;  // _syllables is text-sorted; nothing later can match
        }
        if (syl.text[len] == '\0') {
            for (uint16_t i = 0; i < syl.count; ++i) {
                emit(_ids[syl.first + i]);
            }
        } else {
            cursors.push_back({syl.first, syl.first + syl.count});
        }
    }

    // Continuations, merged id-ascending across their syllables. The cursor
    // list is a few dozen at worst, so a linear min scan is fine.
    while (true) {
        Cursor* best = nullptr;
        for (Cursor& c : cursors) {
            if (c.pos < c.end && (best == nullptr || _ids[c.pos] < _ids[best->pos])) {
                best = &c;
            }
        }
        if (best == nullptr) {
            break;
        }
        emit(_ids[best->pos]);
        best->pos++;
    }
    return total;
}

}  // namespace pime
