# `.mhm` — saved character

## Purpose and provenance

A saved human: modifier values, plus references to the skeleton, proxies,
materials and pose. It is a *recipe*, not geometry — replaying it reproduces the
character.

No specification. The definition is `legacy/python/apps/human.py` (`load`) and
the per-plugin `loadHandler`s. Reader: `src/core/Mhm.cpp`.

**None shipped.** The format is exercised by files the reference itself wrote.

## Grammar

Line-oriented, whitespace-split, first token is the key.

| Key | Payload |
|---|---|
| `version` | `v1.2.0` — see below |
| `name` | free text |
| `uuid` | |
| `tags` | |
| `camera` | six floats |
| `modifier` | `<group>/<name> <value>` — repeated, one per modifier |
| `subdivide` | `True` / `False` |
| *(others)* | skeleton, proxy, material, pose — preserved verbatim |

## Semantics

- **`version` comparison uses major and minor only.** `v1.3.0` carries a patch
  component that the comparison ignores. The reference gets this from
  `from_chars` stopping at the second `.`; we scan the digit run explicitly,
  because relying on a parser's stopping behaviour breaks the moment parsing
  moves behind a helper that demands full consumption.
- **Proxies are referenced by UUID only.** `apps/gui/proxychooser.py:549-551` logs
  *"Loading proxies from filename is no longer supported, they need to be
  referenced by UUID"* and refuses, which is why UUID resolution is what makes a saved
  character's clothes load at all.
- **Unrecognised lines are preserved verbatim** rather than dropped, so loading
  and re-saving a file written by a newer version does not silently discard its
  settings.

## Our support

| | |
|---|---|
| Read | yes — round-trip parity against a real `.mhm` written by the reference |
| Write | not yet |
| Preserved but not yet acted on | skeleton, pose, proxy, material lines |

Round-trip parity means: a `.mhm` the reference wrote, loaded here and applied,
reproduces the geometry the reference itself produces from that file.
