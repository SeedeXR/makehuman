# Session Handover Log

Newest entry first. Every entry carries a `YYYY-MM-DD HH:MM:SS` timestamp.

---

## 2026-08-29 — Session 024 · all 24 Euler conventions, and a third licence

**Ended:** 2026-08-29 14:49:38 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

120 captured cases — 24 conventions × 5 angle sets — plus the quaternion
algebra. CI green.

### A test that was covering nothing

The gimbal-lock angle sets originally used `1.5707963`. Against `_EPS` of
8.9e-16 the guard value is ~1e-8, so **the singular branch was reached zero
times out of 120** while the test's own comment claimed it was covered.

Switched to `math.pi/2` exactly: 24 of 120 now take that path, and the test
**asserts that count** so the coverage cannot silently lapse again.

I only found it because I checked what the branch coverage actually was instead
of trusting the comment I had just written. Worth repeating on any test whose
value depends on hitting a specific branch.

### Quaternion sign

`quaternionFromMatrix` uses the trace method, not the reference's eigenvector
path. Verified across all 120: magnitudes agree to 4.4e-16, **sign differs on
18**. `q` and `−q` are the same rotation, so the test compares up to sign *and*
checks the quaternion rebuilds the same matrix — pinning the rotation exactly
rather than weakening the assertion.

### A real licensing finding

`legacy/python/core/transformations.py` carries **two conflicting licences**:
Christoph Gohlke's **BSD-3-Clause** notice in actual comments at the top, and
MakeHuman's **AGPL** boilerplate inside the module docstring — on a file whose
stated author is Gohlke. Stamping an AGPL header onto someone else's BSD code
does not relicense it.

So the port is SPDX **BSD-3-Clause**, reproduces the upstream notice, and is the
only file in the tree under a third licence. That is *better* for us: BSD is
permissive, so these conversions sit legitimately in the Apache-2.0 foundation
module instead of being trapped on the AGPL side. Recorded as `LICENSING.md` §4.1.

**The new BSD-notice gate found a real gap on its first run** — `Transform.cpp`
had deferred the notice to the header, and BSD requires *source
redistributions* to retain it.

### Convention

`[w, x, y, z]`, scalar FIRST, throughout. Eigen's `.coeffs()` is `[x, y, z, w]`.
A test pins the layout directly, because a swap still yields a unit quaternion
that still rotates — just not as intended.

265/265 across all four builds.

**Next:** wire these into pose loading — `.mhpose`, pose units, and the
order-dependent slerp composition blend.

---

## 2026-08-29 — Session 023 · CPU skinning: the rig deforms the mesh

**Ended:** 2026-08-29 14:29:31 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

Where the rest matrices and the weights finally meet. Both stages match the
reference under a real pose:

| | scope | worst delta |
|---|---|---|
| pose matrices | 163 bones, element-wise | 6.7e-6 |
| skinned mesh | 19,158 vertices | **3.8e-6 dm** (0.38 µm) |

Float32 rounding, not an algorithmic difference.

### The fixture is a real pose

Seven named bones rotated by set angles: **18,069 of 19,158 vertices move**, max
displacement 3.2 dm. A test asserts those numbers directly, so if the pose ever
became trivial, the parity test could not quietly stop testing anything. That
guard exists because a near-identity fixture is the classic way a skinning test
passes while proving nothing.

### `matPoseVerts = matPoseGlobal · inv(matRestGlobal)`

The reference wraps that inverse in a bare `except` — a degenerate bone makes
the matrix singular. **Ours cannot be.** `buildRestMatrices` refuses a
zero-length bone and produces an orthonormal basis, so `rigidInverse` is exact
and total, and there is no failure path to swallow. The earlier decision to
reject degenerate bones pays off here.

### Accumulated matrix skinning

Blend the **matrices**, then apply once — not transform-by-each-bone-and-blend.
Identical for affine transforms, but one matrix-vector multiply per vertex
instead of one per influence.

### The property test that needs no fixture

The identity pose must move nothing. It catches a transposed matrix or a wrong
inverse with no captured data at all, because if `matPoseVerts` is not identity
at rest, every vertex drifts. Passes to within 1e-4 dm.

### Performance

**0.11 ms** to skin the whole body at 4 influences; under 0.01 ms for the
skinning matrices. Both comfortably per-frame.

257/257 across all four builds. CI green, 7/7.

