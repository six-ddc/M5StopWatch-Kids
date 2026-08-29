/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include "engine/hz_data.h"
#include "view/view.h"

/**
 * @brief Stroke-order writing practice for children.
 *
 * Plays the stroke order of the 1037 characters of the primary-school
 * writing list, one stroke at a time, in a tian-zi-ge grid.
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
    };

    void showBrowse();
    void openLearn(uint16_t order);
    void saveProgress();
    void tickAnimation(uint32_t now_ms);

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::BrowsePage> _browse;
    std::unique_ptr<view::LearnPage> _learn;
    hz::DataSource _source;
    Page _page             = Page::Browse;
    uint32_t _last_tick_ms = 0;
    uint32_t _last_key_ms  = 0;
    bool _progress_dirty   = false;
};
