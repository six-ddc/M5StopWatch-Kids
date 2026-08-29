/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>

/**
 * @brief Reader for the ENG1 blob that tools/english_pipeline produces.
 *
 * The blob lives in .rodata and is never copied: every accessor here returns a
 * pointer into it. Nothing allocates, so this is safe to call from anywhere,
 * including while the LVGL lock is held.
 *
 * The format is specified once, in tools/english_pipeline/engformat.py, and
 * this file mirrors it field for field. Change one, change the other.
 *
 * Alignment: the index and string regions are byte-packed, so a 16- or 32-bit
 * field can land on an odd address. Xtensa raises LoadStoreAlignmentError on
 * an unaligned load, so every multi-byte read here goes through rd16/rd32,
 * which assemble the value one byte at a time. The blob regions are the only
 * exception -- the packer pads them to a 4-byte boundary precisely so the
 * palette can be handed to LVGL as a lv_color32_t array.
 *
 * This header deliberately pulls in neither LVGL nor ESP-IDF, so
 * tools/english_host_test can compile it natively.
 */
namespace eng {

/// One 4-bit indexed image. `data` points at the 16-entry palette, which is
/// immediately followed by the pixels -- that is the layout LVGL expects for
/// LV_COLOR_FORMAT_I4, so this pointer goes straight into lv_image_dsc_t.
struct Image {
    const uint8_t* data = nullptr;
    uint16_t w          = 0;
    uint16_t h          = 0;
    uint32_t data_size  = 0;  ///< palette bytes + pixel bytes
};

/// One IMA ADPCM clip. Decode with adpcm::decode().
struct Audio {
    const uint8_t* nibbles = nullptr;
    uint32_t sample_count  = 0;
    int16_t predictor      = 0;
    uint8_t step_index     = 0;
};

struct Word {
    const char* text = nullptr;  ///< the English word, lower case
    const char* zh   = nullptr;  ///< Chinese gloss
};

struct Unit {
    const char* title = nullptr;  ///< Chinese unit name, e.g. "动物"
    uint16_t first    = 0;        ///< index of this unit's first word
    uint16_t count    = 0;
    bool has_sfx      = false;  ///< enough words carry a sound effect to
                                ///< make the listen-and-pick game playable
};

class Data {
public:
    /// Validates the header and remembers the region offsets. Returns false on
    /// a bad magic, an unexpected version, or a size that disagrees with the
    /// header -- in which case every other accessor stays safe but empty.
    bool load(const uint8_t* blob, uint32_t size);

    bool valid() const
    {
        return _blob != nullptr;
    }
    uint16_t wordCount() const
    {
        return _word_count;
    }
    uint16_t unitCount() const
    {
        return _unit_count;
    }
    uint16_t imageW() const
    {
        return _image_w;
    }
    uint16_t imageH() const
    {
        return _image_h;
    }
    uint16_t audioRate() const
    {
        return _audio_rate;
    }

    /// Out-of-range indices yield an all-null Word/Unit rather than reading
    /// past the blob; callers that forgot to bound-check get blanks, not a
    /// crash on a device with no debugger attached.
    Word word(uint16_t index) const;
    Unit unit(uint16_t index) const;

    bool image(uint16_t word_index, Image& out) const;
    bool audio(uint16_t word_index, Audio& out) const;
    bool sfx(uint16_t word_index, Audio& out) const;

private:
    const uint8_t* _blob  = nullptr;
    uint32_t _size        = 0;
    uint16_t _word_count  = 0;
    uint16_t _unit_count  = 0;
    uint16_t _image_w     = 0;
    uint16_t _image_h     = 0;
    uint16_t _audio_rate  = 0;
    uint32_t _words_off   = 0;
    uint32_t _units_off   = 0;
    uint32_t _strings_off = 0;
    uint32_t _blobs_off   = 0;

    const char* str(uint16_t offset) const;
    bool audioAt(uint32_t rel_off, Audio& out) const;
};

}  // namespace eng
