#!/usr/bin/env python3
"""Assert a region of two PNGs differs by at least -- or at most -- a threshold.

Written for one claim that a printed line cannot make: that a worn proxy's
SKIN TONE actually follows the ethnic sliders. `wearing Genitals ... blended to
the skin tone` proves the material asked for the blend; only the pixels prove
the renderer applied it.

The two images must be the same character at two ethnicities, so the sampled
box has the SAME surface normals in both -- a matcap shades by normal, so
comparing two different body parts would measure the normals instead of the
tone. That mistake was made once here and caught by the number disagreeing
with the render.
"""
import argparse, os, sys

# Exit 77 is ctest's SKIP_RETURN_CODE here, and it should now be RARE.
#
# This used to say the opposite: that Pillow and NumPy lived only in the
# `inventories` job, so every caller of this tool skipped in CI and was
# verified on one laptop. That was true and it was a hole -- four gates that
# look at what was actually DRAWN, no-ops everywhere but a developer machine.
# The macOS `build + test` jobs now build a `.venv-mh` with both wheels, which
# is the same path tests/CMakeLists.txt already preferred locally, so the
# import below succeeds and the gates run.
#
# What still legitimately skips: a runner with no GPU, where the render never
# produced the PNG at all. That is the `was not rendered` branch below, and it
# is a different claim from "the wheel is missing".
SKIP = 77
try:
    import numpy as np
    from PIL import Image
except ImportError as exc:  # pragma: no cover - depends on the environment
    print(f"skipping: {exc}", file=sys.stderr)
    raise SystemExit(SKIP)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    # Two ways to say where, and which one is right depends on the image.
    #
    # `--cx/--cy` are DEVICE pixels, correct for `--render` output, which is a
    # fixed 1024x1024 on every machine. They are WRONG for a `--screenshot`
    # viewport grab: that is sized in device pixels, so this machine writes
    # 520x1332 and a 1x runner writes 260x666, and a pixel coordinate derived on
    # one is out of bounds on the other. That is why the two backdrop probes
    # skipped in CI rather than running -- not the wheels alone.
    #
    # `--fx/--fy/--fradius` are FRACTIONS of the image, so the probe names the
    # same place on the model at either scale.
    ap.add_argument("--cx", type=int)
    ap.add_argument("--cy", type=int)
    ap.add_argument("--radius", type=int, default=6)
    ap.add_argument("--fx", type=float)
    ap.add_argument("--fy", type=float)
    ap.add_argument("--fradius", type=float)
    ap.add_argument("--min-response", type=float)
    # The opposite claim, and it needs saying separately because it is the one
    # a "the window changed" check cannot make. The viewport backdrop is drawn
    # BEFORE the body with its depth write off; if it ever wrote depth it would
    # OCCLUDE the model, and every differs-from-plain gate would still pass
    # while the character vanished. So: adding a backdrop must leave the pixels
    # where the model is UNCHANGED.
    ap.add_argument("--max-response", type=float)
    args = ap.parse_args()
    if (args.min_response is None) == (args.max_response is None):
        print("give exactly one of --min-response or --max-response", file=sys.stderr)
        return 2
    pixel = args.cx is not None and args.cy is not None
    frac = args.fx is not None and args.fy is not None
    if pixel == frac:
        print("give exactly one of --cx/--cy or --fx/--fy", file=sys.stderr)
        return 2
    if frac and args.fradius is None:
        print("--fx/--fy need --fradius; a pixel radius would not scale with them",
              file=sys.stderr)
        return 2

    for f in (args.a, args.b):
        if not os.path.exists(f):
            # The render that should have produced it was skipped (no GPU in a
            # headless runner), so there is nothing to judge.
            print(f"skipping: {f} was not rendered", file=sys.stderr)
            return SKIP

    def where(shape):
        """Resolve the box for ONE image, so fractions follow its own size."""
        if pixel:
            return args.radius, args.cy, args.cx
        h, w = shape[0], shape[1]
        # Radius scales with WIDTH alone. Scaling each axis by its own extent
        # would make the box non-square the moment the aspect ratio moved, and
        # a probe that changes shape is measuring something else.
        return max(1, round(args.fradius * w)), round(args.fy * h), round(args.fx * w)

    def patch(path):
        a = np.asarray(Image.open(path).convert("RGB")).astype(float)
        r, cy, cx = where(a.shape)
        if not (r <= cy < a.shape[0] - r and r <= cx < a.shape[1] - r):
            print(f"{path}: box ({cx},{cy})r{r} outside {a.shape[1]}x{a.shape[0]}",
                  file=sys.stderr)
            raise SystemExit(2)
        return a[cy - r:cy + r, cx - r:cx + r].reshape(-1, 3).mean(axis=0)

    # With fractions the box is resolved per image, so two images of DIFFERENT
    # sizes would silently be compared at two different places and the response
    # would be meaningless rather than wrong-looking. Pixel coordinates cannot
    # hide that -- one of the two would go out of bounds -- but fractions can.
    sa = np.asarray(Image.open(args.a)).shape[:2]
    sb = np.asarray(Image.open(args.b)).shape[:2]
    if sa != sb:
        print(f"sizes differ: {sa[1]}x{sa[0]} vs {sb[1]}x{sb[0]}", file=sys.stderr)
        return 1

    pa, pb = patch(args.a), patch(args.b)
    response = float(np.abs(pa - pb).mean())
    want = (f">= {args.min_response}" if args.min_response is not None
            else f"<= {args.max_response}")
    r, cy, cx = where(sa)
    print(f"region ({cx},{cy}) r{r} of {sa[1]}x{sa[0]}: "
          f"{pa.round(1)} vs {pb.round(1)}, response {response:.1f} (need {want})")
    if args.min_response is not None and response < args.min_response:
        print("the region did not follow; the blend is not reaching the pixels",
              file=sys.stderr)
        return 1
    if args.max_response is not None and response > args.max_response:
        print("the region moved when it should not have", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
