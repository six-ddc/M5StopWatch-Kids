"""Binary container format for embedded hanzi stroke-order data (HZS1).

The firmware-side decoder in main/apps/app_hanzi/engine/hz_data.cpp must mirror
this file exactly. Encoder and decoder both live here so build_hanzi_data.py can
round-trip every character before emitting the C array.

Layout (little-endian throughout):

  Header (32 B)
    0   char[4] magic          "HZS1"
    4   u16     version        = 1
    6   u16     coord_scale    quantised coordinate box size (512)
    8   u16     char_count
    10  u16     lesson_count
    12  u32     char_index_off      absolute
    16  u32     lesson_table_off    absolute
    20  u32     strings_off         absolute
    24  u32     chardata_off        absolute
    28  u32     total_size

  CharIndex[char_count]  (8 B each, sorted by codepoint for binary search)
    0   u16 codepoint
    2   u16 pinyin_off     relative to strings_off
    4   u32 data_off       relative to chardata_off

  LessonTable[lesson_count]  (6 B each, in textbook order)
    0   u16 title_off      relative to strings_off
    2   u16 first_char     index into the *teaching order* table
    4   u16 char_count

  OrderTable[char_count] (2 B each)
    u16 char_index -- teaching order -> CharIndex slot. CharIndex itself is
    sorted by codepoint (for lookup), this maps textbook position to it.

  Strings: NUL-terminated UTF-8 (pinyin + lesson titles, deduplicated)

  CharData[char_count]
    u8 stroke_count
    Stroke[stroke_count]:
      u8 brush_radius        quantised units, max half-width of this stroke
      u8 op_count            number of curve ops (implicit leading M / trailing Z excluded)
      u8 median_count
      u8 ops[(op_count+1)//2]   4 bits per op, low nibble first: 0=L 1=Q 2=C
      DeltaStream outline    1 + sum(points per op) points
      DeltaStream median     median_count points

  DeltaStream: first point as int16 x, int16 y; each subsequent point as
    int8 dx; if dx == -128 an escape follows: int16 dx, int16 dy
    else int8 dy
  Measured escape rate at coord_scale=512 is ~1.1% of points.

Every stroke in the source data is a single closed subpath (the builder
verifies this for the whole packed set and skips violators), so the leading M
and trailing Z are implicit and no subpath table is needed.

ALIGNMENT: stroke payloads are byte-packed, so the int16 coordinates land on
arbitrary addresses. Xtensa raises LoadStoreAlignmentError on unaligned 32-bit
access and gives no guarantee for unaligned 16-bit loads from memory-mapped
flash, so the firmware decoder must read every multi-byte field byte-by-byte
(or via memcpy) rather than casting the blob pointer to a struct.
"""

import struct

MAGIC = b"HZS1"
VERSION = 1
HEADER_SIZE = 32
CHAR_INDEX_SIZE = 8
LESSON_SIZE = 6

OP_L, OP_Q, OP_C = 0, 1, 2
OP_POINTS = {OP_L: 1, OP_Q: 2, OP_C: 3}
OP_FROM_SVG = {"L": OP_L, "Q": OP_Q, "C": OP_C}

DELTA_ESCAPE = -128


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------
# delta streams


def encode_delta_stream(points):
    """points: [(int x, int y), ...] -> bytes"""
    if not points:
        raise FormatError("empty delta stream")
    out = bytearray()
    x0, y0 = points[0]
    out += struct.pack("<hh", x0, y0)
    px, py = x0, y0
    for x, y in points[1:]:
        dx, dy = x - px, y - py
        if -127 <= dx <= 127 and -127 <= dy <= 127:
            out += struct.pack("<bb", dx, dy)
        else:
            out += struct.pack("<b", DELTA_ESCAPE)
            out += struct.pack("<hh", dx, dy)
        px, py = x, y
    return bytes(out)