**Next:** M5's remaining items — Euler conventions and `[w,x,y,z]` quaternions
(Eigen's `.coeffs()` is `[x,y,z,w]`, the classic trap), then pose units and
`.mhpose`.

---

## 2026-08-29 — Session 022 · vertex weights

**Ended:** 2026-08-29 14:11:59 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

`.mhw` loading with parity across **139 weighted bones / 57,107 entries**, plus
the compiled 4-influence form across all **19,158 vertices**. Fixture captured
from the reference for this commit.

### Four behaviours that are all load-bearing

1. **The file's numbers are relative, not absolute.** `wtot[v]` is summed over
   every bone *first*, then each weight stored as `w / wtot[v]`. Storing the raw
   numbers scales every vertex by an arbitrary factor.
2. A vertex listed twice under one bone is **merged**, not overwritten.
3. Sub-threshold weights are dropped **after** normalising, not before.
4. **An unweighted vertex binds to the root bone at weight 1.** Without it, the
   vertex collapses to the origin the moment the rig is posed — a failure that
   looks like a modelling bug rather than a loader bug.

### Truncation

Clamping to 4 influences re-normalises, or every heavily-weighted vertex loses
mass and drifts. That path is genuinely exercised, and I checked rather than
assumed: the rig reaches **12** influences and **5,923 vertices** actually hit
truncation. A test asserts `maxInfluences() == 12` precisely so the truncation
test cannot silently stop testing truncation if the data ever changes.

Ties break by **descending bone index** — Python's `sorted(reverse=True)` over
`(weight, bone_index)`. Arbitrary, and replicated because on a mirrored body two
bones can carry identical weights, so it decides which influence survives.

### Performance

15.8 ms to load 57k entries, 1.0 ms to compile to 4 influences. No Python
comparison: the weight path is reachable headlessly but the skeleton it needs is
not, so a like-for-like number does not exist.

252/252 across all four builds; all gates pass locally.

**Next:** CPU linear blend skinning — `matPoseVerts = matPoseGlobal · inv(matRestGlobal)`,
which is where the rest matrices and the weights finally meet.

---

## 2026-08-29 — Session 021 · rest matrices, full element-wise parity

**Ended:** 2026-08-29 14:00:11 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

`matRestGlobal` and `matRestRelative` match the reference **element for element
across all 163 bones**, against the fixture captured sessions ago and unused
until now.

Checked that the parity is real, not a comparison of zeros: 1,987 of the 2,608
fixture floats are non-zero, and bone 0 agrees to five decimals on every
element.

### Three conventions that all had to be right at once

Each of these fails *quietly* — the rig still looks like a rig:

1. **Row-major storage, axes as COLUMNS**, translation in the last column
   (numpy `mat[:3,0]` / `mat[:3,3]`). Either half backwards gives plausible
   matrices that place every bone wrongly.
2. **`cross(yvec, pvec)`**, in that order, for the plane normal. Swapping the
   arguments flips every bone's roll 180° and *still* yields a valid orthonormal
   basis, so nothing downstream complains.
3. **X is rebuilt as `cross(Y, Z)`**, not the plane normal itself. The normal
   only seeds the basis; it is generally not perpendicular to the bone. The
   reference keeps the ideal version commented out directly above for this
   reason.

### Assumptions verified rather than asserted

`rigidInverse` assumes an orthonormal upper-left 3×3, which makes the parent
inverse exact instead of accumulating error. A separate test checks all three
axes are unit and mutually perpendicular on every bone, and that Y is the bone's
own direction. A third confirms `matRestRelative` composes back to
`matRestGlobal` through the parent chain — the invariant every skinning path
will depend on.

`normalize()` returns false on a degenerate vector instead of dividing; the
reference's `matrix.normalize` divides unguarded, so a collapsed plane yields
inf/nan that propagates into every child matrix.

### Performance

0.02 ms to rebuild every rest matrix, 0.01 ms to re-place the joints — the rig
can follow the mesh per frame. No Python comparison for either: the reference's
skeleton path needs `G.app` and cannot run headless.

243/243 across all four builds. CI green.

**Next:** `.mhw` vertex weights — normalisation, influence clamping, then CPU LBS.

---

## 2026-08-29 — Session 020 · M5 begins: .mhskel, and a lesson that did not stick

**Ended:** 2026-08-29 13:49:57 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### The skeleton parser

New AGPL module `mh_rig`. **Exact bone-order parity on all 163 bones.**

The thing worth carrying forward: **there are two orderings in the reference
and they are not the same one.**

1. `fromFile` (`skeleton.py:111-124`) relaxes over the bone map *in file order*.
   The reference calls this breadth-first; it is not. It fixes the order
   children attach to each parent.
2. `getBones()` (`__cacheGetBones`) then does a **real** BFS from the roots with
   a deque. That is the canonical index order — rest-matrix rows, and what every
   exporter writes.

I implemented (1) alone first. It put **153 of 163 bones in the wrong slot**
while still producing a perfectly valid parents-first list that passed every
structural check I had. Only the captured reference order caught it. Structural
invariants would never have found this; a fixture did.

Because (1) depends on file order, the JSON parser must preserve key order —
hence `nlohmann::ordered_json` specifically, pinned by SHA256. The CI dependency
gate caught that I had not recorded it in `LICENSING.md`, which is that gate
earning its keep.

Performance: 1.21 ms to parse; `updateJoints` under 0.01 ms, so re-fitting the
rig every frame is free. **No Python comparison** for either — `mhskeleton.load`
reaches `G.app.selectedHuman` and cannot run headlessly. An absent number, not
an invented one.

### The lesson that did not stick

CI failed on this commit. The legacy-tree gate matched a **comment** in
`src/rig/CMakeLists.txt` recording which `legacy/` file `mh_rig` ports.

That is the **third** gate to match the prose explaining its own rule:

| Gate | Matched | Should have matched |
|---|---|---|
| Forbidden deps | prose about the Autodesk SDK | `#include <fbxsdk`, a link flag |
| Licence boundary | `src/io/CMakeLists.txt` saying "must never depend on one" | a real `mh::core` link |
| Legacy tree | `src/rig/CMakeLists.txt` naming the file it ports | a real `include_directories(legacy/...)` |

**The third happened in the same session I wrote up the second.** Recording the
pattern was not enough to avoid repeating it.

Fixed structurally, not case by case: comment-stripping and syntax anchoring
across the gates, and the rule written into `memory/instruction.md` so the next
gate starts from it. Both gates were then proved **in both directions** — a real
violation introduced and observed to fire, then reverted and observed to go
quiet. All three of these shipped because a gate that has only ever been seen
passing is not known to work.

### Verified

240/240 in debug, release, ASan+UBSan and the forced-fallback build. CI green,
7/7 jobs, read rather than assumed.

**Next:** rest matrices (`matRestGlobal` / `matRestRelative`) — the fixture is
already captured and unused so far.

---

## 2026-08-29 — Session 019 · the licence boundary is now real

**Ended:** 2026-08-29 13:29:07 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

`mh_io` was stamped Apache-2.0 while `PUBLIC`-linking `mh::core` (AGPL). Fixed:
it now links `mh::foundation` and nothing else of ours.

**The evidence, because the stamp is not evidence:**

    nm -u libmh_io.a | c++filt | grep mh::core   ->  0 undefined symbols

**A correction I had to make mid-task.** In the previous commit I wrote that no
ported algorithm crossed the boundary. Wrong — I had grepped `mesh.` call sites
and missed `core::RenderMesh::build` in the glTF and FBX paths. The unweld is a
port of `module3d.py` and it was running inside the Apache module. The fix
moved it out: `writeGlb`/`exportScene` now take a `RenderView` the caller has
already built, which also makes the unweld's cost visible at the call site.

**The bridge** is four plain-data types in foundation — `MeshView`,
`RenderView`, `MeshData`, `MaterialDesc`. Data, no behaviour. `core` produces
them (`Mesh::view()`, `RenderMesh::view()`, `Material::desc()`,
`Mesh::fromData()`), `io` consumes them, so the dependency runs AGPL → Apache.

**The tests failing to link was the boundary working** — they had been getting
`mh::core` transitively through io.

**Two CI gates**, because a comment in `src/io/CMakeLists.txt` saying "must
never depend on one" sat three lines above the line that did, for months. The
SPDX gate cannot catch this: it checks a file *declares* a licence, not that
the dependency direction is legal.

The gate strips comments before grepping — `src/io/CMakeLists.txt` explains why
it must not link `mh::core`, and the naive version flagged that explanation.
**That is the second time I have made exactly this mistake** (the FBX gate had
it too). Pattern worth remembering: a gate that greps for a forbidden string
will match the document that forbids it.

Proved the gate fires by reintroducing the link and watching it trip, then
reverting.

CI green. 232/232 in debug, release, ASan+UBSan and the forced-fallback build.

**Next:** M5 — `.mhskel` parser against the Mixamo bone order recorded in
`docs/rig/mixamo_bone_order.md`.

---

## 2026-08-29 — Session 018 · CI had never passed; resources; Mixamo

**Ended:** 2026-08-29 11:50:38 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### The thing I got wrong

The owner pointed out CI was failing. It had failed on **every push since the
first C++ commit**, and I had not looked once. I wrote "both new gates pass
locally" in a commit message and treated that as evidence about CI. It is not
evidence about CI; it is evidence about this laptop.

Root cause: local is Apple clang 21, the `macos-15` runner is Xcode 16.4, whose
libc++ has only the **integral** `from_chars`/`to_chars`. The floating-point
overloads are C++17 on paper and absent in practice, so the float call resolved
to the deleted `bool` overload. Nothing about this is visible locally.

Fixing it took **two** commits, and the second is the instructive one. The first
fixed five float parsers and missed a sixth, because `Mhm.cpp` had its own
`parseFloat` under a name my hand-audit did not match. My forced-fallback build
could not catch it either: `-DMH_HAVE_FP_CHARCONV=OFF` changes the *shim's*
internals, not what other files call directly — so the guard I built to prove
the fix did not cover the thing that broke.

The rule "don't call `std::from_chars` on a float" is not checkable. **"Only
`foundation` includes `<charconv>`" is**, and that is now a CI gate. Integer
parsing moved into foundation too — not because the integral overloads are
missing anywhere, but because containment is what makes the grep possible.

CI is now green, verified by reading the run, not by inferring it: 7/7 jobs.

### New: mh_foundation (Apache-2.0)

Owns float/int parsing and formatting. `std::from_chars`/`to_chars` where the
library really has them, a `uselocale("C")` fallback where it does not,
availability decided by CMake `check_cxx_source_compiles` rather than a feature
macro — the macro is what lied. `uselocale` over the `_l` variants because it is
thread-local POSIX and exists on glibc too, where `snprintf_l` does not.

A CI job pins the fallback branch open. Newer runners will make it the unused
path, and it would rot silently until the next machine that needs it.

### Licence boundary — open issue, now recorded in todo.md

While placing the shim I found `mh_io` is stamped Apache-2.0 and
`PUBLIC`-links `mh::core` (AGPL), violating the rule its own CMakeLists states
three lines above. Its Apache stamp therefore buys nobody anything today. Not
silently patched — it is a real decision (move shared types down / relicense io
/ invert the interface) and it is written up as an OPEN ISSUE.

### Owner decisions this session

- **Bone naming: the Mixamo standard.** `docs/rig/mixamo_bone_order.md` records
  it as *measured* — 65 bones, extracted with assimp, verified byte-identical
  across all seven reference clips (7/7, 0 differing). It also records the
  `$AssimpFbx$` trap: assimp splits each FBX node transform into synthetic
  Translation/PreRotation/Rotation nodes, so a naive walk gives ~190 nodes and
  every parent index is wrong. Invisible until the rig is already broken.
- **Mixamo rigs committed.** I flagged the licensing question; the owner
  confirmed. `LICENSING.md` §5.5 records the basis and one caveat.

### resources/ populated

57 Lucide icons (ISC, stroke normalised 2 → 1.5 per `design.md` §4, all
re-validated as XML), 42dot Sans variable (OFL-1.1), and the **litsphere**
shader ported to Qt RHI GLSL 450 — compiling via `qsb` to SPIR-V + GLSL 450 +
MSL 12, with the `0.495` constant verified present in the generated Metal.

The font download **truncated silently** the first time — 1.29 MB of 5.77 MB —
and `file` still reported "TrueType Font data". Now validated by size and by
walking the sfnt table directory. A header is not evidence of a whole file.

### On the FBX question

The owner asked where the FBX writer is, "given we are using autodesk fbx sdk".
We are **not** using it, and never have: `LICENSING.md` §5.3 records the EULA
conflict found by reading the installed `License.rtf`. FBX export/import goes
through assimp in `src/io/SceneIO.cpp`. Verified this session: base mesh out at
4,430,208 bytes, magic `Kaydara FBX Binary`, version **7500**, re-imported to
21,833 verts. `otool -L` confirms no fbxsdk is linked.

---

## 2026-08-29 — Session 017 · the .mhmat writer, and why a one-sided test lies

**Ended:** 2026-08-29 11:21:33 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### The writer

`.mhmat` save, closing the M4 material item. The reference's own writer is
explicitly **not** the model, because it loses data — verified, not assumed:

- Round-tripping `brown.mhmat` through `material.py` turns `tag ['makehuman™']`
  into `[]`. It never writes `tag` at all.
- It never writes `autoBlendSkin` or the viewport colour.
- It cannot save `default.mhmat` **at all** headlessly: `autoBlendSkin` routes
  `diffuseColor` through the skin blender, so `toFile` raises
  `AttributeError: 'NoneType' object has no attribute 'selectedHuman'`. In-app
  it writes the *blended* colour over the authored one.

Hard rule 3 says do not port a known-broken behaviour, so ours is lossless.

### The part worth remembering

The C++ round-trip test passed on the first run. It was also **not sufficient**,
and would have shipped a real bug.

Our reader lowercases the key before matching. The reference's compares
`words[0]` directly (`material.py:369-448`). The writer had reused the
lowercase lookup keys, so it emitted `diffusetexture` — which our reader
round-trips perfectly and MakeHuman 1.x silently ignores. The texture simply
disappears, with no error anywhere.

No C++-only test could catch that: reader and writer shared the same wrong
assumption, so they agreed. `tools/verify_material_roundtrip.py` caught it in
one run by feeding our output back through the reference, which reported
`diffuseTexture -> None`.

The lesson is general enough to be worth stating: **a round-trip test through a
single implementation proves self-consistency, not correctness.** Where a file
format has another reader in the world, that reader is the oracle. The tool now
lives in `tools/` and the canonical spellings are guarded by a literal string
check so CI catches a regression without needing Python.

Recorded as `project_context.md` §8.0 (format traps) and §8.1 (the lossless-save
divergence).

Also hardened `portablePath` while in there: `relative(x, "")` for a bare
filename, and a first-COMPONENT check for escaping instead of a `starts_with("..")`
on the string, which would misjudge a directory legitimately named `..cache`.

### Verified

232/232 in debug, release and ASan+UBSan. The reference reads all three written
materials field for field. clang-format and SPDX clean. `data/` left clean --
the tests write beside the source material on purpose, so relative paths resolve
the same way, and remove the files afterwards.

---

## 2026-08-29 — Session 016 · face hiding, and 14 review findings

**Ended:** 2026-08-29 11:11:51 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### Face hiding (closes an M2 item and an M4 item)

The rule that matters, and the one that is easy to get backwards: **a face
survives if ANY of its corners is still visible.** The reference's docstring
says "a face is masked if all of the vertices that define it are masked", and
the code agrees -- `changeVertexMask` (guicommon.py:532-557) derives the mask
via `getFaceMaskForVertices`, which marks every face incident to a *visible*
vertex.

I did not want that resting on a reading, so the fixture pins it: the `stride`
mask hides every 7th vertex -- 2,737 of 19,158 -- and hides **zero** of 18,486
faces. Under the inverted rule almost the whole mesh would vanish. The two
readings cannot both pass.

No shipped asset exercises this at all: all four `.mhclo`/`.proxy` files declare
zero `delete_verts`. The masks are therefore synthetic, but they run through the
reference's own `getFaceMaskForVertices` + `updateIndexBufferFaces`, so the
oracle is still the reference's behaviour.

`RenderMesh` now splits the way the reference does -- unweld once, rebuild the
index per mask. **0.21 ms vs 2.19 ms** for a full rebuild, and 8.7x the
reference's 1.85 ms. Draw ranges match `grpix` exactly, group by group, for all
four masks.

One correction along the way: I first compared `grpix` in corner units and it
failed. `grpix` is in **face** units -- the reference's `index` is a 2-D
(faces, 4) array, so `np.unique(..., return_index=True)` returns rows. Confirmed
by summing the counts: 18,486, the face count, not 73,944.

### Review findings — 14 fixed, 2 of them memory safety

The background review of the io/asset layer came back while this was in flight.
Everything below was reproduced before it was fixed, and each has a regression
test that fails on the pre-fix code (verified by reverting two of them).

**Both HIGH findings were the same root cause**: `delete_verts` indices from the
file were used unbounded.

- `resize(v + 1)` in uint32 wrapped to 0 at `UINT32_MAX`, emptying the vector,
  and the next line wrote to index 4294967295. ASan: *BUS ... WRITE memory
  access* at `loadProxy`. **A five-line text file was enough.**
- The `-` range loop never terminated at `UINT32_MAX` (`i` wraps to 0, condition
  holds again). Without the wrap, `delete_verts 4294967290` still allocated
  **4.29 GB** from a two-line file.

One bound check at the point the index enters fixes both.

**The comment that was wrong.** `ObjWriter.cpp` said `snprintf` "avoids
locale-dependent iostream output". It does not -- `snprintf` honours
`LC_NUMERIC` exactly as iostreams do, and `std::locale::global` sets the C
locale too. Verified: under `de_DE.UTF-8` a vertex wrote as `v 0,5000 0,0000
0,0000`. The glTF case is worse because it stays *valid JSON*: `"max":[0.2,0.3]`
becomes a five-element array, so the accessor bounds are silently garbage and no
validator reports a parse error. Both writers now use `std::to_chars`, which is
locale-independent by definition. OBJ byte-parity still passes, so the C-locale
output is unchanged.

**A startup crash.** `recursive_directory_iterator`'s `operator++` throws; the
`error_code` overload only covers construction. One unreadable subdirectory
anywhere under a search path terminated the process.

Also fixed: a duplicate UUID removed the asset from `entries()`/`findByTag()`
entirely rather than only from resolution; malformed colours loaded silently as
white; `translucency` was not clamped to 0..1; a leading `+` was rejected on a
legal asset; `viewPortAlpha` did not set `hasViewPortColor`; a truncated `verts`
line was skipped rather than rejected (which shifts every later proxy vertex
onto the wrong reference triangle); an unwritable `.mtl` was reported as
success after the OBJ had already emitted `mtllib`; a `-` range did not carry
across a line break as it does in the oracle; `z_depth -5` became 50; the GLB
byte size was unchecked against its uint32 fields; non-finite values reached
both the exporter and the importer.

Five of these are **deliberate divergences** from the oracle rather than parity
fixes, so they are now recorded in `project_context.md` §8.1 -- otherwise a
future parity test would "correct" them back.

### Verified

227/227 in debug, release and ASan+UBSan. clang-format clean (via
`xcrun -f clang-format`; there is no Homebrew LLVM on this machine). SPDX clean.
No benchmark regressed.

---

## 2026-08-29 — Session 015 · legacy consolidation, reviews, visual reference

**Ended:** 2026-08-29 10:48:44 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### Repository consolidation
Everything from the Python era now lives under `legacy/`, moved with `git mv` so
history is preserved:

    legacy-python/   -> legacy/python/
    buildscripts/    -> legacy/buildscripts/
    Jenkinsfile      -> legacy/Jenkinsfile
    requirements.txt -> legacy/requirements.txt
    README.md        -> legacy/README.md     (a new root README replaces it)

34 files referencing the old path were rewritten, and the `file:line` citations
throughout the headers and `memory/` were **spot-checked against the moved
files** — `module3d.py:411`, `humanmodifier.py:644`, `glmodule.py:479` all still
land on the code they claim. The reference oracle and `capture_fixture.py` were
re-run from their new home.

**CI now enforces that `legacy/` is not part of the build.** If the C++ build
ever reaches into it, the port has acquired a hidden dependency on code meant to
be deleted at the end.

Also corrected a CI gate that was wrong: the forbidden-dependency scan matched
the *string* "fbxsdk" anywhere, which would now fail on our own explanatory
comments about why the Autodesk SDK is not used. It now matches actual use — an
include, a link flag, or a CMake target.

### Reviews
Both were skipped on the FBX work; that was a fair criticism and is fixed.
**Ponytail** cut four items that were declared but never exercised —
`separateJson`, `flipForward`, `bufferBytes`, `sourceFormat`. `flipForward` is
the instructive one: it was implemented and plausible-sounding, but both glTF and
MakeHuman are Y-up right-handed, so it corrected nothing real. Net −20 lines.

### Visual reference
`memory/design.md` had been prose-only. It now opens with a link to a rendered
specimen — the workspace with a working six-dot panel menu, colour tokens as real
swatches with measured contrast, the type scale in 42dot Sans, controls, the
Lucide set, and the measured verification table. It is built **in** the tokens it
documents, so spec/specimen drift is visible immediately.

The accent `#f58220` was verified by reading the shipped `icons/makehuman.svg`
rather than chosen, so the palette is grounded in the existing brand.

### Verified
205/205 pass in debug, release and ASan+UBSan after a clean from-scratch rebuild.

---

## 2026-08-29 — Session 005 · M3 targets and macro factors

**Ended:** 2026-08-29 06:36:36 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### Delivered
- **Target system**: `.target` parser, `applyTarget`, `TargetLibrary`. All 1,280
  shipped targets parse — 0 failures, 0 malformed lines, 6,147,800 sparse
  entries, max index 19,157 (= nVerts-1). Byte-level parity on 24 sampled
  targets and on the applied stack across all 19,158 vertices.
- **`MacroFactors`**: all macro derivations, verbatim, including ethnic
  renormalisation with its three degenerate branches. **920 assertions of
  parity** over 34 parameter combinations x 27 values.

### Benchmarks (release)
| Operation | C++ | Python | |
|---|---|---|---|
| load 200 targets | 14.2 ms | 106.2 ms | 7.5x |
| apply 200 targets | 0.11 ms | 4.86 ms | 44.6x |
| load all 1,280 | 465 ms | 3,226 ms | 6.9x |

### Two numbers of my own I had to correct
1. **`project_context.md` claimed "~670 ms for all 1,280 targets"** — I had
   extrapolated that linearly from the 200-target figure in session 001. Wrong
   by 4.8x: the first 200 targets hold 196,644 sparse entries while all 1,280
   hold 6,147,800 (31x the data for 6.4x the files, because `macrodetails` alone
   is 106 MB of the 126 MB). Measured the reference directly at **3,225.63 ms**.
   Against the fabricated baseline this work would have reported as a 0.9x
   *slowdown* rather than a 6.9x speedup.
2. **"26 macro values"** — repeated in `architecture.md` and taken from an early
   subagent report. The reference has **27** (`len(targets._value_cat)`); `age`
   has four values, not three. My implementation was right and the test
   assertion was wrong, which is how it surfaced.

Both are the same failure: a number I *derived* rather than *observed*, then
treated as fact. The pattern to watch is any figure that entered memory without
a command behind it.

### Verified
122/122 tests pass in debug, release and ASan+UBSan (113,379 assertions).
Clean under `-Werror`; clang-format clean.

### Session 006 addendum (2026-08-29 07:00:46) — target index and weighting rule

`TargetIndex` + `targetWeight` complete the parameterisation chain: a filename
becomes a group key plus macro dependencies, and those become a weight.

**Exact parity with the reference: 653 groups over 1,280 components**, with
every group name and every group size matching (`breast` 216,
`macrodetails-height` 144, `macrodetails-proportions` 108,
`macrodetails-universal` 72, `macrodetails` 24). Verified in both directions —
no group missing, none invented.

Two properties worth knowing, both now asserted:
- At defaults, `macrodetails` distributes exactly 1.0 across 6 of its 24
  targets, and `macrodetails-universal` 1.0 across 2 of 72. The scheme is a
  partition of unity per group.
- **But not every group sums to 1.** At height 0.5 both `minheight` and
  `maxheight` are 0 and the group ships no `averageheight` files, so
  `macrodetails-height` contributes *nothing*; same for proportions. A port
  that assumed every group normalises would be wrong here.

`TargetIndex::build` indexes all 1,280 in 18.1 ms.

### Session 007 addendum (2026-08-29 07:26:49) — modifier hierarchy and Human

`Modifier` + `Human` complete M3's parameterisation. **Exact parity on all 291
shipped modifiers** — full name, range, kind and per-side target group names,
compared against objects constructed by the reference's own classes.

Two real bugs the parity test caught, both semantic rather than mechanical:

1. **The target stack ASSIGNS, it does not accumulate.** `setDetail` writes
   `targetsDetailStack[name] = value` (`human.py:918-921`). Five macro modifiers
   — Gender, Age and the three ethnic ones — all resolve to the group
   `macrodetails`, so all five write the same 24 targets. With `+=` the default
   character's stack totalled **5.0**; with assignment it totals **1.0** per
   contributing group, because the five compute identical weights and the last
   write simply wins. This would have made every macro-driven character wrong by
   a constant factor.
2. **A macro modifier has no left/right/center.** The reference leaves them
   unset and resolves targets from the group name (`humanmodifier.py:566`); I
   had overloaded `right`, which broke the side comparison for all 11
   macro/ethnic modifiers.

The default character's stack is **8 targets totalling 2.0** — 6 from
`macrodetails` and 2 from `macrodetails-universal`, each group summing to 1.0.
Height, proportions and breast contribute nothing at neutral, for the reasons
pinned in `test_target_index_parity.cpp`.

**Process note:** a scripted edit silently failed to apply for the fourth time
(clang-format reformatting the anchor). This time the per-anchor `assert` I had
added caught it immediately instead of a test failure doing so three steps
later. Asserting each anchor individually, not just the final state, is what
made the difference.

### Session 008 addendum (2026-08-29 07:51:57) — applyStack and end-to-end parity

**M3's core is complete and validated end to end.**

`Human::applyStack` resets the mesh to its morph base and replays the stack,
mirroring `applyAllTargets` (`human.py:1147-1209`).

**The test that matters: `tests/golden/test_character_parity.cpp`.** Fourteen
characters — the neutral default, each macro axis at both extremes, a bipolar
shape slider on each side, and a mixed six-parameter case — driven through the
*whole* chain in both implementations and compared vertex by vertex. All match
within 1e-5 across 19,158 vertices, and the stack size matches the reference's
for every case.

This is the test the component parity tests could not replace. Every one of
them passed while the accumulate-vs-assign bug was live, because that bug was
in the *composition*, not in any component.

The fixture is generated by driving the reference's real `Human` headlessly
(`tools/capture_fixture.py character`), which needed only a small `G.app` stub
for the progress callback.

Benchmarks (release): `loadModifiers` 0.21 ms for all 291 · `rebuildStack`
0.01 ms · **`applyStack` 0.07 ms** for a full character rebuild. Slider
interaction is effectively free.

**Process note:** a scripted edit failed to apply for the *fifth* time. The
per-anchor assert caught it again. The lesson has stopped being "check your
edits" and become structural: anchors matched against clang-formatted C++ are
inherently fragile, and the assert is what makes that survivable.

### Session 009 addendum (2026-08-29 08:16:24) — .mhm parser, M3 closed

`MhmFile` / `loadMhm` / `applyMhm`. Line-oriented, `#` comments, whitespace
split (`human.py:1459-1643`). Version comparison uses major.minor only, matching
the reference's `v(\d)\.(\d)` regex. **Unrecognised lines are preserved
verbatim** — skeleton, pose, proxy and material lines come from plugins this
build does not have yet, and dropping them would corrupt a round trip.

**Round-trip parity**: `.mhm` files written by the reference's own `Human.save()`
are loaded in C++, applied, and produce geometry matching what the reference
itself produces from the same file, across all 19,158 vertices.

The fixture is self-validating: the reference's `.mhm` load and its direct
modifier-setting path were compared against each other and agree to 2.9e-6, so
the fixture is not merely consistent with one code path.

**M3 is closed.** The chain from a saved model file to final geometry is
complete and parity-tested at every stage.

### Session 010 addendum (2026-08-29 08:41:19) — M4 opens: proxy fitting

`Proxy` / `loadProxy` / `fitProxy`. A proxy vertex is bound to a triangle of
base-mesh vertices plus an offset:

    P_i = SUM_k w_ik * H[v_ik] + M * d_i

**Parity on 3 shipped proxies x 2 bodies.** The second body matters: the
`TMatrix` offset rescaling is near-identity on the neutral shape, so a bug in it
only surfaces once proportions change. Both bodies match within 1e-5.

`loadProxy` 0.59 ms · `fitProxy` under 0.01 ms for 1,064 vertices — proxy
refitting is free at interactive rates.

One test assumption of mine was wrong again, caught by the test: I asserted
every proxy declares `basemesh hm08`, but `data/3dobjs/base.mhclo` is the
alpha-7 converter and declares `alpha_7`. The parser had read it correctly.

**Not implemented:** the `TMatrix` shear forms (`shear_*`, `l_shear_*`,
`r_shear_*`). No shipped asset uses them — all three are scale-only — so
implementing them now would be untestable speculation. Recorded in `todo.md`
for whenever a community asset needs it.

### Session 011 addendum (2026-08-29 09:07:08) — `.mhmat` materials

`Material` / `loadMaterial`. All keys, the seven texture channels with their
intensities, SSS scales, shader params and defines, and `shaderConfig`.

`effectiveDefines()` reproduces the reference's derivation (`material.py:956-1016`)
including that **bump is suppressed when normal is active** (`:984-995`) and that
a channel contributes nothing without its texture. The list is **sorted**,
because the sorted define list is the shader-variant cache key (`:1015`) — that
is a contract, not formatting.

Validation policy, chosen deliberately: a **known** key with an unparseable
value is an error (silently keeping the default would change the asset's
appearance with no diagnostic), while an **unknown** key is ignored, because
community assets carry keys this build has never seen. `-Werror` surfaced this
design question by flagging the `lineNo` I had declared and never used.

**Process note — the anchor problem, properly diagnosed.** A scripted edit
failed for the sixth and seventh time this iteration. The root cause is now
clear and worth recording: I write anchors from memory of the text I authored,
but clang-format rewrites whitespace immediately afterwards — collapsing
alignment, and writing `}  // namespace` with two spaces. A regex substitution
also matched the wrong `return m;` because `MaterialError::message()` has a
local named `m` too. The assert caught every one before a write, so nothing was
corrupted. **The reliable procedure is: read the file, copy exact current text,
or match whitespace-tolerantly — never retype from memory.**

### Session 012 addendum (2026-08-29 09:30:58) — asset index

`AssetIndex` / `peekAsset`. UUID -> path resolution, tag queries, ordered search
paths.

This is what makes a saved character's clothes resolve: **`.mhm` references
proxies by UUID only**. Loading by filename was deliberately removed upstream
(`proxychooser.py:550-552` logs an error and refuses).

Three decisions worth recording:
- **Metadata is peeked, not parsed.** The scan stops at the `verts` line, where
  the megabytes begin (`proxy.py:1035-1036`). A test asserts a bogus `uuid`
  placed *after* `verts` does not win, so the early exit is behaviour, not just
  an optimisation.
- **Earlier search paths win a UUID collision**, matching user-data-over-
  system-data precedence (`getpath.py:289-308`).
- **Collisions are reported, not silently resolved.** The reference lets the
  last writer win (`proxychooser.py:617-623`), which makes resolution depend on
  directory iteration order. Keeping the first and exposing `duplicateUuids()`
  makes a packaging mistake visible instead of load-order-dependent.

It also replaces the reference's `filecache`, which is a **Python pickle**
(`filecache.py:44`) — arbitrary code execution on load, and one of the security
issues recorded in `project_context.md` §8.

Another derived-number slip, caught by the test: I asserted 6 shipped assets
(3 proxies + 3 materials) when there are 7 — I had forgotten
`a7_converter.proxy`. Counted from disk this time.

### Session 013 addendum (2026-08-29 09:56:34) — M7 opens: OBJ export

**The first Apache-2.0 module.** `mh::io` is written from the published
Wavefront specification, not translated from the AGPL reference, so it is
separately reusable — the licence boundary in `LICENSING.md` §4 is now real code
rather than a plan. It links `mh::core` (AGPL), which is the allowed direction;
the reverse would be a violation.

`writeObj` produces **face lines byte-identical to the reference's own export**
across all 18,486 faces, and the same first-vertex text. Round-tripping the real
19,158-vertex mesh through our writer and reader gives a max delta of 0.

Two deliberate divergences:
- **Feet-on-ground uses the mesh's actual minimum**, not the reference's named
  "ground" joint applied to Y only regardless of orientation — which its own
  TODO flags as wrong (`core/export.py:102-109`).
- **`map_Bump` is emitted** when a normal map exists. The reference copies the
  texture into `textures/` and then never references it
  (`wavefront.py:277-280`, with `map_Disp` commented out).

Also fixed a duplicate-library linker warning: `mh_io` PUBLIC-links `mh_core`,
so listing both in the test target made the linker see `libmh_core.a` twice.

Reference-fixture note: the reference's `writeObjFile` reads
`mesh.object.material` for its `usemtl` line, so generating the comparison file
needs the same weakref-safe material stub the subdivision fixture uses.

### Session 014 addendum (2026-08-29 10:22:10) — glTF 2.0 / GLB export

`writeGlb`, Apache-2.0, written from the published spec. **The first subsystem
with no reference to compare against** — the Python MakeHuman has no glTF
support at all — so the testing posture changed deliberately:

1. **Spec conformance checked against the bytes**: GLB magic, version 2, the
   total-length field, `JSON`/`BIN\0` chunk types, 4-byte chunk alignment, and
   JSON padded with spaces rather than NULs.
2. **An independent reader.** assimp is a different implementation by different
   authors, wired in as a **test-only** dependency (never linked into the
   shipped libraries, recorded in `LICENSING.md`). It agrees on 21,833 vertices
   and 36,972 triangles, sees normals, UVs and a material, and confirms every
   face is a triangle.

Three details that are easy to get wrong and are now pinned by tests:
- **Units.** glTF's unit is the metre, MakeHuman's is the decimetre. The default
  converts; without it every model is 10x too large in any spec-honouring
  engine. assimp reads the exported figure back at **1.69 m** — a real human
  height, which is the check that actually proves it.
- **UV origin.** glTF's is top-left, MakeHuman's is bottom-left, so V is
  flipped. Getting this wrong mirrors every texture vertically.
- **POSITION accessors carry min/max**, which the spec requires and many
  loaders reject the file without.

Blinn-Phong is converted to metallic-roughness (metallic 0 — skin, cloth and
hair are all dielectric; roughness from `1 - shininess`), since that is the only
PBR model core glTF defines and the reference has no PBR data at all.

### Next
Remaining glTF work: skins, morph targets and embedded textures — all of which
need M5 (skeleton) first for the skin case. Or STL/PLY, which are trivial by
comparison. M5 is the higher-value path now that geometry export works.

---

## 2026-08-29 — Session 004 · Catmull-Clark + review round 3

**Ended:** 2026-08-29 05:45:32 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### Delivered
**`Subdivider`** — one level of Catmull-Clark, split into a topology pass and a
geometry pass. **Byte-level parity with the reference on the full base mesh**:
75,008 verts / 73,944 faces / 37,364 edges, with `fvert` and `fuvs`
bit-identical and positions within 1e-5.

| | C++ | Python | |
|---|---|---|---|
| `Subdivider::build` | 6.27 ms | 202.30 ms | 32x |
| `Subdivider::refresh` | **0.46 ms** | 28.21 ms | 61x |

The refresh figure is the headline: the reference's subdivided update path
(`update_coords` 7.64 ms + `calcNormals` on 75k verts 20.57 ms = 28.21 ms)
exceeds a 16.6 ms frame budget before anything is drawn, which is why
subdivided editing cannot hold 60 fps today.

### Review round 3 — 6 findings, all fixed
The review verified the core math independently with a numpy transcription of
the reference (bit-identical indices, `max|Δcoord| = 1.9e-6`) and then found six
issues around it:

| Sev | Defect | Fix |
|---|---|---|
| HIGH | `refresh()` read the *parent's* adjacency. If `buildAdjacency()` was never called — or `setFaces()` was called after it, which clears it — every vertex reported 0 faces and silently took the valence<3 fallback | the subdivider owns its face adjacency |
| MED | `Mesh::buildAdjacency` deduplicates a vertex repeated within one face; the reference does not (`module3d.py:709-717`). A triangle stored as a degenerate quad — exactly what `loadObj` produces — got a different valence, flipping the interior/boundary branch | count per corner over `min(vpp, vertsPerFaceForExport)`, no dedup |
| MED | gate was `vertsPerPrimitive != 4`; the reference gates on `vertsPerFaceForExport` (`:516-518`), so a triangle OBJ was subdivided where the reference declines | corrected gate |
| MED | `std::ranges::sort` is unstable, so first/last adjacent face was arbitrary where `np.unique` gives min/max — non-manifold edge points diverged by up to 0.192 and output was not reproducible across toolchains | `stable_sort` (verified: Δ drops to 4.9e-10) |
| LOW | `matches()` compared counts only, so a same-size topology swap passed and `refresh()` wrote wrong geometry through a stale table | `Mesh::topologyVersion()`, O(1) and exact; applied to `RenderMesh` too |
| LOW | the result's `origCoord_` stayed at the zero placeholder, so `resetToOriginal()` collapsed it to the origin | `captureOriginal()` after refresh |

**Resolved the `maxpole` caveat by not needing it.** The reference sizes one
`vface` array to `max(maxFaces, maxpole, 4)` because it reuses that array for
both faces and edges. Separate face and edge adjacency arrays, each sized from
its own measured maximum, remove the shared bound entirely.

### New coverage
- **Golden subdivision fixture** (`tools/capture_fixture.py subdiv`): positions,
  UVs and both index arrays captured from the reference and compared
  byte-for-byte. Matching *counts* proves nothing about geometry; this does.
- The **boundary base-vertex rule** is now exercised. On a rectangular grid every
  boundary vertex has fewer than 3 faces, so the earlier tests only ever hit the
  valence<3 fallback — an open 3-quad fan reaches the real boundary branch.

**Process note:** a scripted private-member insert silently failed for the third
time because clang-format had changed the whitespace the anchor matched. Now
asserting the edit landed before building, rather than discovering it from a
compile error.

### Verified
93/93 tests pass in debug, release and ASan+UBSan (112,145 assertions).
Clean under `-Werror`; clang-format clean.

### Next
M3 — targets and modifiers: the `.target` parser, the compiled target blob, and
the modifier weighting rule (`weight = value x PROD(factors)`).

---

## 2026-08-29 — Session 003 · M1 complete, M2 tangents + unweld

**Ended:** 2026-08-29 05:05:04 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### Delivered
- **M1 complete**: `.clang-format`, `.clang-tidy`, CI (format · build/test across
  debug+release+asan · benchmark · three licence gates), `tools/capture_fixture.py`.
- **Golden fixtures**: 65 files / 3.4 MB captured from the reference, each with a
  MANIFEST recording reference commit + interpreter + numpy version.
- **Byte-level parity tests**: coord, fvert, fuvs, texco, vnorm, group compared
  element-by-element over the whole base mesh. 18 assertions, all passing.
- **Correct tangents** (Lengyel + Gram-Schmidt + handedness).
- **`RenderMesh`**: the GPU unweld. 19,158 → 21,833 render verts, 110,916
  indices, 139 draw ranges, fan triangulation.

### Two review rounds, 19 findings total, all fixed
Round 1 (11 findings) on the build-system commit — 3 memory-safety.
Round 2 (12 findings) on tangents + RenderMesh — 4 HIGH:

| Sev | Defect | Fix |
|---|---|---|
| HIGH | tangents used corners 0,1,2 only and broadcast to all 4, so corner 3 got a basis from a triangle it is not in and triangle (0,2,3) contributed nothing — **179° max error on real base-mesh data** | accumulate per triangle over the same fan a renderer draws |
| HIGH | the "compute normals if missing" guard could never fire — `setCoords` zero-filled `vnorm_`, so tangents were orthogonalised against the **zero vector** and handedness was always +1 | `vnorm_` left empty; guard calls `calcNormals()` |
| HIGH | `setUVs` after `setFaces` could shrink `texco_`, stranding UV indices → ASan heap-buffer-overflow | `setUVs` validates and returns `expected` |
| HIGH | `setCoords` after `setFaces` could shrink `coord_`, stranding vertex indices → ASan heap-buffer-overflow | `setCoords` validates and returns `expected` |
| MED | group ranges sized from `faceGroups().size()`, so a larger group id left indices unreachable from every draw range | sized from `max(group)+1`, as the reference does (module3d.py:857) |
| MED | `vertsPerPrimitive > 4` silently lost geometry (a pentagon gave 2 triangles, not 3) | general fan triangulation |
| MED | `refreshPositions` gathered through a stale table after a topology change | O(1) topology fingerprint + `matches()` |
| LOW | `tmap()` documented as an index into `texco()` but all-zero when there are no UVs | cleared when absent |

**Correction to my own earlier record:** the reference's tangent code has
**three** bugs, not two. Beyond `module3d.py:411` and `:429`, it also never masks
`vface` by `nfaces` — unlike `calcVertexNormals` at `:366` — so face 0 is folded
into every vertex whose valence is below the array stride.

**Process note:** the round-2 review found a bug I introduced *while fixing*
round 1. Writing the failing test first was what made each fix verifiable. Two of
my own patches also silently failed to apply because clang-format had changed the
whitespace my anchors matched on — worth checking `grep` after a scripted edit
rather than trusting that a replace landed.

### Verified
72/72 tests pass in debug, release and ASan+UBSan (111,906 assertions).
Clean under `-Werror`; clang-format and SPDX gates pass.

Benchmarks (release): load base.obj 11.2 ms vs 211.8 ms (19x) · calcNormals
0.09 ms vs 5.18 ms (56x) · calcVertexTangents 0.20 ms · RenderMesh::build
2.14 ms vs 3.43 ms · refreshPositions 0.04 ms.

### Next
Catmull-Clark subdivision — compute `maxpole` first (`todo.md` caveat), then
parity against the reference's 75,008 verts / 73,944 faces.

---

## 2026-08-29 — Session 002 · M1 build system + M2 mesh core

**Started:** 2026-08-29 03:58:00
**Ended:** 2026-08-29 04:09:41
**Agent:** Claude Opus 5 (1M context)
**Branch:** master

### Objective
Stand up the C++ build system and deliver the first real, tested, benchmarked
module of the port.

### Delivered

**Build system (M1)**
- `CMakeLists.txt` + `CMakePresets.json`: `macos-arm64-{debug,release,asan}`
- **C++23**, not C++20 — `std::expected` is C++23. Verified available in Apple
  clang 21's libc++ before adopting. All docs claiming C++20 were corrected.
- Warning set: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wdouble-promotion …` with **`-Werror` on**. Builds clean.
- Catch2 v3.7.1 via FetchContent, **pinned tag**, include tree marked `SYSTEM`
  so third-party warnings never surface without weakening ours.

**`mh::core` (M2, partial)**
- `Types.h` — `Vec2`/`Vec3`, cross/dot, the decimetre constant.
- `Mesh` — SoA, dual index space (`fvert`/`fuvs` independent, per module3d.py:627/:629),
  degenerate-quad triangles, face groups, adjacency, area-weighted normals,
  bounding box, `heightCm`.
- `ObjReader` — `std::expected`-based, bounds-checked. Handles `v/vt/f/g/o`,
  negative indices, leading-dot floats. Rejects loose vertices, out-of-range
  indices, n-gons, and empty meshes rather than reading out of bounds.

### Verification (all observed this session)
- **27/27 tests pass**, debug and release.
- **27/27 pass under ASan + UBSan.**
- Clean build under `-Werror`.

### Measured — release, vs. the Python baseline

| Operation | C++ | Python | Speedup |
|---|---|---|---|
| load base.obj (parse + adjacency + normals) | **5.24 ms** | 211.80 ms | **40.4x** |
| calcNormals full mesh | 0.07 ms | 5.18 ms | 74.1x |
| calcFaceNormals | 0.03 ms | 0.68 ms | 25.2x |
| calcVertexNormals | 0.04 ms | 1.69 ms | 39.8x |
| buildAdjacency | 0.14 ms | (inside the 211.8 ms) | — |

The ≤20 ms base-mesh-load success metric in `project_context.md` §6 is **met**
(5.24 ms warm; ~10.7 ms cold file cache).

### Corrections made this session
1. **`std::expected` is C++23, not C++20.** The build failed on it. Verified the
   standard level with a scratch compile before bumping, then corrected every
   doc that said C++20.
2. **I asserted 172 face groups in a parity test from a subagent's number without
   verifying it. The test failed at 139.** Root cause: `base.obj` has 172 `g`
   *statements* but only 139 *distinct* names (`helper-*-eyelashes-*` recur 9x
   each). The reference keys a dict by name and only creates a group for an
   unseen name (`wavefront.py:120-123`), so it produces 139. Confirmed by running
   the reference loader directly: 139 groups, 125 `joint-`, 13 `helper-`. The
   implementation was right; my assertion was wrong. This is exactly the failure
   `agent_profile.md` §3.3 warns about — a subagent figure used without a
   spot-check. The corrected test now asserts all three numbers.

### Ponytail review — applied
- Dropped `Eigen3::Eigen` from `mh_core`: linked but never included. Eigen is
  added when `mh_rig` needs quaternions and decomposition.
- Removed `MH_BUILD_APP` / `MH_USE_ASSIMP` options that gated nothing.
- Removed `Vec4` and `RGBA8` — declared, zero uses.
- Kept the POD `Vec3` over `Eigen::Vector3f`: SoA storage wants a
  trivially-copyable type, and the whole thing is 20 lines.

### Deferred, with reason
Empty CMake targets for `mh-foundation`/`mh-rig`/`mh-io`/`mh-render`/`mh-ui`/`mh-app`
were **not** created. Scaffolding targets with no sources is exactly the
speculative structure `philosophy.md` §2 rejects; each is added with its first
real source file.

### Code review — 11 findings, all fixed (2026-08-29 04:19:39)

`/code-review --effort high` on `61f48893`. Every finding was reproduced by the
reviewer with a probe binary and ASan, and each is now covered by a regression
test in `tests/regression/test_review_findings.cpp` that fails on the old code.

| # | Sev | Defect | Fix |
|---|---|---|---|
| 1 | HIGH | `maxValence_` was `uint8_t`; ≥256 incident faces wrapped it to 0, zeroing the adjacency stride so **every** vertex normal silently became the `{0,1,0}` fallback | widened to `uint32_t` (reference raises `RuntimeError` here, module3d.py:713-715) |
| 2 | HIGH | `setFaces` left stale `vface_`; the staleness guard still held because `coord_` was unchanged, so `fnorm_` was indexed through dead face indices — ASan container-overflow | `setFaces` clears `vface_`/`nfaces_` |
| 3 | HIGH | `calcFaceNormals` indexed `coord_[fvert_[...]]` unguarded while `setFaces` validated nothing — ASan heap-buffer-overflow | `setFaces` now validates every index and returns `std::expected<void, MeshError>` |
| 4 | MED-HIGH | a malformed `v`/`vt` line was silently dropped, **shifting every later index** and loading a different mesh | returns `MalformedVertex` |
| 5 | MED | `o` was treated as `g`, creating a spurious empty group and never setting the name; reproduced on the shipped `data/3dobjs/axis.obj` | `o` sets the name only (wavefront.py:128-129) |
| 6 | MED | `f` with <3 corners silently ignored while >4 was a hard error | returns `DegenerateFace` |
| 7 | LOW | `parseCorner` accepted trailing garbage, so `f 1x 2 3` parsed | end-of-token check, as `parseFloat` already had |
| 8 | LOW | `maxValence()` documented as the reference's `MAX_FACES`, but the reference uses `max(maxFaces, maxpole, 4)` | doc corrected; caveat recorded in `todo.md` under Catmull-Clark |
| 9 | LOW | `hasUV_` was order-dependent between `setUVs`/`setFaces` | `hasUV()` is now derived, not stored |
| 10 | LOW | `setFaces` checked `faceGroup` size but not `faceUVs` | symmetric size check |
| 11 | LOW | benchmark harness raised on empty `data/targets`, killing the run **before** writing its JSON | section guarded like sections 5-6 |

Findings 1-3 and 4 are the ones that mattered: three were memory-safety defects
and one silently produced a wrong mesh from a valid-looking file.

One correction of my own during this work: my first regression test for finding 1
used a *planar* fan, whose true normals legitimately are `{0,1,0}` — the same as
the zero-guard fallback, so the test could not distinguish them. Rebuilt as a
cone. A second iteration asserted `+Y` on the apex when the winding gives `-Y`;
the sign is an artefact of my test geometry, not a property under test.

**After fixes: 42/42 tests pass**, debug and release, and under ASan+UBSan.
Release benchmark: load base.obj 5.2-10.6 ms (file-cache dependent) vs 211.8 ms.

### Next session starts here
1. Run `memory/session_start.md` in full.
2. `.clang-format` / `.clang-tidy`, then the CI workflow.
3. `tools/capture_fixture.py` — golden fixtures gate every subsequent port step.
4. Then M2 remainder: correct tangents, unweld/index buffer, Catmull-Clark
   (compute `maxpole` first — see the caveat in `todo.md`).

---

## 2026-08-29 — Session 001 · Grounding, restructure, memory bootstrap

**Started:** 2026-08-29 03:30:00
**Ended:** 2026-08-29 03:57:55
**Agent:** Claude Opus 5 (1M context)
**Branch:** master

### Objective
Understand the MakeHuman codebase in depth, establish repository structure and the
`memory/` system, and set up the foundation for a Python → C++/Qt6 port.

### Completed

| Time | Work |
|---|---|
| 03:30 | Repo recon: 209 Python files, ~60k LOC, 212 MB |
| 03:35 | Verified toolchain: Qt 6.11.0, CMake 4.3.2, Ninja 1.13.2, clang 21, assimp 6.0.4, Eigen 5.0.1, TBB — all present |
| 03:36 | graphify AST extraction: 5,333 nodes / 10,108 edges |
| 03:40 | Knowledge graph complete: **5,392 nodes · 9,319 edges · 358 communities** → `graphify-out/` |
| 03:41 | **Repo restructured**: `makehuman/` → `legacy/python/`, `makehuman/data/` → `data/` (+ symlink), created `src/ include/ tests/ benchmarks/ tools/ resources/ cmake/ packaging/ docs/ third_party/ memory/` |
| 03:39 | Reference venv built: numpy 2.5.1 + PyQt5 + PyOpenGL on Python 3.14.6 |
| 03:50 | **Performance baseline measured** → `benchmarks/baseline_python.json` |
| 04:00 | 7 parallel subsystem analyses completed and spot-verified |
| 04:15 | `memory/` written — 12 documents |

### Key findings (all verified against source)

1. **Base mesh**: 19,158 verts / 18,486 **quad** faces / 21,334 UVs. Subdivided: 75,008 / 73,944.
2. **No VBOs anywhere.** `glVertexPointer(3, GL_FLOAT, 0, obj.verts)` — `legacy/python/lib/glmodule.py:479`. Fixed-function, client-side arrays, re-fed every frame. This is hotspot #1 and the reason a Metal/RHI port is transformative.
3. **Zero import capability.** No importer machinery of any kind (grep-verified). Only mesh reader is `wavefront.loadObjFile`, which ignores `vn` and `usemtl`. Import is greenfield.
4. **Zero docking infrastructure.** No `QDockWidget`/`QSplitter`/`saveState` anywhere. The dockable UI is 100% new work.
5. **FBX exporter**: hand-rolled, FBX **7300 (2013)**, no animation, no blendshapes, **verified 10× unit bug** at every scale but decimetre (`plugins/9_export_fbx/__init__.py:112` + `fbx_binary.py:736`), forged Creator string, 12+ circular imports. Treat as a format reference only, never port.
6. **Licence position resolved.** Code AGPL-3.0, assets CC0. pyFBX is GPL-2.0-**or-later** → upgradeable to GPLv3 → **compatible with AGPLv3**. An earlier automated pass flagged a conflict; that flag was wrong and has been retracted.
7. **Upstream licence contradiction found**: `algos3d.defaultTargetLicense()` (`core/algos3d.py:507-509`) claims targets are AGPL3, while `LICENSE.md` §C **and** every `.target` file header say CC0. Authoritative reading: **CC0**. Also: asset licence headers are **not machine-readable** — `updateFromComment` only accepts lowercase keys, so every shipped asset loads with the default LicenseInfo.
8. **Three tangent bugs** in `core/module3d.py` (:411 chained assignment, :429 missing `axis=`, :1212 operator precedence). Tangents in the reference are provably wrong — must NOT be parity-matched.
9. **Modifier weighting rule** established exactly: `weight = sliderValue × Π factors`, factors derived from filename tokens against a 9-category table.
10. **The mesh drives the rig**, not vice versa — joints are means of vertex clouds, so changing a slider rebuilds bone rest matrices.

### Measured baseline (macOS 26.6.2, arm64, Python 3.14.6, numpy 2.5.1)

| Operation | Median |
|---|---|
| Load base.obj | 211.8 ms |
| Catmull-Clark build | 202.3 ms |
| Load 200 targets (ASCII) | 104.4 ms (~670 ms for all 1,280) |
| Subdiv calcNormals | **20.6 ms** — exceeds a 16.6 ms frame budget on its own |
| Subdiv update_coords | 7.6 ms |
| calcNormals (base) | 5.2 ms |
| Apply 200 targets | 4.8 ms |
| updateIndexBuffer | 3.4 ms |
| Apply 1 target | 0.04 ms |

### Corrections made this session
- The graphify doc-extraction pass flagged pyFBX GPLv2 as possibly incompatible with AGPLv3. **Wrong** — the header reads "either version 2 … or (at your option) any later version". Verified at `legacy/python/licenses/pyFbx-license.txt:4-6` and retracted.
- The benchmark harness initially failed on subdivision because `Object3D.object` is a **weakref** property (`core/module3d.py:459-464`) and the stub was collected immediately. Fixed by holding a strong reference.

### Files changed
- Restructured: `makehuman/` → `legacy/python/`, `makehuman/data/` → `data/`
- Added: `memory/` (12 docs), `benchmarks/baseline_python_core.py`, `benchmarks/baseline_python.json`, `graphify-out/`, `CLAUDE.md`, `AGENT.md`, `LICENSING.md`, `.gitignore` updates
- Created empty: `src/ include/ tests/ tools/ resources/ cmake/ packaging/ docs/ third_party/`

### Blockers / open questions for the user
1. **Typeface** — instruction was "red 42 dot sans". Assumed **42dot Sans** (SIL OFL 1.1). Needs confirmation.
2. **"Open rig"** — ambiguous. Could mean the existing open `.mhskel` format, OpenSim rigs (referenced at `legacy/python/apps/compat.py:181-188` as a downloadable asset), or a specific third-party project. **Not guessing.** Does not block M1–M8.
3. **Commercial-derivative expectation** — AGPL copyleft means a closed-source fork is not possible. Output and assets are fully free (CC0). Mitigation is the Apache-2.0 clean-room module boundary (`architecture.md` §II.1). Flagged for awareness; no action needed to proceed.

### Next session starts here
1. Run `memory/session_start.md` in full.
2. Begin **M1**: `CMakeLists.txt` + `CMakePresets.json` + module targets + Catch2 + CI.
3. Then `tools/capture_fixture.py` — golden fixtures gate every subsequent port step.
