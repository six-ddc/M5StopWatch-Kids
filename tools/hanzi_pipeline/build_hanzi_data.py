#!/usr/bin/env python3
"""Build the embedded hanzi stroke-order dataset for AppHanzi.

Sources (both cached under .cache/, safe to re-run). Both endpoints have
working defaults; override with STROKE_URL / BOOK_URL, each a format string
with one {} placeholder. Once .cache/ is warm neither is needed again.
  * stroke data  -- hanzi-writer-data@2.0.1, derived from Make Me a Hanzi,
                    itself derived from the Arphic PL fonts under the Arphic
                    Public License. Per-character JSON: a `strokes` list of
                    SVG paths in a 1024-unit box with the baseline at y=900.
  * character set -- primary-school textbook lists from shukong-app. Each
                    lesson has a `recognition` field (识字表, characters
                    pupils must recognise) and a `writing` field (写字表,
                    characters pupils must be able to write); both carry
                    per-character pinyin. --field controls which are used
                    (default `both`: recognition chars first, then writing
                    chars not already covered, per lesson, in textbook order).

Outputs:
  main/assets/hanzi/hanzi_data.c   blob as a const array in .rodata
  main/assets/hanzi/hanzi_data.h   declaration
  main/assets/fonts/charset_hanzi_ui.txt   charset for lv_font_conv
  .cache/golden/<cp>.png           reference renders for the M2 host diff

Usage:
  python3 tools/hanzi_pipeline/build_hanzi_data.py
  python3 tools/hanzi_pipeline/build_hanzi_data.py --no-golden --limit 50
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hzformat as fmt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CACHE = os.path.join(HERE, ".cache")
STROKE_CACHE = os.path.join(CACHE, "strokes")
BOOK_CACHE = os.path.join(CACHE, "books")
GOLDEN_DIR = os.path.join(CACHE, "golden")

# Format strings with one {} placeholder: the character for STROKE_URL, the
# volume code for BOOK_URL. Override either through the environment.
#
#   strokes -- hanzi-writer-data, derived from Make Me a Hanzi, whose glyph
#              outlines come from the Arphic PL fonts (Arphic Public License).
#   books   -- textbook character lists with per-character pinyin, from the
#              shukong-app data set (MIT).
#
# A warm .cache/ needs neither; they are only hit when a character or a volume
# is missing locally.
STROKE_URL = os.environ.get(
    "STROKE_URL",
    "https://cdn.jsdelivr.net/npm/hanzi-writer-data@2.0.1/{}.json")
BOOK_URL = os.environ.get(
    "BOOK_URL",
    "https://raw.githubusercontent.com/vipzhicheng/shukong-app/HEAD"
    "/public/books/renjiao/{}.json")

# 一年级上册 .. 三年级上册; the writing lists alone accumulate to 1037
# characters, recognition+writing merged accumulates to ~1847.
DEFAULT_BOOKS = ["111", "121", "211", "221", "311"]
DEFAULT_FIELD = "both"

SOURCE_BOX = 1024          # source coordinate box
SOURCE_BASELINE = 900      # y axis points up, baseline at 900
COORD_SCALE = 512          # quantised box we store
GOLDEN_SIZE = 160          # half the on-device 320px cell; keeps the set ~26 MB
GOLDEN_SS = 4              # supersampling factor for the reference render
BLOB_LIMIT = 1300 * 1024   # hard gate; app partition is 11 MB, room to spare

# Fixed UI strings that must be covered by the subset font. Every piece of
# chrome in both apps has to appear here -- a character missing from the subset
# renders as a silent box, and nothing in the build catches it.
UI_STRINGS = [
    # stroke-order app
    "笔顺", "返回", "重播", "速度", "慢", "标准", "快",
    "第", "笔", "字", "课", "共", "页", "无数据",
    # launcher app names
    "识字", "算术",
    # arithmetic quiz
    "连对", "错题", "答对", "道", "开始",
    # arithmetic result screen
    "太棒了", "真不错", "继续加油", "别灰心", "再来一关",
    "A 再来一关   B 地图",
    # difficulty tier names, mirroring math::levelName() in
    # main/apps/app_math/game/problem.cpp
    "10 以内加法", "10 以内减法", "20 以内进退位",
    "整十数加减", "两位数加减一位数", "两位数加减两位数",
    # arithmetic gamification (star map / unlockable playgrounds)
    "星尘", "金星题", "地图", "最佳",
    "解锁新玩法", "已解锁", "对不对", "玩法", "点一下继续",
    "数字花园", "数字池塘", "进位山洞", "整十高山", "大数森林", "大数海洋",
    # Button hints are one label per button, placed under the button it names,
    # so they are listed the way they are rendered rather than as the single
    # bottom line they used to share.
    "A 换一关", "B 开始",
    # English app chrome. The word glosses are deliberately NOT listed here --
    # they come from tools/english_pipeline/wordlist.py via english_glyphs(),
    # so adding a word there is all it takes to keep the font in step.
    "英语",
    "认一认", "听一听", "看一看", "选一选", "点一下再听",
    "学完了", "需复习", "本组",
    "A 重播", "B 下一个",
    "A 再来一次", "B 换一组", "A 听一听", "B 下一组",
]
# Pinyin needs toned Latin vowels on top of plain ASCII.
PINYIN_EXTRA = "āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜüńňǹḿ"


def log(msg):
    print(msg, flush=True)


# --------------------------------------------------------------------------
# fetching


def fetch(url_tmpl, key, path, what):
    """Fetch one cache entry. An emptied-out endpoint is a clear message
    rather than a mangled URL."""
    if not url_tmpl:
        raise SystemExit(
            f"{path} is not cached and {what} is empty. "
            f"Set {what} to a URL template with one {{}} placeholder.")
    url = url_tmpl.format(key)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    r = subprocess.run(["curl", "-sfL", "-m", "20", "-o", path, url],
                       capture_output=True)
    return r.returncode == 0 and os.path.getsize(path) > 0


def load_book(code):
    path = os.path.join(BOOK_CACHE, f"book_{code}.json")
    if not os.path.exists(path) and not fetch(BOOK_URL, code, path, "BOOK_URL"):
        raise SystemExit(f"cannot fetch textbook {code}")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_strokes(char):
    path = os.path.join(STROKE_CACHE, f"{char}.json")
    if not os.path.exists(path):
        if not fetch(STROKE_URL, char, path, "STROKE_URL"):
            if os.path.exists(path):
                os.remove(path)
            return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def lesson_chars(group):
    """group: {'lesson', 'characters': [{'character', 'pinyin'}, ...]} ->
    [(char, pinyin), ...], de-duplicated within the group, order kept."""
    out = []
    dup = set()
    for item in group.get("characters", []):
        c = item.get("character", "")
        if len(c) != 1 or c in dup:
            continue
        dup.add(c)
        out.append((c, item.get("pinyin", "")))
    return out


def volume_lesson_groups(vol, field):
    """-> [(lesson_key, [(char, pinyin), ...])] in textbook order for one
    volume, per --field. 'both' merges recognition (识字表) and writing
    (写字表) per lesson, recognition characters first, driven by the
    recognition list's order since writing's lesson keys are always an
    order-preserving subsequence of recognition's (verified across the
    一~三年级上册 volumes)."""
    recognition = vol.get("recognition", [])
    writing = vol.get("writing", [])
    if field == "writing":
        return [(g.get("lesson", ""), lesson_chars(g)) for g in writing]
    if field == "recognition":
        return [(g.get("lesson", ""), lesson_chars(g)) for g in recognition]

    writing_by_key = {}
    for g in writing:
        writing_by_key.setdefault(g.get("lesson", ""), []).append(g)
    out = []
    handled_writing_keys = set()
    for g in recognition:
        key = g.get("lesson", "")
        merged = OrderedDict(lesson_chars(g))
        for wg in writing_by_key.get(key, []):
            for c, py in lesson_chars(wg):
                merged.setdefault(c, py)
        handled_writing_keys.add(key)
        out.append((key, list(merged.items())))
    # Fallback: a writing lesson whose key never appeared in recognition
    # (not observed in the volume data, but keeps the merge total).
    for g in writing:
        key = g.get("lesson", "")
        if key in handled_writing_keys:
            continue
        handled_writing_keys.add(key)
        out.append((key, lesson_chars(g)))
    return out


def build_char_list(books, limit, field=DEFAULT_FIELD):
    """-> (ordered [(char, pinyin)], lessons [(title, [chars])])"""
    seen = OrderedDict()
    lessons = []
    for code in books:
        book = load_book(code)
        for grade in book.get("grades", []):
            for vol in grade.get("volumes", []):
                titles = vol.get("lessons", {})
                for key, items in volume_lesson_groups(vol, field):
                    fresh = []
                    for c, pinyin in items:
                        if c in seen:
                            continue
                        seen[c] = pinyin
                        fresh.append(c)
                    if fresh:
                        title = titles.get(key, "")
                        label = f"{key}·{title}" if title else key
                        lessons.append((label, fresh))
    ordered = list(seen.items())
    if limit:
        keep = {c for c, _ in ordered[:limit]}
        ordered = ordered[:limit]
        lessons = [(t, [c for c in cs if c in keep]) for t, cs in lessons]
        lessons = [(t, cs) for t, cs in lessons if cs]
    return ordered, lessons


# --------------------------------------------------------------------------
# geometry


PATH_TOKEN = re.compile(r"([MLQCZ])([^MLQCZ]*)")
NUMBER = re.compile(r"-?\d+(?:\.\d+)?")


def parse_path(path):
    """SVG subset -> [(cmd, [(x, y), ...])] in source coordinates."""
    out = []
    for cmd, args in PATH_TOKEN.findall(path):
        nums = [float(v) for v in NUMBER.findall(args)]
        if len(nums) % 2:
            raise ValueError(f"odd coordinate count in {cmd}{args!r}")
        out.append((cmd, [(nums[i], nums[i + 1]) for i in range(0, len(nums), 2)]))
    return out


def to_storage(pt):
    """Source coords -> quantised storage coords: flip y, then scale."""
    x, y = pt
    k = COORD_SCALE / SOURCE_BOX
    return (int(round(x * k)), int(round((SOURCE_BASELINE - y) * k)))


def flatten_quad(p0, p1, p2, steps):
    pts = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        pts.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))
    return pts


def flatten_cubic(p0, p1, p2, p3, steps):
    pts = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        pts.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                    a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return pts


def outline_polygon(ops, outline, steps=12):
    """Storage-space control points -> dense polygon (for radius + golden)."""
    poly = [outline[0]]
    cursor = 1
    for op in ops:
        if op == fmt.OP_L:
            poly.append(outline[cursor])
            cursor += 1
        elif op == fmt.OP_Q:
            poly += flatten_quad(poly[-1], outline[cursor], outline[cursor + 1], steps)
            cursor += 2
        else:
            poly += flatten_cubic(poly[-1], outline[cursor], outline[cursor + 1],
                                  outline[cursor + 2], steps)
            cursor += 3
    return poly


def point_segment_distance(p, a, b):
    px, py = p
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    span = dx * dx + dy * dy
    if span <= 0.0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / span))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def brush_radius(polygon, median):
    """Widest half-width along the median: for each median point the distance
    to the nearest outline edge, maximised over the median."""
    best = 0.0
    n = len(polygon)
    for p in median:
        near = min(point_segment_distance(p, polygon[i], polygon[(i + 1) % n])
                   for i in range(n))
        best = max(best, near)
    return max(1, min(255, int(math.ceil(best))))


# --------------------------------------------------------------------------
# normalisation


class SkipChar(Exception):
    pass


def normalise_char(raw):
    """Source JSON -> list of encodable strokes (storage coordinates)."""
    strokes = []
    for path, median_src in zip(raw["strokes"], raw["medians"]):
        segments = parse_path(path)
        if not segments or segments[0][0] != "M":
            raise SkipChar("path does not start with M")
        if [c for c, _ in segments].count("M") != 1 or segments[-1][0] != "Z":
            raise SkipChar("stroke is not a single closed subpath")

        outline = [to_storage(segments[0][1][0])]
        ops = []
        for cmd, pts in segments[1:-1]:
            ops.append(fmt.OP_FROM_SVG[cmd])
            outline += [to_storage(p) for p in pts]

        # A trailing point identical to the start is redundant: Z closes it.
        while len(ops) > 1 and outline[-1] == outline[0] and ops[-1] == fmt.OP_L:
            ops.pop()
            outline.pop()

        median = [to_storage(p) for p in median_src]
        deduped = [median[0]]
        for p in median[1:]:
            if p != deduped[-1]:
                deduped.append(p)
        if len(deduped) < 2:
            deduped = median[:2] if len(median) >= 2 else median * 2
        median = deduped

        polygon = outline_polygon(ops, outline)
        strokes.append({
            "ops": ops,
            "outline": outline,
            "median": median,
            "brush_radius": brush_radius(polygon, median),
        })
    return strokes


# --------------------------------------------------------------------------
# outputs


def render_golden(strokes, path):
    """Reference render, written as binary PGM so the host test can read it
    without a PNG decoder. PIL's polygon fill is an independent implementation
    from the engine's scanline rasteriser, which is what makes the diff useful.
    Box-downsampling an SSxSS render is exact supersampling."""
    from PIL import Image, ImageDraw

    big = GOLDEN_SIZE * GOLDEN_SS
    k = big / COORD_SCALE
    img = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(img)
    for s in strokes:
        poly = outline_polygon(s["ops"], s["outline"], steps=16)
        draw.polygon([(x * k, y * k) for x, y in poly], fill=255)
    img.resize((GOLDEN_SIZE, GOLDEN_SIZE), Image.BOX).save(path)


def emit_c_array(blob, out_c, out_h):
    os.makedirs(os.path.dirname(out_c), exist_ok=True)
    banner = ("/*\n"
              " * Generated by tools/hanzi_pipeline/build_hanzi_data.py -- do not edit.\n"
              " *\n"
              " * Stroke data derived from Make Me a Hanzi via hanzi-writer-data,\n"
              " * whose outlines come from the Arphic PL fonts under the Arphic\n"
              " * Public License. Character lists and pinyin from shukong-app.\n"
              " */\n")
    with open(out_h, "w", encoding="utf-8") as f:
        f.write(banner)
        f.write("#pragma once\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
        f.write("extern const unsigned char hanzi_data_blob[];\n")
        f.write("extern const unsigned int hanzi_data_blob_size;\n")
        f.write("\n#ifdef __cplusplus\n}\n#endif\n")
    with open(out_c, "w", encoding="utf-8") as f:
        f.write(banner)
        f.write('#include "hanzi_data.h"\n\n')
        # 4-byte alignment lets the header/index be read as words; stroke
        # payloads are still byte-packed and must be read byte-wise.
        f.write("__attribute__((aligned(4)))\n")
        f.write("const unsigned char hanzi_data_blob[] = {\n")
        for i in range(0, len(blob), 16):
            row = ", ".join(f"0x{b:02X}" for b in blob[i:i + 16])
            f.write(f"    {row},\n")
        f.write("};\n\n")
        f.write(f"const unsigned int hanzi_data_blob_size = {len(blob)};\n")


def english_glyphs():
    """Chinese glosses owned by the English app's word list.

    They live in tools/english_pipeline/wordlist.py, which is their single
    source of truth -- copying the characters into UI_STRINGS would mean every
    new word needs editing in two places, and the failure mode when someone
    forgets is a silent row of boxes on the device. Importing instead means
    adding a word there is enough.

    A missing English pipeline is not an error: this repo's hanzi build has to
    keep working on its own.
    """
    try:
        here = os.path.dirname(os.path.abspath(__file__))
        eng = os.path.join(os.path.dirname(here), "english_pipeline")
        if eng not in sys.path:
            sys.path.insert(0, eng)
        import wordlist  # noqa: PLC0415 -- optional, resolved at call time

        return set(wordlist.chinese_glyphs())
    except Exception as exc:  # pragma: no cover - diagnostics only
        print(f"  note: English word list not read ({exc}); its glosses are not "
              f"in the subset font")
        return set()


def emit_charset(lessons, path):
    chars = set()
    for text in UI_STRINGS:
        chars.update(text)
    for title, _ in lessons:
        chars.update(title)
    chars.update(PINYIN_EXTRA)
    chars.update(english_glyphs())
    # Lower-case Latin carries the pinyin; upper-case names the buttons in the
    # arithmetic app's hints ("A 再来一关"). The full stop is what
    # LV_LABEL_LONG_DOT renders as an ellipsis, and the space has to exist as a
    # real glyph or the label draws a placeholder box for it.
    chars.update("abcdefghijklmnopqrstuvwxyz")
    chars.update("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    chars.update("0123456789/·-.,:!? ")
    ordered = "".join(sorted(chars))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(ordered + "\n")
    return ordered


# --------------------------------------------------------------------------


def round_trip(blob, chars, lessons):
    parsed = fmt.parse_blob(blob)
    if parsed["coord_scale"] != COORD_SCALE:
        raise SystemExit("round-trip: coord_scale mismatch")
    if len(parsed["order"]) != len(chars):
        raise SystemExit("round-trip: character count mismatch")
    for i, (want, got) in enumerate(zip(chars, parsed["order"])):
        if want["codepoint"] != got["codepoint"]:
            raise SystemExit(f"round-trip: char {i} codepoint mismatch")
        if want["pinyin"] != got["pinyin"]:
            raise SystemExit(f"round-trip: char {i} pinyin mismatch")
        if len(want["strokes"]) != len(got["strokes"]):
            raise SystemExit(f"round-trip: char {i} stroke count mismatch")
        for j, (ws, gs) in enumerate(zip(want["strokes"], got["strokes"])):
            for key in ("ops", "outline", "median", "brush_radius"):
                if ws[key] != gs[key]:
                    raise SystemExit(
                        f"round-trip: char {i} stroke {j} field {key} mismatch")
    for want, got in zip(lessons, parsed["lessons"]):
        if want["title"] != got["title"] or want["first_char"] != got["first_char"] \
                or want["char_count"] != got["char_count"]:
            raise SystemExit("round-trip: lesson table mismatch")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--books", nargs="*", default=DEFAULT_BOOKS)
    ap.add_argument("--field", choices=["writing", "recognition", "both"],
                    default=DEFAULT_FIELD,
                    help="character list source: 写字表, 识字表, or both merged "
                         "(default: both, recognition chars first per lesson)")
    ap.add_argument("--limit", type=int, default=0, help="cap character count")
    ap.add_argument("--no-golden", action="store_true")
    ap.add_argument("--out-dir", default=os.path.join(REPO, "main", "assets"))
    args = ap.parse_args()

    ordered, lesson_groups = build_char_list(args.books, args.limit, args.field)
    log(f"character list: {len(ordered)} chars in {len(lesson_groups)} lessons")

    pool = fmt.StringPool()
    chars, index_of = [], {}
    skipped = []
    for char, pinyin in ordered:
        raw = load_strokes(char)
        if raw is None:
            skipped.append((char, "no stroke data"))
            continue
        try:
            strokes = normalise_char(raw)
        except SkipChar as exc:
            skipped.append((char, str(exc)))
            continue
        index_of[char] = len(chars)
        chars.append({
            "codepoint": ord(char),
            "pinyin": pinyin,
            "pinyin_off": pool.intern(pinyin),
            "strokes": strokes,
        })
    if skipped:
        log(f"skipped {len(skipped)}: " +
            ", ".join(f"{c}({why})" for c, why in skipped[:10]))

    lessons = []
    for title, group in lesson_groups:
        members = [index_of[c] for c in group if c in index_of]
        if not members:
            continue
        first, count = min(members), len(members)
        if members != list(range(first, first + count)):
            raise SystemExit(f"lesson {title!r} is not a contiguous range")
        lessons.append({"title": title, "title_off": pool.intern(title),
                        "first_char": first, "char_count": count})

    blob = fmt.build_blob(chars, lessons, pool, COORD_SCALE)
    log(f"blob: {len(blob)} bytes ({len(blob) / 1024:.1f} KiB), "
        f"{len(blob) / max(1, len(chars)):.0f} B/char")

    round_trip(blob, chars, lessons)
    log("round-trip: OK")

    if len(blob) > BLOB_LIMIT:
        raise SystemExit(f"blob {len(blob)} B exceeds gate {BLOB_LIMIT} B")

    emit_c_array(blob,
                 os.path.join(args.out_dir, "hanzi", "hanzi_data.c"),
                 os.path.join(args.out_dir, "hanzi", "hanzi_data.h"))
    # Raw blob for the host test, which binds it directly instead of compiling
    # the 3.7 MB C array.
    os.makedirs(CACHE, exist_ok=True)
    with open(os.path.join(CACHE, "hanzi_data.bin"), "wb") as f:
        f.write(blob)
    charset = emit_charset(lesson_groups,
                           os.path.join(args.out_dir, "fonts", "charset_hanzi_ui.txt"))
    log(f"charset: {len(charset)} glyphs")

    if not args.no_golden:
        os.makedirs(GOLDEN_DIR, exist_ok=True)
        for c in chars:
            render_golden(c["strokes"],
                          os.path.join(GOLDEN_DIR, f"{c['codepoint']:04X}.pgm"))
        log(f"golden: {len(chars)} renders at {GOLDEN_SIZE}px in {GOLDEN_DIR}")

    strokes_total = sum(len(c["strokes"]) for c in chars)
    log(f"done: {len(chars)} chars, {strokes_total} strokes, "
        f"{strokes_total / max(1, len(chars)):.1f} strokes/char")


if __name__ == "__main__":
    main()
