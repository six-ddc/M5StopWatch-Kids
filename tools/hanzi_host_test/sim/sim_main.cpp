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

#include <apps/common/pinyin_ime/py_normalize.h>

#include "hz_data.h"
#include "view.h"

namespace {

constexpr int kScreen       = 466;
constexpr int kScreenRadius = 233;  // real visible area on the round panel

std::vector<uint16_t> g_framebuffer(static_cast<size_t>(kScreen) * kScreen);

int g_failures = 0;

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
            const int dx      = x - cx;
            const int dy      = y - cy;
            const bool inside = (dx * dx + dy * dy) <= (kScreenRadius * kScreenRadius);
            uint8_t r = 0, g = 0, b = 0;
            if (inside) {
                const uint16_t c = g_framebuffer[static_cast<size_t>(y) * kScreen + x];
                r                = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
                g                = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
                b                = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
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

struct OffPanel {
    int count        = 0;
    int worst_radius = 0;
    int x0 = kScreen, y0 = kScreen, x1 = -1, y1 = -1;
};

/// Lit pixels beyond the visible radius. Black is fine out there -- an unlit
/// AMOLED pixel is invisible either way -- but anything else is a widget the
/// glass cannot show. The bounding box is reported too: it is what tells you
/// *which* widget overflowed, since the screenshot is already circle-masked.
OffPanel findOffPanel()
{
    OffPanel out;
    const int cx = kScreen / 2;
    const int cy = kScreen / 2;
    for (int y = 0; y < kScreen; y++) {
        for (int x = 0; x < kScreen; x++) {
            if (g_framebuffer[static_cast<size_t>(y) * kScreen + x] == 0) {
                continue;
            }
            const int dx = x - cx;
            const int dy = y - cy;
            const int r2 = dx * dx + dy * dy;
            if (r2 <= kScreenRadius * kScreenRadius) {
                continue;
            }
            ++out.count;
            const int r = static_cast<int>(__builtin_sqrt(static_cast<double>(r2)));
            if (r > out.worst_radius) out.worst_radius = r;
            if (x < out.x0) out.x0 = x;
            if (x > out.x1) out.x1 = x;
            if (y < out.y0) out.y0 = y;
            if (y > out.y1) out.y1 = y;
        }
    }
    return out;
}

/// Bounding box of everything lit, as a sanity readout next to the screenshot.
void litBounds(int& x0, int& y0, int& x1, int& y1)
{
    x0 = kScreen;
    y0 = kScreen;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < kScreen; y++) {
        for (int x = 0; x < kScreen; x++) {
            if (g_framebuffer[static_cast<size_t>(y) * kScreen + x] == 0) {
                continue;
            }
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
}

/// Writes the screenshot and checks the same frame for lit pixels beyond the
/// round panel's visible radius, tallying failures in g_failures.
void checkedScreenshot(const std::string& out_dir, const std::string& name)
{
    writeScreenshot(out_dir + "/" + name + ".ppm");

    const OffPanel off = findOffPanel();
    int x0, y0, x1, y1;
    litBounds(x0, y0, x1, y1);

    std::printf("  %-22s lit box %3d..%3d x %3d..%3d", name.c_str(), x0, x1, y0, y1);
    if (off.count > 0) {
        ++g_failures;
        std::printf("   OFF-PANEL: %d px at %d..%d x %d..%d, out to r=%d\n", off.count, off.x0, off.x1, off.y0, off.y1,
                    off.worst_radius);
    } else {
        std::printf("   ok\n");
    }
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

// Scripted touch input, so the sim exercises LVGL's real press/gesture
// dispatch (bubble-flag walking included) instead of calling page methods.
// Each indev read consumes one frame; after the script ends the touch
// reports released.
struct TouchFrame {
    lv_point_t point;
    bool pressed;
};
std::vector<TouchFrame> g_touch_frames;
size_t g_touch_index = 0;

void touchReadCb(lv_indev_t* /*indev*/, lv_indev_data_t* data)
{
    if (g_touch_index < g_touch_frames.size()) {
        const TouchFrame& f = g_touch_frames[g_touch_index++];
        data->point         = f.point;
        data->state         = f.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Press at (x0,y), drag to (x1,y) in 20 px steps (well over the 3 px/read
// velocity floor and 50 px distance threshold), release, and let LVGL chew
// through it.
void swipeHorizontal(int x0, int x1, int y)
{
    g_touch_frames.clear();
    g_touch_index  = 0;
    const int step = x1 > x0 ? 20 : -20;
    for (int x = x0; (step > 0) ? (x < x1) : (x > x1); x += step) {
        g_touch_frames.push_back({{static_cast<int32_t>(x), static_cast<int32_t>(y)}, true});
    }
    g_touch_frames.push_back({{static_cast<int32_t>(x1), static_cast<int32_t>(y)}, false});
    pump(33, static_cast<int>(g_touch_frames.size()) + 5);
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
    if (!readFile(HANZI_BLOB_PATH, blob)) {
        std::fprintf(stderr, "cannot read %s -- run the pipeline first\n", HANZI_BLOB_PATH);
        return 2;
    }
    hz::DataSource src;
    if (!src.bind(blob.data(), blob.size())) {
        std::fprintf(stderr, "blob failed header validation\n");
        return 2;
    }
    std::printf("blob: %zu bytes, %u chars, %u lessons\n", blob.size(), src.charCount(), src.lessonCount());

    lv_init();

    lv_display_t* disp = lv_display_create(kScreen, kScreen);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, g_framebuffer.data(), nullptr, g_framebuffer.size() * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flushCb);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touchReadCb);

    // --- Browse page ---
    view::BrowsePage browse;
    bool to_search_fired = false;
    if (!browse.create(
            screen, &src, [](uint16_t order) { std::printf("[sim] browse cell selected -> order %u\n", order); },
            [&to_search_fired]() {
                to_search_fired = true;
                std::printf("[sim] browse -> search gesture fired\n");
            })) {
        std::fprintf(stderr, "BrowsePage::create failed\n");
        return 1;
    }
    browse.showLesson(lesson, 0);
    pump(33, 5);
    checkedScreenshot(out_dir, "browse");

    // A horizontal swipe through LVGL's real indev pipeline must reach the
    // page's gesture handler -- exactly what a direct method call cannot
    // cover (the GESTURE_BUBBLE flag walk dropped it once already).
    swipeHorizontal(360, 100, 233);
    if (!to_search_fired) {
        ++g_failures;
        std::printf("  browse swipe            GESTURE NOT DELIVERED\n");
    } else {
        std::printf("  browse swipe            ok\n");
    }
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
    checkedScreenshot(out_dir, "learn_00");

    // Advance the stroke-order animation and capture a handful of frames
    // spread across intro / reveal / land / pause so the sequence is visible
    // without dumping every single frame.
    constexpr int kTotalFrames = 240;  // ~7.9s of animation at 33ms/frame
    constexpr int kSampleEvery = 20;   // -> learn_01 .. learn_12
    int shot                   = 0;
    for (int frame = 1; frame <= kTotalFrames; frame++) {
        LvglLockGuard lock;  // update() requires the LVGL lock; a no-op on host
        learn.update(33);
        lv_tick_inc(33);
        lv_timer_handler();
        if (frame % kSampleEvery == 0) {
            shot++;
            char name[64];
            std::snprintf(name, sizeof(name), "learn_%02d", shot);
            checkedScreenshot(out_dir, name);
        }
    }

    // --- Search page (T9 pinyin lookup) ---
    learn.setHidden(true);

    // Build the engine exactly the way AppHanzi::buildSearchIndex does:
    // every reading of every character, normalised, keyed by teaching order.
    std::vector<std::string> texts;
    std::vector<uint16_t> ids;
    for (uint16_t order = 0; order < src.charCount(); ++order) {
        const char* p = src.pinyinAt(order);
        while (*p != '\0') {
            const char* start = p;
            while (*p != '\0' && *p != ' ') p++;
            std::string token(start, static_cast<size_t>(p - start));
            char plain[24];
            if (pime::pyNormalize(token.c_str(), plain, sizeof(plain)) > 0) {
                texts.emplace_back(plain);
                ids.push_back(order);
            }
            while (*p == ' ') p++;
        }
    }
    std::vector<pime::Entry> entries;
    for (size_t i = 0; i < texts.size(); ++i) {
        entries.push_back({texts[i].c_str(), ids[i]});
    }
    pime::T9Engine engine;
    if (!engine.build(entries.data(), entries.size())) {
        std::fprintf(stderr, "T9Engine::build failed\n");
        return 1;
    }

    view::SearchPage search;
    bool to_browse_fired = false;
    if (!search.create(screen, &src, &engine, [&to_browse_fired]() {
            to_browse_fired = true;
            std::printf("[sim] search -> browse gesture fired\n");
        })) {
        std::fprintf(stderr, "SearchPage::create failed\n");
        return 1;
    }
    pump(33, 3);
    checkedScreenshot(out_dir, "search_00");  // empty state

    swipeHorizontal(100, 360, 300);
    if (!to_browse_fired) {
        ++g_failures;
        std::printf("  search swipe            GESTURE NOT DELIVERED\n");
    } else {
        std::printf("  search swipe            ok\n");
    }

    auto pressDigits = [&](const char* digits) {
        for (const char* c = digits; *c != '\0'; c++) {
            search.ime().handleLetterKey(static_cast<uint8_t>(*c - '2'));
            pump(33, 2);
        }
    };

    // "hao" (426) collides with gao/gan/han/...: interpretation chips plus a
    // full candidate strip.
    pressDigits("426");
    checkedScreenshot(out_dir, "search_01");

    // Switch to the second interpretation.
    search.ime().handleInterpChip(1);
    pump(33, 2);
    checkedScreenshot(out_dir, "search_02");

    // Reading pass-through: the picked candidate must surface the toned
    // reading it was shown under (the "gao" interpretation is selected at
    // this point), which the learn page then displays instead of the
    // character's primary reading.
    {
        search.ime().handleCandidate(1);
        uint16_t order = 0;
        char reading[16];
        if (!search.takePick(order, reading, sizeof(reading))) {
            ++g_failures;
            std::printf("  candidate pick          NOT DELIVERED\n");
        } else {
            std::printf("  candidate pick          order %u reading %s\n", order, reading);
            if (reading[0] == '\0') {
                ++g_failures;
                std::printf("  picked reading          EMPTY\n");
            }
            search.setHidden(true);
            learn.setHidden(false);
            learn.showCharacter(order, reading);
            pump(33, 3);
            checkedScreenshot(out_dir, "search_pick_learn");
            learn.setHidden(true);
            search.setHidden(false);
            pump(33, 2);
        }
    }

    // A huge homophone set ("shi", 744) paged forward once.
    search.ime().handleDeleteLong();
    pump(33, 2);
    pressDigits("744");
    search.nextCandidatePage();
    pump(33, 2);
    checkedScreenshot(out_dir, "search_03");

    // Widest chip row, sampled from the real syllable set rather than made
    // up: the digit string whose interpretations are most numerous, with the
    // summed text length as tie-break.
    std::string widest;
    size_t widest_score = 0;
    for (const std::string& t : texts) {
        std::string digits;
        for (char c : t) digits.push_back(pime::pyDigitOf(c));
        for (size_t len = 1; len <= digits.size(); ++len) {
            const std::string prefix = digits.substr(0, len);
            const char* interps[pime::T9Engine::kMaxInterps];
            const uint16_t n = engine.interpretations(prefix.c_str(), interps, pime::T9Engine::kMaxInterps);
            size_t score     = static_cast<size_t>(n) * 100;
            for (uint16_t i = 0; i < n; ++i) score += std::strlen(interps[i]);
            if (score > widest_score) {
                widest_score = score;
                widest       = prefix;
            }
        }
    }
    std::printf("  widest interpretation row: digits %s\n", widest.c_str());
    search.ime().handleDeleteLong();
    pump(33, 2);
    pressDigits(widest.c_str());
    checkedScreenshot(out_dir, "search_04");

    // Page the interpretation window forward: the second window must lead
    // with a back-"..." chip.
    search.ime().handleInterpChip(3);
    pump(33, 2);
    checkedScreenshot(out_dir, "search_05");

    if (g_failures == 0) {
        std::printf("RESULT: OK\n");
        return 0;
    }
    std::printf("RESULT: FAIL (%d screenshot(s) with off-panel pixels)\n", g_failures);
    return 1;
}
