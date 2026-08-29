/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Host simulator for AppMath's view layer.
 *
 * Compiles the real quiz.cpp / result.cpp against a real LVGL and an offscreen
 * 466x466 framebuffer, then renders the worst cases the layout has to survive:
 * the widest possible equation, the longest status line, the longest tier name.
 *
 * The check that actually earns its keep is countOutsideCircle(): the panel is
 * round with a visible radius of 233, so any lit pixel beyond that is a widget
 * hanging off the edge of the glass. On the bench that is easy to miss; here it
 * is a non-zero exit code.
 *
 *   cmake -S tools/math_host_test -B build_math && cmake --build build_math
 *   ./build_math/math_sim --out /tmp/mathshots
 */
#include <assets/assets.h>
#include <lvgl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "view.h"

namespace {

constexpr int kScreen       = 466;
constexpr int kScreenRadius = 233;  // real visible area on the round panel

std::vector<uint16_t> g_framebuffer(static_cast<size_t>(kScreen) * kScreen);

int g_failures = 0;

void flushCb(lv_display_t* disp, const lv_area_t* /*area*/, uint8_t* /*px_map*/)
{
    // DIRECT render mode already renders straight into g_framebuffer.
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
}

struct OffPanel {
    int count         = 0;
    int worst_radius  = 0;
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

void capture(const std::string& out_dir, const std::string& name)
{
    pump(33, 3);
    writeScreenshot(out_dir + "/" + name + ".ppm");

    const OffPanel off = findOffPanel();
    int x0, y0, x1, y1;
    litBounds(x0, y0, x1, y1);

    std::printf("  %-22s lit box %3d..%3d x %3d..%3d", name.c_str(), x0, x1, y0, y1);
    if (off.count > 0) {
        ++g_failures;
        std::printf("   OFF-PANEL: %d px at %d..%d x %d..%d, out to r=%d\n", off.count, off.x0,
                    off.x1, off.y0, off.y1, off.worst_radius);
    } else {
        std::printf("   ok\n");
    }
}

math::Problem makeProblem(uint8_t lhs, math::Op op, uint8_t rhs, uint8_t answer,
                          uint8_t distractor, bool answer_on_left)
{
    math::Problem p;
    p.lhs            = lhs;
    p.rhs            = rhs;
    p.op             = op;
    p.answer         = answer;
    p.distractor     = distractor;
    p.answer_on_left = answer_on_left;
    return p;
}

void equationText(const math::Problem& p, char* buf, size_t n)
{
    std::snprintf(buf, n, "%u %c %u", p.lhs, p.op == math::Op::Add ? '+' : '-', p.rhs);
}

int32_t textWidth(const char* text, const lv_font_t* font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/// Widest equation the generator can actually produce, found by sampling every
/// tier and measuring each string.
///
/// Hand-picking a wide-looking equation does not work: "88 + 99" renders very
/// wide but sums are capped at 99, so the app can never show it. Testing a
/// string that cannot occur is worse than not testing -- it reports a failure
/// that is not real, or passes on a case that is not the true worst one.
math::Problem findWidestEquation(int32_t& width_out, const char*& tier_out)
{
    math::Problem best{};
    int32_t best_width = -1;
    const char* best_tier = "";
    char buf[64];

    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        const auto level = static_cast<math::Level>(tier);
        math::Generator gen(0x5EED0000u + tier);
        for (int i = 0; i < 200000; ++i) {
            const math::Problem p = gen.next(level);
            equationText(p, buf, sizeof(buf));
            const int32_t w = textWidth(buf, &lv_font_digit_96);
            if (w > best_width) {
                best_width = w;
                best        = p;
                best_tier   = math::levelName(level);
            }
        }
    }
    width_out = best_width;
    tier_out  = best_tier;
    return best;
}

/// Widest *completed* equation a judge question can show, at the 64 px face it
/// renders in. The shown value can be the answer or the decoy, so both are
/// measured -- the decoy can be wider than the truth ("50 + 40 = 100" cannot
/// happen, but a two-digit decoy against a one-digit answer can).
math::Problem findWidestJudge(int32_t& width_out, const char*& tier_out, bool& shown_true_out)
{
    math::Problem best{};
    int32_t best_width = -1;
    const char* best_tier = "";
    bool best_shown_true  = true;
    char buf[64];

    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        const auto level = static_cast<math::Level>(tier);
        math::Generator gen(0x1D6E0000u + tier);
        for (int i = 0; i < 200000; ++i) {
            math::Problem p = gen.next(level);
            p.kind          = math::Kind::Judge;
            for (bool truth : {true, false}) {
                p.answer_on_left = truth;
                std::snprintf(buf, sizeof(buf), "%u %c %u = %u", p.lhs,
                              p.op == math::Op::Add ? '+' : '-', p.rhs, p.shownValue());
                const int32_t w = textWidth(buf, &lv_font_digit_64);
                if (w > best_width) {
                    best_width      = w;
                    best            = p;
                    best_tier       = math::levelName(level);
                    best_shown_true = truth;
                }
            }
        }
    }
    width_out      = best_width;
    tier_out       = best_tier;
    shown_true_out = best_shown_true;
    best.answer_on_left = best_shown_true;
    return best;
}

/// Widest value that can land on an answer card, over the same sampling.
int32_t findWidestCardValue(uint8_t& value_out)
{
    int32_t best = -1;
    value_out    = 0;
    char buf[8];
    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        math::Generator gen(0xCA5D0000u + tier);
        for (int i = 0; i < 200000; ++i) {
            const math::Problem p = gen.next(static_cast<math::Level>(tier));
            for (uint8_t v : {p.answer, p.distractor}) {
                std::snprintf(buf, sizeof(buf), "%u", v);
                const int32_t w = textWidth(buf, &lv_font_digit_64);
                if (w > best) {
                    best      = w;
                    value_out = v;
                }
            }
        }
    }
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

