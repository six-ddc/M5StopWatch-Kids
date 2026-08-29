/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>

// Decoder for the HZS1 stroke-order blob produced by
// tools/hanzi_pipeline/build_hanzi_data.py. Keep this file free of LVGL and
// ESP-IDF headers so the engine can be built and diffed on a host.
//
// Stroke data derived from Make Me a Hanzi via hanzi-writer-data; the
// Arphic Public License asks that this attribution stay with it.

namespace hz {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

// Storage coordinates are a plain top-left-origin box (the pipeline already
// flipped the source y axis), so placing a character is scale + offset only.
struct Transform {
    float scale = 1.0f;
    float ox    = 0.0f;
    float oy    = 0.0f;
};

struct Stroke {
    const Point* outline = nullptr;  // closed polygon, screen space, flattened
    const Point* median  = nullptr;  // skeleton polyline, screen space
    uint16_t outline_count = 0;
    uint16_t median_count  = 0;
    float brush_radius     = 0.0f;   // screen px, widest half-width of the stroke
    float median_length    = 0.0f;   // total polyline length in px
};

struct Character {
    uint32_t codepoint     = 0;
    uint16_t stroke_count  = 0;
    const Stroke* strokes  = nullptr;
};

// Bump allocator for decoded geometry. Callers hand in one reusable block so a
// character decode never touches the heap. Growing arrays are built by
// repeated push(); the pointers stay contiguous.
class Arena {
public:
    Arena() = default;
    Arena(void* buffer, size_t size);

    void reset();
    bool overflowed() const
    {
        return _overflow;
    }
    size_t used() const
    {
        return _used;
    }
    size_t capacity() const
    {
        return _cap;
    }

    template <typename T>
    T* alloc(size_t count)
    {
        return static_cast<T*>(allocRaw(sizeof(T) * count, alignof(T)));
    }

    // Appends one element; consecutive pushes of the same type are contiguous.
    template <typename T>
    T* push(const T& value)
    {
        T* slot = static_cast<T*>(allocRaw(sizeof(T), alignof(T)));
        if (slot != nullptr) {
            *slot = value;
        }
        return slot;
    }

private:
    void* allocRaw(size_t bytes, size_t align);

    uint8_t* _base  = nullptr;
    size_t _cap     = 0;
    size_t _used    = 0;
    bool _overflow  = false;
};

class DataSource {
public:
    struct Lesson {
        const char* title   = nullptr;
        uint16_t first_char = 0;
        uint16_t char_count = 0;
    };

    // Validates the header; returns false on a malformed or truncated blob.
    bool bind(const uint8_t* blob, size_t size);
    bool valid() const
    {
        return _blob != nullptr;
    }

    uint16_t charCount() const
    {
        return _char_count;
    }
    uint16_t lessonCount() const
    {
        return _lesson_count;
    }
    uint16_t coordScale() const
    {
        return _coord_scale;
    }

    // All character accessors take a teaching-order index (0 = first character
    // of the first lesson), which is what the UI navigates by.
    uint32_t codepointAt(uint16_t order) const;
    const char* pinyinAt(uint16_t order) const;

    Lesson lessonAt(uint16_t index) const;
    // Lesson containing a teaching-order index, or -1 when out of range.
    int32_t lessonOfChar(uint16_t order) const;
    // Teaching-order index for a codepoint, or -1 when absent.
    int32_t findByCodepoint(uint32_t codepoint) const;

    // Decodes one character into arena-backed screen-space geometry. Returns
    // false if the arena is too small or the payload is malformed.
    bool decode(uint16_t order, const Transform& tf, Character& out, Arena& arena) const;

private:
    const uint8_t* charIndexEntry(uint16_t slot) const;
    uint16_t slotOfOrder(uint16_t order) const;
    const char* stringAt(uint32_t rel) const;

    const uint8_t* _blob = nullptr;
    size_t _size         = 0;
    uint16_t _coord_scale = 0;
    uint16_t _char_count  = 0;
    uint16_t _lesson_count = 0;
    uint32_t _char_index_off  = 0;
    uint32_t _lesson_table_off = 0;
    uint32_t _order_table_off  = 0;
    uint32_t _strings_off      = 0;
    uint32_t _chardata_off     = 0;
};

}  // namespace hz