def decode_delta_stream(buf, pos, count):
    """-> ([(x, y), ...], new_pos)"""
    x, y = struct.unpack_from("<hh", buf, pos)
    pos += 4
    points = [(x, y)]
    for _ in range(count - 1):
        (dx,) = struct.unpack_from("<b", buf, pos)
        pos += 1
        if dx == DELTA_ESCAPE:
            dx, dy = struct.unpack_from("<hh", buf, pos)
            pos += 4
        else:
            (dy,) = struct.unpack_from("<b", buf, pos)
            pos += 1
        x, y = x + dx, y + dy
        points.append((x, y))
    return points, pos


# --------------------------------------------------------------------------
# strokes / characters


def encode_stroke(stroke):
    """stroke: {'ops': [op...], 'outline': [(x,y)...], 'median': [(x,y)...],
                'brush_radius': int} -> bytes"""
    ops = stroke["ops"]
    outline = stroke["outline"]
    median = stroke["median"]
    radius = stroke["brush_radius"]

    expected = 1 + sum(OP_POINTS[o] for o in ops)
    if len(outline) != expected:
        raise FormatError(f"outline has {len(outline)} points, ops imply {expected}")
    for name, value in (("op_count", len(ops)), ("median_count", len(median)),
                        ("brush_radius", radius)):
        if not 0 <= value <= 255:
            raise FormatError(f"{name}={value} does not fit in u8")

    out = bytearray(struct.pack("<BBB", radius, len(ops), len(median)))
    packed = bytearray((len(ops) + 1) // 2)
    for i, op in enumerate(ops):
        if op not in OP_POINTS:
            raise FormatError(f"unknown op {op}")
        if i % 2 == 0:
            packed[i // 2] |= op
        else:
            packed[i // 2] |= op << 4
    out += packed
    out += encode_delta_stream(outline)
    out += encode_delta_stream(median)
    return bytes(out)


def decode_stroke(buf, pos):
    radius, op_count, median_count = struct.unpack_from("<BBB", buf, pos)
    pos += 3
    packed_len = (op_count + 1) // 2
    packed = buf[pos:pos + packed_len]
    pos += packed_len
    ops = []
    for i in range(op_count):
        nibble = packed[i // 2]
        ops.append(nibble & 0x0F if i % 2 == 0 else (nibble >> 4) & 0x0F)
    outline_count = 1 + sum(OP_POINTS[o] for o in ops)
    outline, pos = decode_delta_stream(buf, pos, outline_count)
    median, pos = decode_delta_stream(buf, pos, median_count)
    return {"ops": ops, "outline": outline, "median": median,
            "brush_radius": radius}, pos


def encode_char(strokes):
    if not 0 < len(strokes) <= 255:
        raise FormatError(f"stroke_count={len(strokes)} out of range")
    out = bytearray(struct.pack("<B", len(strokes)))
    for s in strokes:
        out += encode_stroke(s)
    return bytes(out)


def decode_char(buf, pos):
    (stroke_count,) = struct.unpack_from("<B", buf, pos)
    pos += 1
    strokes = []
    for _ in range(stroke_count):
        s, pos = decode_stroke(buf, pos)
        strokes.append(s)
    return strokes, pos


# --------------------------------------------------------------------------
# string pool


class StringPool:
    """Deduplicating NUL-terminated UTF-8 pool; offsets fit u16."""

    def __init__(self):
        self._buf = bytearray()
        self._offsets = {}
        self.intern("")  # offset 0 is the empty string

    def intern(self, text):
        if text in self._offsets:
            return self._offsets[text]
        off = len(self._buf)
        if off > 0xFFFF:
            raise FormatError("string pool exceeds 64 KiB")
        self._offsets[text] = off
        self._buf += text.encode("utf-8") + b"\0"
        return off

    def get(self, off):
        end = self._buf.index(b"\0", off)
        return self._buf[off:end].decode("utf-8")

    def to_bytes(self):
        return bytes(self._buf)


# --------------------------------------------------------------------------
# container


def build_blob(chars, lessons, pool, coord_scale):
    """chars:   [{'codepoint', 'pinyin_off', 'strokes'}] in *textbook* order
       lessons: [{'title_off', 'first_char', 'char_count'}] (textbook order)
       Returns the full blob. CharIndex is emitted sorted by codepoint; an
       OrderTable maps textbook position -> CharIndex slot."""
    char_count = len(chars)
    lesson_count = len(lessons)

    payloads = [encode_char(c["strokes"]) for c in chars]

    # codepoint-sorted index, plus teaching-order -> index-slot map
    by_cp = sorted(range(char_count), key=lambda i: chars[i]["codepoint"])
    slot_of_order = [0] * char_count
    for slot, order_i in enumerate(by_cp):
        slot_of_order[order_i] = slot

    char_index_off = HEADER_SIZE
    lesson_table_off = char_index_off + char_count * CHAR_INDEX_SIZE
    order_table_off = lesson_table_off + lesson_count * LESSON_SIZE
    strings_off = order_table_off + char_count * 2
    strings = pool.to_bytes()
    chardata_off = strings_off + len(strings)

    # data offsets follow codepoint order so the index is a plain sorted array
    data_offsets = {}
    cursor = 0
    for slot, order_i in enumerate(by_cp):
        data_offsets[order_i] = cursor
        cursor += len(payloads[order_i])
    total_size = chardata_off + cursor

    out = bytearray()
    out += struct.pack("<4sHHHHIIIII", MAGIC, VERSION, coord_scale, char_count,
                       lesson_count, char_index_off, lesson_table_off,
                       strings_off, chardata_off, total_size)
    assert len(out) == HEADER_SIZE, len(out)

    for order_i in by_cp:
        c = chars[order_i]
        out += struct.pack("<HHI", c["codepoint"], c["pinyin_off"],
                           data_offsets[order_i])
    for lsn in lessons:
        out += struct.pack("<HHH", lsn["title_off"], lsn["first_char"],
                           lsn["char_count"])
    for order_i in range(char_count):
        out += struct.pack("<H", slot_of_order[order_i])
    out += strings
    for order_i in by_cp:
        out += payloads[order_i]

    assert len(out) == total_size, (len(out), total_size)
    return bytes(out)


def parse_blob(buf):
    """Mirror of build_blob, used by the round-trip check."""
    (magic, version, coord_scale, char_count, lesson_count, char_index_off,
     lesson_table_off, strings_off, chardata_off,
     total_size) = struct.unpack_from("<4sHHHHIIIII", buf, 0)
    if magic != MAGIC:
        raise FormatError(f"bad magic {magic!r}")
    if version != VERSION:
        raise FormatError(f"unsupported version {version}")
    if total_size != len(buf):
        raise FormatError(f"total_size={total_size} but blob is {len(buf)} bytes")

    order_table_off = lesson_table_off + lesson_count * LESSON_SIZE

    def string_at(rel):
        start = strings_off + rel
        end = buf.index(b"\0", start)
        return buf[start:end].decode("utf-8")

    index = []
    for i in range(char_count):
        cp, pinyin_off, data_off = struct.unpack_from(
            "<HHI", buf, char_index_off + i * CHAR_INDEX_SIZE)
        index.append({"codepoint": cp, "pinyin": string_at(pinyin_off),
                      "data_off": data_off})

    chars_by_slot = []
    for entry in index:
        strokes, _ = decode_char(buf, chardata_off + entry["data_off"])
        chars_by_slot.append({**entry, "strokes": strokes})

    order = []
    for i in range(char_count):
        (slot,) = struct.unpack_from("<H", buf, order_table_off + i * 2)
        order.append(chars_by_slot[slot])

    lessons = []
    for i in range(lesson_count):
        title_off, first_char, cnt = struct.unpack_from(
            "<HHH", buf, lesson_table_off + i * LESSON_SIZE)
        lessons.append({"title": string_at(title_off), "first_char": first_char,
                        "char_count": cnt})

    return {"coord_scale": coord_scale, "order": order, "lessons": lessons}
