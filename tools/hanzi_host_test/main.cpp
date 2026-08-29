/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Host-side verification for the AppHanzi rendering engine (milestone M2).
//
// Decodes every character in the blob, rasterises it with the real engine code,
// and diffs against the pipeline's independently-produced golden renders. The
// point is to catch fill-rule, flattening and decode faults on a workstation
// instead of on the watch.
//
// Build:  cmake -S tools/hanzi_host_test -B build_host && cmake --build build_host
// Run:    ./build_host/hanzi_host_test [--dump <codepoint-hex>]

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hz_data.h"
#include "hz_raster.h"

namespace {

constexpr int kSize = 160;  // must match GOLDEN_SIZE in build_hanzi_data.py

// A stroke edge is never sharper than the pipeline's own flattening, so any
// residual difference should be a thin anti-aliasing seam. Anything past this
// is a real geometry or fill-rule fault.
constexpr int kHardDiff       = 100;
constexpr double kHardFraction = 0.005;  // 0.5% of the canvas

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

// Minimal binary PGM (P5) reader; the pipeline writes nothing fancier.
bool readPgm(const std::string& path, Image& img)
{
    std::vector<uint8_t> raw;
    if (!readFile(path, raw)) {
        return false;
    }
    size_t pos = 0;
    auto token = [&]() -> std::string {
        while (pos < raw.size() && (std::isspace(raw[pos]) || raw[pos] == '#')) {
            if (raw[pos] == '#') {
                while (pos < raw.size() && raw[pos] != '\n') pos++;
            } else {
                pos++;
            }
        }
        std::string t;
        while (pos < raw.size() && !std::isspace(raw[pos])) {
            t += static_cast<char>(raw[pos++]);
        }
        return t;
    };
    if (token() != "P5") {
        return false;
    }
    img.w             = std::atoi(token().c_str());
    img.h             = std::atoi(token().c_str());
    const int maxval  = std::atoi(token().c_str());
    if (img.w <= 0 || img.h <= 0 || maxval != 255) {
        return false;
    }
    pos++;  // single whitespace after the header
    const size_t need = static_cast<size_t>(img.w) * img.h;
    if (raw.size() - pos < need) {
        return false;
    }
    img.px.assign(raw.begin() + static_cast<long>(pos),
                  raw.begin() + static_cast<long>(pos + need));
    return true;
}

void writePgm(const std::string& path, const uint8_t* data, int w, int h)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "P5\n%d %d\n255\n", w, h);
    std::fwrite(data, 1, static_cast<size_t>(w) * h, f);
    std::fclose(f);
}

std::string utf8Of(uint32_t cp)
{
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

struct Report {
    uint32_t codepoint = 0;
    int max_diff       = 0;
    double mean_diff   = 0.0;
    int hard_pixels    = 0;
    bool decoded       = true;
};

}  // namespace

