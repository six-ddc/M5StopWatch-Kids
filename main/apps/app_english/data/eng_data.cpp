/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "eng_data.h"

namespace eng {

namespace {

constexpr uint32_t kHeaderSize    = 40;
constexpr uint32_t kWordEntrySize = 16;
constexpr uint32_t kUnitEntrySize = 8;
constexpr uint16_t kVersion       = 1;

// Every multi-byte read goes through these. The index and string regions are
// byte-packed, so a u32 routinely lands on an odd address, and Xtensa faults
// on an unaligned load rather than fixing it up in microcode.
inline uint16_t rd16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t rd32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool Data::load(const uint8_t* blob, uint32_t size)
{
    _blob = nullptr;
    if (blob == nullptr || size < kHeaderSize) {
        return false;
    }
    if (blob[0] != 'E' || blob[1] != 'N' || blob[2] != 'G' || blob[3] != '1') {
        return false;
    }
    if (rd16(blob + 4) != kVersion) {
        return false;
    }

    const uint16_t word_count  = rd16(blob + 6);
    const uint16_t unit_count  = rd16(blob + 8);
    const uint32_t words_off   = rd32(blob + 16);
    const uint32_t units_off   = rd32(blob + 20);
    const uint32_t strings_off = rd32(blob + 24);
    const uint32_t blobs_off   = rd32(blob + 28);
    const uint32_t total       = rd32(blob + 32);

    // A truncated blob is the failure mode that matters: the linker will
    // happily give us a short array if the generator was interrupted, and
    // every later read would then walk off the end of .rodata.
    if (total != size) {
        return false;
    }
    if (words_off + static_cast<uint32_t>(word_count) * kWordEntrySize > size ||
        units_off + static_cast<uint32_t>(unit_count) * kUnitEntrySize > size || strings_off >= size ||
        blobs_off > size) {
        return false;
    }

    _blob        = blob;
    _size        = size;
    _word_count  = word_count;
    _unit_count  = unit_count;
    _image_w     = rd16(blob + 10);
    _image_h     = rd16(blob + 12);
    _audio_rate  = rd16(blob + 14);
    _words_off   = words_off;
    _units_off   = units_off;
    _strings_off = strings_off;
    _blobs_off   = blobs_off;
    return true;
}

const char* Data::str(uint16_t offset) const
{
    const uint32_t at = _strings_off + offset;
    if (at >= _size) {
        return nullptr;
    }
    return reinterpret_cast<const char*>(_blob + at);
}

Word Data::word(uint16_t index) const
{
    Word w;
    if (!valid() || index >= _word_count) {
        return w;
    }
    const uint8_t* e = _blob + _words_off + static_cast<uint32_t>(index) * kWordEntrySize;
    w.text           = str(rd16(e));
    w.zh             = str(rd16(e + 2));
    return w;
}

Unit Data::unit(uint16_t index) const
{
    Unit u;
    if (!valid() || index >= _unit_count) {
        return u;
    }
    const uint8_t* e = _blob + _units_off + static_cast<uint32_t>(index) * kUnitEntrySize;
    u.title          = str(rd16(e));
    u.first          = rd16(e + 2);
    u.count          = rd16(e + 4);
    u.has_sfx        = (rd16(e + 6) & 0x1) != 0;

    // Clamp rather than trust: a unit that claims words past the end of the
    // table would otherwise turn into an out-of-bounds read every frame the
    // picker is on screen.
    if (u.first > _word_count) {
        u.first = _word_count;
    }
    if (static_cast<uint32_t>(u.first) + u.count > _word_count) {
        u.count = static_cast<uint16_t>(_word_count - u.first);
    }
    return u;
}

bool Data::image(uint16_t word_index, Image& out) const
{
    out = Image{};
    if (!valid() || word_index >= _word_count) {
        return false;
    }
    const uint8_t* e   = _blob + _words_off + static_cast<uint32_t>(word_index) * kWordEntrySize;
    const uint32_t rel = rd32(e + 4);
    if (rel == 0) {
        return false;  // 0 is "no image", not offset zero
    }
    const uint32_t at = _blobs_off + rel;
    if (at + 4 > _size) {
        return false;
    }
    const uint16_t w = rd16(_blob + at);
    const uint16_t h = rd16(_blob + at + 2);
    // 16 palette entries of 4 bytes, then two 4-bit indices per byte.
    const uint32_t bytes = 16u * 4u + (static_cast<uint32_t>(w) * h) / 2u;
    if (w == 0 || h == 0 || at + 4 + bytes > _size) {
        return false;
    }
    out.data      = _blob + at + 4;  // palette first -- LVGL wants it that way
    out.w         = w;
    out.h         = h;
    out.data_size = bytes;
    return true;
}

bool Data::audioAt(uint32_t rel_off, Audio& out) const
{
    out = Audio{};
    if (rel_off == 0) {
        return false;
    }
    const uint32_t at = _blobs_off + rel_off;
    if (at + 8 > _size) {
        return false;
    }
    const uint32_t samples = rd32(_blob + at);
    const uint32_t bytes   = (samples + 1u) / 2u;
    if (samples == 0 || at + 8 + bytes > _size) {
        return false;
    }
    out.sample_count = samples;
    out.predictor    = static_cast<int16_t>(rd16(_blob + at + 4));
    out.step_index   = _blob[at + 6];
    out.nibbles      = _blob + at + 8;
    return true;
}

bool Data::audio(uint16_t word_index, Audio& out) const
{
    out = Audio{};
    if (!valid() || word_index >= _word_count) {
        return false;
    }
    const uint8_t* e = _blob + _words_off + static_cast<uint32_t>(word_index) * kWordEntrySize;
    return audioAt(rd32(e + 8), out);
}

bool Data::sfx(uint16_t word_index, Audio& out) const
{
    out = Audio{};
    if (!valid() || word_index >= _word_count) {
        return false;
    }
    const uint8_t* e = _blob + _words_off + static_cast<uint32_t>(word_index) * kWordEntrySize;
    return audioAt(rd32(e + 12), out);
}

}  // namespace eng
