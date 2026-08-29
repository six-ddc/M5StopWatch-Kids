#!/usr/bin/env python3
"""Build the embedded English word dataset for AppEnglish.

Inputs (both cached under .cache/, safe to re-run):
  * pictures -- ARASAAC's open pictogram API, no key needed. Ids are pinned in
                wordlist.py, so the build does not depend on search ranking.
                Override the endpoint with PICTO_API.
  * audio    -- a local dictionary mdd/mdx pair. This one is a path on your
                machine, so it has no useful default: give it with
                --dict-mdd / --dict-mdx or $DICT_MDD / $DICT_MDX. Without it
                the build still runs, the words just ship mute.

Pipeline:
  mdd mp3 -> ffmpeg (16 kHz mono s16le) -> trim + peak normalise -> IMA ADPCM
  source png -> 144x144 on a black matte -> 16-colour palette -> I4

Outputs:
  main/assets/english/english_data.c   blob as a const array in .rodata
  main/assets/english/english_data.h   declaration
  tools/english_pipeline/manifest.json per-word build record (picture id,
                                       which recording was used and why) plus a
                                       `review` block: missing pictures,
                                       missing audio, odd-looking tiles
  .cache/english_data.bin              raw blob, for a future host test
  .cache/preview/sheet_NN.png          captioned contact sheets, 60 words each,
                                       of every image as the device will
                                       actually render it

Usage:
  python3 tools/english_pipeline/build_english_data.py
  python3 tools/english_pipeline/build_english_data.py --no-cache
  python3 tools/english_pipeline/build_english_data.py --skip-audio
  python3 tools/english_pipeline/build_english_data.py --dict-mdd ~/dict.mdd
"""

import argparse
import io
import json
import os
import re
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engformat as fmt
import wordlist

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CACHE = os.path.join(HERE, ".cache")
# Labels on the contact sheets carry Chinese glosses, so the preview needs the
# same typeface the device uses: LXGW WenKai (SIL OFL), a kai face matching the
# stroke data's style -- https://github.com/lxgw/LxgwWenKai. Drop the .ttf in
# the path below, or point UI_FONT at it.
PREVIEW_FONT = os.environ.get("UI_FONT") or os.path.join(
    REPO, "tools", "hanzi_pipeline", ".cache", "fonts", "LXGWWenKai-Regular.ttf")
# Words per contact sheet. 480 pictures on one image is unreadable; 60 fits a
# screen at a size where a wrong picture is still obvious.
PREVIEW_PER_SHEET = 60
PREVIEW_COLS = 10
PICT_CACHE = os.path.join(CACHE, "pictograms")
SEARCH_CACHE = os.path.join(CACHE, "search")
AUDIO_CACHE = os.path.join(CACHE, "audio_src")

# Audio comes out of a local dictionary mdd, with its mdx alongside for the
# entries whose recording is only reachable through the entry HTML. Neither
# path is hardcoded: pass --dict-mdd / --dict-mdx, or set DICT_MDD / DICT_MDX.
# Without them the build still runs, it just skips audio.
MDD_PATH = os.environ.get("DICT_MDD", "")
MDX_PATH = os.environ.get("DICT_MDX", "")

# Pictogram library. ARASAAC is a free AAC symbol set with an open REST API
# and no key; ids are pinned per word in wordlist.py. Any service with the same
# two shapes drops in: a keyword search returning objects that carry an `_id`,
# and an id -> PNG fetch. Override with PICTO_API.
#
# A normal build never touches this -- every picture the word list pins is
# already in .cache/pictograms/. It is only used to add words or refetch.
PICTO_API = os.environ.get("PICTO_API", "https://api.arasaac.org/api/pictograms")
PICTO_SEARCH = PICTO_API + "/en/search/{}"
PICTO_IMAGE = PICTO_API + "/{}?resolution=500"


def require_picto_api():
    if not PICTO_API:
        raise RuntimeError(
            "PICTO_API is empty -- point it at a pictogram service to fetch "
            "new pictures. Cached pictures under .cache/pictograms/ still build.")
USER_AGENT = "M5StopWatch-Kids-english-pipeline/1.0"

FFMPEG = "/opt/homebrew/bin/ffmpeg"
if not os.path.exists(FFMPEG):
    FFMPEG = "ffmpeg"

# Silence gate for the trim, in absolute 16-bit sample value. Dictionary clips
# are already tight; this just shaves the encoder ramp off the front and any tail
# the MP3 decoder padded on.
TRIM_FLOOR = 300
TRIM_GUARD_MS = 20
# Peak normalisation target. The device speaker is small and these recordings
# vary by a good 10 dB between words; capping the gain keeps a quiet-but-noisy
# clip from having its noise floor pulled up.
PEAK_TARGET = 0.90
PEAK_MAX_GAIN = 8.0

# Whole blob has to sit in .rodata next to the 1.2 MB hanzi blob. The factory
# partition is 0xB00000 = 11 MB and everything else in the image (code, LVGL,
# fonts, the hanzi and maths assets) came to 2.7 MB the last time this was
# measured, so the real ceiling is about 8.8 MB. 8 MB leaves a megabyte of
# slack: still a smoke alarm rather than a wall, but a smoke alarm that goes
# off before the linker does.
BLOB_LIMIT = 8 * 1024 * 1024


def log(msg):
    print(msg, flush=True)


def warn(msg):
    print(f"WARNING: {msg}", flush=True)


