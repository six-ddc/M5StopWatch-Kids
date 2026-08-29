/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hz_data.h"
#include <cmath>
#include <cstring>

namespace hz {

namespace {

constexpr uint8_t kMagic[4]   = {'H', 'Z', 'S', '1'};
constexpr uint16_t kVersion   = 1;
constexpr size_t kHeaderSize  = 32;
constexpr size_t kCharIndexSize = 8;
constexpr size_t kLessonSize    = 6;

constexpr uint8_t kOpL = 0;
constexpr uint8_t kOpQ = 1;
constexpr uint8_t kOpC = 2;

// Curve flattening budget, in screen pixels. 0.25 px keeps the polygon within
// half a subsample of the true curve at the raster's 4x vertical sampling.
constexpr float kFlattenTolerance = 0.25f;
constexpr uint16_t kMaxSegments   = 24;

// The blob is byte-packed, so every multi-byte field is assembled by hand:
// Xtensa faults on unaligned 32-bit access and the payload offsets are odd as
// often as not.
inline uint16_t rdU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline int16_t rdI16(const uint8_t* p)
{
    return static_cast<int16_t>(rdU16(p));
}

inline uint32_t rdU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t segmentsFor(float deviation)
{
    if (deviation <= kFlattenTolerance) {
        return 1;
    }
    const float n = std::ceil(std::sqrt(deviation / kFlattenTolerance));
    if (n >= static_cast<float>(kMaxSegments)) {
        return kMaxSegments;
    }
    return static_cast<uint16_t>(n);
}

// Max distance between a quadratic and its chord is |p0 - 2p1 + p2| / 4.
inline uint16_t quadSegments(const Point& p0, const Point& p1, const Point& p2)
{
    const float dx = p0.x - 2.0f * p1.x + p2.x;
    const float dy = p0.y - 2.0f * p1.y + p2.y;
    return segmentsFor(std::sqrt(dx * dx + dy * dy) * 0.25f);
}

// Standard cubic flatness bound from the two second differences.
inline uint16_t cubicSegments(const Point& p0, const Point& p1, const Point& p2,
                              const Point& p3)
{
    const float ax = p0.x - 2.0f * p1.x + p2.x;
    const float ay = p0.y - 2.0f * p1.y + p2.y;
    const float bx = p1.x - 2.0f * p2.x + p3.x;
    const float by = p1.y - 2.0f * p2.y + p3.y;
    const float a  = std::sqrt(ax * ax + ay * ay);
    const float b  = std::sqrt(bx * bx + by * by);
    return segmentsFor(0.75f * (a > b ? a : b));
}

// Walks the delta stream one point at a time; see hzformat.py for the layout.
class DeltaReader {
public:
    DeltaReader(const uint8_t* p, const uint8_t* end) : _p(p), _end(end) {}

    bool first(int32_t& x, int32_t& y)
    {
        if (_p + 4 > _end) {
            return false;
        }
        x  = rdI16(_p);
        y  = rdI16(_p + 2);
        _p += 4;
        _x = x;
        _y = y;
        return true;
    }

    bool next(int32_t& x, int32_t& y)
    {
        if (_p >= _end) {
            return false;
        }
        const int8_t head = static_cast<int8_t>(*_p++);
        int32_t dx, dy;
        if (head == -128) {
            if (_p + 4 > _end) {
                return false;
            }
            dx = rdI16(_p);
            dy = rdI16(_p + 2);
            _p += 4;
        } else {
            if (_p >= _end) {
                return false;
            }
            dx = head;
            dy = static_cast<int8_t>(*_p++);
        }
        _x += dx;
        _y += dy;
        x = _x;
        y = _y;
        return true;
    }

