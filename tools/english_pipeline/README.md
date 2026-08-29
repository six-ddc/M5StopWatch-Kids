# English content pipeline

Builds the ENG1 blob the English app reads: 40 units x 12 concrete words, each
with a 144x144 16-colour picture and a spoken recording.

```
python3 tools/english_pipeline/build_english_data.py            # normal build
python3 tools/english_pipeline/build_english_data.py --no-cache # refetch everything
python3 tools/english_pipeline/build_english_data.py --skip-audio
python3 tools/english_pipeline/build_english_data.py --pack-units 24
```

Outputs:

| path | what |
| --- | --- |
| `main/assets/english/english_data.bin` / `.h` | the blob, linked in by `main/CMakeLists.txt` via `EMBED_FILES` |
| `tools/english_pipeline/manifest.json` | per-word build record: picture id, which recording was used and why |
| `.cache/preview/sheet_NN.png` | contact sheets of every image exactly as the panel renders it |
| `.cache/english_data.bin` | raw blob, for the host-side test |

The word list lives in `wordlist.py`; the binary layout is defined by
`engformat.py` and mirrored by the firmware decoder. Nothing here writes to
`engformat.py` -- change the format there and in the firmware together.

## Requirements

* Python 3.9+, `mdict-utils`, `Pillow`
* `ffmpeg` (looked up at `/opt/homebrew/bin/ffmpeg`, falls back to `$PATH`)

Everything the build needs is already in `.cache/` and in the repo, so a normal
run costs a handful of seconds and touches neither the network nor any
dictionary file. The two inputs below only matter when adding words or
refetching.

## The two inputs

| setting | what it points at | default |
| --- | --- | --- |
| `$PICTO_API` | pictogram service base URL | `https://api.arasaac.org/api/pictograms` |
| `--dict-mdd` / `$DICT_MDD` | a local dictionary `.mdd` holding the recordings | **none** -- a path on your machine |
| `--dict-mdx` / `$DICT_MDX` | its `.mdx`, for entries that link audio by number | **none** |

Pictures come from [ARASAAC](https://arasaac.org), an open AAC symbol set with
a REST API that needs no key. Ids are pinned per word in `wordlist.py`, so the
default works as-is and the build never depends on search ranking. Any service
with the same two shapes drops in via `$PICTO_API`: a keyword search returning
objects that carry an `_id`, and an `id -> PNG` fetch.

The dictionary is the one input with no useful default, because it is a file on
your own disk rather than a service. Without it the build still runs -- cached
recordings are reused and anything new simply ships mute. `--skip-audio` makes
that explicit.

## What the build does

**Pictures.** ARASAAC ids are pinned per word in `wordlist.py`, fetched at 500 px,
scaled to fit 144x144 (aspect preserved, centred), composited onto an opaque
black matte -- the panel is black AMOLED, so black reads as transparent -- then
quantised to a 16-colour palette and packed as I4, two pixels per byte, high
nibble first. Only `black` overrides the matte: it is drawn as a pure `#000000`
splat, which on black is a blank tile, so it gets a white card.

**Audio.** The mdd does *not* hold `en_us_<word>.mp3` for every headword, and
the gaps are exactly the words a six-year-old learns first (red, blue, milk,
horse, eye). Those entries do have audio; it is referenced from the entry HTML
in the mdx by number. Where the mdx points such a word at an `en_us_` file it
often points at a *homophone's* recording, which would say the wrong thing out
loud, so a filename that does not match the headword is never trusted.
Preference order:

1. `en_us_<word>.mp3` — US, the name proves it is the right word
2. a numbered `type_us` link in the headword's pron block — US
3. `en_gb_<word>.mp3` — UK
4. a numbered `type_uk` link — UK
5. nothing — the word ships mute, with a warning and a null entry in the manifest

The clip is then decoded to 16 kHz mono, trimmed, peak-normalised and encoded as
IMA ADPCM (4 bits/sample, one continuous run seeded by the predictor and step
index in the blob header). The encoder is checked against a decoder in the same
file on every run; ~24 dB SNR is normal for speech at this rate.

## Checks the build runs

* `wordlist.validate()` — 12 words per unit, no duplicates inside a unit, glosses
  present and short enough
* full round trip of the finished blob, read back byte-wise the way the Xtensa
  decoder has to read it: every string, every offset, every palette entry, every
  pixel, and an ADPCM decode of every clip that must land above 12 dB
* alignment: `blobs_off` and every blob start on a 4-byte boundary. The blob
  region also opens with four pad bytes, because a `WordIndex` uses offset 0 to
  mean "no payload" and a real blob must never land there.

## Adding words

Add the entry to `wordlist.py` (`en`, `zh`, and once you have eyeballed it, a
pinned `pic` id), configure the two inputs above, and re-run. Leave `pic` out
and the builder searches and takes the top hit, which is right maybe nine times
in ten -- `plane` returns a carpenter's plane, `hand` returns the verb, `water`
returns someone watering a plant. Check the contact sheets in `.cache/preview/`
before committing.

One cross-pipeline dependency: the Chinese glosses are drawn with the subset UI
font built by `tools/hanzi_pipeline`. The build prints the exact set of
characters this word list needs and records it in `manifest.json` as
`font_charset_needed`; any character missing from
`main/assets/fonts/charset_hanzi_ui.txt` renders as a silent box on the device.
