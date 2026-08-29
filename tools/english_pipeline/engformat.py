"""
ENG1 -- the binary the English app reads.

Same bargain as HZS1 next door: one blob in .rodata, no filesystem, no
allocation to read it. The firmware decoder in
main/apps/app_english/data/eng_data.cpp mirrors this file field for field, so
any change here is a change there.

Layout
------
  Header          40 B, at offset 0
  WordIndex[]     16 B each, sorted by unit then by teaching order
  UnitTable[]      8 B each
  Strings          NUL-terminated UTF-8, deduplicated
  Blobs            images and audio, each 4-byte aligned

Header (40 B, little endian)
  char[4]  magic          "ENG1"
  u16      version        1
  u16      word_count
  u16      unit_count
  u16      image_w        every image is this wide  (160)
  u16      image_h        and this tall             (160)
  u16      audio_rate     ADPCM sample rate         (16000)
  u32      word_index_off
  u32      unit_table_off
  u32      strings_off
  u32      blobs_off
  u32      total_size
  u32      reserved       0

WordIndex (16 B)
  u16 text_off     offset into Strings -- the English word, lower case
  u16 zh_off       offset into Strings -- the Chinese gloss
  u32 image_off    offset from blobs_off, or 0 for none
  u32 audio_off    offset from blobs_off, or 0 for none (spoken word)
  u32 sfx_off      offset from blobs_off, or 0 for none (moo, beep, rain)

UnitTable (8 B)
  u16 title_off    offset into Strings -- Chinese unit name, e.g. "动物"
  u16 first_word   index of this unit's first WordIndex entry
  u16 word_count
  u16 flags        bit0 = enough words carry sfx to play the listen-and-pick game

Image blob (4-byte aligned)
  u16 w
  u16 h
  u32 palette[16]  LVGL wants the palette immediately before the pixels, so
                   lv_image_dsc_t.data points *here*, not at the pixels.
                   Each entry is B,G,R,A bytes -- lv_color32_t's memory order.
  u8  pixels[w*h/2]  two 4-bit indices per byte, high nibble first

  data_size for LVGL = 64 + w*h/2

Audio blob (4-byte aligned)
  u32 sample_count   decoded 16-bit samples at audio_rate
  i16 predictor      IMA ADPCM initial predictor
  u8  step_index     IMA ADPCM initial step index
  u8  reserved       0
  u8  nibbles[(sample_count+1)/2]   two 4-bit codes per byte, low nibble first

Alignment
---------
Xtensa raises LoadStoreAlignmentError on an unaligned 16- or 32-bit access, and
the string and index regions are byte-packed. The firmware decoder therefore
reads every multi-byte field through explicit byte assembly (see rd16/rd32 in
eng_data.cpp) rather than casting a pointer. Blobs are the exception: they are
padded to a 4-byte boundary precisely so the palette can be handed to LVGL as a
lv_color32_t array, and so the audio header can be read the same cheap way.
"""

import struct

MAGIC = b"ENG1"
VERSION = 1

HEADER_SIZE = 40
WORD_ENTRY_SIZE = 16
UNIT_ENTRY_SIZE = 8

# Every image ships at this size, and the size is set by geometry, not taste.
#
# The quiz shows two pictures side by side, and they have to sit under the two
# bezel buttons without any corner escaping the 233 px visible radius. At 144
# the cards land 197 px from centre with 16 px of black between them; at 160
# the same layout puts a corner at 248 px -- off the glass -- and squeezes the
# gap to 4 px. 144 is the largest size that leaves the pair legible.
#
# One image is then 10,432 B (palette + pixels), so a 12-word unit costs about
# 125 KB of pictures.
IMAGE_W = 144
IMAGE_H = 144

# 16 colours is plenty for the flat cartoon artwork the picture libraries
# produce, and I4 is decoded by LVGL's core -- no TJPGD, no LODEPNG. That
# matters because the host simulator builds with both decoders switched off,
# so this is the only colour format that renders identically on the device and
# in tools/english_host_test.
PALETTE_SIZE = 16
IMAGE_DATA_SIZE = IMAGE_W * IMAGE_H // 2
IMAGE_BLOB_SIZE = 4 + PALETTE_SIZE * 4 + IMAGE_DATA_SIZE

# Speech sits almost entirely below 8 kHz, so 16 kHz costs nothing audible on a
# single spoken word and puts one word at ~8 KB -- the same size the original
# 44.1 kHz MP3 would have been, but decodable in about a hundred lines with no
# external component. The codec runs at 44100, so playback upsamples.
AUDIO_RATE = 16000

UNIT_FLAG_HAS_SFX = 0x1


def pack_header(word_count, unit_count, word_index_off, unit_table_off,
                strings_off, blobs_off, total_size):
    return struct.pack(
        "<4sHHHHHHIIIIII",
        MAGIC, VERSION, word_count, unit_count,
        IMAGE_W, IMAGE_H, AUDIO_RATE,
        word_index_off, unit_table_off, strings_off, blobs_off,
        total_size, 0,
    )


def pack_word(text_off, zh_off, image_off, audio_off, sfx_off):
    return struct.pack("<HHIII", text_off, zh_off, image_off, audio_off, sfx_off)


def pack_unit(title_off, first_word, word_count, flags):
    return struct.pack("<HHHH", title_off, first_word, word_count, flags)


def pack_image_blob(palette, pixels):
    """palette: 16 (r,g,b,a) tuples. pixels: bytes, two 4-bit indices each."""
    assert len(palette) == PALETTE_SIZE, len(palette)
    assert len(pixels) == IMAGE_DATA_SIZE, len(pixels)
    out = bytearray(struct.pack("<HH", IMAGE_W, IMAGE_H))
    for (r, g, b, a) in palette:
        # lv_color32_t is {blue, green, red, alpha} in memory.
        out += struct.pack("<BBBB", b, g, r, a)
    out += pixels
    return bytes(out)


def pack_audio_blob(sample_count, predictor, step_index, nibbles):
    head = struct.pack("<IhBB", sample_count, predictor, step_index, 0)
    return head + nibbles


assert struct.calcsize("<4sHHHHHHIIIIII") == HEADER_SIZE
assert struct.calcsize("<HHIII") == WORD_ENTRY_SIZE
assert struct.calcsize("<HHHH") == UNIT_ENTRY_SIZE
