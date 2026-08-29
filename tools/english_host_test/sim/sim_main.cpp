/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Host simulator for the English app's view layer.
 *
 * Compiles the real unit.cpp / card.cpp / quiz.cpp / result.cpp against a real
 * LVGL and an offscreen 466x466 framebuffer, feeding them the actual ENG1 blob
 * the pipeline produced -- real pictures, real words, real Chinese glosses.
 *
 * Two checks earn their keep here:
 *
 *   countOffPanel()  the panel is round with a visible radius of 233, so any
 *                    lit pixel beyond that is a widget hanging off the glass.
 *                    This is what pinned the picture size at 144: two 160 px
 *                    cards under the bezel buttons throw a corner to r=248.
 *
 *   ink coverage     an I4 palette or stride mistake does not crash, it draws
 *                    garbage or nothing. Asserting that each picture actually
 *                    lights a plausible fraction of its card catches a blank
 *                    or a smeared image, which a screenshot nobody opens will
 *                    not.
 *
 *   cmake -S tools/english_host_test -B build_english && cmake --build build_english
 *   ./build_english/english_sim --out /tmp/engshots
 */
#include <assets/assets.h>
#include <lvgl.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "eng_data.h"
#include "session.h"
#include "view.h"

namespace {

constexpr int kScreen       = 466;
constexpr int kScreenRadius = 233;  // real visible area on the round panel

std::vector<uint16_t> g_framebuffer(static_cast<size_t>(kScreen) * kScreen);

int g_failures = 0;

void flushCb(lv_display_t* disp, const lv_area_t* /*area*/, uint8_t* /*px_map*/)
{
    lv_display_flush_ready(disp);
}

void pump(uint32_t ms, int iterations = 1)
{
    for (int i = 0; i < iterations; i++) {
        lv_tick_inc(ms);
        lv_timer_handler();
    }
}

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
}

struct OffPanel {
    int count        = 0;
    int worst_radius = 0;
    int x0 = kScreen, y0 = kScreen, x1 = -1, y1 = -1;
};

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

/// Fraction of a rectangle that is not pure black, in percent. Used to prove a
/// picture actually drew: an I4 stride or palette mistake tends to yield 0%
/// (nothing) or ~100% (garbage), and neither looks like a pictogram.
int inkPercent(int cx, int cy, int w, int h)
{
    const int x0 = std::max(0, cx - w / 2);
    const int y0 = std::max(0, cy - h / 2);
    const int x1 = std::min(kScreen - 1, cx + w / 2);
    const int y1 = std::min(kScreen - 1, cy + h / 2);
    int lit      = 0;
    int total    = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            ++total;
            if (g_framebuffer[static_cast<size_t>(y) * kScreen + x] != 0) {
                ++lit;
            }
        }
    }
    return total == 0 ? 0 : (lit * 100) / total;
}

void capture(const std::string& out_dir, const std::string& name)
{
    pump(33, 3);
    writeScreenshot(out_dir + "/" + name + ".ppm");

    const OffPanel off = findOffPanel();
    int x0, y0, x1, y1;
    litBounds(x0, y0, x1, y1);

    std::printf("  %-26s lit box %3d..%3d x %3d..%3d", name.c_str(), x0, x1, y0, y1);
    if (off.count > 0) {
        ++g_failures;
        std::printf("   OFF-PANEL: %d px at %d..%d x %d..%d, out to r=%d\n", off.count, off.x0, off.x1, off.y0, off.y1,
                    off.worst_radius);
    } else {
        std::printf("   ok\n");
    }
}

/// Same as capture(), plus an assertion that both quiz cards drew a picture.
void captureQuiz(const std::string& out_dir, const std::string& name)
{
    capture(out_dir, name);
    // Card centres, mirroring the constants in view/quiz.cpp.
    const int cx    = kScreen / 2;
    const int cy    = kScreen / 2;
    const int left  = inkPercent(cx - 84, cy - 40, 144, 144);
    const int right = inkPercent(cx + 84, cy - 40, 144, 144);
    std::printf("      ink: left %d%%  right %d%%\n", left, right);
    if (left < 3 || left > 97 || right < 3 || right > 97) {
        ++g_failures;
        std::printf("      INK OUT OF RANGE -- picture blank or garbled\n");
    }
}

