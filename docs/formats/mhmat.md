# `.mhmat` — material

## Purpose and provenance

Surface appearance: colours, seven texture channels, and shader configuration.
Blinn-Phong, not PBR.

No specification. The definition is `legacy/python/shared/material.py:364-500`
(reading) and `:511-620` (writing). Reader and writer: `src/core/Material.cpp`.

**3 shipped**: `data/materials/xray.mhmat`, `data/skins/default.mhmat`,
`data/eyes/materials/brown.mhmat`. CC0.

## Grammar

Line-oriented, whitespace-split. `#` and `//` are comments **only as the first
token**.

| Group | Keys |
|---|---|
| Identity | `name` `description` `tag` |
| Colours | `ambientColor` `diffuseColor` `specularColor` `emissiveColor` `viewPortColor` |
| Scalars | `shininess` `opacity` `translucency` `viewPortAlpha` |
| Flags | `shadeless` `wireframe` `transparent` `alphaToCoverage` `backfaceCull` `depthless` `castShadows` `receiveShadows` `autoBlendSkin` |
| SSS | `sssEnabled` `sssRScale` `sssGScale` `sssBScale` |
| Textures | `diffuseTexture` `bumpmapTexture` `normalmapTexture` `displacementmapTexture` `specularmapTexture` `transparencymapTexture` `aomapTexture`, each with a matching `...Intensity` except diffuse |
| Shader | `shader` `shaderParam` `shaderDefine` `shaderConfig` `uvMap` |

## A real example

```
name Eye_brown
tag MakeHuman™
ambientColor 0.11 0.11 0.11
shininess 1.0
diffuseTexture brown_eye.png
shader data/shaders/glsl/litsphere
shaderParam litsphereTexture data/litspheres/skinmat_eye.png
shaderConfig ambientOcclusion True
```

## Semantics

- **Booleans** accept `yes` / `enabled` / `true`, case-insensitively
  (`material.py:357`). The writer emits `True`/`False`, which that reader
  accepts.
- **Keys are read case-INsensitively here but case-SENSITIVELY by the
  reference** (`material.py:369-448` compares `words[0]` directly). Writing is
  therefore not symmetric with reading: a file saying `diffusetexture` loads in
  this port and is **silently ignored** by MakeHuman 1.x — the texture simply
  disappears. The writer must use the canonical camelCase spellings.
- `translucency`, `opacity`, `shininess`, `viewPortAlpha` clamp to `0..1`.
- `uvMap` pointing at `uvs/default.mhuv` is a sentinel meaning "no override".
- **`shader`** is a stem: `_vertex_shader.txt` and friends are stripped.
- `effectiveDefines()` derives the shader-variant key from the enabled channels,
  **sorted** — the sorted list is the cache key, so order is load-bearing.

## Our support

| | |
|---|---|
| Read | yes — all keys above |
| Write | **yes, and lossless** |
| Deliberately different | see below |

**Our writer is lossless; the reference's is not.** Verified:

- It never writes `tag`. Round-tripping `brown.mhmat` through `material.py`
  turns `['makehuman™']` into `[]`.
- It never writes `autoBlendSkin` or the viewport colour.
- It **cannot save `default.mhmat` at all** headlessly: `autoBlendSkin` routes
  `diffuseColor` through the skin blender, so `toFile` raises
  `AttributeError: 'NoneType' object has no attribute 'selectedHuman'`. In-app
  it writes the *blended* colour over the authored one.

Losing user data on save is a defect, not a format rule, so everything the
reader understands is written.

## Compatibility

Round-trip verified in both directions: our reader against our writer (a second
save must reproduce the first), and `tools/verify_material_roundtrip.py` feeds
our output back through `material.py` and compares field by field.

## Known issues in the reference

- Discards the result of parsing a colour, so `diffuseColor 0.5 0.5` loads
  silently as white.
- `translucency` is clamped in the reference but a typo'd value elsewhere is
  not — see the port for which keys clamp.
