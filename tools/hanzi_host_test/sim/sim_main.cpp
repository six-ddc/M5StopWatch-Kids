/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Host LVGL UI simulator for AppHanzi.
//
// Builds the real view code (main/apps/app_hanzi/view/browse.cpp,
// main/apps/app_hanzi/view/learn.cpp) and engine code against a real LVGL 9.5
// running on a 466x466 offscreen RGB565 framebuffer, so layout and rendering
// bugs show up on a workstation instead of on the watch.
//
// Build: cmake -S tools/hanzi_host_test -B build_host && cmake --build build_host
// Run:   ./build_host/hanzi_sim [--char <order>] [--lesson <n>] [--out <dir>]
//                               [--root <hanzi_pipeline/.cache dir>]

#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "hz_data.h"
#include "view.h"

namespace {

constexpr int kScreen       = 466;
constexpr int kScreenRadius = 233;  // real visible area on the round panel

std::vector<uint16_t> g_framebuffer(static_cast<size_t>(kScreen) * kScreen);

void flushCb(lv_display_t* disp, const lv_area_t* /*area*/, uint8_t* /*px_map*/)
{
    // DIRECT render mode already renders straight into g_framebuffer; there is
    // no real transport to flush to.
    lv_display_flush_ready(disp);
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

// Writes a circle-masked PPM (P6): pixels outside the round panel's visible
// radius are forced black, matching what a user would actually see.
void writeScreenshot(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open %s for writing\n", path.c_str());
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", kScreen, kScreen);
    std::vector<uint8_t> row(static_cast<size_t>(kScreen) * 3);
    const int cx = kScreen / 2;
    const int cy = kScreen / 2;
    for (int y = 0; y < kScreen; y++) {
        for (int x = 0; x < kScreen; x++) {
            const int dx     = x - cx;
            const int dy     = y - cy;
            const bool inside = (dx * dx + dy * dy) <= (kScreenRadius * kScreenRadius);
            uint8_t r = 0, g = 0, b = 0;
            if (inside) {
                const uint16_t c = g_framebuffer[static_cast<size_t>(y) * kScreen + x];
                r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
                g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
                b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
            }
            row[static_cast<size_t>(x) * 3 + 0] = r;
            row[static_cast<size_t>(x) * 3 + 1] = g;
            row[static_cast<size_t>(x) * 3 + 2] = b;
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    std::printf("wrote %s\n", path.c_str());
}

// Advances LVGL's own clock and lets its refresh timer run. LV_TICK_CUSTOM is
// off in sim/lv_conf.h, so nothing moves the clock unless we do it here.
void pump(uint32_t ms, int iterations = 1)
{
    for (int i = 0; i < iterations; i++) {
        lv_tick_inc(ms);
        lv_timer_handler();
    }
}

}  // namespace

int main(int argc, char** argv)
{
    uint16_t char_order = 0;
    uint16_t lesson     = 0;
    std::string out_dir = ".";
    std::string root    = "tools/hanzi_pipeline/.cache";

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--char") == 0 && i + 1 < argc) {
            char_order = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--lesson") == 0 && i + 1 < argc) {
            lesson = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        }
    }
    std::filesystem::create_directories(out_dir);

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
    std::printf("blob: %zu bytes, %u chars, %u lessons\n", blob.size(), src.charCount(),
                src.lessonCount());

    lv_init();

    lv_display_t* disp = lv_display_create(kScreen, kScreen);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, g_framebuffer.data(), nullptr,
                           g_framebuffer.size() * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flushCb);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // --- Browse page ---
    view::BrowsePage browse;
    if (!browse.create(screen, &src, [](uint16_t order) {
            std::printf("[sim] browse cell selected -> order %u\n", order);
        })) {
        std::fprintf(stderr, "BrowsePage::create failed\n");
        return 1;
    }
    browse.showLesson(lesson, 0);
    pump(33, 5);
    writeScreenshot(out_dir + "/browse.ppm");
    browse.setHidden(true);

    // --- Learn page ---
    view::LearnPage learn;
    if (!learn.create(screen, &src)) {
        std::fprintf(stderr, "LearnPage::create failed\n");
        return 1;
    }
    if (!learn.showCharacter(char_order)) {
        std::fprintf(stderr, "LearnPage::showCharacter(%u) failed\n", char_order);
        return 1;
    }
    pump(33, 5);
    writeScreenshot(out_dir + "/learn_00.ppm");

    // Advance the stroke-order animation and capture a handful of frames
    // spread across intro / reveal / land / pause so the sequence is visible
    // without dumping every single frame.
    constexpr int kTotalFrames  = 240;  // ~7.9s of animation at 33ms/frame
    constexpr int kSampleEvery  = 20;   // -> learn_01 .. learn_12
    int shot = 0;
    for (int frame = 1; frame <= kTotalFrames; frame++) {
        LvglLockGuard lock;  // update() requires the LVGL lock; a no-op on host
        learn.update(33);
        lv_tick_inc(33);
        lv_timer_handler();
        if (frame % kSampleEvery == 0) {
            shot++;
            char name[64];
            std::snprintf(name, sizeof(name), "/learn_%02d.ppm", shot);
            writeScreenshot(out_dir + name);
        }
    }

    std::printf("RESULT: OK\n");
    return 0;
}