int main(int argc, char** argv)
{
    std::string root     = "tools/hanzi_pipeline/.cache";
    std::string dump_dir = ".";
    std::vector<uint32_t> dump_cps;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            // Comma-separated hex codepoints, e.g. --dump 6211,4E00,98CE
            const char* list = argv[++i];
            while (*list != '\0') {
                dump_cps.push_back(static_cast<uint32_t>(std::strtoul(list, nullptr, 16)));
                const char* comma = std::strchr(list, ',');
                list              = (comma == nullptr) ? "" : comma + 1;
            }
        } else if (std::strcmp(argv[i], "--dump-dir") == 0 && i + 1 < argc) {
            dump_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        }
    }

    std::vector<uint8_t> blob;
    if (!readFile(root + "/hanzi_data.bin", blob)) {
        std::fprintf(stderr, "cannot read %s/hanzi_data.bin -- run the pipeline first\n",
                     root.c_str());
        return 2;
    }

    hz::DataSource src;
    if (!src.bind(blob.data(), blob.size())) {
        std::fprintf(stderr, "blob failed header validation\n");
        return 2;
    }
    std::printf("blob: %zu bytes, %u chars, %u lessons, coord scale %u\n", blob.size(),
                src.charCount(), src.lessonCount(), src.coordScale());

    // Mirrors the on-device budget: one reusable arena, one scratch row.
    std::vector<uint8_t> arena_mem(16 * 1024);
    std::vector<float> scratch(static_cast<size_t>(kSize) + 4096);
    std::vector<uint8_t> canvas(static_cast<size_t>(kSize) * kSize);

    hz::Transform tf;
    tf.scale = static_cast<float>(kSize) / static_cast<float>(src.coordScale());

    std::vector<Report> reports;
    size_t missing_golden = 0;
    size_t max_arena      = 0;
    size_t max_outline    = 0;

    for (uint16_t order = 0; order < src.charCount(); order++) {
        hz::Arena arena(arena_mem.data(), arena_mem.size());
        hz::Character ch;
        Report rep;
        rep.codepoint = src.codepointAt(order);

        if (!src.decode(order, tf, ch, arena)) {
            rep.decoded = false;
            reports.push_back(rep);
            std::fprintf(stderr, "decode failed for U+%04X (%s)%s\n", rep.codepoint,
                         utf8Of(rep.codepoint).c_str(),
                         arena.overflowed() ? " [arena overflow]" : "");
            continue;
        }
        max_arena = std::max(max_arena, arena.used());

        std::fill(canvas.begin(), canvas.end(), 0);
        hz::Rasterizer raster(scratch.data(), scratch.size());
        for (uint16_t s = 0; s < ch.stroke_count; s++) {
            const hz::Stroke& st = ch.strokes[s];
            max_outline          = std::max<size_t>(max_outline, st.outline_count);
            hz::Mask mask;
            if (!hz::polygonBounds(st.outline, st.outline_count, kSize, kSize, mask.x0,
                                   mask.y0, mask.w, mask.h)) {
                continue;
            }
            // Render straight into the shared canvas by pointing the mask at the
            // right offset; stride keeps the rows aligned to the full canvas.
            mask.data   = canvas.data() + static_cast<size_t>(mask.y0) * kSize + mask.x0;
            mask.stride = kSize;
            if (!raster.fill(st.outline, st.outline_count, mask)) {
                std::fprintf(stderr, "raster scratch too small for U+%04X stroke %u\n",
                             rep.codepoint, s);
            }
        }

        char path[512];
        std::snprintf(path, sizeof(path), "%s/golden/%04X.pgm", root.c_str(),
                      rep.codepoint);
        Image golden;
        if (!readPgm(path, golden) || golden.w != kSize || golden.h != kSize) {
            missing_golden++;
            continue;
        }

        long total = 0;
        for (size_t i = 0; i < canvas.size(); i++) {
            const int d = std::abs(static_cast<int>(canvas[i]) - static_cast<int>(golden.px[i]));
            total += d;
            rep.max_diff = std::max(rep.max_diff, d);
            if (d >= kHardDiff) {
                rep.hard_pixels++;
            }
        }
        rep.mean_diff = static_cast<double>(total) / static_cast<double>(canvas.size());
        reports.push_back(rep);

        if (std::find(dump_cps.begin(), dump_cps.end(), rep.codepoint) != dump_cps.end()) {
            char out[512];
            std::snprintf(out, sizeof(out), "%s/engine_%04X.pgm", dump_dir.c_str(),
                          rep.codepoint);
            writePgm(out, canvas.data(), kSize, kSize);
            std::snprintf(out, sizeof(out), "%s/golden_%04X.pgm", dump_dir.c_str(),
                          rep.codepoint);
            writePgm(out, golden.px.data(), kSize, kSize);
            std::printf("dumped U+%04X (%s)\n", rep.codepoint,
                        utf8Of(rep.codepoint).c_str());
        }
    }

    size_t failed = 0, decode_failed = 0;
    double worst_mean = 0.0;
    for (const Report& r : reports) {
        if (!r.decoded) {
            decode_failed++;
            continue;
        }
        worst_mean = std::max(worst_mean, r.mean_diff);
        if (static_cast<double>(r.hard_pixels) >
            kHardFraction * static_cast<double>(kSize) * kSize) {
            failed++;
        }
    }

    std::vector<Report> ranked;
    for (const Report& r : reports) {
        if (r.decoded) {
            ranked.push_back(r);
        }
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const Report& a, const Report& b) { return a.hard_pixels > b.hard_pixels; });

    std::printf("\nworst 10 by pixels differing by >=%d:\n", kHardDiff);
    for (size_t i = 0; i < ranked.size() && i < 10; i++) {
        const Report& r = ranked[i];
        std::printf("  U+%04X %s  hard=%5d (%.3f%%)  max=%3d  mean=%.2f\n", r.codepoint,
                    utf8Of(r.codepoint).c_str(), r.hard_pixels,
                    100.0 * r.hard_pixels / (kSize * kSize), r.max_diff, r.mean_diff);
    }

    std::printf("\nchars=%u  decode_failed=%zu  missing_golden=%zu  over_threshold=%zu\n",
                src.charCount(), decode_failed, missing_golden, failed);
    std::printf("worst mean diff=%.3f  max arena=%zu B  max outline points=%zu\n",
                worst_mean, max_arena, max_outline);

    if (decode_failed != 0 || failed != 0) {
        std::printf("RESULT: FAIL\n");
        return 1;
    }
    std::printf("RESULT: PASS\n");
    return 0;
}
