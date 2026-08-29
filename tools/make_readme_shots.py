#!/usr/bin/env python3
"""Turn simulator screenshots into the round PNGs the README shows.

The sims already mask to the visible circle, but they write PPM, which has no
alpha -- so everything outside r=233 comes out black. On a black background
that is invisible; on the README's white or dark-grey background it is an
obvious square. This re-masks the same circle into an alpha channel so the
image reads as the round panel it actually is, on any background.

Usage:
  python3 tools/make_readme_shots.py            # run the sims, then convert
  python3 tools/make_readme_shots.py --no-run   # convert whatever is cached
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
OUT_DIR = os.path.join(REPO, "docs", "images")

SCREEN = 466
RADIUS = 233  # must match kScreenRadius in the sims
SS = 8  # supersampling for the alpha edge; the panel is round, the pixels are not

# (sim, screenshot basename, output name). The hanzi sim writes into the
# working directory; the other two take --out.
SHOTS = [
    ("hanzi", "search_01", "hanzi-search.png"),
    ("hanzi", "browse", "hanzi-browse.png"),
    ("hanzi", "learn_06", "hanzi-learn.png"),
    ("math", "map_fresh", "math-map.png"),
    ("math", "quiz_typical", "math-quiz.png"),
    ("english", "card_first", "english-card.png"),
    ("english", "quiz_listen", "english-quiz.png"),
]

SIMS = {
    "hanzi": os.path.join(REPO, "build_host", "hanzi_sim"),
    "math": os.path.join(REPO, "build_math", "math_sim"),
    "english": os.path.join(REPO, "build_english", "english_sim"),
}


def run_sims(tmp):
    """Run each sim so its .ppm files land under tmp/<name>/."""
    for name, exe in SIMS.items():
        if not os.path.exists(exe):
            sys.exit(f"{exe} not built -- see the host test section of the README")
        d = os.path.join(tmp, name)
        os.makedirs(d, exist_ok=True)
        if name == "hanzi":
            # It has no --out flag and looks its blob up by a path relative to
            # the repo root, so it has to run there; move the frames after.
            subprocess.run([exe], cwd=REPO, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for f in os.listdir(REPO):
                if f.endswith(".ppm"):
                    shutil.move(os.path.join(REPO, f), os.path.join(d, f))
        else:
            subprocess.run([exe, "--out", d], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"  ran {name}_sim -> {len(os.listdir(d))} frames")


def round_mask():
    """Anti-aliased alpha for the visible circle."""
    big = Image.new("L", (SCREEN * SS, SCREEN * SS), 0)
    ImageDraw.Draw(big).ellipse(
        [(SCREEN // 2 - RADIUS) * SS, (SCREEN // 2 - RADIUS) * SS,
         (SCREEN // 2 + RADIUS) * SS, (SCREEN // 2 + RADIUS) * SS],
        fill=255)
    return big.resize((SCREEN, SCREEN), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-run", action="store_true",
                    help="convert from --src instead of running the sims")
    ap.add_argument("--src", default=None,
                    help="directory holding <sim>/<name>.ppm (implies --no-run)")
    args = ap.parse_args()

    tmp = args.src or tempfile.mkdtemp(prefix="readme-shots-")
    made_tmp = args.src is None
    try:
        if not args.no_run and args.src is None:
            run_sims(tmp)

        os.makedirs(OUT_DIR, exist_ok=True)
        mask = round_mask()
        for sim, base, out_name in SHOTS:
            src = os.path.join(tmp, sim, base + ".ppm")
            if not os.path.exists(src):
                sys.exit(f"missing {src}")
            im = Image.open(src).convert("RGB")
            if im.size != (SCREEN, SCREEN):
                sys.exit(f"{src} is {im.size}, expected {SCREEN}x{SCREEN}")
            im.putalpha(mask)
            dst = os.path.join(OUT_DIR, out_name)
            im.save(dst, optimize=True)
            print(f"  {out_name:<22} {os.path.getsize(dst) / 1024:5.1f} KB")
    finally:
        if made_tmp and not args.src:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
