# Third-party licence texts

`LICENSING.md` §8 step 6 requires the licence text of every dependency to live
here. This directory holds those texts; `LICENSING.md` §5.1 remains the
authoritative list of what we depend on and why.

| File | Covers |
|---|---|
| `Apache-2.0.txt` | The Apache License 2.0, verbatim, as shipped in KTX-Software `LICENSES/Apache-2.0.txt`. Covers **libktx** and its in-tree `external/basisu`, `external/dfdutils` and `external/astc-encoder`. |
| `KTX-Software-LICENSE.md` | KTX-Software's own licence summary, verbatim from the v4.4.2 tree. |

## libktx: one file is deliberately excluded, and it is not cosmetic

KTX-Software ships `external/etcdec/etcdec.cxx` under the **Ericsson Texture
Compression Codec Software License Agreement** (`LicenseRef-ETCSLA`). Upstream
says so in its own words, in `KTX-Software-LICENSE.md` here:

> The file lib/etcdec.cxx is not open source. It is made available under the
> terms of an Ericsson license, found in the file itself.

It grants rights **only** "for the purpose of developing, manufacturing,
selling, using and distributing products including the Software in binary form,
which products are used for compression and/or decompression according to the
Khronos standard specifications OpenGL, OpenGL ES and WebGL", and terminates on
patent litigation. That is a **field-of-use restriction**, which
`LICENSING.md` §5.2 forbids outright, and it is an additional restriction of
the kind AGPL-3.0 distribution cannot carry.

**We therefore never compile it.** `-DKTX_FEATURE_ETC_UNPACK=OFF` removes it:

- `CMakeLists.txt:47` declares `option( KTX_FEATURE_ETC_UNPACK "ETC decoding
  support." ON )` — so the default is ON and the flag must be set explicitly.
- `CMakeLists.txt:435-438` appends `external/etcdec/etcdec.cxx` to the libktx
  sources **only** inside `if (KTX_FEATURE_ETC_UNPACK)`.
- `CMakeLists.txt:572` defines `SUPPORT_SOFTWARE_ETC_UNPACK` from the same
  option, and `lib/etcunpack.cxx` guards its entire body on it (`#if` at line
  29 through `#endif` at line 277).

Verified rather than assumed: a v4.4.2 build configured with that flag compiled
66 objects, and `etcdec` appears **0 times** in the build log. We *encode* to
ETC1S/BasisLZ; software ETC *decoding* is not something a glTF exporter needs,
so nothing is given up by excluding it.

`src/io/CMakeLists.txt` sets the flag with `CACHE BOOL "" FORCE`, which is
deliberate: a `FORCE`d cache entry overrides the command line, so
`-DKTX_FEATURE_ETC_UNPACK=ON` cannot smuggle the Ericsson file into a build.
Changing this control means editing the file, in a diff someone reviews.

## The gate, and the proof it can fail

A comment cannot enforce a licence, so `ktx_excludes_ericsson_sla`
(`tests/ktx_excludes_ericsson_sla.cmake`) asserts it against the **built
binary**. It reads symbols with `nm` rather than archive members with `ar`,
because this project builds libktx **shared** and `ar` reports "Inappropriate
file type or format" on a Mach-O dylib — that was the first version of this
gate, and it failed safe rather than passing on a check it could not perform.
`etcdec.cxx` contributes `setupAlphaTable`, `decompressBlockETC2c` and
`alphaTableInitialized`, so their absence is the evidence.

It carries a positive control: if `ktxTexture2_CompressBasis` is missing the
gate fails rather than passes, so a stripped or wrong file cannot produce a
meaningless green.

Both directions are measured, because a gate never seen red is decoration:

| Configuration | `etcdec` compiled | Gate |
|---|---|---|
| As shipped (`KTX_FEATURE_ETC_UNPACK OFF`) | 0 | **passes** |
| This file mutated to force it `ON` | 1 | **fails**, naming the cause |

The red case needs the *file* edited, not a `-D` flag — the `FORCE` above is
exactly why. Line numbers cited here are v4.4.2
(`4d6fc70eaf62ad0558e63e8d97eb9766118327a6`) and are worth re-checking on any
version bump; the guard is what matters, not the line it sits on.
