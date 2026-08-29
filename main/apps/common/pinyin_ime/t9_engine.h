/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// T9 pinyin candidate engine. Pure logic, no LVGL / ESP-IDF headers -- the
// host tests compile this file directly (same discipline as hz_data.h).
//
// A digit string is ambiguous twice over: it can spell several letter
// prefixes (426 -> hao/gao/han/gan/...), and each prefix covers several
// characters. The engine therefore answers in two layers, mirroring how a
// phone IME presents them: interpretations() lists the letter prefixes worth
// offering, query() lists the characters under one of them.

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

// The view talks to this interface only, so any lookup strategy (or app)
// can back it.
class CandidateSource {
public:
    virtual ~CandidateSource() = default;

    // Letter interpretations of a T9 digit string ('2'..'9' only): the
    // distinct prefixes that lead to at least one syllable, best first (the
    // one containing the lowest id wins). Returns the total count; writes at
    // most `cap` pointers. The strings stay valid until the next
    // interpretations() call on the same object.
    virtual uint16_t interpretations(const char* digits, const char** out, uint16_t cap) const = 0;

    // Character candidates of one interpretation string as returned above:
    // exact-syllable matches first, then longer continuations, both id
    // ascending, duplicates removed. Skips `offset` results, writes at most
    // `cap` ids, returns the total match count.
    virtual uint16_t query(const char* interp, uint16_t* out, uint16_t cap, uint16_t offset) const = 0;
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

    uint16_t interpretations(const char* digits, const char** out, uint16_t cap) const override;
    uint16_t query(const char* interp, uint16_t* out, uint16_t cap, uint16_t offset) const override;

private:
    struct Syllable {
        char text[kMaxSyllable + 1];
        char digits[kMaxSyllable + 1];
        uint32_t first = 0;  // range into _ids, id-ascending
        uint16_t count = 0;
    };

    std::vector<Syllable> _syllables;  // sorted by text
    std::vector<uint16_t> _ids;
    uint16_t _max_id = 0;

    // interpretations() result storage; see the interface note on lifetime.
    mutable char _interp_text[kMaxInterps][kMaxSyllable + 1];
    // query() dedup scratch, one bit per id, sized by build().
    mutable std::vector<uint8_t> _seen;
};

}  // namespace pime
