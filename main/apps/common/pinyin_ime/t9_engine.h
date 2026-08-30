/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Pinyin candidate engine. Pure logic, no LVGL / ESP-IDF headers -- the
// host tests compile this file directly (same discipline as hz_data.h).
//
// Besides the historical T9 layers (interpretations() for digit strings,
// query() for letter prefixes), the engine decomposes its syllable table for
// the wheel-picker UI: every syllable splits uniquely into a first unit
// (zh/ch/sh whole, otherwise the first letter -- bare vowels like "a" count
// too) and a suffix (possibly empty: the unit itself is the syllable). The
// enumeration is data-driven, so the picker can never dial a combination
// with an empty candidate list.

namespace pime {

// One (reading, character) pair fed to build(). Multi-reading characters
// contribute one entry per reading. `syllable` is toneless lowercase ASCII
// (see pyNormalize). `id` doubles as the ranking weight -- lower is more
// common. For the hanzi app the id is the teaching-order index, whose
// extended tail the pipeline sorts by frequency, so id order is usage order.
struct Entry {
    const char* syllable;
    uint16_t id;
};

// The picker view talks to this interface only, so any lookup strategy (or
// app) can back it. All strings returned here stay valid for the lifetime of
// the source's current build.
class CandidateSource {
public:
    virtual ~CandidateSource() = default;

    // First units in letter order ("a", "b", ..., "ch", ..., "zh"). Only
    // units that begin at least one syllable exist.
    virtual uint16_t unitCount() const              = 0;
    virtual const char* unitAt(uint16_t unit) const = 0;

    // Suffixes of one unit in letter order. The empty string appears when
    // the unit alone is a complete syllable ("a", "e", "m", "o").
    virtual uint16_t suffixCount(uint16_t unit) const                  = 0;
    virtual const char* suffixAt(uint16_t unit, uint16_t suffix) const = 0;

    // Splits a toneless syllable into its (unit, suffix) indices. False when
    // the syllable is not in the table.
    virtual bool locate(const char* syllable, uint16_t& unit, uint16_t& suffix) const = 0;

    // Characters whose reading is exactly `syllable`, id ascending (id order
    // is usage order, see Entry). Skips `offset` results, writes at most
    // `cap` ids, returns the total match count. Unlike query() this never
    // includes longer continuations: on the picker a complete syllable is
    // dialled, and "xia" belongs to the x+ia combination, not to x+i.
    virtual uint16_t queryExact(const char* syllable, uint16_t* out, uint16_t cap, uint16_t offset) const = 0;
};

class T9Engine : public CandidateSource {
public:
    // Longest pinyin syllable is 6 letters (zhuang / shuang).
    static constexpr size_t kMaxSyllable  = 6;
    static constexpr uint16_t kMaxInterps = 24;

    // Copies everything out of `entries`; duplicates collapse. Fails (and
    // clears) on an empty list, an overlong or non-ASCII syllable, or more
    // than 65535 ids.
    bool build(const Entry* entries, size_t count);
    void clear();
    bool ready() const
    {
        return !_syllables.empty();
    }

    uint16_t unitCount() const override;
    const char* unitAt(uint16_t unit) const override;
    uint16_t suffixCount(uint16_t unit) const override;
    const char* suffixAt(uint16_t unit, uint16_t suffix) const override;
    bool locate(const char* syllable, uint16_t& unit, uint16_t& suffix) const override;
    uint16_t queryExact(const char* syllable, uint16_t* out, uint16_t cap, uint16_t offset) const override;

    // The historical T9 digit layers, kept as concrete methods: the letter
    // interpretations of a digit string, and prefix matching with longer
    // continuations. Host-tested; no current view uses them.
    uint16_t interpretations(const char* digits, const char** out, uint16_t cap) const;
    uint16_t query(const char* interp, uint16_t* out, uint16_t cap, uint16_t offset) const;

private:
    struct Syllable {
        char text[kMaxSyllable + 1];
        char digits[kMaxSyllable + 1];
        uint32_t first = 0;  // range into _ids, id-ascending
        uint16_t count = 0;
    };
    struct Unit {
        char text[3];
        uint32_t first = 0;  // range into _unit_syllables
        uint16_t count = 0;
    };

    std::vector<Syllable> _syllables;  // sorted by text
    std::vector<uint16_t> _ids;
    // Units sorted by text; each spans a run of _unit_syllables holding
    // indices into _syllables in text order (so suffixes come out sorted:
    // dropping a shared prefix preserves lexicographic order).
    std::vector<Unit> _units;
    std::vector<uint16_t> _unit_syllables;
    uint16_t _max_id = 0;

    // interpretations() result storage; see the interface note on lifetime.
    mutable char _interp_text[kMaxInterps][kMaxSyllable + 1];
    // query() dedup scratch, one bit per id, sized by build().
    mutable std::vector<uint8_t> _seen;
};

}  // namespace pime
