// Host-side invariant tests for the pinyin_ime T9 engine, fed by the real
// shipped blob (HANZI_BLOB_PATH). Follows the math_logic_test skeleton:
// check()/describe() accounting, exhaustive small inputs plus full-data
// sweeps, non-zero exit on any failure.
//
// The strongest test here is differential: a deliberately naive reference
// implementation (maps and linear scans, no shared code with the engine)
// must agree with the engine byte for byte on every probed input.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hz_data.h"
#include "py_normalize.h"
#include "t9_engine.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

void check(bool ok, const std::string& what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        if (g_failures <= 20) {
            std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        } else if (g_failures == 21) {
            std::fprintf(stderr, "FAIL: (further failures suppressed)\n");
        }
    }
}

std::vector<uint8_t> readFile(const char* path)
{
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return data;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    data.resize(static_cast<size_t>(size));
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) {
        data.clear();
    }
    std::fclose(f);
    return data;
}

// ---------------------------------------------------------------------------
// naive reference implementation

struct Reference {
    // syllable -> ids, id-ascending
    std::map<std::string, std::vector<uint16_t>> by_syllable;

    std::string digitsOf(const std::string& syl) const
    {
        std::string d;
        for (char c : syl) {
            d.push_back(pime::pyDigitOf(c));
        }
        return d;
    }

    // best-first letter interpretations of a digit string
    std::vector<std::string> interpretations(const std::string& digits) const
    {
        std::map<std::string, uint16_t> weight;  // prefix -> min id
        for (const auto& [syl, ids] : by_syllable) {
            if (syl.size() < digits.size()) {
                continue;
            }
            const std::string prefix = syl.substr(0, digits.size());
            if (digitsOf(prefix) != digits) {
                continue;
            }
            auto [it, fresh] = weight.emplace(prefix, ids.front());
            if (!fresh) {
                it->second = std::min(it->second, ids.front());
            }
        }
        std::vector<std::string> out;
        for (const auto& [prefix, _] : weight) {
            out.push_back(prefix);
        }
        std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
            if (weight.at(a) != weight.at(b)) {
                return weight.at(a) < weight.at(b);
            }
            return a < b;
        });
        return out;
    }

    // exact matches first, then continuations merged id-ascending, deduped
    std::vector<uint16_t> query(const std::string& interp) const
    {
        std::vector<uint16_t> out;
        std::set<uint16_t> seen;
        auto exact = by_syllable.find(interp);
        if (exact != by_syllable.end()) {
            for (uint16_t id : exact->second) {
                if (seen.insert(id).second) {
                    out.push_back(id);
                }
            }
        }
        std::vector<uint16_t> tail;
        for (const auto& [syl, ids] : by_syllable) {
            if (syl.size() > interp.size() && syl.compare(0, interp.size(), interp) == 0) {
                tail.insert(tail.end(), ids.begin(), ids.end());
            }
        }
        std::sort(tail.begin(), tail.end());
        for (uint16_t id : tail) {
            if (seen.insert(id).second) {
                out.push_back(id);
            }
        }
        return out;
    }
};

std::vector<uint16_t> engineQueryAll(const pime::T9Engine& engine, const std::string& interp)
{
    const uint16_t total = engine.query(interp.c_str(), nullptr, 0, 0);
    std::vector<uint16_t> out(total);
    if (total > 0) {
        engine.query(interp.c_str(), out.data(), total, 0);
    }
    return out;
}

std::vector<std::string> engineInterpsAll(const pime::T9Engine& engine, const std::string& digits)
{
    const char* ptrs[pime::T9Engine::kMaxInterps];
    const uint16_t n = engine.interpretations(digits.c_str(), ptrs, pime::T9Engine::kMaxInterps);
    std::vector<std::string> out;
    for (uint16_t i = 0; i < std::min<uint16_t>(n, pime::T9Engine::kMaxInterps); ++i) {
        out.emplace_back(ptrs[i]);
    }
    return out;
}

}  // namespace

