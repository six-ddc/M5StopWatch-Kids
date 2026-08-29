/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>

using namespace mooncake;

AppLauncher::AppLauncher()
{
    setAppInfo().name = "Launcher";
}

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    // Nothing else opens the launcher -- it puts itself on screen and stays
    // the fallback everything returns to.
    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;
    _view = std::make_unique<view::LauncherView>();
    _view->init(getAppProps());
    _view->onAppClicked = [this](int appID) {
        mclog::tagInfo(getAppInfo().name, "open app id {}", appID);
        openApp(appID);
    };
}

void AppLauncher::onLauncherRunning()
{
    if (!_view) {
        return;
    }
    LvglLockGuard lock;
    _view->update();
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    _view.reset();
}
