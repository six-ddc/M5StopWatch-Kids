/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

// Polling the buttons costs an I2C transaction and the main loop spins far
// faster than a human can press. 40 Hz is plenty and keeps the bus free.
constexpr uint32_t kKeyPollMs = 25;

}  // namespace

/* -------------------------------------------------------------------------- */
/*                               Page indicator                               */
/* -------------------------------------------------------------------------- */
class PageIndicator {
public:
    const int dot_size     = 8;
    const int dot_size_big = 14;
    const int dot_gap      = 16;

    void init(int pageNum, int pageGap, lv_obj_t* parent, int posX, int posY)
    {
        _page_num = pageNum;
        _page_gap = pageGap;

        _panel = std::make_unique<Container>(parent);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_FLOATING);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setPadding(0, 0, 24, 24);
        _panel->setPos(posX, posY);
        _panel->setBorderWidth(0);
        _panel->setHeight(24);
        _panel->setWidth((pageNum * dot_size) + (pageNum - 1) * (dot_gap - dot_size) + 24 * 2);
        _panel->setBgOpa(0);

        for (int i = 0; i < pageNum; i++) {
            _dots.push_back(std::make_unique<Container>(_panel->get()));
            _dots.back()->setAlign(LV_ALIGN_CENTER);
            _dots.back()->setPos(i * dot_gap - (pageNum - 1) * dot_gap / 2, 0);
            _dots.back()->setBgColor(lv_color_hex(0xFFFFFF));
            _dots.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _dots.back()->setRadius(LV_RADIUS_CIRCLE);
            _dots.back()->setSize(dot_size, dot_size);
            _dots.back()->setBorderWidth(0);
        }

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= _page_num) {
            return;
        }
        _current_index = index;
        _last_index    = index;
        update_dots();
    }

    void update(int scrollValue)
    {
        _last_index = _current_index;

        // Absolute icon index, then folded back onto the real app list -- the
        // scroll runs across five copies of it.
        int abs_index  = (scrollValue + _page_gap / 2) / _page_gap;
        _current_index = abs_index % _page_num;
        if (_current_index < 0) {
            _current_index += _page_num;
        }

        if (_last_index != _current_index) {
            update_dots();
        }
    }

private:
    int _page_num = 0;
    int _page_gap = 0;

    int _current_index = 0;
    int _last_index    = 0;

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Container>> _dots;

    void update_dots()
    {
        for (int i = 0; i < _page_num; i++) {
            if (i == _current_index) {
                _dots[i]->setSize(dot_size_big, dot_size_big);
                _dots[i]->setOpa(255);
            } else {
                _dots[i]->setSize(dot_size, dot_size);
                _dots[i]->setOpa(128);
            }
        }
    }
};

/* -------------------------------------------------------------------------- */
/*                             Dynamic icon label                             */
/* -------------------------------------------------------------------------- */
class DynamicIconLabel {
public:
    const int show_range      = 150;
    const int pos_y           = 155;
    const int transition_zone = 80;

    void init(const std::vector<std::string>& iconLabelTexts, int iconGap, lv_obj_t* parent)
    {
        _icon_label_texts = iconLabelTexts;
        _icon_gap         = iconGap;

        _label = std::make_unique<Label>(parent);
        _label->setTextColor(lv_color_hex(0xFFFFFF));
        // App names are Chinese here, so this uses the shared subset font
        // rather than the Latin one the original launcher had.
        _label->setTextFont(&lv_font_hanzi_ui_24);
        _label->setAlign(LV_ALIGN_CENTER);
        _label->addFlag(LV_OBJ_FLAG_FLOATING);
        _label->setOpa(255);

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= static_cast<int>(_icon_label_texts.size())) {
            return;
        }
        _current_index = index;
        _last_index    = index;
        _label->setText(_icon_label_texts[index]);
        _label->setPos(0, pos_y);
    }

    void update(int scrollValue)
    {
        _last_index = _current_index;

        // Distance from the nearest icon's centre drives the fade.
        _current_index        = (scrollValue + _icon_gap / 2) / _icon_gap;
        int icon_center_pos_x = _current_index * _icon_gap;
        int distance_to_icon  = std::abs(scrollValue - icon_center_pos_x);

        if (_current_index < 0) {
            _current_index = 0;
        }
        if (_current_index >= static_cast<int>(_icon_label_texts.size())) {
            _current_index = static_cast<int>(_icon_label_texts.size()) - 1;
        }

        bool should_be_visible = (distance_to_icon <= show_range);

        if (_last_index != _current_index) {
            _label->setText(_icon_label_texts[_current_index]);
        }

        if (should_be_visible && distance_to_icon > (show_range - transition_zone)) {
            float fade_ratio =
                1.0f - (float)(distance_to_icon - (show_range - transition_zone)) / transition_zone;
            _label->setOpa(255 * fade_ratio);
        } else if (should_be_visible) {
            _label->setOpa(255);
        }
    }

