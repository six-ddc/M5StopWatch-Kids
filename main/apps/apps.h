/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "build_config.h"

#if KIDS_APP_COUNT > 1
#include "app_launcher/app_launcher.h"
#endif
#ifdef CONFIG_KIDS_APP_HANZI
#include "app_hanzi/app_hanzi.h"
#endif
#ifdef CONFIG_KIDS_APP_MATH
#include "app_math/app_math.h"
#endif
#ifdef CONFIG_KIDS_APP_ENGLISH
#include "app_english/app_english.h"
#endif
