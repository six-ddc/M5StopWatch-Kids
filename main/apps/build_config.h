/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

// Pull in the CONFIG_* macros ourselves. ESP-IDF only puts build/config on the
// include path; nothing includes sdkconfig.h for you. This header is often the
// first include in a translation unit, i.e. before any IDF header has dragged
// sdkconfig.h in -- without this line every switch below would read as "off"
// and the counts would silently come out as zero.
#include <sdkconfig.h>

// Which apps this build contains, derived from the Kconfig switches in
// main/Kconfig.projbuild.
//
// Kconfig leaves an unselected bool *undefined* rather than defining it to 0,
// and `defined()` cannot be used inside an arithmetic macro, so each switch is
// normalised to a 0/1 constant first and only then summed.

#ifdef CONFIG_KIDS_APP_HANZI
#define KIDS_N_HANZI 1
#else
#define KIDS_N_HANZI 0
#endif

#ifdef CONFIG_KIDS_APP_MATH
#define KIDS_N_MATH 1
#else
#define KIDS_N_MATH 0
#endif

#ifdef CONFIG_KIDS_APP_ENGLISH
#define KIDS_N_ENGLISH 1
#else
#define KIDS_N_ENGLISH 0
#endif

#define KIDS_APP_COUNT (KIDS_N_HANZI + KIDS_N_MATH + KIDS_N_ENGLISH)

// A single-app build has no launcher to go back to. The app opens straight
// from boot, and "go home" (A+B long press) becomes a no-op -- closing the
// only app would leave the device on a blank screen with nothing to reopen
// it. CMakeLists.txt drops the launcher sources for the same reason.
#define KIDS_STANDALONE (KIDS_APP_COUNT == 1)