private:
    std::vector<std::string> _icon_label_texts;
    int _icon_gap      = 0;
    int _current_index = 0;
    int _last_index    = 0;

    std::unique_ptr<Label> _label;
};

static std::string _tag        = "LauncherView";
static constexpr int _icon_gap = 466;  // must equal the screen width: the snap
                                       // and the index maths both assume it
// Five copies: [0:Backup] [1:Buffer] [2:Main] [3:Buffer] [4:Backup]
static constexpr int _loop_copies       = 5;
static constexpr int _center_copy_index = 2;

// Survives the view so reopening the launcher lands back on the app the child
// just came out of, instead of snapping to the first one.
static int _last_clicked_icon_pos_x = -1;
static std::unique_ptr<PageIndicator> _page_indicator;
static std::unique_ptr<DynamicIconLabel> _dynamic_icon_label;

LauncherView::~LauncherView()
{
    _icon_images.clear();
    _icon_panels.clear();
    _lr_indicators_images.clear();
    _lr_indicator_panels.clear();
    _panel.reset();
    _page_indicator.reset();
    _dynamic_icon_label.reset();
}

void LauncherView::init(std::vector<mooncake::AppProps_t> appProps)
{
    mclog::tagInfo(_tag, "init");

    _key_manager = std::make_unique<input::KeyManager>();

    /* ------------------------------ Screen setup ------------------------------ */
    ScreenActive screen;
    screen.removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    /* ---------------------------------- Panel --------------------------------- */
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setSize(466, 466);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->setBgColor(lv_color_hex(0x000000));
    _panel->addFlag(LV_OBJ_FLAG_SCROLL_ONE);
    _panel->setPaddingAll(0);
    lv_obj_set_scroll_snap_x(_panel->get(), LV_SCROLL_SNAP_CENTER);

    /* ---------------------------------- Icons --------------------------------- */
    int icon_x = 0;
    int icon_y = -15;  // nudged up to leave room for the page dots
    std::vector<std::string> icon_label_texts;

    // Repeat the whole list so a drag in either direction always has icons to
    // land on; handle_scroll_in_loop() teleports back to the middle copy.
    for (int loop = 0; loop < _loop_copies; loop++) {
        for (const auto& props : appProps) {
            _icon_panels.push_back(std::make_unique<Container>(_panel->get()));
            _icon_panels.back()->setAlign(LV_ALIGN_CENTER);
            _icon_panels.back()->setSize(200, 200);
            _icon_panels.back()->setPos(icon_x, icon_y);
            _icon_panels.back()->setBorderWidth(0);
            _icon_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _icon_panels.back()->setBgOpa(0);

            auto app_id = props.appID;
            auto pos_x  = icon_x;
            _icon_panels.back()->onClick().connect([&, app_id, pos_x]() {
                _clicked_app_id          = app_id;
                _last_clicked_icon_pos_x = pos_x;
            });

            icon_label_texts.push_back(props.info.name);

            if (props.info.icon != nullptr) {
                _icon_images.push_back(std::make_unique<Image>(_icon_panels.back()->get()));
                _icon_images.back()->setSrc(props.info.icon);
                _icon_images.back()->setAlign(LV_ALIGN_CENTER);
            }

            icon_x += _icon_gap;
        }
    }

    /* ------------------------------ LR indicators ----------------------------- */
    // Arrows at the screen edges. They are also the touch targets for paging,
    // so a child who has not worked out the swipe still has a way through.
    _lr_indicator_panels.push_back(std::make_unique<Container>(_panel->get()));
    _lr_indicator_panels.back()->setAlign(LV_ALIGN_CENTER);
    _lr_indicator_panels.back()->setSize(52, 160);
    _lr_indicator_panels.back()->setPos(-200, 0);
    _lr_indicator_panels.back()->setBorderWidth(0);
    _lr_indicator_panels.back()->addFlag(LV_OBJ_FLAG_FLOATING);
    _lr_indicator_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lr_indicator_panels.back()->setBgOpa(0);
    _lr_indicator_panels.back()->onClick().connect([this]() { scroll_to_nearby_icon(-1); });

    _lr_indicators_images.push_back(std::make_unique<Image>(_lr_indicator_panels.back()->get()));
    _lr_indicators_images.back()->setSrc(&icon_indicator_left);
    _lr_indicators_images.back()->align(LV_ALIGN_CENTER, 0, 0);

    _lr_indicator_panels.push_back(std::make_unique<Container>(_panel->get()));
    _lr_indicator_panels.back()->setAlign(LV_ALIGN_CENTER);
    _lr_indicator_panels.back()->setSize(52, 160);
    _lr_indicator_panels.back()->setPos(200, 0);
    _lr_indicator_panels.back()->setBorderWidth(0);
    _lr_indicator_panels.back()->addFlag(LV_OBJ_FLAG_FLOATING);
    _lr_indicator_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lr_indicator_panels.back()->setBgOpa(0);
    _lr_indicator_panels.back()->onClick().connect([this]() { scroll_to_nearby_icon(1); });

    _lr_indicators_images.push_back(std::make_unique<Image>(_lr_indicator_panels.back()->get()));
    _lr_indicators_images.back()->setSrc(&icon_indicator_right);
    _lr_indicators_images.back()->align(LV_ALIGN_CENTER, 0, 0);

    /* ------------------------------ Page indicator ---------------------------- */
    _page_indicator = std::make_unique<PageIndicator>();
    _page_indicator->init(appProps.size(), _icon_gap, _panel->get(), 0, 200);

    /* --------------------------- Dynamic icon label --------------------------- */
    _dynamic_icon_label = std::make_unique<DynamicIconLabel>();
    _dynamic_icon_label->init(icon_label_texts, _icon_gap, _panel->get());

    /* ----------------------------- History restore ---------------------------- */
    // Start life parked on the middle copy. Without this the first frames would
    // have to teleport their way in from scroll_x 0, which is briefly visible.
    const int base_offset_rounds = _center_copy_index * static_cast<int>(appProps.size());
    int restore_icon_pos_x       = base_offset_rounds * _icon_gap;

    if (_last_clicked_icon_pos_x != -1) {
        restore_icon_pos_x       = _last_clicked_icon_pos_x;
        _last_clicked_icon_pos_x = -1;
    }

    _panel->scrollBy(-restore_icon_pos_x, 0, LV_ANIM_OFF);
    _page_indicator->update(restore_icon_pos_x);
    _dynamic_icon_label->update(restore_icon_pos_x);
    _state = STATE_NORMAL;

    // The boot splash is parented to the active screen and nothing else clears
    // it, so it has to go once the launcher has something to show in its place.
    GetHAL().bootLogo.reset();
}

