#!/usr/bin/env python3
"""Assert a region of two PNGs differs by at least a threshold.

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

# Exit 77 is ctest's SKIP_RETURN_CODE here. Pillow and NumPy are installed in
# the `inventories` CI job for the eye-asset check, NOT in the macOS
# `build + test` jobs that run ctest -- so in CI this check skips rather than
# failing for want of a wheel. It runs locally, where .venv-mh has both and
# where the rendering it inspects actually happens. Stated plainly because a
# gate that skips everywhere is not a gate: this one is verified on the
# developer machine and is a no-op in CI.
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
    ap.add_argument("--cx", type=int, required=True)
    ap.add_argument("--cy", type=int, required=True)
    ap.add_argument("--radius", type=int, default=6)
    ap.add_argument("--min-response", type=float, required=True)
    args = ap.parse_args()

    for f in (args.a, args.b):
        if not os.path.exists(f):
            # The render that should have produced it was skipped (no GPU in a
            # headless runner), so there is nothing to judge.
            print(f"skipping: {f} was not rendered", file=sys.stderr)
            return SKIP

    def patch(path):
        a = np.asarray(Image.open(path).convert("RGB")).astype(float)
        r, cy, cx = args.radius, args.cy, args.cx
        if not (r <= cy < a.shape[0] - r and r <= cx < a.shape[1] - r):
            print(f"{path}: box ({cx},{cy})r{r} outside {a.shape[1]}x{a.shape[0]}",
                  file=sys.stderr)
            raise SystemExit(2)
        return a[cy - r:cy + r, cx - r:cx + r].reshape(-1, 3).mean(axis=0)

    pa, pb = patch(args.a), patch(args.b)
    response = float(np.abs(pa - pb).mean())
    print(f"region ({args.cx},{args.cy}) r{args.radius}: "
          f"{pa.round(1)} vs {pb.round(1)}, response {response:.1f} "
          f"(need >= {args.min_response})")
    if response < args.min_response:
        print("the region did not follow; the blend is not reaching the pixels",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
