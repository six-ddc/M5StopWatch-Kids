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

constexpr const char* kTag = "EngCardPage";

// Layout, relative to the centre of the 466 px round panel (radius 233).
// The picture sits above centre so the word can take the optical middle; the
// gloss and the button hint stack under it. Bottom of the hint lands 152 px
// down against 176 px of half-width available there.
constexpr int16_t kArcSize  = 440;
constexpr int16_t kArcWidth = 10;
constexpr int16_t kImageY   = -25;
constexpr int16_t kWordY    = 88;
constexpr int16_t kGlossY   = 138;
// Button hints go under the buttons, which are on the bezel like ears at 10
// and 2 o'clock -- the same (+/-95, -145) the arithmetic app uses. The picture
// and the two lines under it therefore sit 30 px lower than they used to: the
// top of the picture was 3 px below the hint's baseline row otherwise.
constexpr int16_t kHintY = -145;
constexpr int16_t kHintX = 95;

void rootClickedCb(lv_event_t* e)
{
    auto* page = static_cast<CardPage*>(lv_event_get_user_data(e));
    if (page != nullptr) {
        page->handleClicked();
    }
}

}  // namespace

void eng_view::fillImageDsc(lv_image_dsc_t& dsc, const eng::Image& image)
{
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf    = LV_COLOR_FORMAT_I4;
    dsc.header.flags = 0;
    dsc.header.w     = image.w;
    dsc.header.h     = image.h;
    // I4 packs two pixels per byte, so a row is half its width, rounded up.
    dsc.header.stride     = static_cast<uint32_t>((image.w + 1) / 2);
    dsc.header.reserved_2 = 0;
    dsc.data              = image.data;
    dsc.data_size         = image.data_size;
}

CardPage::~CardPage()
{
    destroy();
}

bool CardPage::create(lv_obj_t* parent)
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

    // The whole page replays the word, not just the picture. At 144 px on a
    // 466 px panel the picture is under a tenth of the glass, so a child aiming
    // at "the cat" mostly hit black and nothing happened. Nothing else on this
    // page is clickable -- the arc explicitly clears the flag -- so a tap
    // anywhere lands here.
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

    _image = lv_image_create(_root);
    lv_obj_align(_image, LV_ALIGN_CENTER, 0, kImageY);

    // The word carries the page, so it gets the big face. The pinyin font is
    // the only large one in the build with lower-case Latin -- which is all an
    // English word for this age group ever needs.
    _word = lv_label_create(_root);
    lv_obj_set_style_text_font(_word, &lv_font_hanzi_pinyin_44, 0);
    lv_obj_set_style_text_color(_word, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(_word, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_word, "");
    lv_obj_align(_word, LV_ALIGN_CENTER, 0, kWordY);

    _gloss = lv_label_create(_root);
    lv_obj_set_style_text_font(_gloss, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_gloss, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_gloss, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_gloss, "");
    lv_obj_align(_gloss, LV_ALIGN_CENTER, 0, kGlossY);

    _hint_a = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_a, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_a, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_a, "A 重播");
    lv_obj_align(_hint_a, LV_ALIGN_CENTER, -kHintX, kHintY);

    _hint_b = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_b, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_b, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_b, "B 下一个");
    lv_obj_align(_hint_b, LV_ALIGN_CENTER, kHintX, kHintY);

    return true;
}

void CardPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _arc    = nullptr;
    _image  = nullptr;
    _word   = nullptr;
    _gloss  = nullptr;
    _hint_a = nullptr;
    _hint_b = nullptr;
}

void CardPage::setHidden(bool hidden)
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

void CardPage::show(const eng::Data& data, uint16_t word, uint8_t index, uint8_t total)
{
    if (_root == nullptr) {
        return;
    }

    const eng::Word w = data.word(word);
    lv_label_set_text(_word, w.text != nullptr ? w.text : "");
    lv_obj_align(_word, LV_ALIGN_CENTER, 0, kWordY);
    lv_label_set_text(_gloss, w.zh != nullptr ? w.zh : "");
    lv_obj_align(_gloss, LV_ALIGN_CENTER, 0, kGlossY);

    eng::Image img;
    if (data.image(word, img)) {
        // The descriptor is reused for every word, so the decoded copy LVGL
        // kept for the previous one has to go first. On the device the image
        // cache is 1 MB and would otherwise hand back the last picture; in the
        // host simulator the cache is disabled and this is a no-op, which is
        // exactly the sort of difference that hides a bug until it is on
        // hardware.
        lv_image_cache_drop(&_dsc);
        fillImageDsc(_dsc, img);
        lv_image_set_src(_image, &_dsc);
        lv_obj_clear_flag(_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(_image, LV_ALIGN_CENTER, 0, kImageY);

    lv_arc_set_value(_arc, total == 0 ? 0 : (static_cast<int32_t>(index) * 100) / total);
}

void CardPage::handleClicked()
{
    // Called from inside lv_timer_handler with the lock held: note it and let
    // the app drain it from onRunning, where audio and NVS are legal.
    _tap_pending = true;
}

bool CardPage::takeTap()
{
    if (!_tap_pending) {
        return false;
    }
    _tap_pending = false;
    return true;
}

void CardPage::clearTap()
{
    _tap_pending = false;
}