void LauncherView::update()
{
    const uint32_t now = GetHAL().millis();
    if (_key_manager && now - _last_key_ms >= kKeyPollMs) {
        _last_key_ms = now;
        switch (_key_manager->update()) {
            case input::KeyEvent::GoPrevious:
                scroll_to_nearby_icon(-1);
                break;
            case input::KeyEvent::GoNext:
                scroll_to_nearby_icon(1);
                break;
            default:
                break;
        }
    }

    switch (_state) {
        case STATE_STARTUP:
            handle_state_startup();
            break;
        case STATE_NORMAL:
            handle_state_normal();
            break;
        default:
            break;
    }
}

void LauncherView::scroll_to_nearby_icon(int direction)
{
    auto current_scroll_x = _panel->getScrollX();
    int current_index     = (current_scroll_x + _icon_gap / 2) / _icon_gap;
    int target_index      = current_index + direction;

    int target_x        = target_index * _icon_gap;
    int scroll_distance = target_x - current_scroll_x;
    _panel->scrollBy(-scroll_distance, 0, LV_ANIM_ON);
}

void LauncherView::handle_state_startup()
{
    _state = STATE_NORMAL;
}

void LauncherView::handle_state_normal()
{
    if (_clicked_app_id != -1) {
        if (onAppClicked) {
            onAppClicked(_clicked_app_id);
        }
        _clicked_app_id = -1;
    }

    handle_scroll_in_loop();

    int scroll_x = _panel->getScrollX();
    _page_indicator->update(scroll_x);
    _dynamic_icon_label->update(scroll_x);
}

void LauncherView::handle_scroll_in_loop()
{
    // Copy index: 0 1 [2] 3 4. Drift into 1 or 3 and we jump a whole set back
    // towards 2, which is invisible because every set looks identical.
    int total_icons   = _icon_panels.size();
    int icons_per_set = total_icons / _loop_copies;
    int set_width_px  = icons_per_set * _icon_gap;

    int current_scroll_x = _panel->getScrollX();

    int left_trigger_limit  = 1 * set_width_px + (set_width_px / 2);
    int right_trigger_limit = 3 * set_width_px + (set_width_px / 2);

    // Never teleport mid-animation: the snap would be left stranded between two
    // icons. A manual drag (PRESSED) is fine -- that is what makes the endless
    // scroll endless.
    bool is_auto_scrolling =
        lv_obj_is_scrolling(_panel->get()) && !lv_obj_has_state(_panel->get(), LV_STATE_PRESSED);

    if (!is_auto_scrolling) {
        if (current_scroll_x < left_trigger_limit) {
            _panel->scrollBy(-set_width_px, 0, LV_ANIM_OFF);
        } else if (current_scroll_x > right_trigger_limit) {
            _panel->scrollBy(set_width_px, 0, LV_ANIM_OFF);
        }
    }
}