    const uint8_t* cursor() const
    {
        return _p;
    }

private:
    const uint8_t* _p;
    const uint8_t* _end;
    int32_t _x = 0;
    int32_t _y = 0;
};

}  // namespace

// --------------------------------------------------------------------------
// Arena

Arena::Arena(void* buffer, size_t size) : _base(static_cast<uint8_t*>(buffer)), _cap(size) {}

void Arena::reset()
{
    _used     = 0;
    _overflow = false;
}

void* Arena::allocRaw(size_t bytes, size_t align)
{
    if (_base == nullptr) {
        _overflow = true;
        return nullptr;
    }
    size_t start = (_used + align - 1) & ~(align - 1);
    if (start + bytes > _cap) {
        _overflow = true;
        return nullptr;
    }
    _used = start + bytes;
    return _base + start;
}

// --------------------------------------------------------------------------
// DataSource

bool DataSource::bind(const uint8_t* blob, size_t size)
{
    _blob = nullptr;
    if (blob == nullptr || size < kHeaderSize) {
        return false;
    }
    if (std::memcmp(blob, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    if (rdU16(blob + 4) != kVersion) {
        return false;
    }

    const uint16_t coord_scale  = rdU16(blob + 6);
    const uint16_t char_count   = rdU16(blob + 8);
    const uint16_t lesson_count = rdU16(blob + 10);
    const uint32_t char_index   = rdU32(blob + 12);
    const uint32_t lesson_table = rdU32(blob + 16);
    const uint32_t strings      = rdU32(blob + 20);
    const uint32_t chardata     = rdU32(blob + 24);
    const uint32_t total        = rdU32(blob + 28);

    if (total != size || coord_scale == 0 || char_count == 0) {
        return false;
    }
    const uint32_t order_table = lesson_table + lesson_count * kLessonSize;
    if (char_index != kHeaderSize ||
        lesson_table != char_index + char_count * kCharIndexSize ||
        strings != order_table + char_count * 2u || chardata < strings ||
        chardata > size) {
        return false;
    }

    _blob             = blob;
    _size             = size;
    _coord_scale      = coord_scale;
    _char_count       = char_count;
    _lesson_count     = lesson_count;
    _char_index_off   = char_index;
    _lesson_table_off = lesson_table;
    _order_table_off  = order_table;
    _strings_off      = strings;
    _chardata_off     = chardata;
    return true;
}

const uint8_t* DataSource::charIndexEntry(uint16_t slot) const
{
    return _blob + _char_index_off + static_cast<size_t>(slot) * kCharIndexSize;
}

uint16_t DataSource::slotOfOrder(uint16_t order) const
{
    return rdU16(_blob + _order_table_off + static_cast<size_t>(order) * 2u);
}

const char* DataSource::stringAt(uint32_t rel) const
{
    return reinterpret_cast<const char*>(_blob + _strings_off + rel);
}

uint32_t DataSource::codepointAt(uint16_t order) const
{
    if (!valid() || order >= _char_count) {
        return 0;
    }
    return rdU16(charIndexEntry(slotOfOrder(order)));
}

const char* DataSource::pinyinAt(uint16_t order) const
{
    if (!valid() || order >= _char_count) {
        return "";
    }
    return stringAt(rdU16(charIndexEntry(slotOfOrder(order)) + 2));
}

DataSource::Lesson DataSource::lessonAt(uint16_t index) const
{
    Lesson out;
    if (!valid() || index >= _lesson_count) {
        out.title = "";
        return out;
    }
    const uint8_t* p = _blob + _lesson_table_off + static_cast<size_t>(index) * kLessonSize;
    out.title        = stringAt(rdU16(p));
    out.first_char   = rdU16(p + 2);
    out.char_count   = rdU16(p + 4);
    return out;
}

int32_t DataSource::lessonOfChar(uint16_t order) const
{
    if (!valid() || order >= _char_count) {
        return -1;
    }
    // Lessons are contiguous and ordered, so a linear scan over ~130 entries is
    // both simplest and cache-friendly.
    for (uint16_t i = 0; i < _lesson_count; i++) {
        const Lesson lsn = lessonAt(i);
        if (order >= lsn.first_char && order < lsn.first_char + lsn.char_count) {
            return i;
        }
    }
    return -1;
}

int32_t DataSource::findByCodepoint(uint32_t codepoint) const
{
    if (!valid() || codepoint > 0xFFFF) {
        return -1;
    }
    int32_t lo = 0;
    int32_t hi = static_cast<int32_t>(_char_count) - 1;
    int32_t slot = -1;
    while (lo <= hi) {
        const int32_t mid = (lo + hi) / 2;
        const uint32_t cp = rdU16(charIndexEntry(static_cast<uint16_t>(mid)));
        if (cp == codepoint) {
            slot = mid;
            break;
        }
        if (cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (slot < 0) {
        return -1;
    }
    for (uint16_t order = 0; order < _char_count; order++) {
        if (slotOfOrder(order) == slot) {
            return order;
        }
    }
    return -1;
}

bool DataSource::decode(uint16_t order, const Transform& tf, Character& out,
                        Arena& arena) const
{
    out = Character{};
    if (!valid() || order >= _char_count) {
        return false;
    }

    const uint16_t slot     = slotOfOrder(order);
    const uint8_t* entry    = charIndexEntry(slot);
    const uint32_t data_rel = rdU32(entry + 4);
    const uint8_t* p        = _blob + _chardata_off + data_rel;
    const uint8_t* end      = _blob + _size;
    if (p >= end) {
        return false;
    }

    const uint16_t stroke_count = *p++;
    if (stroke_count == 0) {
        return false;
    }
    Stroke* strokes = arena.alloc<Stroke>(stroke_count);
    if (strokes == nullptr) {
        return false;
    }

    for (uint16_t s = 0; s < stroke_count; s++) {
        if (p + 3 > end) {
            return false;
        }
        const uint8_t radius       = *p++;
        const uint8_t op_count     = *p++;
        const uint8_t median_count = *p++;
        const size_t packed_len    = (static_cast<size_t>(op_count) + 1) / 2;
        if (p + packed_len > end || median_count < 2) {
            return false;
        }
        const uint8_t* ops = p;
        p += packed_len;

        DeltaReader reader(p, end);
        int32_t qx = 0, qy = 0;
        if (!reader.first(qx, qy)) {
            return false;
        }

        Point cur{qx * tf.scale + tf.ox, qy * tf.scale + tf.oy};
        Point* outline_start = arena.push(cur);
        if (outline_start == nullptr) {
            return false;
        }
        uint16_t outline_count = 1;

        for (uint16_t i = 0; i < op_count; i++) {
            const uint8_t op = (i % 2 == 0) ? (ops[i / 2] & 0x0F) : ((ops[i / 2] >> 4) & 0x0F);
            Point ctrl[3];
            uint8_t need = 0;
            if (op == kOpL) {
                need = 1;
            } else if (op == kOpQ) {
                need = 2;
            } else if (op == kOpC) {
                need = 3;
            } else {
                return false;
            }
            for (uint8_t k = 0; k < need; k++) {
                if (!reader.next(qx, qy)) {
                    return false;
                }
                ctrl[k] = Point{qx * tf.scale + tf.ox, qy * tf.scale + tf.oy};
            }

            uint16_t steps = 1;
            if (op == kOpQ) {
                steps = quadSegments(cur, ctrl[0], ctrl[1]);
            } else if (op == kOpC) {
                steps = cubicSegments(cur, ctrl[0], ctrl[1], ctrl[2]);
            }

            for (uint16_t step = 1; step <= steps; step++) {
                const float t = static_cast<float>(step) / static_cast<float>(steps);
                const float u = 1.0f - t;
                Point pt;
                if (op == kOpL) {
                    pt = ctrl[0];
                } else if (op == kOpQ) {
                    pt.x = u * u * cur.x + 2.0f * u * t * ctrl[0].x + t * t * ctrl[1].x;
                    pt.y = u * u * cur.y + 2.0f * u * t * ctrl[0].y + t * t * ctrl[1].y;
                } else {
                    const float a = u * u * u;
                    const float b = 3.0f * u * u * t;
                    const float c = 3.0f * u * t * t;
                    const float d = t * t * t;
                    pt.x = a * cur.x + b * ctrl[0].x + c * ctrl[1].x + d * ctrl[2].x;
                    pt.y = a * cur.y + b * ctrl[0].y + c * ctrl[1].y + d * ctrl[2].y;
                }
                if (arena.push(pt) == nullptr) {
                    return false;
                }
                outline_count++;
            }
            cur = (op == kOpL) ? ctrl[0] : ((op == kOpQ) ? ctrl[1] : ctrl[2]);
        }

        // The reader consumed the outline stream; restart on the median.
        DeltaReader median_reader(reader.cursor(), end);
        // Rewind is not needed: DeltaReader::cursor() already points past the
        // last outline point, which is where the median stream begins.
        if (!median_reader.first(qx, qy)) {
            return false;
        }
        Point mpt{qx * tf.scale + tf.ox, qy * tf.scale + tf.oy};
        Point* median_start = arena.push(mpt);
        if (median_start == nullptr) {
            return false;
        }
        float length = 0.0f;
        Point prev   = mpt;
        for (uint16_t i = 1; i < median_count; i++) {
            if (!median_reader.next(qx, qy)) {
                return false;
            }
            mpt = Point{qx * tf.scale + tf.ox, qy * tf.scale + tf.oy};
            if (arena.push(mpt) == nullptr) {
                return false;
            }
            length += std::sqrt((mpt.x - prev.x) * (mpt.x - prev.x) +
                                (mpt.y - prev.y) * (mpt.y - prev.y));
            prev = mpt;
        }
        p = median_reader.cursor();

        strokes[s].outline       = outline_start;
        strokes[s].outline_count = outline_count;
        strokes[s].median        = median_start;
        strokes[s].median_count  = median_count;
        strokes[s].brush_radius  = static_cast<float>(radius) * tf.scale;
        strokes[s].median_length = length;
    }

    out.codepoint    = rdU16(entry);
    out.stroke_count = stroke_count;
    out.strokes      = strokes;
    return true;
}

}  // namespace hz