# --------------------------------------------------------------------------
# IMA ADPCM
#
# The reference codec, 4 bits per sample, no block headers: the whole clip is
# one run, seeded by the predictor/step_index the audio blob carries. Written
# out longhand because it has to stay bit-identical to the C decoder in
# main/apps/app_english/data/eng_audio.cpp -- adpcm_decode() below is that
# decoder, and encode() is checked against it on every run.

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
assert len(STEP_TABLE) == 89 and len(INDEX_TABLE) == 16


def adpcm_encode(samples, predictor=0, index=0):
    """[int16] -> (nibbles bytes, predictor, index). Two codes per byte, low
    nibble first, which is what the format doc specifies and what every other
    IMA implementation does."""
    out = bytearray((len(samples) + 1) // 2)
    pred, idx = predictor, index
    for n, sample in enumerate(samples):
        step = STEP_TABLE[idx]
        diff = sample - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        # Greedy 3-bit magnitude: step, step/2, step/4.
        if diff >= step:
            code |= 4
            diff -= step
        step >>= 1
        if diff >= step:
            code |= 2
            diff -= step
        step >>= 1
        if diff >= step:
            code |= 1

        # Reconstruct exactly the way the decoder will, so the encoder's
        # predictor never drifts from the decoder's.
        step = STEP_TABLE[idx]
        delta = step >> 3
        if code & 4:
            delta += step
        if code & 2:
            delta += step >> 1
        if code & 1:
            delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + INDEX_TABLE[code]))

        if n % 2 == 0:
            out[n // 2] = code
        else:
            out[n // 2] |= code << 4
    return bytes(out), predictor, index


def adpcm_decode(nibbles, sample_count, predictor=0, index=0):
    """Mirror of adpcm_encode, used to verify the encode round-trip."""
    out = []
    pred, idx = predictor, index
    for n in range(sample_count):
        byte = nibbles[n // 2]
        code = byte & 0xF if n % 2 == 0 else byte >> 4
        step = STEP_TABLE[idx]
        delta = step >> 3
        if code & 4:
            delta += step
        if code & 2:
            delta += step >> 1
        if code & 1:
            delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + INDEX_TABLE[code]))
        out.append(pred)
    return out


def snr_db(original, decoded):
    """Signal-to-noise of the ADPCM round trip. IMA at 4 bits lands around
    20 dB on speech; anything much below that means the codec is wrong, not
    that the clip is hard."""
    import math
    sig = sum(float(s) * s for s in original)
    err = sum((float(a) - b) ** 2 for a, b in zip(original, decoded))
    if err <= 0 or sig <= 0:
        return 99.0
    return 10.0 * math.log10(sig / err)


# --------------------------------------------------------------------------
# audio source: dictionary mdd/mdx


class DictAudio:
    """Locates and extracts one headword recording per word.

    Written against the layout these dictionary bundles usually have, where
    recordings sit in the mdd under predictable names and the entry HTML in
    the mdx links the rest by number.

    The naive assumption -- that the mdd holds en_us_<word>.mp3 for every word
    -- does not hold: the gaps are exactly the words a six-year-old learns
    first (red, blue, milk, horse, eye). Those entries do have audio, it is
    just referenced from the mdx entry HTML by number, e.g.
    <a href="sound://mp3/44936.mp3"><span class="pron type_uk">.

    Worse, where the mdx *does* point at an en_us_ file for such a word, it
    often points at a homophone's recording, which would play the wrong word
    out loud. So a name that does not match the headword is never trusted.
    Preference order:

      1. en_us_<word>.mp3           US, name proves it is the right word
      2. numbered type_us link      US, the entry's own headword recording
      3. en_gb_<word>.mp3           UK, name proves it
      4. numbered type_uk link      UK
      5. nothing -- warn, word ships mute
    """

    # The headword pron block is the first <span class="pron"> in the entry; it
    # ends where the first following <div class=...> starts (word forms).
    PRON_BLOCK = re.compile(r'<span class="pron">(.*?)</span>\s*<div class=', re.S)
    PRON_LINK = re.compile(
        r'href="sound://[^"]*?/([^"/]+\.mp3)"[^>]*>\s*<span class="pron type_(uk|us)"',
        re.I)
    REDIRECT = re.compile(r"^@@@LINK=(.*?)\s*$")

    def __init__(self, mdd_path, mdx_path):
        from mdict_utils.reader import MDD

        self._mdd_path = mdd_path
        self._mdx_path = mdx_path
        self._mdd = MDD(mdd_path)
        # (offset, key) pairs in record order; get_record() needs the offset of
        # the entry and of its successor, so keep the list, not just a set.
        self._keys = list(self._mdd._key_list)
        self._by_name = {}
        for i, (_, key) in enumerate(self._keys):
            name = key.decode("utf-8", "replace").replace("\\", "/").split("/")[-1]
            self._by_name.setdefault(name.lower(), i)
        self._entries = None

    def has(self, name):
        return name.lower() in self._by_name

    def read(self, name):
        """Pull one file out of the mdd without walking the whole archive: the
        key list gives the record offset, and get_record() decompresses just
        the block that contains it."""
        from mdict_utils.reader import get_record

        i = self._by_name[name.lower()]
        offset, key = self._keys[i]
        length = self._keys[i + 1][0] - offset if i + 1 < len(self._keys) else -1
        return get_record(self._mdd, key, offset, length)

    def _load_entries(self, words):
        """One pass over the mdx for the words we need, plus a second pass for
        any @@@LINK redirect targets they name (sun -> Sun.)."""
        from mdict_utils.reader import MDX

        md = MDX(self._mdx_path)
        wanted = {w.lower() for w in words}
        entries = {}
        for k, v in md.items():
            kk = k.decode("utf-8", "replace").lower()
            if kk in wanted:
                entries.setdefault(kk, []).append(v.decode("utf-8", "replace"))

        follow = set()
        for htmls in entries.values():
            for html in htmls:
                m = self.REDIRECT.match(html.strip())
                if m:
                    follow.add(m.group(1).lower())
        follow -= set(entries)
        if follow:
            for k, v in md.items():
                kk = k.decode("utf-8", "replace").lower()
                if kk in follow:
                    entries.setdefault(kk, []).append(v.decode("utf-8", "replace"))
        self._entries = entries

    def _links(self, word):
        """-> [(mp3 name, 'us'|'uk')] from the headword pron block."""
        out = []
        seen = set()
        todo = [word.lower()]
        while todo:
            key = todo.pop(0)
            if key in seen:
                continue
            seen.add(key)
            for html in self._entries.get(key, []):
                m = self.REDIRECT.match(html.strip())
                if m:
                    todo.append(m.group(1).lower())
                    continue
                block = self.PRON_BLOCK.search(html)
                if not block:
                    continue
                for name, kind in self.PRON_LINK.findall(block.group(1)):
                    out.append((name, kind.lower()))
        return out

    def resolve(self, words):
        """-> {word: (mp3 name, accent, how)} for every word that has audio."""
        if self._entries is None:
            self._load_entries(words)
        found = {}
        for word in words:
            direct_us = f"en_us_{word}.mp3"
            direct_gb = f"en_gb_{word}.mp3"
            links = self._links(word)
            numbered_us = [n for n, k in links if k == "us" and n[:-4].isdigit()]
            numbered_uk = [n for n, k in links if k == "uk" and n[:-4].isdigit()]
            if self.has(direct_us):
                found[word] = (direct_us, "us", "en_us name match")
            elif numbered_us and self.has(numbered_us[0]):
                found[word] = (numbered_us[0], "us", "mdx headword type_us")
            elif self.has(direct_gb):
                found[word] = (direct_gb, "uk", "en_gb name match")
            elif numbered_uk and self.has(numbered_uk[0]):
                found[word] = (numbered_uk[0], "uk", "mdx headword type_uk")
        return found


def fetch_audio(source, words, use_cache):
    """-> ({word: mp3 bytes}, {word: provenance dict}). Cached per word, so a
    re-run never touches the 793 MB mdd at all."""
    os.makedirs(AUDIO_CACHE, exist_ok=True)
    meta_path = os.path.join(AUDIO_CACHE, "index.json")
    meta = {}
    if use_cache and os.path.exists(meta_path):
        with open(meta_path, encoding="utf-8") as f:
            meta = json.load(f)

    def cached(w):
        # A word known to have no recording is cached as a null entry, so a
        # re-run does not re-open the mdd just to fail again. --no-cache
        # re-checks everything.
        if not use_cache or w not in meta:
            return False
        return (meta[w].get("file") is None
                or os.path.exists(os.path.join(AUDIO_CACHE, f"{w}.mp3")))

    missing = [w for w in words if not cached(w)]
    if missing:
        if not MDD_PATH or not os.path.exists(MDD_PATH):
            warn("no dictionary mdd given (--dict-mdd / $DICT_MDD) -- "
                 f"{len(missing)} words ship mute")
            return {}, meta
        src = source()
        t = time.time()
        resolved = src.resolve(missing)
        log(f"mdd/mdx: resolved {len(resolved)}/{len(missing)} recordings "
            f"in {time.time() - t:.1f}s")
        for w in missing:
            hit = resolved.get(w)
            if hit is None:
                meta[w] = {"file": None, "accent": None,
                           "picked_by": "no headword recording found"}
                continue
            name, accent, how = hit
            with open(os.path.join(AUDIO_CACHE, f"{w}.mp3"), "wb") as f:
                f.write(src.read(name))
            meta[w] = {"file": name, "accent": accent, "picked_by": how}
        with open(meta_path, "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=1, sort_keys=True)

    mp3s = {}
    for w in words:
        path = os.path.join(AUDIO_CACHE, f"{w}.mp3")
        if meta.get(w, {}).get("file") and os.path.exists(path):
            with open(path, "rb") as f:
                mp3s[w] = f.read()
    return mp3s, meta


def decode_mp3(mp3, word):
    """-> [int16] at fmt.AUDIO_RATE, mono."""
    r = subprocess.run(
        [FFMPEG, "-v", "error", "-i", "pipe:0",
         "-ar", str(fmt.AUDIO_RATE), "-ac", "1", "-f", "s16le", "pipe:1"],
        input=mp3, capture_output=True)
    if r.returncode != 0 or not r.stdout:
        warn(f"{word}: ffmpeg failed ({r.stderr.decode('utf-8', 'replace').strip()})")
        return []
    return list(struct.unpack(f"<{len(r.stdout) // 2}h", r.stdout[:len(r.stdout) // 2 * 2]))


def clean_pcm(samples):
    """Trim the silence either side, then peak-normalise."""
    if not samples:
        return samples
    guard = fmt.AUDIO_RATE * TRIM_GUARD_MS // 1000
    first, last = 0, len(samples) - 1
    while first < len(samples) and abs(samples[first]) < TRIM_FLOOR:
        first += 1
    while last > first and abs(samples[last]) < TRIM_FLOOR:
        last -= 1
    if first >= last:
        return samples
    first = max(0, first - guard)
    last = min(len(samples) - 1, last + guard)
    cut = samples[first:last + 1]

    peak = max(abs(s) for s in cut)
    if peak:
        gain = min(PEAK_MAX_GAIN, PEAK_TARGET * 32767.0 / peak)
        if gain > 1.01:
            cut = [max(-32768, min(32767, int(s * gain))) for s in cut]
    return cut


# --------------------------------------------------------------------------
# image source: pictogram library


class NotFound(Exception):
    """The library answers 404 for a word it has no pictogram for -- that is an
    answer, not a failure, so it must not be retried."""


def http_get(url, tries=3):
    last = None
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                raise NotFound(url)
            last = exc
            time.sleep(0.5 * (attempt + 1))
        except (urllib.error.URLError, OSError) as exc:
            last = exc
            time.sleep(0.5 * (attempt + 1))
    raise RuntimeError(f"GET {url} failed after {tries} tries: {last}")


def search_pictogram(word, use_cache, sleep):
    """-> pictogram id or None. Only reached for words wordlist.py did not pin."""
    os.makedirs(SEARCH_CACHE, exist_ok=True)
    path = os.path.join(SEARCH_CACHE, f"{word}.json")
    if use_cache and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            res = json.load(f)
    else:
        require_picto_api()
        try:
            res = json.loads(http_get(PICTO_SEARCH.format(
                urllib.parse.quote(word))).decode("utf-8"))
        except NotFound:
            res = []
        with open(path, "w", encoding="utf-8") as f:
            json.dump(res, f)
        time.sleep(sleep)
    return res[0]["_id"] if res else None


def fetch_pictogram(pid, use_cache, sleep):
    os.makedirs(PICT_CACHE, exist_ok=True)
    path = os.path.join(PICT_CACHE, f"{pid}.png")
    if use_cache and os.path.exists(path):
        with open(path, "rb") as f:
            return f.read()
    require_picto_api()
    raw = http_get(PICTO_IMAGE.format(pid))
    if raw[:4] != b"\x89PNG":
        raise RuntimeError(f"pictogram {pid} is not a PNG")
    with open(path, "wb") as f:
        f.write(raw)
    time.sleep(sleep)
    return raw


def parse_bg(spec):
    if not spec:
        return (0, 0, 0)
    return tuple(int(spec[i:i + 2], 16) for i in (1, 3, 5))


def render_i4(png, bg):
    """PNG -> (palette [(r,g,b,a) x16], pixels bytes).

    Fit-and-centre rather than stretch: the source art is not square and the
    aspect ratio is the difference between a bus and a squashed bus. The matte
    is opaque because I4 has no alpha -- what the panel shows for a transparent
    pixel is whatever colour we bake in, and on this display black *is*
    transparent."""
    from PIL import Image

    src = Image.open(io.BytesIO(png)).convert("RGBA")
    src.thumbnail((fmt.IMAGE_W, fmt.IMAGE_H), Image.LANCZOS)
    canvas = Image.new("RGBA", (fmt.IMAGE_W, fmt.IMAGE_H), bg + (255,))
    canvas.paste(src, ((fmt.IMAGE_W - src.width) // 2,
                       (fmt.IMAGE_H - src.height) // 2), src)
    flat = canvas.convert("RGB")
    quant = flat.quantize(colors=fmt.PALETTE_SIZE, method=Image.Quantize.MEDIANCUT)

    raw = quant.getpalette()[:fmt.PALETTE_SIZE * 3]
    palette = [(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2], 255)
               for i in range(len(raw) // 3)]
    # An image with fewer than 16 distinct colours still needs 16 entries: the
    # firmware hands the palette straight to LVGL as a fixed-size array.
    while len(palette) < fmt.PALETTE_SIZE:
        palette.append((0, 0, 0, 255))

    idx = quant.tobytes()
    pixels = bytearray(fmt.IMAGE_DATA_SIZE)
    for i in range(0, len(idx), 2):
        # High nibble first: pixel 0 is the top half of byte 0.
        pixels[i // 2] = ((idx[i] & 0xF) << 4) | (idx[i + 1] & 0xF)
    return palette, bytes(pixels)


def unpack_i4(palette, pixels):
    """Inverse of the packing above. The contact sheet is drawn from this
    rather than from the quantised PIL image, so what you eyeball is the I4
    data that actually ships, nibble order and all."""
    from PIL import Image

    img = Image.new("RGB", (fmt.IMAGE_W, fmt.IMAGE_H))
    px = img.load()
    for i in range(fmt.IMAGE_W * fmt.IMAGE_H):
        byte = pixels[i // 2]
        code = byte >> 4 if i % 2 == 0 else byte & 0xF
        px[i % fmt.IMAGE_W, i // fmt.IMAGE_W] = palette[code][:3]
    return img


# Share of the tile that is not black. Below the floor the pictogram is line
# art the matte swallowed; above the ceiling the tile is one flat slab, which
# is either a scene pictogram (fine) or a white-card unit (also fine) or a
# background rectangle that swallowed the subject (not fine). It cannot tell
# those apart, so it flags and a human looks.
PREVIEW_LIT_LOW = 0.05
PREVIEW_LIT_HIGH = 0.85


def lit_ratio(palette, pixels):
    dark = {i for i, c in enumerate(palette) if max(c[:3]) <= 40}
    if not dark:
        return 1.0
    n = 0
    for i in range(fmt.IMAGE_W * fmt.IMAGE_H):
        byte = pixels[i // 2]
        code = byte >> 4 if i % 2 == 0 else byte & 0xF
        if code not in dark:
            n += 1
    return n / float(fmt.IMAGE_W * fmt.IMAGE_H)


# --------------------------------------------------------------------------
# blob assembly


class StringPool:
    """NUL-terminated UTF-8, deduplicated. Offset 0 is the empty string so a
    zero text_off is unambiguous."""

    def __init__(self):
        self.buf = bytearray(b"\0")
        self.offsets = {"": 0}

    def intern(self, text):
        off = self.offsets.get(text)
        if off is None:
            off = len(self.buf)
            self.buf += text.encode("utf-8") + b"\0"
            if off > 0xFFFF:
                raise SystemExit("string pool exceeded 64 KiB")
            self.offsets[text] = off
        return off


class BlobPool:
    """Payload region. Every blob starts 4-byte aligned so the firmware can
    read the image palette as lv_color32_t[] and the audio header as words.

    The region opens with four pad bytes on purpose: a WordIndex uses offset 0
    to mean 'no image/no audio', so no real blob may live at relative 0."""

    def __init__(self):
        self.buf = bytearray(b"\0\0\0\0")
        self.offsets = {}

    def add(self, data):
        key = bytes(data)
        off = self.offsets.get(key)
        if off is not None:
            return off
        while len(self.buf) % 4:
            self.buf += b"\0"
        off = len(self.buf)
        self.buf += data
        self.offsets[key] = off
        return off


def build_blob(words, units, pool, blobs):
    """Header | WordIndex[] | UnitTable[] | Strings | Blobs."""
    word_index_off = fmt.HEADER_SIZE
    unit_table_off = word_index_off + len(words) * fmt.WORD_ENTRY_SIZE
    strings_off = unit_table_off + len(units) * fmt.UNIT_ENTRY_SIZE
    blobs_off = strings_off + len(pool.buf)
    blobs_off += (-blobs_off) % 4
    total = blobs_off + len(blobs.buf)

    out = bytearray(fmt.pack_header(len(words), len(units), word_index_off,
                                    unit_table_off, strings_off, blobs_off,
                                    total))
    for w in words:
        out += fmt.pack_word(w["text_off"], w["zh_off"], w["image_off"],
                             w["audio_off"], w["sfx_off"])
    for u in units:
        out += fmt.pack_unit(u["title_off"], u["first_word"], u["word_count"],
                             u["flags"])
    out += pool.buf
    out += b"\0" * ((-len(out)) % 4)
    assert len(out) == blobs_off, (len(out), blobs_off)
    out += blobs.buf
    assert len(out) == total
    return bytes(out)


def round_trip(blob, words, units):
    """Re-read the blob the way the firmware will -- byte-wise, no struct
    casts -- and check every field against what went in. This is the only
    check that the offsets written into the index actually point at the right
    payloads."""

    def rd16(off):
        return blob[off] | (blob[off + 1] << 8)

    def rd32(off):
        return (blob[off] | (blob[off + 1] << 8) | (blob[off + 2] << 16)
                | (blob[off + 3] << 24))

    def string(off):
        end = blob.index(b"\0", off)
        return blob[off:end].decode("utf-8")

    if blob[:4] != fmt.MAGIC:
        raise SystemExit("round-trip: bad magic")
    if rd16(4) != fmt.VERSION:
        raise SystemExit("round-trip: bad version")
    if rd16(6) != len(words) or rd16(8) != len(units):
        raise SystemExit("round-trip: count mismatch")
    if rd16(10) != fmt.IMAGE_W or rd16(12) != fmt.IMAGE_H:
        raise SystemExit("round-trip: image size mismatch")
    if rd16(14) != fmt.AUDIO_RATE:
        raise SystemExit("round-trip: audio rate mismatch")
    widx, utab, stro, blbo, total = (rd32(16), rd32(20), rd32(24), rd32(28),
                                     rd32(32))
    if total != len(blob):
        raise SystemExit(f"round-trip: total_size {total} != {len(blob)}")
    for name, off in (("word_index", widx), ("unit_table", utab),
                      ("strings", stro), ("blobs", blbo)):
        if off >= len(blob):
            raise SystemExit(f"round-trip: {name}_off out of range")
    if blbo % 4:
        raise SystemExit("round-trip: blobs_off is not 4-byte aligned")

    for i, w in enumerate(words):
        base = widx + i * fmt.WORD_ENTRY_SIZE
        if string(stro + rd16(base)) != w["en"]:
            raise SystemExit(f"round-trip: word {i} text mismatch")
        if string(stro + rd16(base + 2)) != w["zh"]:
            raise SystemExit(f"round-trip: word {i} gloss mismatch")
        image_off, audio_off = rd32(base + 4), rd32(base + 8)
        if bool(image_off) != bool(w["image_off"]) or image_off != w["image_off"]:
            raise SystemExit(f"round-trip: word {i} image_off mismatch")
        if audio_off != w["audio_off"]:
            raise SystemExit(f"round-trip: word {i} audio_off mismatch")
        if image_off:
            at = blbo + image_off
            if at % 4:
                raise SystemExit(f"round-trip: {w['en']} image blob unaligned")
            if rd16(at) != fmt.IMAGE_W or rd16(at + 2) != fmt.IMAGE_H:
                raise SystemExit(f"round-trip: {w['en']} image header wrong")
            got = blob[at + 4 + fmt.PALETTE_SIZE * 4:
                       at + fmt.IMAGE_BLOB_SIZE]
            if got != w["pixels"]:
                raise SystemExit(f"round-trip: {w['en']} pixels differ")
            for n, (r, g, b, a) in enumerate(w["palette"]):
                e = at + 4 + n * 4
                if (blob[e], blob[e + 1], blob[e + 2], blob[e + 3]) != (b, g, r, a):
                    raise SystemExit(f"round-trip: {w['en']} palette {n} differs")
        if audio_off:
            at = blbo + audio_off
            if at % 4:
                raise SystemExit(f"round-trip: {w['en']} audio blob unaligned")
            count = rd32(at)
            if count != w["sample_count"]:
                raise SystemExit(f"round-trip: {w['en']} sample_count wrong")
            pred = rd16(at + 4)
            pred = pred - 0x10000 if pred > 0x7FFF else pred
            nib = blob[at + 8:at + 8 + (count + 1) // 2]
            decoded = adpcm_decode(nib, count, pred, blob[at + 6])
            if snr_db(w["pcm"], decoded) < 12.0:
                raise SystemExit(f"round-trip: {w['en']} audio decodes to noise")

    for i, u in enumerate(units):
        base = utab + i * fmt.UNIT_ENTRY_SIZE
        if string(stro + rd16(base)) != u["title"]:
            raise SystemExit(f"round-trip: unit {i} title mismatch")
        if rd16(base + 2) != u["first_word"] or rd16(base + 4) != u["word_count"]:
            raise SystemExit(f"round-trip: unit {i} range mismatch")
        if rd16(base + 6) != u["flags"]:
            raise SystemExit(f"round-trip: unit {i} flags mismatch")


# --------------------------------------------------------------------------
# outputs


BANNER = ("/*\n"
          " * Generated by tools/english_pipeline/build_english_data.py -- do not edit.\n"
          " *\n"
          " * Pictures come from a pictogram library; word audio is re-encoded\n"
          " * from a local dictionary bundle. See the pipeline for both.\n"
          " */\n")


def emit_blob_and_header(blob, out_bin, out_h):
    """Write the blob as a raw file plus a header that names it.

    The blob used to be emitted as a C array. At 60 words that was a 6 MB .c
    file and merely ugly; at 480 words it is 43 MB and GCC needs many minutes
    to chew through it, which makes every unrelated rebuild painful.

    ESP-IDF can link a binary straight in via EMBED_FILES (see
    main/CMakeLists.txt), so the compiler never sees the bytes at all: the
    build goes from minutes to instant, and the repo stores 7 MB instead of
    43 MB. The header just gives the linker-provided symbols the names the
    firmware already uses.
    """
    os.makedirs(os.path.dirname(out_bin), exist_ok=True)
    with open(out_bin, "wb") as f:
        f.write(blob)

    with open(out_h, "w", encoding="utf-8") as f:
        f.write(BANNER)
        f.write("#pragma once\n\n"
                "#ifdef __cplusplus\n"
                'extern "C" {\n'
                "#endif\n\n"
                "/* Provided by the linker: main/CMakeLists.txt embeds\n"
                " * assets/english/english_data.bin via EMBED_FILES. */\n"
                "extern const unsigned char english_data_blob[] "
                'asm("_binary_english_data_bin_start");\n'
                "extern const unsigned char english_data_blob_tail[] "
                'asm("_binary_english_data_bin_end");\n\n'
                "#define english_data_blob_size "
                "((unsigned int)(english_data_blob_tail - english_data_blob))\n\n"
                "#ifdef __cplusplus\n"
                "}\n"
                "#endif\n")


def emit_preview(previews, out_dir):
    """Contact sheets of exactly what the panel will show -- the only practical
    way to catch a pictogram that is technically valid and visually wrong.

    Split 60 to a sheet and captioned, because one sheet of 480 tiles is a
    13 MB PNG that nobody scrolls through. `previews` is
    [(english, chinese, image, lit_ratio)]; the caption turns red when the tile
    is nearly blank or nearly solid, which is what a wrong `bg` looks like.
    """
    from PIL import Image, ImageDraw, ImageFont

    for stale in os.listdir(out_dir) if os.path.isdir(out_dir) else []:
        os.remove(os.path.join(out_dir, stale))
    os.makedirs(out_dir, exist_ok=True)
    try:
        font = ImageFont.truetype(PREVIEW_FONT, 15)
    except OSError:
        font = ImageFont.load_default()

    cw, ch = fmt.IMAGE_W + 8, fmt.IMAGE_H + 36
    for start in range(0, len(previews), PREVIEW_PER_SHEET):
        chunk = previews[start:start + PREVIEW_PER_SHEET]
        rows = (len(chunk) + PREVIEW_COLS - 1) // PREVIEW_COLS
        sheet = Image.new("RGB", (PREVIEW_COLS * cw, rows * ch), (20, 20, 22))
        draw = ImageDraw.Draw(sheet)
        for n, (en, zh, img, lit) in enumerate(chunk):
            x = (n % PREVIEW_COLS) * cw + 4
            y = (n // PREVIEW_COLS) * ch + 2
            sheet.paste(img, (x, y))
            odd = lit < PREVIEW_LIT_LOW or lit > PREVIEW_LIT_HIGH
            ink = (255, 96, 96) if odd else (228, 228, 228)
            draw.text((x, y + fmt.IMAGE_H + 2), f"{start + n} {en}",
                      font=font, fill=ink)
            draw.text((x, y + fmt.IMAGE_H + 18), f"{zh}  {lit * 100:.0f}%",
                      font=font, fill=ink)
        sheet.save(os.path.join(
            out_dir, f"sheet_{start // PREVIEW_PER_SHEET + 1:02d}.png"))


# --------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", action=argparse.BooleanOptionalAction, default=True,
                    help="reuse .cache/ (default: yes; --no-cache refetches)")
    ap.add_argument("--skip-audio", action="store_true",
                    help="build images only, e.g. on a machine with no mdd")
    ap.add_argument("--dict-mdd", default=MDD_PATH, metavar="PATH",
                    help="dictionary mdd holding the recordings "
                         "(default: $DICT_MDD)")
    ap.add_argument("--dict-mdx", default=MDX_PATH, metavar="PATH",
                    help="its mdx, for entries that link audio by number "
                         "(default: $DICT_MDX)")
    ap.add_argument("--sleep", type=float, default=0.2,
                    help="delay between pictogram requests")
    ap.add_argument("--out-dir", default=os.path.join(REPO, "main", "assets"))
    ap.add_argument("--pack-units", type=int, default=None, metavar="N",
                    help="only put the first N units into english_data.c. "
                         "The cache still holds every word, so this trades "
                         "firmware size for how much is on the watch without "
                         "re-fetching anything. Default: all units.")
    args = ap.parse_args()

    problems = wordlist.validate()
    for line in problems:
        warn(f"wordlist: {line}")
    if problems:
        raise SystemExit("wordlist.py is inconsistent, refusing to build")

    # The cache is built from the whole list; only what goes into the .c file
    # is trimmed. That way a fresh clone still has every picture and recording
    # on disk and switching how much ships is one flag, not a re-fetch.
    all_units = wordlist.UNITS
    packed_units = all_units
    if args.pack_units is not None:
        if args.pack_units < 1:
            raise SystemExit("--pack-units needs at least 1 unit")
        packed_units = all_units[:args.pack_units]

    entries = wordlist.all_words()
    unique = sorted({w["en"] for _, _, w in entries})
    log(f"word list: {len(entries)} words in {len(all_units)} units "
        f"({len(unique)} distinct headwords)")
    if len(packed_units) != len(all_units):
        packed_words = sum(len(ws) for _, ws in packed_units)
        log(f"packing:   {len(packed_units)} units / {packed_words} words into "
            f"the firmware; the rest stay in .cache/ only")

    # ---- audio -----------------------------------------------------------
    mp3s, audio_meta = ({}, {})
    if not args.skip_audio:
        mp3s, audio_meta = fetch_audio(
            lambda: DictAudio(args.dict_mdd, args.dict_mdx), unique, args.cache)
    pcm = {}
    for w in unique:
        if w not in mp3s:
            continue
        samples = clean_pcm(decode_mp3(mp3s[w], w))
        if len(samples) < fmt.AUDIO_RATE // 20:
            warn(f"{w}: decoded to {len(samples)} samples, dropping")
            continue
        pcm[w] = samples
    no_audio = [w for w in unique if w not in pcm]
    for w in no_audio:
        warn(f"{w}: no headword recording in the dictionary -- ships mute")

    # ---- images ----------------------------------------------------------
    pool = StringPool()
    blobs = BlobPool()
    words, previews, manifest_words = [], [], []
    no_image, odd_images = [], []
    audio_cache_blob = {}
    snr_total, snr_n, snr_min = 0.0, 0, 99.0

    # entries covers the whole list (the cache was filled from it above); the
    # blob is built only from the units that actually ship.
    packed_entries = [e for e in entries if e[0] < len(packed_units)]

    for unit_index, unit_title, w in packed_entries:
        en, zh = w["en"], w["zh"]
        pid, how = w.get("pic"), "pinned"
        if pid is None:
            how = "search"
            try:
                pid = search_pictogram(en, args.cache, args.sleep)
            except Exception as exc:
                warn(f"{en}: pictogram search failed ({exc})")
        image_off = 0
        palette, pixels = None, None
        if pid is None:
            no_image.append(en)
            warn(f"{en}: no pictogram available")
        else:
            try:
                png = fetch_pictogram(pid, args.cache, args.sleep)
                palette, pixels = render_i4(png, parse_bg(w.get("bg")))
                image_off = blobs.add(fmt.pack_image_blob(palette, pixels))
                lit = lit_ratio(palette, pixels)
                previews.append((en, zh, unpack_i4(palette, pixels), lit))
                if lit < PREVIEW_LIT_LOW or lit > PREVIEW_LIT_HIGH:
                    odd_images.append((en, unit_title, lit))
            except Exception as exc:      # network, decode, anything
                no_image.append(en)
                warn(f"{en}: pictogram {pid} failed ({exc})")
                pid = None

        audio_off = 0
        sample_count = 0
        if en in pcm:
            key = f"audio:{en}"
            if key in audio_cache_blob:
                audio_off, sample_count = audio_cache_blob[key]
            else:
                samples = pcm[en]
                nibbles, pred, idx = adpcm_encode(samples)
                decoded = adpcm_decode(nibbles, len(samples), pred, idx)
                snr = snr_db(samples, decoded)
                snr_total += snr
                snr_n += 1
                snr_min = min(snr_min, snr)
                if snr < 12.0:
                    warn(f"{en}: ADPCM round trip only {snr:.1f} dB")
                sample_count = len(samples)
                audio_off = blobs.add(
                    fmt.pack_audio_blob(sample_count, pred, idx, nibbles))
                audio_cache_blob[key] = (audio_off, sample_count)

        words.append({
            "en": en, "zh": zh,
            "text_off": pool.intern(en), "zh_off": pool.intern(zh),
            "image_off": image_off, "audio_off": audio_off, "sfx_off": 0,
            "palette": palette, "pixels": pixels,
            "pcm": pcm.get(en, []), "sample_count": sample_count,
        })
        meta = audio_meta.get(en, {})
        manifest_words.append({
            "en": en, "zh": zh, "unit": unit_index, "unit_title": unit_title,
            "sfx_wanted": bool(w.get("sfx")),
            "image": None if pid is None else {
                "id": pid, "picked_by": how,
                "background": w.get("bg", "#000000"),
                "format": f"I4 {fmt.IMAGE_W}x{fmt.IMAGE_H}, 16-colour palette",
            },
            "audio": None if en not in pcm else {

                "file": meta.get("file"), "accent": meta.get("accent"),
                "picked_by": meta.get("picked_by"),
                "samples": sample_count,
                "seconds": round(sample_count / fmt.AUDIO_RATE, 3),
                "format": f"IMA ADPCM 4-bit, {fmt.AUDIO_RATE} Hz mono",
            },
        })

    units = []
    first = 0
    for title, unit_words in packed_units:
        # The flag says "this unit can run the listen-and-pick game", so it has
        # to describe the blobs that actually shipped, not the wish list in
        # wordlist.py. No sound-effect source exists yet, hence: never set.
        has_sfx = sum(1 for w in words[first:first + len(unit_words)]
                      if w["sfx_off"]) >= 4
        units.append({
            "title": title, "title_off": pool.intern(title),
            "first_word": first, "word_count": len(unit_words),
            "flags": fmt.UNIT_FLAG_HAS_SFX if has_sfx else 0,
        })
        first += len(unit_words)

    blob = build_blob(words, units, pool, blobs)
    round_trip(blob, words, units)
    log("round-trip: OK (index, palettes, pixels, ADPCM)")

    if len(blob) > BLOB_LIMIT:
        raise SystemExit(f"blob {len(blob)} B exceeds gate {BLOB_LIMIT} B")

    out_dir = os.path.join(args.out_dir, "english")
    emit_blob_and_header(blob,
                         os.path.join(args.out_dir, "english", "english_data.bin"),
                         os.path.join(args.out_dir, "english", "english_data.h"))
    os.makedirs(CACHE, exist_ok=True)
    with open(os.path.join(CACHE, "english_data.bin"), "wb") as f:
        f.write(blob)
    if previews:
        emit_preview(previews, os.path.join(CACHE, "preview"))

    manifest = {
        "generated_by": "tools/english_pipeline/build_english_data.py",
        "format": "ENG1 v%d" % fmt.VERSION,
        "blob_bytes": len(blob),
        "sources": {
            "images": {"kind": "pictogram library"},
            "audio": {"kind": "dictionary mdd"},
        },
        "font_charset_needed": wordlist.chinese_glyphs(),
        "review": {
            "no_image": sorted(set(no_image)),
            "no_audio": no_audio,
            "odd_lit": [{"en": e, "unit": u, "lit": round(r, 4)}
                        for e, u, r in sorted(odd_images, key=lambda t: t[2])],
        },
        "units": [{"index": i, "title": t,
                   "words": [w["en"] for w in ws]}
                  for i, (t, ws) in enumerate(all_units)],
        "words": manifest_words,
    }
    with open(os.path.join(HERE, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)

    # ---- summary ---------------------------------------------------------
    # Count distinct blobs: identical payloads are stored once (the two
    # `orange` entries share a recording), and the leftover is padding.
    image_bytes = len({w["image_off"] for w in words if w["image_off"]}) \
        * fmt.IMAGE_BLOB_SIZE
    audio_bytes = sum(8 + (w["sample_count"] + 1) // 2
                      for w in {w["audio_off"]: w for w in words
                                if w["audio_off"]}.values())
    pad_bytes = len(blobs.buf) - image_bytes - audio_bytes
    header_bytes = fmt.HEADER_SIZE
    index_bytes = len(words) * fmt.WORD_ENTRY_SIZE
    unit_bytes = len(units) * fmt.UNIT_ENTRY_SIZE
    string_bytes = len(pool.buf)
    with_image = sum(1 for w in words if w["image_off"])
    with_audio = sum(1 for w in words if w["audio_off"])
    seconds = sum(w["sample_count"] for w in words) / fmt.AUDIO_RATE
    uk = sum(1 for m in manifest_words
             if m["audio"] and m["audio"]["accent"] == "uk")

    log("")
    log(f"words        {len(words)} in {len(units)} units")
    log(f"images       {with_image}/{len(words)} "
        f"({100.0 * with_image / len(words):.1f}%)")
    log(f"audio        {with_audio}/{len(words)} "
        f"({100.0 * with_audio / len(words):.1f}%), {uk} British, "
        f"{seconds:.1f}s total")
    if snr_n:
        log(f"adpcm        {snr_total / snr_n:.1f} dB mean, "
            f"{snr_min:.1f} dB worst")
    log(f"blob         {len(blob)} B ({len(blob) / 1024.0:.1f} KiB)")
    log(f"  header     {header_bytes} B")
    log(f"  word index {index_bytes} B")
    log(f"  unit table {unit_bytes} B")
    log(f"  strings    {string_bytes} B")
    log(f"  images     {image_bytes} B ({100.0 * image_bytes / len(blob):.1f}%)")
    log(f"  audio      {audio_bytes} B ({100.0 * audio_bytes / len(blob):.1f}%)")
    log(f"  alignment  {pad_bytes} B")
    if no_image:
        log(f"MISSING IMAGE ({len(no_image)}): {', '.join(sorted(set(no_image)))}")
    if no_audio:
        log(f"MISSING AUDIO ({len(no_audio)}): {', '.join(no_audio)}")
    if odd_images:
        log(f"CHECK BY EYE ({len(odd_images)} outside "
            f"{PREVIEW_LIT_LOW:.0%}-{PREVIEW_LIT_HIGH:.0%} lit -- a scene "
            f"pictogram and a white-card unit both land here legitimately):")
        for en, unit, r in sorted(odd_images, key=lambda t: t[2]):
            log(f"  {r * 100:5.1f}%  {en:<14s} [{unit}]")
    log(f"chinese glyphs needed by this word list ({len(wordlist.chinese_glyphs())}): "
        f"{wordlist.chinese_glyphs()}")
    log("  ^ these must be in main/assets/fonts/charset_hanzi_ui.txt or the "
        "gloss draws as boxes")


if __name__ == "__main__":
    main()
