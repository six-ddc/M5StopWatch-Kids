/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Host-side animation check for AppHanzi (milestone M3).
//
// Runs the real animator + compositor at the on-device canvas size and:
//   * verifies incremental drawing against a full repaint every frame, which is
//     the ghosting/dirty-rect check that a device soak would otherwise find;
//   * records dirty-rect sizes so the per-frame cost is known before flashing;
//   * optionally writes the frames as PPM for visual review.
//
// Run: ./anim_host_test --char 6211 --frames-dir out/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hz_anim.h"
#include "hz_compose.h"
#include "hz_data.h"
#include "hz_raster.h"

namespace {

constexpr int kCanvas = 320;  // matches the on-device tian-zi-ge canvas
constexpr uint32_t kFrameMs = 33;

uint16_t rgb565(int r, int g, int b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

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

void writePpm(const std::string& path, const uint16_t* rgb, int n)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", n, n);
    std::vector<uint8_t> row(static_cast<size_t>(n) * 3);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            const uint16_t c = rgb[static_cast<size_t>(y) * n + x];
            const int r = ((c >> 11) & 0x1F) * 255 / 31;
            const int g = ((c >> 5) & 0x3F) * 255 / 63;
            const int b = (c & 0x1F) * 255 / 31;
            row[static_cast<size_t>(x) * 3 + 0] = static_cast<uint8_t>(r);
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>(g);
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>(b);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv)
{
    std::string root       = "tools/hanzi_pipeline/.cache";
    std::string frames_dir;
    uint32_t want_cp = 0x6211;  // 我
    int max_frames   = 400;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--char") == 0 && i + 1 < argc) {
            want_cp = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 16));
        } else if (std::strcmp(argv[i], "--frames-dir") == 0 && i + 1 < argc) {
            frames_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        }
    }

    std::vector<uint8_t> blob;
    if (!readFile(root + "/hanzi_data.bin", blob)) {
        std::fprintf(stderr, "cannot read blob under %s\n", root.c_str());
        return 2;
    }
    hz::DataSource src;
    if (!src.bind(blob.data(), blob.size())) {
        std::fprintf(stderr, "blob failed validation\n");
        return 2;
    }

    const int32_t order = src.findByCodepoint(want_cp);
    if (order < 0) {
        std::fprintf(stderr, "U+%04X is not in the character set\n", want_cp);
        return 2;
    }

    std::vector<uint8_t> arena_mem(16 * 1024);
    hz::Arena arena(arena_mem.data(), arena_mem.size());
    hz::Character ch;

    // Centre the glyph in the cell with a small inset, same as the device will.
    const float inset = 0.90f;
    hz::Transform tf;
    tf.scale = (static_cast<float>(kCanvas) / static_cast<float>(src.coordScale())) * inset;
    tf.ox    = 0.5f * (kCanvas - src.coordScale() * tf.scale);
    tf.oy    = tf.ox;

    if (!src.decode(static_cast<uint16_t>(order), tf, ch, arena)) {
        std::fprintf(stderr, "decode failed\n");
        return 2;
    }
    std::printf("U+%04X pinyin=%s strokes=%u arena=%zu B\n", want_cp,
                src.pinyinAt(static_cast<uint16_t>(order)), ch.stroke_count, arena.used());

    const size_t px = static_cast<size_t>(kCanvas) * kCanvas;
    std::vector<uint16_t> canvas(px);
    std::vector<uint8_t> base(px), stroke(px), reveal(px);
    std::vector<uint16_t> reference(px);
    std::vector<float> scratch(static_cast<size_t>(kCanvas) + 4096);

    hz::Buffers bufs;
    bufs.canvas = canvas.data();
    bufs.base   = base.data();
    bufs.stroke = stroke.data();
    bufs.reveal = reveal.data();
    bufs.size   = kCanvas;

    hz::Palette pal;
    pal.paper = rgb565(250, 248, 242);
    pal.ink   = rgb565(30, 28, 26);
    pal.ghost = 36;
    pal.guide = 44;

    hz::Compositor comp;
    if (!comp.bind(bufs, pal)) {
        std::fprintf(stderr, "compositor bind failed\n");
        return 2;
    }
    hz::Rasterizer raster(scratch.data(), scratch.size());

    comp.resetBase(true);
    if (!comp.addGhost(ch, raster)) {
        std::fprintf(stderr, "ghost render failed\n");
        return 2;
    }
    comp.repaintAll();

    hz::AnimConfig cfg;
    hz::Animator anim;
    anim.begin(&ch, cfg);

    long dirty_px_total = 0;
    int dirty_frames    = 0;
    int worst_dirty     = 0;
    int mismatches      = 0;
    int frames_written  = 0;
    int loops           = 0;

    hz::Phase prev_phase = anim.phase();
    for (int frame = 0; frame < max_frames; frame++) {
        const bool changed = anim.tick(kFrameMs);

        if (prev_phase == hz::Phase::Done && anim.phase() == hz::Phase::Intro) {
            // Looped back to the start: rebuild the static layers exactly as
            // the device will on restart.
            comp.resetBase(true);
            comp.addGhost(ch, raster);
            comp.repaintAll();
            loops++;
        }
        prev_phase = anim.phase();

        if (anim.strokeJustStarted()) {
            if (!comp.beginStroke(ch, anim.strokeIndex(), raster)) {
                std::fprintf(stderr, "beginStroke failed at %u\n", anim.strokeIndex());
                return 2;
            }
        }
        if (changed && anim.phase() == hz::Phase::Reveal) {
            const hz::Rect d =
                comp.advance(ch.strokes[anim.strokeIndex()], anim.revealFrom(), anim.revealTo());
            if (d.valid()) {
                dirty_px_total += static_cast<long>(d.w) * d.h;
                worst_dirty = std::max<int>(worst_dirty, d.w * d.h);
                dirty_frames++;
            }
        }
        if (anim.strokeJustLanded()) {
            comp.land();
        }
        // Ghosting check: an incremental frame must be bit-identical to a full
        // repaint of the same state. heap_caps_malloc garbage on the device
        // would not show up here, but stale dirty rects would.
        std::memcpy(reference.data(), canvas.data(), px * sizeof(uint16_t));
        comp.repaintAll();
        if (std::memcmp(reference.data(), canvas.data(), px * sizeof(uint16_t)) != 0) {
            size_t bad = 0;
            for (size_t i = 0; i < px; i++) {
                if (reference[i] != canvas[i]) {
                    bad++;
                }
            }
            if (mismatches < 5) {
                std::fprintf(stderr,
                             "frame %d: incremental != full repaint (%zu px, phase %d)\n",
                             frame, bad, static_cast<int>(anim.phase()));
            }
            mismatches++;
        }

        if (!frames_dir.empty() && loops == 0) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/frame_%04d.ppm", frames_dir.c_str(), frame);
            writePpm(path, canvas.data(), kCanvas);
            frames_written++;
        }
        if (loops >= 1) {
            break;  // one full pass is enough
        }
    }

    std::printf("frames=%d dirty_frames=%d avg_dirty=%.0f px worst_dirty=%d px\n", max_frames,
                dirty_frames, dirty_frames ? static_cast<double>(dirty_px_total) / dirty_frames : 0.0,
                worst_dirty);
    std::printf("full-canvas = %d px, so the average frame touches %.1f%% of it\n",
                static_cast<int>(px),
                dirty_frames ? 100.0 * (static_cast<double>(dirty_px_total) / dirty_frames) / px : 0.0);
    if (frames_written > 0) {
        std::printf("wrote %d frames to %s\n", frames_written, frames_dir.c_str());
    }

    if (mismatches != 0) {
        std::printf("RESULT: FAIL (%d frames differed from a full repaint)\n", mismatches);
        return 1;
    }
    std::printf("RESULT: PASS (incremental drawing matches full repaint exactly)\n");
    return 0;
}
