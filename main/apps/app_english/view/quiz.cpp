/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <assets/assets.h>
#include <mooncake_log.h>
#include <cstdint>
#include <cstdio>
#include "view.h"

using namespace eng_view;

namespace {

constexpr const char* kTag = "EngQuizPage";

// The two pictures go under the two bezel buttons (roughly 10 and 2 o'clock)
// so that "press the left ear" and "pick the left picture" are one gesture.
//
// 152 px cards centred at (+/-84, -40) put their outer corners 198 px from
// centre against the 233 px radius, with 16 px of black between them. This is
// the layout that fixed the 144 px picture size, not the other way round --
// see engformat.py. tools/english_host_test/sim asserts nothing lit escapes.
constexpr int16_t kArcSize  = 440;
constexpr int16_t kArcWidth = 10;
constexpr int16_t kCardSize = 152;
constexpr int16_t kCardX    = 84;
constexpr int16_t kCardY    = -40;
constexpr int16_t kStatusY  = -155;
constexpr int16_t kPromptY  = 75;
constexpr int16_t kHintY    = 140;

void rootClickedCb(lv_event_t* e)
{
    auto* page = static_cast<QuizPage*>(lv_event_get_user_data(e));
    if (page != nullptr) {
        page->handleRootClicked();
    }
}

void cardClickedCb(lv_event_t* e)
{
    auto* page = static_cast<QuizPage*>(lv_event_get_user_data(e));
    auto* obj  = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (page == nullptr || obj == nullptr) {
        return;
    }
    const auto index = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj)));
    page->handleCardClicked(index == 0);
}

}  // namespace

QuizPage::~QuizPage()
{
    destroy();
}

