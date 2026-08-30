/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "py_normalize.h"

#include <cstring>

namespace pime {

namespace {

// Every non-ASCII character the pipeline can emit (PINYIN_EXTRA in
// build_hanzi_data.py, plus the ê family for future-proofing). The pipeline
// asserts its pinyin stays within the subset font's alphabet and
// hanzi_logic_test asserts this table consumes the whole blob, so the two
// lists cannot drift apart silently.
struct ToneMap {
    const char* seq;  // one UTF-8 encoded character
    char plain;
};

constexpr ToneMap kToneMap[] = {
    {"ā", 'a'}, {"á", 'a'}, {"ǎ", 'a'}, {"à", 'a'}, {"ē", 'e'}, {"é", 'e'}, {"ě", 'e'}, {"è", 'e'},
    {"ī", 'i'}, {"í", 'i'}, {"ǐ", 'i'}, {"ì", 'i'}, {"ō", 'o'}, {"ó", 'o'}, {"ǒ", 'o'}, {"ò", 'o'},
    {"ū", 'u'}, {"ú", 'u'}, {"ǔ", 'u'}, {"ù", 'u'}, {"ǖ", 'v'}, {"ǘ", 'v'}, {"ǚ", 'v'}, {"ǜ", 'v'},
    {"ü", 'v'}, {"ń", 'n'}, {"ň", 'n'}, {"ǹ", 'n'}, {"ḿ", 'm'}, {"ê", 'e'}, {"ế", 'e'}, {"ề", 'e'},
};

}  // namespace

size_t pyNormalize(const char* toned, char* out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (toned == nullptr || toned[0] == '\0') {
        return 0;
    }
    size_t written = 0;
    const char* p  = toned;
    while (*p != '\0') {
        char plain               = 0;
        size_t step              = 0;
        const unsigned char lead = static_cast<unsigned char>(*p);
        if (lead < 0x80) {
            if (lead >= 'a' && lead <= 'z') {
                plain = static_cast<char>(lead);
                step  = 1;
            }
        } else {
            for (const ToneMap& m : kToneMap) {
                const size_t n = std::strlen(m.seq);
                if (std::strncmp(p, m.seq, n) == 0) {
                    plain = m.plain;
                    step  = n;
                    break;
                }
            }
        }
        if (plain == 0 || written + 1 >= cap) {
            out[0] = '\0';
            return 0;
        }
        out[written++] = plain;
        p += step;
    }
    out[written] = '\0';
    return written;
}

size_t pyDisplay(const char* plain, char* out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (plain == nullptr) {
        return 0;
    }
    size_t written = 0;
    for (const char* p = plain; *p != '\0'; ++p) {
        const size_t need = *p == 'v' ? 2 : 1;
        if (written + need >= cap) {
            out[0] = '\0';
            return 0;
        }
        if (*p == 'v') {
            out[written++] = static_cast<char>(0xC3);  // U+00FC "ü"
            out[written++] = static_cast<char>(0xBC);
        } else {
            out[written++] = *p;
        }
    }
    out[written] = '\0';
    return written;
}

char pyDigitOf(char letter)
{
    if (letter < 'a' || letter > 'z') {
        return 0;
    }
    // Standard phone keypad: abc=2 def=3 ghi=4 jkl=5 mno=6 pqrs=7 tuv=8
    // wxyz=9.
    static const char kDigits[] = "22233344455566677778889999";
    return kDigits[letter - 'a'];
}

}  // namespace pime
