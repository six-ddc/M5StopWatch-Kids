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
#include <cmath>
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
    if (g_touch_frames.empty()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    // Holds the last frame once the script runs out, so a script ending in a
    // pressed frame keeps the finger down (for mid-press screenshots); every
    // script that wants a release ends with a released frame.
    const TouchFrame& f =
        g_touch_frames[g_touch_index < g_touch_frames.size() ? g_touch_index++ : g_touch_frames.size() - 1];
    data->point = f.point;
    data->state = f.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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

// Vertical drag on one wheel column: press, move in `steps` equal frames
// (one indev read each, 33 ms apart, so the picker sees a real velocity),
// optionally hold still for `hold` frames (a paused finger sheds its
// velocity -- that is how a precise one-detent drag differs from a fling),
// then release. `settle_frames` lets the snap animation finish -- pass a
// small number to leave it mid-flight for a screenshot.
void dragVertical(int x, int y0, int y1, int steps, int settle_frames = 40, int hold = 0)
{
    g_touch_frames.clear();
    g_touch_index = 0;
    for (int i = 0; i <= steps; i++) {
        const int y = y0 + (y1 - y0) * i / steps;
        g_touch_frames.push_back({{static_cast<int32_t>(x), static_cast<int32_t>(y)}, true});
    }
    for (int i = 0; i < hold; i++) {
        g_touch_frames.push_back({{static_cast<int32_t>(x), static_cast<int32_t>(y1)}, true});
    }
    g_touch_frames.push_back({{static_cast<int32_t>(x), static_cast<int32_t>(y1)}, false});
    pump(33, static_cast<int>(g_touch_frames.size()) + settle_frames);
}

// A light tap: three pressed frames on one point, then release.
void tapAt(int x, int y, int settle_frames = 30)
{
    g_touch_frames.clear();
    g_touch_index      = 0;
    const lv_point_t p = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
    for (int i = 0; i < 3; i++) {
        g_touch_frames.push_back({p, true});
    }
    g_touch_frames.push_back({p, false});
    pump(33, 4 + settle_frames);
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

    // --- Search page (three-wheel syllable picker) ---
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
    if (!search.create(screen, &src, &engine)) {
        std::fprintf(stderr, "SearchPage::create failed\n");
        return 1;
    }
    pump(33, 3);

    pime::PickerView& picker = search.picker();
    auto expectSyllable      = [&](const char* want, const char* what) {
        const char* got = picker.syllable();
        if (std::strcmp(got, want) != 0) {
            ++g_failures;
            std::printf("  %-22s syllable \"%s\", want \"%s\"\n", what, got, want);
        } else {
            std::printf("  %-22s ok (syllable \"%s\")\n", what, got);
        }
    };
    auto expectSettledAt = [&](int want_index, const char* what) {
        const float offset = picker.wheelOffset(2);
        const bool settled = picker.wheelSettled() && std::fabs(offset - std::round(offset)) < 0.001f;
        if (!settled || picker.candidateIndex() != want_index) {
            ++g_failures;
            std::printf("  %-22s offset %.3f index %u, want settled at %d\n", what, offset, picker.candidateIndex(),
                        want_index);
        } else {
            std::printf("  %-22s ok (settled at %d)\n", what, want_index);
        }
    };
    // Screen-absolute x of a wheel column.
    auto colX = [&](uint8_t wheel) { return 233 + picker.wheelX(wheel); };

    // Default state: no empty state on a wheel picker -- it opens dialled to
    // "hao" with the best-known character in the band.
    expectSyllable("hao", "default state");
    {
        uint16_t first = 0;
        engine.queryExact("hao", &first, 1, 0);
        std::printf("  default candidate       order %u reading %s\n", first, src.pinyinAt(first));
        if (picker.candidateIndex() != 0) {
            ++g_failures;
            std::printf("  default candidate       INDEX %u, want 0\n", picker.candidateIndex());
        }
    }
    checkedScreenshot(out_dir, "picker_default");

    // Unit-wheel linkage through the real indev: one detent down goes h->g,
    // and the suffix "ao" survives because g+ao is legal (iOS date-picker
    // semantics). The character wheel returns to the best-known first.
    dragVertical(colX(0), 260, 260 + 88, 12, 40, /*hold=*/10);
    expectSyllable("gao", "unit h->g keeps ao");
    expectSettledAt(0, "unit change resets");
    dragVertical(colX(0), 260 + 88, 260, 12, 40, /*hold=*/10);
    expectSyllable("hao", "unit g->h back");

    // Fling the character wheel: inertia must land on a whole detent.
    dragVertical(colX(2), 320, 160, 4);
    {
        const float offset = picker.wheelOffset(2);
        if (!picker.wheelSettled() || std::fabs(offset - std::round(offset)) > 0.001f || offset < 1.0f) {
            ++g_failures;
            std::printf("  fling snap              offset %.3f (want whole detent > 0)\n", offset);
        } else {
            std::printf("  fling snap              ok (landed on %d)\n", static_cast<int>(offset));
        }
    }

    // A light tap on the row below the band scrolls it into the band.
    {
        const int before = picker.candidateIndex();
        tapAt(colX(2), 233 + 85);
        expectSettledAt(before + 1, "tap row below");
    }

    // Physical keys: B one detent down, A back up; A at the top answers
    // with a bounce, not a move.
    {
        const int before = picker.candidateIndex();
        search.nextCandidatePage();
        pump(33, 30);
        expectSettledAt(before + 1, "key B steps down");
        for (int i = 0; i <= before + 1; i++) {
            search.previousCandidatePage();
            pump(33, 30);
        }
        expectSettledAt(0, "key A steps to top");
        search.previousCandidatePage();
        pump(33, 30);
        expectSettledAt(0, "key A at top bounces");
    }

    // Reading pass-through: tapping the band confirms the dialled character
    // and surfaces the toned reading it was picked under, which the learn
    // page then displays.
    {
        tapAt(colX(2), 233, 10);
        uint16_t order = 0;
        char reading[16];
        if (!search.takePick(order, reading, sizeof(reading))) {
            ++g_failures;
            std::printf("  band tap pick           NOT DELIVERED\n");
        } else {
            char plain[16];
            std::printf("  band tap pick           order %u reading %s\n", order, reading);
            if (pime::pyNormalize(reading, plain, sizeof(plain)) == 0 || std::strcmp(plain, picker.syllable()) != 0) {
                ++g_failures;
                std::printf("  picked reading          \"%s\" does not match syllable \"%s\"\n", reading,
                            picker.syllable());
            }
            search.setHidden(true);
            learn.setHidden(false);
            learn.showCharacter(order, reading);
            pump(33, 3);
            checkedScreenshot(out_dir, "picker_pick_learn");
            learn.setHidden(true);
            search.setHidden(false);
            pump(33, 2);
        }
    }

    // Bare-vowel syllable: the suffix wheel offers the quiet empty-suffix
    // mark, and the whole line still confirms to a real character.
    {
        if (!picker.selectSyllable("a")) {
            ++g_failures;
            std::printf("  bare vowel a            selectSyllable FAILED\n");
        }
        pump(33, 3);
        expectSyllable("a", "bare vowel a");
        uint16_t u = 0, s = 0;
        if (!engine.locate("a", u, s) || engine.suffixAt(u, s)[0] != '\0') {
            ++g_failures;
            std::printf("  bare vowel a            suffix not the empty item\n");
        }
        checkedScreenshot(out_dir, "picker_a");
        tapAt(colX(2), 233, 10);
        uint16_t order   = 0;
        char reading[16] = {};
        char plain[16];
        if (!search.takePick(order, reading, sizeof(reading)) ||
            pime::pyNormalize(reading, plain, sizeof(plain)) == 0 || std::strcmp(plain, "a") != 0) {
            ++g_failures;
            std::printf("  bare vowel pick         order %u reading \"%s\" (want an 'a' reading)\n", order, reading);
        } else {
            std::printf("  bare vowel pick         order %u reading %s\n", order, reading);
        }
    }

    // Longest suffix: x + iang. The wheels must keep every column inside
    // its measured budget (the off-panel check on the shot is the proof).
    picker.selectSyllable("xiang");
    pump(33, 3);
    expectSyllable("xiang", "long suffix xiang");
    checkedScreenshot(out_dir, "picker_xiang");

    // Suffix fallback: one detent down from x lands on w, which has no
    // "iang" -- the wheel snaps to the nearest suffix in letter order.
    dragVertical(colX(0), 260, 260 + 88, 12, 40, /*hold=*/10);
    expectSyllable("wo", "unit x->w nearest");

    // Big candidate list: shi. A hard fling coasts with inertia -- capture
    // it mid-flight, then let it settle on a detent.
    picker.selectSyllable("shi");
    pump(33, 3);
    {
        const uint16_t total = engine.queryExact("shi", nullptr, 0, 0);
        std::printf("  shi candidates          %u\n", total);
        if (total < 30) {
            ++g_failures;
            std::printf("  shi candidates          suspiciously few\n");
        }
        dragVertical(colX(2), 340, 130, 3, /*settle_frames=*/3, 0);
        checkedScreenshot(out_dir, "picker_shi_scroll");
        pump(33, 40);
        const float offset = picker.wheelOffset(2);
        if (!picker.wheelSettled() || std::fabs(offset - std::round(offset)) > 0.001f || offset < 2.0f) {
            ++g_failures;
            std::printf("  shi fling               offset %.3f (want whole detent, several rows in)\n", offset);
        } else {
            std::printf("  shi fling               ok (landed on %d of %u)\n", static_cast<int>(offset), total);
        }

        // Regression: stabbing a moving wheel is a catch, never a confirm.
        // Fling again, then tap the band while the snap animation is still
        // in flight -- the wheel must stop and settle, and no pick may fire.
        dragVertical(colX(2), 130, 340, 3, /*settle_frames=*/2);
        if (picker.wheelSettled()) {
            ++g_failures;
            std::printf("  catch setup             wheel already settled (fling too short)\n");
        }
        tapAt(colX(2), 233, 40);
        uint16_t order = 0;
        if (search.takePick(order)) {
            ++g_failures;
            std::printf("  catch tap               CONFIRMED A PICK (order %u), must only stop the wheel\n", order);
        } else {
            const float caught = picker.wheelOffset(2);
            if (!picker.wheelSettled() || std::fabs(caught - std::round(caught)) > 0.001f) {
                ++g_failures;
                std::printf("  catch tap               offset %.3f (want stopped on a whole detent)\n", caught);
            } else {
                std::printf("  catch tap               ok (no pick, stopped on %d)\n", static_cast<int>(caught));
            }
        }
    }

    // Degenerate wheel: a syllable with a single candidate must not coast,
    // must not rubber-hold, and shows no phantom fade rows.
    {
        char lonely[8] = {};
        for (uint16_t u = 0; u < engine.unitCount() && lonely[0] == '\0'; u++) {
            for (uint16_t s = 0; s < engine.suffixCount(u); s++) {
                char syl[8];
                std::snprintf(syl, sizeof(syl), "%s%s", engine.unitAt(u), engine.suffixAt(u, s));
                if (engine.queryExact(syl, nullptr, 0, 0) == 1) {
                    std::memcpy(lonely, syl, sizeof(lonely));
                    break;
                }
            }
        }
        if (lonely[0] == '\0') {
            std::printf("  degenerate wheel        (no single-candidate syllable in this data)\n");
        } else {
            picker.selectSyllable(lonely);
            pump(33, 3);
            std::printf("  degenerate syllable     %s\n", lonely);
            checkedScreenshot(out_dir, "picker_single");
            dragVertical(colX(2), 320, 160, 4);
            expectSettledAt(0, "degenerate no coast");
        }
    }

    // NVS-resume path: showCharacter dials the wheels onto that character's
    // syllable and its exact list position.
    {
        uint16_t fourth = 0;
        engine.queryExact("shi", &fourth, 1, 3);
        search.showCharacter(fourth);
        pump(33, 3);
        expectSyllable("shi", "resume syllable");
        expectSettledAt(3, "resume position");
        checkedScreenshot(out_dir, "picker_resume");
    }

    // --- Input-mode cycling (picker -> dial -> T9 -> picker) with state
    // --- carry, through LVGL's real gesture dispatch.
    {
        auto expectMode = [&](uint8_t want, const char* what) {
            if (search.mode() != want) {
                ++g_failures;
                std::printf("  %-22s mode %u, want %u\n", what, search.mode(), want);
            } else {
                std::printf("  %-22s ok (mode %u)\n", what, want);
            }
        };
        auto expectStr = [&](const char* got, const char* want, const char* what) {
            if (std::strcmp(got, want) != 0) {
                ++g_failures;
                std::printf("  %-22s \"%s\", want \"%s\"\n", what, got, want);
            } else {
                std::printf("  %-22s ok (\"%s\")\n", what, got);
            }
        };
        // All swipe scripts run at y=150: over the picker that is plain
        // wheel surface (a horizontal move releases the wheel), over the
        // dial it is inside the ring (r < 170, so no letter arms), and over
        // the keypad it is above the top key row (no key press suppresses
        // the gesture).
        auto swipeLeft = [&] {
            swipeHorizontal(360, 106, 150);
            pump(33, 15);
        };
        auto swipeRight = [&] {
            swipeHorizontal(106, 360, 150);
            pump(33, 15);
        };

        // While a wheel is being steered, a horizontal continuation of the
        // same press must not switch modes: scrollActive() suppresses it.
        picker.selectSyllable("xiang");
        pump(33, 3);
        // 24 px of vertical travel starts wheel steering (tap slop is 8) but
        // stays under LVGL's 50 px gesture limit, so the horizontal leg is
        // what fires the (suppressed) LEFT gesture.
        g_touch_frames.clear();
        g_touch_index = 0;
        for (int i = 0; i <= 2; i++) {
            g_touch_frames.push_back({{static_cast<int32_t>(colX(2)), static_cast<int32_t>(250 + i * 12)}, true});
        }
        for (int i = 1; i <= 6; i++) {
            g_touch_frames.push_back({{static_cast<int32_t>(colX(2) - i * 20), 274}, true});
        }
        g_touch_frames.push_back({{static_cast<int32_t>(colX(2) - 120), 274}, false});
        pump(33, static_cast<int>(g_touch_frames.size()) + 40);
        expectMode(0, "steer suppresses swipe");

        // Dial the state to carry: syllable "xiang", candidate 0.
        picker.selectSyllable("xiang");
        pump(33, 3);
        uint16_t xiang_first = 0;
        engine.queryExact("xiang", &xiang_first, 1, 0);

        swipeLeft();
        expectMode(1, "swipe -> dial");
        expectStr(search.dial().prefix(), "xiang", "dial carries prefix");
        if (search.dial().candidatePage() != 0) {
            ++g_failures;
            std::printf("  dial carries page       page %u, want 0 (holds order %u)\n", search.dial().candidatePage(),
                        xiang_first);
        }
        checkedScreenshot(out_dir, "mode_dial_xiang");

        swipeLeft();
        expectMode(2, "swipe -> T9");
        expectStr(search.keypad().digits(), "94264", "T9 carries digits");
        expectStr(search.keypad().interpretation(), "xiang", "T9 selects interp");
        checkedScreenshot(out_dir, "mode_t9_xiang");

        swipeLeft();
        expectMode(0, "swipe wraps to picker");
        expectSyllable("xiang", "picker restores");

        swipeRight();
        expectMode(2, "right swipe reverses");
        expectStr(search.keypad().digits(), "94264", "T9 restores digits");

        // A finger resting on a pad key owns the press: the gesture is
        // ignored, the keystroke stays a keystroke.
        g_touch_frames.clear();
        g_touch_index = 0;
        for (int i = 0; i <= 8; i++) {
            g_touch_frames.push_back({{static_cast<int32_t>(141 + i * 20), 321}, true});
        }
        g_touch_frames.push_back({{static_cast<int32_t>(141 + 8 * 20), 321}, false});
        pump(33, static_cast<int>(g_touch_frames.size()) + 20);
        expectMode(2, "key press suppresses");

        // Partial prefix carry: the dial types "xi" (unfinished on purpose),
        // T9 keeps it as the selected interpretation, and the picker
        // completes it to the first legal syllable in letter order ("xi"
        // itself is one).
        swipeRight();
        expectMode(1, "back to dial");
        search.dial().reset();
        search.dial().typeLetter('x');
        search.dial().typeLetter('i');
        pump(33, 3);
        checkedScreenshot(out_dir, "mode_dial_xi");
        swipeLeft();
        expectStr(search.keypad().digits(), "94", "T9 partial digits");
        expectStr(search.keypad().interpretation(), "xi", "T9 partial interp");
        checkedScreenshot(out_dir, "mode_t9_xi");
        swipeLeft();
        expectSyllable("xi", "picker completes xi");

        // Empty carry: the dial and keypad have an empty state, the wheels
        // do not -- an empty prefix lands the picker on its default.
        swipeLeft();
        expectMode(1, "picker -> dial again");
        search.dial().reset();
        pump(33, 3);
        checkedScreenshot(out_dir, "mode_dial_empty");
        swipeRight();
        expectMode(0, "empty carry to picker");
        expectSyllable("hao", "empty falls to default");

        // The dirty flag is a one-shot the app drains for its NVS write.
        uint8_t stored = 0;
        if (!search.takeModeDirty(stored) || stored != 0) {
            ++g_failures;
            std::printf("  mode dirty              not raised (or wrong mode %u)\n", stored);
        } else if (search.takeModeDirty(stored)) {
            ++g_failures;
            std::printf("  mode dirty              fired twice\n");
        } else {
            std::printf("  mode dirty              ok (one-shot, mode 0)\n");
        }

        // setMode restores the NVS value instantly, still carrying state,
        // and without re-raising the dirty flag.
        search.setMode(2);
        pump(33, 3);
        expectMode(2, "setMode restores T9");
        expectStr(search.keypad().interpretation(), "hao", "setMode carries state");
        if (search.takeModeDirty(stored)) {
            ++g_failures;
            std::printf("  setMode dirty           restore must not mark dirty\n");
        }
        search.setMode(0);
        pump(33, 3);
        expectSyllable("hao", "setMode back to picker");
    }

    if (g_failures == 0) {
        std::printf("RESULT: OK\n");
        return 0;
    }
    std::printf("RESULT: FAIL (%d screenshot(s) with off-panel pixels)\n", g_failures);
    return 1;
}