std::vector<uint8_t> readBlob()
{
    FILE* f = std::fopen(ENGLISH_BLOB_PATH, "rb");
    if (f == nullptr) {
        std::printf("cannot open %s -- run tools/english_pipeline/build_english_data.py\n", ENGLISH_BLOB_PATH);
        return {};
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> out(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    out.resize(got);
    return out;
}

/// The word whose text renders widest in the big face -- the one most likely
/// to run off the glass. Picking a long-looking word by hand is not the same
/// thing: what matters is measured width, not letter count.
uint16_t widestWord(const eng::Data& data, int32_t& width_out)
{
    uint16_t best      = 0;
    int32_t best_width = -1;
    for (uint16_t i = 0; i < data.wordCount(); ++i) {
        const eng::Word w = data.word(i);
        if (w.text == nullptr) {
            continue;
        }
        lv_point_t size;
        lv_text_get_size(&size, w.text, &lv_font_hanzi_pinyin_44, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > best_width) {
            best_width = size.x;
            best       = i;
        }
    }
    width_out = best_width;
    return best;
}

/// The word with the longest Chinese gloss, for the same reason.
uint16_t widestGloss(const eng::Data& data, int32_t& width_out)
{
    uint16_t best      = 0;
    int32_t best_width = -1;
    for (uint16_t i = 0; i < data.wordCount(); ++i) {
        const eng::Word w = data.word(i);
        if (w.zh == nullptr) {
            continue;
        }
        lv_point_t size;
        lv_text_get_size(&size, w.zh, &lv_font_hanzi_ui_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > best_width) {
            best_width = size.x;
            best       = i;
        }
    }
    width_out = best_width;
    return best;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string out_dir = ".";
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }
    std::filesystem::create_directories(out_dir);

    const std::vector<uint8_t> blob = readBlob();
    if (blob.empty()) {
        return 1;
    }
    eng::Data data;
    if (!data.load(blob.data(), static_cast<uint32_t>(blob.size()))) {
        std::printf("blob rejected by eng::Data::load\n");
        return 1;
    }
    std::printf("blob: %zu bytes, %u words, %u units, %ux%u art\n", blob.size(), data.wordCount(), data.unitCount(),
                data.imageW(), data.imageH());

    lv_init();

    lv_display_t* disp = lv_display_create(kScreen, kScreen);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, g_framebuffer.data(), nullptr, g_framebuffer.size() * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flushCb);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    int32_t word_w              = 0;
    int32_t gloss_w             = 0;
    const uint16_t widest_word  = widestWord(data, word_w);
    const uint16_t widest_gloss = widestGloss(data, gloss_w);
    std::printf("widest word  \"%s\" at %d px\n", data.word(widest_word).text, word_w);
    std::printf("widest gloss \"%s\" at %d px\n", data.word(widest_gloss).zh, gloss_w);

    std::printf("\n[units]\n");
    {
        eng_view::UnitPage units;
        if (!units.create(screen)) {
            std::printf("unit page failed to create\n");
            return 1;
        }

        uint8_t best[8] = {};
        units.show(data, 0, best);
        capture(out_dir, "units_fresh");

        // Every unit selected in turn, all three stars lit -- the widest the
        // centre line and the dot row ever get.
        for (uint8_t i = 0; i < 8; ++i) {
            best[i] = 3;
        }
        for (uint16_t u = 0; u < data.unitCount(); ++u) {
            units.show(data, u, best);
            char name[48];
            std::snprintf(name, sizeof(name), "units_sel_%u", u);
            capture(out_dir, name);
        }

        units.destroy();
    }

    std::printf("\n[card]\n");
    {
        eng_view::CardPage card;
        if (!card.create(screen)) {
            std::printf("card page failed to create\n");
            return 1;
        }

        card.show(data, 0, 0, 12);
        capture(out_dir, "card_first");
        // The picture has to be visibly there, not a blank tile.
        const int ink = inkPercent(kScreen / 2, kScreen / 2 - 55, 144, 144);
        std::printf("      ink: picture %d%%\n", ink);
        if (ink < 3 || ink > 97) {
            ++g_failures;
            std::printf("      INK OUT OF RANGE -- picture blank or garbled\n");
        }

        card.show(data, widest_word, 5, 12);
        capture(out_dir, "card_widest_word");

        card.show(data, widest_gloss, 11, 12);
        capture(out_dir, "card_widest_gloss");

        // One card per unit, kept as a screenshot: a spread across every
        // category is what tells you whether the artwork actually reads at
        // 144 px, which no aggregate statistic can.
        for (uint16_t u = 0; u < data.unitCount(); ++u) {
            const eng::Unit unit = data.unit(u);
            card.show(data, unit.first, 0, static_cast<uint8_t>(unit.count));
            char name[64];
            std::snprintf(name, sizeof(name), "unitcard_%02u_%s", u, data.word(unit.first).text);
            capture(out_dir, name);
        }

        // Walk every word once: this is the cheap way to catch a single bad
        // image blob among sixty.
        int worst_ink          = 100;
        int best_ink           = 0;
        const char* worst_name = "";
        for (uint16_t i = 0; i < data.wordCount(); ++i) {
            card.show(data, i, static_cast<uint8_t>(i % 12), 12);
            pump(33, 2);
            const int px = inkPercent(kScreen / 2, kScreen / 2 - 55, 144, 144);
            if (px < worst_ink) {
                worst_ink  = px;
                worst_name = data.word(i).text;
            }
            best_ink           = std::max(best_ink, px);
            const OffPanel off = findOffPanel();
            if (off.count > 0) {
                ++g_failures;
                std::printf("  word %-12s OFF-PANEL %d px to r=%d\n", data.word(i).text, off.count, off.worst_radius);
            }
        }
        std::printf("  all %u words: ink %d%%..%d%% (thinnest \"%s\")\n", data.wordCount(), worst_ink, best_ink,
                    worst_name);
        if (worst_ink < 3) {
            ++g_failures;
            std::printf("  A PICTURE IS BLANK\n");
        }

        card.destroy();
    }

    std::printf("\n[quiz]\n");
    {
        eng_view::QuizPage quiz;
        if (!quiz.create(screen)) {
            std::printf("quiz page failed to create\n");
            return 1;
        }

        eng::Question q;
        q.target      = 0;
        q.decoy       = 1;
        q.target_left = true;
        q.activity    = eng::Activity::ListenPick;
        quiz.show(data, q, 0, eng::kQuestionsPerRound, 0);
        captureQuiz(out_dir, "quiz_listen");

        quiz.showFeedback(data, q, true, true);
        captureQuiz(out_dir, "quiz_listen_correct");

        quiz.show(data, q, 3, eng::kQuestionsPerRound, 4);
        captureQuiz(out_dir, "quiz_listen_streak");

        quiz.showFeedback(data, q, false, false);
        captureQuiz(out_dir, "quiz_listen_wrong");

        // Reading question with the widest word the data actually contains.
        q.target      = widest_word;
        q.decoy       = (widest_word + 1) % data.wordCount();
        q.target_left = false;
        q.activity    = eng::Activity::ReadPick;
        quiz.show(data, q, 6, eng::kQuestionsPerRound, 0);
        captureQuiz(out_dir, "quiz_read_widest");

        quiz.showFeedback(data, q, false, true);
        captureQuiz(out_dir, "quiz_read_wrong");

        // Drive a whole round through the real session, so the pairs shown are
        // ones the game can actually produce.
        eng::Session s;
        s.start(data, 0, 20260821u);
        while (s.stage() == eng::Stage::Learn) {
            s.advanceLearn();
        }
        int n = 0;
        while (s.stage() == eng::Stage::Quiz) {
            const eng::Question rq = s.question();
            quiz.show(data, rq, s.index(), s.total(), s.stats().streak);
            pump(33, 2);
            const OffPanel off = findOffPanel();
            if (off.count > 0) {
                ++g_failures;
                std::printf("  round q%d OFF-PANEL %d px to r=%d\n", n, off.count, off.worst_radius);
            }
            s.submit(rq.target_left);
            s.advanceQuiz();
            ++n;
        }
        std::printf("  played a full %d-question round, all inside the glass\n", n);

        quiz.destroy();
    }

    std::printf("\n[result]\n");
    {
        eng_view::ResultPage result;
        if (!result.create(screen)) {
            std::printf("result page failed to create\n");
            return 1;
        }

        eng_view::Summary sum;
        sum.total = eng::kQuestionsPerRound;
        sum.unit  = data.unit(0).title;

        for (uint8_t stars = 0; stars <= 3; ++stars) {
            sum.stars   = stars;
            sum.correct = static_cast<uint8_t>(stars == 3 ? 10 : stars * 3);
            sum.verdict = stars == 3 ? "太棒了" : stars == 2 ? "真不错" : stars == 1 ? "继续加油" : "别灰心";
            sum.missed_count = 0;
            result.beginSummary(sum);
            result.finishSummary(sum);
            char name[48];
            std::snprintf(name, sizeof(name), "result_%u_star", stars);
            capture(out_dir, name);
        }

        // Worst case for the review line: three of the longest words.
        sum.stars        = 0;
        sum.correct      = 3;
        sum.verdict      = "别灰心";
        sum.missed_count = 3;
        sum.missed[0]    = data.word(widest_word).text;
        sum.missed[1]    = data.word((widest_word + 1) % data.wordCount()).text;
        sum.missed[2]    = data.word((widest_word + 2) % data.wordCount()).text;
        result.beginSummary(sum);
        capture(out_dir, "result_missed_widest");

        // Mid-celebration, one star landed.
        sum.stars        = 3;
        sum.correct      = 10;
        sum.verdict      = "太棒了";
        sum.missed_count = 0;
        result.beginSummary(sum);
        result.revealStar(0);
        capture(out_dir, "result_mid_celebration");

        result.destroy();
    }

    std::printf("\nfailures=%d\nRESULT: %s\n", g_failures, g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
