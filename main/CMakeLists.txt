file(GLOB_RECURSE MY_SRCS
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "assets/*.c"
    "assets/*.cc"
    "assets/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
)

set(MY_INCS
    "."
)

idf_component_register(
    SRCS
        "main.cpp"
        ${MY_SRCS}
    INCLUDE_DIRS
        ${MY_INCS}
    # The English word data is linked in as a binary rather than compiled from
    # a C array. At 480 words the array form is a 43 MB .c file that takes GCC
    # minutes to parse on every rebuild; embedded this way the compiler never
    # sees it. assets/english/english_data.h declares the symbols the linker
    # creates (_binary_english_data_bin_start / _end).
    EMBED_FILES
        "assets/english/english_data.bin"
)