bool QuizPage::create(lv_obj_t* parent)
{
    if (parent == nullptr) {
        mclog::tagError(kTag, "no parent");
        return false;
    }

    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    // Everything that is not a card replays the word. On a listening question
    // the recording *is* the question, so a child who missed it had no way
    // back to it at all: the two cards answer, and there was nothing else to
    // touch. The cards are created after this and sit on top, so a tap on a
    // picture is still an answer and never a replay.
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, rootClickedCb, LV_EVENT_CLICKED, this);

    _arc = lv_arc_create(_root);
    lv_obj_set_size(_arc, kArcSize, kArcSize);
    lv_obj_center(_arc);
    lv_obj_remove_style(_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_rotation(_arc, 270);
    lv_arc_set_bg_angles(_arc, 0, 360);
    lv_arc_set_range(_arc, 0, 100);
    lv_arc_set_value(_arc, 0);
    lv_obj_set_style_arc_width(_arc, kArcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_arc, kArcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(kTrackColor), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(kAccent), LV_PART_INDICATOR);

    _status = lv_label_create(_root);
    lv_obj_set_style_text_font(_status, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_status, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_status, "");
    lv_obj_align(_status, LV_ALIGN_CENTER, 0, kStatusY);

    // ListenPick puts the Chinese instruction here; ReadPick replaces it with
    // the English word in the big face. One label serves both, restyled.
    _prompt = lv_label_create(_root);
    lv_obj_set_style_text_font(_prompt, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_prompt, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_prompt, "");
    lv_obj_align(_prompt, LV_ALIGN_CENTER, 0, kPromptY);

    // Which ear does what. Obvious after the first round, but the first round
    // is the one that decides whether a four-year-old keeps playing.
    _hint = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_hint, "选一选");
    lv_obj_align(_hint, LV_ALIGN_CENTER, 0, kHintY);
    // "遍" is not in the generated font subset, so the replay line is phrased
    // with characters the charset already carries. Check
    // main/assets/fonts/charset_hanzi_ui.txt before writing new UI text --
    // a missing glyph renders as a silent box.

    for (uint8_t i = 0; i < 2; ++i) {
        _cards[i] = lv_obj_create(_root);
        lv_obj_remove_style_all(_cards[i]);
        lv_obj_set_size(_cards[i], kCardSize, kCardSize);
        lv_obj_align(_cards[i], LV_ALIGN_CENTER, i == 0 ? -kCardX : kCardX, kCardY);
        lv_obj_clear_flag(_cards[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_cards[i], 18, 0);
        lv_obj_set_style_bg_opa(_cards[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(_cards[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(_cards[i], cardClickedCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(_cards[i], reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        _images[i] = lv_image_create(_cards[i]);
        lv_obj_center(_images[i]);
        // The picture must not eat the tap -- the card is the target.
        lv_obj_clear_flag(_images[i], LV_OBJ_FLAG_CLICKABLE);
    }
    resetCardStyles();

    return true;
}

void QuizPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _arc    = nullptr;
    _status = nullptr;
    _prompt = nullptr;
    _hint   = nullptr;
    for (uint8_t i = 0; i < 2; ++i) {
        _cards[i]  = nullptr;
        _images[i] = nullptr;
    }
}

void QuizPage::setHidden(bool hidden)
{
    if (_root == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void QuizPage::resetCardStyles()
{
    for (uint8_t i = 0; i < 2; ++i) {
        if (_cards[i] == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_color(_cards[i], lv_color_hex(kCardBg), 0);
        lv_obj_set_style_border_width(_cards[i], 3, 0);
        lv_obj_set_style_border_color(_cards[i], lv_color_hex(kCardBorder), 0);
    }
}

void QuizPage::show(const eng::Data& data, const eng::Question& question, uint8_t index, uint8_t total, uint8_t streak)
{
    if (_root == nullptr) {
        return;
    }

    const uint16_t left     = question.target_left ? question.target : question.decoy;
    const uint16_t right    = question.target_left ? question.decoy : question.target;
    const uint16_t words[2] = {left, right};

    for (uint8_t i = 0; i < 2; ++i) {
        eng::Image img;
        if (data.image(words[i], img)) {
            // Same descriptor every question, so the previous decode has to be
            // dropped or the device (1 MB image cache) redraws the last pair.
            lv_image_cache_drop(&_dsc[i]);
            fillImageDsc(_dsc[i], img);
            lv_image_set_src(_images[i], &_dsc[i]);
            lv_obj_clear_flag(_images[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_images[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_center(_images[i]);
    }

    char buf[32];
    if (streak >= 2) {
        std::snprintf(buf, sizeof(buf), "%u/%u · 连对 %u", index + 1u, total, streak);
    } else {
        std::snprintf(buf, sizeof(buf), "%u/%u", index + 1u, total);
    }
    lv_label_set_text(_status, buf);
    lv_obj_align(_status, LV_ALIGN_CENTER, 0, kStatusY);

    _listen = question.activity == eng::Activity::ListenPick;
    if (_listen) {
        // Nothing written: the question is the sound. A child who cannot read
        // a word yet can still answer this one.
        lv_obj_set_style_text_font(_prompt, &lv_font_hanzi_ui_24, 0);
        lv_obj_set_style_text_color(_prompt, lv_color_hex(kAccent), 0);
        lv_label_set_text(_prompt, "听一听");
        // Say where the replay is, on the page that has one.
        lv_label_set_text(_hint, "点一下再听");
    } else {
        const eng::Word w = data.word(question.target);
        lv_obj_set_style_text_font(_prompt, &lv_font_hanzi_pinyin_44, 0);
        lv_obj_set_style_text_color(_prompt, lv_color_hex(kInk), 0);
        lv_label_set_text(_prompt, w.text != nullptr ? w.text : "");
        lv_label_set_text(_hint, "选一选");
    }
    lv_obj_align(_prompt, LV_ALIGN_CENTER, 0, kPromptY);
    lv_obj_align(_hint, LV_ALIGN_CENTER, 0, kHintY);
    _replay_pending = false;

    resetCardStyles();
}

void QuizPage::showFeedback(const eng::Data& data, const eng::Question& question, bool correct, bool picked_left)
{
    if (_root == nullptr) {
        return;
    }

    const uint8_t picked        = picked_left ? 0 : 1;
    const uint8_t correct_index = question.target_left ? 0 : 1;

    lv_obj_set_style_bg_color(_cards[picked], lv_color_hex(correct ? kRightBg : kWrongBg), 0);
    lv_obj_set_style_border_width(_cards[picked], 5, 0);
    lv_obj_set_style_border_color(_cards[picked], lv_color_hex(correct ? kRightBorder : kWrongBorder), 0);

    if (!correct) {
        // Show which one it was. The pause is there to be read.
        lv_obj_set_style_bg_color(_cards[correct_index], lv_color_hex(kRightBg), 0);
        lv_obj_set_style_border_width(_cards[correct_index], 5, 0);
        lv_obj_set_style_border_color(_cards[correct_index], lv_color_hex(kRightBorder), 0);
    }

    // Whatever was asked, the answer ends up spelled out: on a ListenPick this
    // is the first time the written form appears, which is how the shape gets
    // attached to a sound the child has just proved they know.
    const eng::Word w = data.word(question.target);
    lv_obj_set_style_text_font(_prompt, &lv_font_hanzi_pinyin_44, 0);
    lv_obj_set_style_text_color(_prompt, lv_color_hex(correct ? kRightBorder : kInk), 0);
    lv_label_set_text(_prompt, w.text != nullptr ? w.text : "");
    lv_obj_align(_prompt, LV_ALIGN_CENTER, 0, kPromptY);
}

void QuizPage::handleCardClicked(bool left)
{
    _tap_pending = true;
    _tap_left    = left;
}

void QuizPage::handleRootClicked()
{
    // Reading questions get no replay, so the tap is simply dropped there
    // rather than queued for an app that would refuse it anyway.
    if (_listen) {
        _replay_pending = true;
    }
}

bool QuizPage::takeTap(bool& picked_left)
{
    if (!_tap_pending) {
        return false;
    }
    _tap_pending = false;
    picked_left  = _tap_left;
    return true;
}

bool QuizPage::takeReplayTap()
{
    if (!_replay_pending) {
        return false;
    }
    _replay_pending = false;
    return true;
}

void QuizPage::clearTap()
{
    _tap_pending    = false;
    _replay_pending = false;
}