int main()
{
    const std::vector<uint8_t> blob = readFile(HANZI_BLOB_PATH);
    hz::DataSource src;
    if (blob.empty() || !src.bind(blob.data(), blob.size())) {
        std::fprintf(stderr, "cannot bind %s\n", HANZI_BLOB_PATH);
        return 2;
    }
    std::printf("blob: %zu bytes, %u chars, %u lessons\n", blob.size(), src.charCount(), src.lessonCount());

    // -- normalisation: explicit table cases -------------------------------
    const struct {
        const char* toned;
        const char* plain;
    } kNorm[] = {
        {"hǎo", "hao"}, {"hào", "hao"}, {"lǜ", "lu"}, {"nǚ", "nu"}, {"zhuāng", "zhuang"},
        {"ér", "er"},   {"ń", "n"},     {"ḿ", "m"},   {"de", "de"}, {"ê", "e"},
    };
    for (const auto& c : kNorm) {
        char out[16];
        pime::pyNormalize(c.toned, out, sizeof(out));
        check(std::strcmp(out, c.plain) == 0, std::string("normalize ") + c.toned + " -> " + out + ", want " + c.plain);
    }
    {
        char out[16];
        check(pime::pyNormalize("Hao", out, sizeof(out)) == 0, "normalize rejects upper case");
        check(pime::pyNormalize("", out, sizeof(out)) == 0, "normalize rejects empty");
        check(pime::pyNormalize("hǎo!", out, sizeof(out)) == 0, "normalize rejects stray punctuation");
    }

    // -- build entries from the real blob ----------------------------------
    // Also proves every reading in the blob is consumable: pyNormalize
    // failing on shipped data is a pipeline/table drift, not a skip.
    Reference ref;
    std::vector<std::string> syllable_store;
    struct Reading {
        std::string syllable;
        uint16_t order;
    };
    std::vector<Reading> readings;
    for (uint16_t order = 0; order < src.charCount(); ++order) {
        const char* pinyin = src.pinyinAt(order);
        check(pinyin != nullptr && pinyin[0] != '\0', "char " + std::to_string(order) + " has empty pinyin");
        std::string field(pinyin != nullptr ? pinyin : "");
        size_t start = 0;
        while (start < field.size()) {
            size_t end = field.find(' ', start);
            if (end == std::string::npos) {
                end = field.size();
            }
            const std::string toned = field.substr(start, end - start);
            start                   = end + 1;
            char plain[16];
            const size_t n = pime::pyNormalize(toned.c_str(), plain, sizeof(plain));
            check(n > 0 && n <= pime::T9Engine::kMaxSyllable,
                  "char " + std::to_string(order) + " reading not normalisable: " + toned);
            if (n == 0) {
                continue;
            }
            readings.push_back({plain, order});
            ref.by_syllable[plain].push_back(order);
        }
    }
    for (auto& [syl, ids] : ref.by_syllable) {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    std::printf("readings: %zu across %zu distinct syllables\n", readings.size(), ref.by_syllable.size());

    pime::T9Engine engine;
    {
        std::vector<pime::Entry> entries;
        entries.reserve(readings.size());
        for (const Reading& r : readings) {
            entries.push_back({r.syllable.c_str(), r.order});
        }
        check(engine.build(entries.data(), entries.size()), "engine build");
    }

    // -- structure: lessons cover exactly the textbook prefix --------------
    {
        const hz::DataSource::Lesson last = src.lessonAt(static_cast<uint16_t>(src.lessonCount() - 1));
        const uint16_t core_end           = last.first_char + last.char_count;
        check(core_end > 0 && core_end <= src.charCount(), "lesson tables cover a sane prefix");
        for (uint16_t order = 0; order < src.charCount(); ++order) {
            const bool in_lesson = src.lessonOfChar(order) >= 0;
            check(in_lesson == (order < core_end),
                  "order " + std::to_string(order) +
                      (in_lesson ? " unexpectedly in a lesson" : " missing from its lesson"));
        }
        std::printf("textbook prefix: %u chars, extended tail: %u chars\n", core_end, src.charCount() - core_end);
    }

    // -- round-trip: every reading finds its own character -----------------
    for (const Reading& r : readings) {
        std::string digits;
        for (char c : r.syllable) {
            digits.push_back(pime::pyDigitOf(c));
        }
        const std::vector<std::string> interps = engineInterpsAll(engine, digits);
        const bool listed                      = std::find(interps.begin(), interps.end(), r.syllable) != interps.end();
        check(listed, "interpretations(" + digits + ") misses " + r.syllable);
        if (!listed) {
            continue;
        }
        const std::vector<uint16_t> ids = engineQueryAll(engine, r.syllable);
        check(std::find(ids.begin(), ids.end(), r.order) != ids.end(),
              "query(" + r.syllable + ") misses char order " + std::to_string(r.order));
    }

    // -- differential vs the reference on every reachable digit string -----
    std::set<std::string> probes;
    for (const auto& [syl, _] : ref.by_syllable) {
        std::string digits;
        for (char c : syl) {
            digits.push_back(pime::pyDigitOf(c));
        }
        for (size_t len = 1; len <= digits.size(); ++len) {
            probes.insert(digits.substr(0, len));
        }
    }
    // ... and every unreachable short one, which must return nothing.
    for (int a = 2; a <= 9; ++a) {
        for (int b = 1; b <= 9; ++b) {
            probes.insert(b == 1 ? std::string(1, char('0' + a)) : std::string{char('0' + a), char('0' + b)});
        }
    }
    size_t nonempty = 0;
    for (const std::string& digits : probes) {
        const std::vector<std::string> got  = engineInterpsAll(engine, digits);
        const std::vector<std::string> want = ref.interpretations(digits);
        check(got == want, "interpretations(" + digits + ") diverges (" + std::to_string(got.size()) + " vs " +
                               std::to_string(want.size()) + ")");
        check(want.size() <= pime::T9Engine::kMaxInterps, "interpretation count overflows kMaxInterps at " + digits);
        if (!got.empty()) {
            nonempty++;
        }
        for (const std::string& interp : got) {
            const std::vector<uint16_t> ids = engineQueryAll(engine, interp);
            check(!ids.empty(), "empty candidates for offered interpretation " + interp);
            check(ids == ref.query(interp), "query(" + interp + ") diverges from reference");
        }
    }
    std::printf("differential: %zu digit strings probed, %zu reachable\n", probes.size(), nonempty);

    // -- prefix monotonicity across the two layers -------------------------
    // Extending a digit string may only narrow the union of candidates.
    for (const std::string& digits : probes) {
        if (digits.size() < 2) {
            continue;
        }
        const std::string parent = digits.substr(0, digits.size() - 1);
        std::set<uint16_t> parent_union, child_union;
        for (const std::string& i : ref.interpretations(parent)) {
            for (uint16_t id : ref.query(i)) {
                parent_union.insert(id);
            }
        }
        for (const std::string& i : engineInterpsAll(engine, digits)) {
            for (uint16_t id : engineQueryAll(engine, i)) {
                child_union.insert(id);
            }
        }
        const bool subset =
            std::includes(parent_union.begin(), parent_union.end(), child_union.begin(), child_union.end());
        check(subset, "candidates grow when extending " + parent + " -> " + digits);
    }

    // -- paging: offset/cap slicing reassembles the full list --------------
    for (const std::string& interp : {std::string("shi"), std::string("y"), std::string("zhuang")}) {
        const std::vector<uint16_t> all = engineQueryAll(engine, interp);
        std::vector<uint16_t> paged;
        uint16_t page[5];
        for (uint16_t off = 0; off < all.size(); off += 5) {
            const uint16_t total = engine.query(interp.c_str(), page, 5, off);
            check(total == all.size(), "paged total drifts for " + interp);
            for (uint16_t i = 0; i < std::min<size_t>(5, all.size() - off); ++i) {
                paged.push_back(page[i]);
            }
        }
        check(paged == all, "paging reassembly diverges for " + interp);
    }

    // -- determinism: a second build answers identically -------------------
    {
        pime::T9Engine second;
        std::vector<pime::Entry> entries;
        for (const Reading& r : readings) {
            entries.push_back({r.syllable.c_str(), r.order});
        }
        check(second.build(entries.data(), entries.size()), "second engine build");
        for (const std::string& digits : {std::string("426"), std::string("7"), std::string("94264")}) {
            check(engineInterpsAll(engine, digits) == engineInterpsAll(second, digits),
                  "interpretations nondeterministic for " + digits);
        }
    }

    std::printf("checks=%d failures=%d RESULT: %s\n", g_checks, g_failures, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
