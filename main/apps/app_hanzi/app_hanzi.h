/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/build_config.h>
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include "engine/hz_data.h"
#include "view/view.h"

/**
 * @brief Stroke-order writing practice for children.
 *
 * Plays the stroke order of the primary-school characters (textbook lessons
 * plus the level-1 通用规范汉字表, 3500 characters in all), one stroke at a
 * time, in a tian-zi-ge grid. Characters outside the textbook lessons are
 * reached through the T9 pinyin search page only.
 */
class AppHanzi : public mooncake::AppAbility {
public:
    AppHanzi();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Page : uint8_t {
        Browse,
        Learn,
        Search,
    };
    // Where the learn page was entered from, so GoHome backs out to the same
    // place (a child hopping between homophones keeps their search context).
    enum class LearnFrom : uint8_t {
        Browse,
        Search,
    };

    bool buildSearchIndex();
    void showBrowse();
    void showSearch();
    void openLearn(uint16_t order, LearnFrom from, const char* reading = nullptr);
    void saveProgress();
    void tickAnimation(uint32_t now_ms);

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::BrowsePage> _browse;
    std::unique_ptr<view::LearnPage> _learn;
    std::unique_ptr<view::SearchPage> _search;
    pime::T9Engine _engine;
    hz::DataSource _source;
    Page _page             = Page::Browse;
    LearnFrom _learn_from  = LearnFrom::Browse;
    uint32_t _last_tick_ms = 0;
    uint32_t _last_key_ms  = 0;
    bool _progress_dirty   = false;
};
