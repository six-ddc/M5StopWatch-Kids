/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <mooncake_templates.h>
#include <memory>
#include "view/view.h"

/**
 * @brief Home screen: pick between the two apps by scrolling left and right.
 *
 * AppLauncherBase does the hard part of the handover -- it closes itself before
 * the chosen app opens, and reopens itself once that app goes back to sleep. An
 * app returning home therefore only has to call close() on itself.
 */
class AppLauncher : public mooncake::templates::AppLauncherBase {
public:
    AppLauncher();

    void onLauncherCreate() override;
    void onLauncherOpen() override;
    void onLauncherRunning() override;
    void onLauncherClose() override;

private:
    std::unique_ptr<view::LauncherView> _view;
};