    std::printf("[quiz]\n");
    {
        view::QuizPage quiz;
        if (!quiz.create(screen)) {
            std::fprintf(stderr, "quiz page failed to create\n");
            return 2;
        }

        // Ordinary case: the tier children actually struggle with.
        quiz.showProblem(makeProblem(8, math::Op::Add, 7, 15, 5, true), 4, 10, 3, false, false);
        capture(out_dir, "quiz_typical");

        // Widest case the generator can actually reach, measured rather than
        // guessed, shown with the longest status line on top of it.
        int32_t eq_width = 0;
        const char* eq_tier = "";
        const math::Problem widest = findWidestEquation(eq_width, eq_tier);
        char eq[64];
        equationText(widest, eq, sizeof(eq));

        uint8_t widest_value = 0;
        const int32_t value_width = findWidestCardValue(widest_value);

        // Cards are 150 px wide with a 3 px border, so 144 px of usable inside.
        constexpr int32_t kCardInnerW = 144;
        std::printf("  widest equation:   \"%s\" = %-3u %3d px  (from %s)\n", eq, widest.answer,
                    eq_width, eq_tier);
        std::printf("  widest card value: %-11u %3d px  (card inner width %d px)\n",
                    widest_value, value_width, kCardInnerW);
        if (value_width > kCardInnerW) {
            ++g_failures;
            std::printf("  CARD OVERFLOW: %u needs %d px\n", widest_value, value_width);
        }

        // What the equation would cost if feedback appended the answer to it.
        // Measured rather than reasoned about, because the reasoning is what
        // got this wrong the first time.
        char completed[64];
        std::snprintf(completed, sizeof(completed), "%s = %u", eq, widest.answer);
        std::printf("  if completed:      \"%s\"   %3d px  (not done -- see the app README)\n",
                    completed, textWidth(completed, &lv_font_digit_96));

        quiz.showProblem(widest, 10, 10, 10, false, false);
        capture(out_dir, "quiz_widest");

        quiz.showFeedback(false, true, 0, false);
        capture(out_dir, "quiz_widest_feedback");

        // Correct-answer feedback with a milestone pulse mid-flight.
        quiz.showProblem(makeProblem(46, math::Op::Sub, 28, 18, 28, true), 7, 10, 4, false,
                         false);
        quiz.showFeedback(true, true, 5, true);
        capture(out_dir, "quiz_correct_milestone");

        // Replay phase, which uses a different status line.
        quiz.showProblem(makeProblem(30, math::Op::Add, 40, 70, 60, false), 10, 10, 0, true,
                         false);
        capture(out_dir, "quiz_retry");

        // Gold question status line.
        quiz.showProblem(makeProblem(58, math::Op::Add, 27, 85, 75, true), 10, 10, 9, false,
                         true);
        capture(out_dir, "quiz_gold");

        // Judge question: the completed equation drops to the 64 px face; the
        // widest reachable case is measured, not guessed. The equation sits at
        // kEquationY=30 with the 64 px line spanning roughly y -2..62, where
        // the circle allows ~447 px -- asserted through the off-panel check.
        int32_t judge_width = 0;
        const char* judge_tier = "";
        bool judge_truth = true;
        const math::Problem judge = findWidestJudge(judge_width, judge_tier, judge_truth);
        std::printf("  widest judge:      \"%u %c %u = %u\"  %3d px  (from %s, %s)\n", judge.lhs,
                    judge.op == math::Op::Add ? '+' : '-', judge.rhs, judge.shownValue(),
                    judge_width, judge_tier, judge_truth ? "true" : "decoy");

        quiz.showProblem(judge, 7, 10, 2, false, false);
        capture(out_dir, "quiz_judge_widest");

        // Judge feedback rewrites to the true equation, which can be a
        // different (wider or narrower) string; render that too.
        quiz.showFeedback(false, true, 0, false);
        capture(out_dir, "quiz_judge_feedback");

        quiz.destroy();
    }

