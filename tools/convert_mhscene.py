#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Converts the reference's pickled `.mhscene` files to this port's JSON.

    ./.venv-mh/bin/python tools/convert_mhscene.py data/scenes/*.mhscene

WHY THIS EXISTS. `memory/project_context.md` lists the reference's `.mhscene`
as a verified defect: it is a **Python pickle**, so opening a scene someone
sent you executes whatever that file names. CLAUDE.md rule 3 forbids porting a
known-broken behaviour, and "the loader is an RCE vector" is as broken as it
gets. The three scenes that shipped with the asset bootstrap
(`data/scenes/{default,coldlights,warmlights}.mhscene`) really are pickles --
protocol 2, with `cmaterial\\nColor` globals in them -- and as of 2026-09-24
NOTHING in `src/`, `tests/` or `tools/` read them. They were dead data in a
dangerous format.

So this converts them ONCE, to JSON, and the C++ loader reads only JSON and
refuses a pickle outright.

WHAT IT REFUSES TO DO. It does not `pickle.load` the file in the ordinary
sense: `find_class` is overridden to permit exactly one name, `material.Color`,
and to raise on everything else. A pickle that names `os.system` therefore
fails here rather than running. That is the whole reason the reading happens in
a tool you can audit instead of in the application.

THE SCHEMA IT READS is not documented anywhere except the reference's own
`shared/scene.py`, which writes a bare sequence of pickles with no framing:

    version (5), then the Environment's attributes in SORTED name order,
    then the light count, then each Light's attributes in SORTED name order.

`sorted()` is load-bearing and its result is not the obvious one:
`'areaLightSize' < 'areaLights'` because `'S' < 's'` in ASCII, so the float
comes before the int. Reading the raw bytes of `default.mhscene` confirms that
ordering -- `G@\\x10...` (4.0) precedes `K\\x01` (1).

WHAT IT DROPS, AND WHY THAT IS NOT LOSS. MEASURED across all three shipped
scenes: `fov` is 180.0, `attenuation` 0.0, `areaLights` 1, `areaLightSize` 4.0
and `specular` white -- IDENTICAL in every light of every file. They carry no
information to preserve. They are also meaningless in this port's shader, which
is a microfacet BRDF with directional lights (`resources/shaders/rhi/pbr.frag`)
rather than the reference's fixed-function point lights. And `fov` 180 with
zero attenuation is already the definition of a light that neither falls off
nor has a cone -- i.e. a directional one -- so turning `position - focus` into
a direction is faithful to what the file meant, not an approximation of it.

What genuinely varies between the three, and is therefore what this keeps: the
number of lights, their colours, and the ambient colour.
"""
import json
import pickle
import sys
from pathlib import Path


class _Color:
    """Stand-in for the reference's `material.Color`.

    Its pickles are NEWOBJ + BUILD, so the fields arrive as `_r`/`_g`/`_b` in
    the instance dict and no code of the reference's needs to run.
    """


class _Restricted(pickle.Unpickler):
    """An unpickler that can build exactly one class and nothing else."""

    def find_class(self, module, name):
        if (module, name) == ("material", "Color"):
            return _Color
        raise pickle.UnpicklingError(f"refused to resolve {module}.{name}")


# Both in the sorted order the reference writes them in; see the module docstring.
_ENVIRONMENT = ["ambience", "skybox"]
_LIGHT = sorted(
    ["position", "focus", "color", "specular", "fov", "attenuation", "areaLights", "areaLightSize"]
)


def _rgb(value):
    """A `_Color` (or `[_Color, n]`, which is how `specular` is defaulted) as a list."""
    if isinstance(value, list):
        value = value[0]
    return [round(getattr(value, "_r"), 4), round(getattr(value, "_g"), 4), round(getattr(value, "_b"), 4)]


def _direction(position, focus):
    """`position - focus`, normalised. See the docstring for why this is exact."""
    d = [p - f for p, f in zip(position, focus)]
    length = sum(c * c for c in d) ** 0.5
    if length <= 0.0:
        raise ValueError(f"light at its own focus {position}, no direction to take")
    return [round(c / length, 4) for c in d]


def convert(path: Path) -> dict:
    with path.open("rb") as handle:
        unpickler = _Restricted(handle)
        version = unpickler.load()
        environment = {name: unpickler.load() for name in _ENVIRONMENT}
        count = unpickler.load()
        lights = [{name: unpickler.load() for name in _LIGHT} for _ in range(count)]

    if version != 5:
        raise ValueError(f"{path}: mhscene version {version}, this tool knows 5")
    if environment["skybox"] is not None:
        # Never seen in the shipped three. Refuse rather than drop it silently:
        # a skybox is an environment map, which is the one thing pbr.frag says
        # outright that it does not have.
        raise ValueError(f"{path}: carries a skybox, which this port cannot render")

    return {
        "name": path.stem,
        "ambient": _rgb(environment["ambience"]),
        "lights": [
            {"direction": _direction(light["position"], light["focus"]), "color": _rgb(light["color"])}
            for light in lights
        ],
    }


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for arg in argv[1:]:
        path = Path(arg)
        scene = convert(path)
        out = path.with_suffix(".json")
        out.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
        lights = ", ".join(str(light["color"]) for light in scene["lights"])
        print(f"{path.name} -> {out.name}: ambient {scene['ambient']}, {len(scene['lights'])} light(s) {lights}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
