# Sources are gathered per app rather than in one sweep, so that turning an app
# off in menuconfig also drops its code and its data.
#
# IMPORTANT: adding a new app, font, image or blob means adding it to the right
# branch below. A file that no branch mentions is silently left out of the
# build -- there is no glob catching the leftovers on purpose.

# ---- always in: HAL, shared app plumbing, the UI font everything draws with --
file(GLOB_RECURSE MY_SRCS
    "apps/common/*.c"
    "apps/common/*.cc"
    "apps/common/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
)
# The launcher and all three apps label things with this one.
list(APPEND MY_SRCS "assets/fonts/lv_font_hanzi_ui_24.c")
# The shared T9 keyboard (apps/common/pinyin_ime) draws its pinyin chips
# with this small latin subset.
list(APPEND MY_SRCS "assets/fonts/lv_font_pinyin_latin_32.c")

# ---- count the apps: one app means no launcher -----------------------------
set(KIDS_APP_COUNT 0)
foreach(_app CONFIG_KIDS_APP_HANZI CONFIG_KIDS_APP_MATH CONFIG_KIDS_APP_ENGLISH)
    if(${_app})
        math(EXPR KIDS_APP_COUNT "${KIDS_APP_COUNT} + 1")
    endif()
endforeach()

# This file is evaluated twice: once during early expansion, when only
# REQUIRES matters and no CONFIG_* is defined yet, and again for real. Every
# app looks disabled the first time round, so only complain on the real pass.
if(KIDS_APP_COUNT EQUAL 0 AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(FATAL_ERROR
        "No app selected. Turn on at least one of KIDS_APP_HANZI / "
        "KIDS_APP_MATH / KIDS_APP_ENGLISH in `idf.py menuconfig` "
        "-> Kids firmware.")
endif()

if(KIDS_APP_COUNT GREATER 1)
    file(GLOB_RECURSE LAUNCHER_SRCS
        "apps/app_launcher/*.c"
        "apps/app_launcher/*.cc"
        "apps/app_launcher/*.cpp"
    )
    list(APPEND MY_SRCS
        ${LAUNCHER_SRCS}
        "assets/images/icon_indicator_left.c"
        "assets/images/icon_indicator_right.c"
    )
endif()

# ---- per-app sources and data ----------------------------------------------
if(CONFIG_KIDS_APP_HANZI)
    file(GLOB_RECURSE HANZI_SRCS
        "apps/app_hanzi/*.c"
        "apps/app_hanzi/*.cc"
        "apps/app_hanzi/*.cpp"
    )
    list(APPEND MY_SRCS ${HANZI_SRCS})
endif()

if(CONFIG_KIDS_APP_MATH)
    file(GLOB_RECURSE MATH_SRCS
        "apps/app_math/*.c"
        "apps/app_math/*.cc"
        "apps/app_math/*.cpp"
    )
    list(APPEND MY_SRCS ${MATH_SRCS}
        "assets/fonts/lv_font_digit_64.c"
        "assets/fonts/lv_font_digit_96.c"
    )
endif()

if(CONFIG_KIDS_APP_ENGLISH)
    file(GLOB_RECURSE ENGLISH_SRCS
        "apps/app_english/*.c"
        "apps/app_english/*.cc"
        "apps/app_english/*.cpp"
    )
    list(APPEND MY_SRCS ${ENGLISH_SRCS})
endif()

# Shared between two apps: hanzi draws pinyin with it, english draws the
# Chinese gloss with it. Needed if either one is in.
if(CONFIG_KIDS_APP_HANZI OR CONFIG_KIDS_APP_ENGLISH)
    list(APPEND MY_SRCS "assets/fonts/lv_font_hanzi_pinyin_44.c")
endif()

# Launcher icons. Each app names its own in its constructor
# (setAppInfo().icon), so they follow the app -- but only a build that has a
# launcher ever draws them.
if(KIDS_APP_COUNT GREATER 1)
    if(CONFIG_KIDS_APP_HANZI)
        list(APPEND MY_SRCS "assets/images/icon_hanzi.c")
    endif()
    if(CONFIG_KIDS_APP_MATH)
        list(APPEND MY_SRCS "assets/images/icon_math.c")
    endif()
    if(CONFIG_KIDS_APP_ENGLISH)
        list(APPEND MY_SRCS "assets/images/icon_english.c")
    endif()
endif()

# ---- embedded binaries ------------------------------------------------------
# The English word data and the hanzi stroke data are linked in as binaries
# rather than compiled from C arrays. At 480 words the English array form is a
# 43 MB .c file that takes GCC minutes to parse on every rebuild (the
# 3500-character hanzi array would be ~14 MB); embedded this way the compiler
# never sees them. The matching assets/*/..._data.h declares the symbols the
# linker creates (_binary_..._bin_start / _end).
set(MY_EMBEDS "")
if(CONFIG_KIDS_APP_HANZI)
    list(APPEND MY_EMBEDS "assets/hanzi/hanzi_data.bin")
endif()
if(CONFIG_KIDS_APP_ENGLISH)
    list(APPEND MY_EMBEDS "assets/english/english_data.bin")
endif()

set(MY_INCS
    "."
)

idf_component_register(
    SRCS
        "main.cpp"
        ${MY_SRCS}
    INCLUDE_DIRS
        ${MY_INCS}
    EMBED_FILES
        ${MY_EMBEDS}
)
