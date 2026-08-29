/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Which apps exist is a build-time choice; see main/Kconfig.projbuild.
#if KIDS_APP_COUNT > 1
    // Install order is also the launcher's left-to-right icon order. The
    // launcher opens itself from onLauncherCreate, so nothing is opened here.
    GetMooncake().installApp(std::make_unique<AppLauncher>());
#ifdef CONFIG_KIDS_APP_HANZI
    GetMooncake().installApp(std::make_unique<AppHanzi>());
#endif
#ifdef CONFIG_KIDS_APP_MATH
    GetMooncake().installApp(std::make_unique<AppMath>());
#endif
#ifdef CONFIG_KIDS_APP_ENGLISH
    GetMooncake().installApp(std::make_unique<AppEnglish>());
#endif
#else
    // Single-app build: no launcher, so nothing would ever open the app.
    // Install it and open it here, and it stays open for good -- "go home"
    // is a no-op in this configuration (see the app's GoHome handler).
#ifdef CONFIG_KIDS_APP_HANZI
    const int app_id = GetMooncake().installApp(std::make_unique<AppHanzi>());
#endif
#ifdef CONFIG_KIDS_APP_MATH
    const int app_id = GetMooncake().installApp(std::make_unique<AppMath>());
#endif
#ifdef CONFIG_KIDS_APP_ENGLISH
    const int app_id = GetMooncake().installApp(std::make_unique<AppEnglish>());
#endif
    GetMooncake().openApp(app_id);
#endif

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