    std::printf("[result]\n");
    {
        view::ResultPage result;
        if (!result.create(screen)) {
            std::fprintf(stderr, "result page failed to create\n");
            return 2;
        }

        view::Summary s;
        s.correct       = 10;
        s.total         = 10;
        s.stars         = 3;
        s.verdict       = "太棒了";
        s.level         = math::Level::AddWithin10;
        s.wallet_before = math::Wallet{8, 9};

        // Freshly begun: stars unlit, wallet at its pre-round value.
        result.beginSummary(s);
        capture(out_dir, "result_begin");

        // Mid-celebration: two stars landed, dust mid-pour, star pulsing.
        result.revealStar(0);
        result.revealStar(1);
        result.showWallet(11, 0);
        result.pulseWalletStar();
        capture(out_dir, "result_mid_celebration");

        // Finished, with the widest wallet the display can meet for years:
        // five digits of stars, nine dust.
        math::Wallet fat;
        fat.stars = 65535;
        fat.dust  = 9;
        s.level   = math::Level::TwoDigitFull;  // longest kid name in the tier line
        s.verdict = "别灰心";
        s.correct = 0;
        s.stars   = 0;
        result.finishSummary(s, fat);
        capture(out_dir, "result_widest_wallet");

        // Unlock overlay on top of everything.
        result.showUnlock(math::Mode::Judge);
        capture(out_dir, "result_unlock");
        result.hideUnlock();

        result.destroy();
    }

    std::printf("[map]\n");
    {
        view::MapPage map;
        if (!map.create(screen)) {
            std::fprintf(stderr, "map page failed to create\n");
            return 2;
        }

        // Fresh start: only the first tier open, nothing earned.
        view::MapInfo info;
        info.selected     = 0;
        info.max_unlocked = 0;
        map.show(info);
        capture(out_dir, "map_fresh");

        // Everything open, every best-star count lit, the fattest wallet, and
        // the longest tier names selected in turn.
        info.max_unlocked = math::kLevelCount - 1;
        info.wallet.stars = 65535;
        info.wallet.dust  = 9;
        for (uint8_t i = 0; i < math::kLevelCount; ++i) {
            info.best_stars[i] = 3;
        }
        for (uint8_t i = 0; i < math::kLevelCount; ++i) {
            info.selected = i;
            map.show(info);
            char name[32];
            std::snprintf(name, sizeof(name), "map_tier_%u", i);
            capture(out_dir, name);
        }

        map.destroy();
    }

    std::printf("\nfailures=%d\nRESULT: %s\n", g_failures, g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
