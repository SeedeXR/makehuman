# Session Handover Log

Newest entry first. Every entry carries a `YYYY-MM-DD HH:MM:SS` timestamp.

---

## 2026-09-01 00:08:19 — Session 076 · **BVH export, and Blender contradicting our own reader**

### The chunk
`io::writeBvh(path, BvhFile)`. Round-trips `tpose.bvh` and the 60-frame,
212-joint `face-poseunits.bvh` at a worst matrix delta of **5.96e-08** — float32
epsilon, and exactly the floor a pure Euler decompose/recompose achieves (which
I measured separately first, to know whether any error was the maths or mine).

### The bug: positional vs axis-identity channel mapping
`BvhReader` assigns channel values by **axis identity** — `Xrotation` always
lands in `ax`, `Yrotation` in `ay`, `Zrotation` in `az` — and then always calls
`eulerMatrix(az, ay, ax, order)` (`BvhReader.cpp:323-337`).

My first writer popped the angles **positionally** off `eulerFromMatrix`'s
result. That agrees only when the channels happen to be Z,Y,X. Otherwise it
writes a completely different rotation: **1.69** off on a matrix element.

Diagnosis order mattered. The failing test showed deltas of ~2e-3, which looked
like a precision problem. It was not: a direct probe found the true worst was
**1.69**, and a separate probe showed the Euler round-trip alone was exact. That
ruled out precision and pointed straight at the mapping.

### Blender contradicted our own reader, and was right to
Our reader said the round-trip was exact. Blender put generation 1 **11.19**
away from the original.

Both are correct. `readBvh` converts Z-up input (both shipped MakeHuman poses
measure Z-up), so the in-memory data is Y-up and that is what gets written. The
11.19 is the up-axis conversion, not an export error. Proven by isolating it:
Y-up in -> Y-up out compares **0.0** in Blender across all 163 bones.

Standard BVH is conventionally Y-up, so our output is the more conformant file —
but **a Z-up source does not come back as Z-up**, and that is documented in
`BvhWriter.h` rather than left to be discovered.

Our own reader agreeing with our own writer proves self-consistency, not
correctness. The third-party importer is what made the difference visible.

### A claim of mine that was simply wrong
I asserted byte-level idempotency. Generation 2 differs by **7 bytes of
340,964**: angles pass through a float32 `Mat4` between generations, so
low-order digits shift and exact text reproduction is unattainable. The test now
pins idempotency in **meaning** — the two generations decode to the same
transforms — which is both true and the property that matters.

### Verification
ctest **395/395** in debug, release and ASan; TSan run separately (the combined
sweep exceeded the command timeout, which is not a failure). Format clean.
CI green on `44540d6d`.

### Still blocked on the owner
**SonarQube credentials.**

---

## 2026-09-01 00:31:02 — Session 077 · **M5 exhausted; a depth test with real teeth**

### M5 is done as far as I can take it
Two more entries were stale and are now closed against live code:
- **Pose library UI**: the window *does* select a pose.
  `src/app/main.cpp:443` builds the `Pose` AssetGroup, the Materials
  `AssetPanel` presents it, and the `chosen` callback **probes the pose before
  committing** — one that fails to load restores the picker rather than becoming
  an undo entry that does nothing.
- **Depth range `[0,1]`**: never a problem. The renderer takes Qt RHI's
  `clipSpaceCorrMatrix()` (`SceneResources.cpp:273`), so the live backend
  supplies its own convention. The item assumed we would transliterate the
  reference's GL projection; we never did.

**What is genuinely left in M5** needs someone else or another milestone:
SonarQube (owner credentials), `.mhpose` and `.mhupb` (no asset ships to verify
against), skin normals/tangents and GPU LBS (M6), and the bone-naming decision
below.

### The depth test, because "correct by construction" is not "tested"
An ignored depth range still renders — every "does it draw" test passes while
the picture is wrong. That is exactly the kind of gap that stays open for months.

The eye proxy is the right probe: it sits **entirely inside the skull**. So from
**behind**, adding the eyes must change **0 pixels**. And from the **front** it
must change more than 0, or the test would also pass with a renderer that had
simply stopped drawing the proxy.

**Mutation-verified**, with the match asserted this time: disabling depth
testing puts **16 eye pixels through the back of the head** and fails the test.

### Open question for the owner
`memory/todo.md` still carries "Bone naming/order: Mixamo standard (owner
decision)". What shipped is a **179-bone superset keeping MakeHuman names** plus
a **total 65-bone retarget table**. Whether exported rigs should additionally be
**renamed** to `mixamorig:*` is a product decision, not a technical one — it
changes what other tools see. Raised, not assumed.

### Verification
ctest **395/395** in debug, release, ASan; TSan run separately (the combined
sweep exceeds the 10-minute command timeout). Format clean.
CI green on `29c90f62`.

---

## 2026-09-01 00:54:55 — Session 078 · **the litsphere was not pixel-faithful, and no image test could tell**

### The finding
The todo tracked two magic numbers as the whole of litsphere parity: the `0.495`
scale and the `2.0 − mean(shading)` term. Both were already correct.

Diffing our shader against the reference line by line found a **third** term
nobody had listed. The reference samples the **raw interpolated normal**:

    vec3 normal = vNormal;          // litsphere_fragment_shader.txt:78

We wrote `normalize(vNormal)`. Both vertex stages normalize identically, so this
is purely a fragment-stage divergence — and it is not cosmetic. Interpolation
shortens a normal across a triangle, which pulls the litsphere UV toward the
sphere centre, and the shipped litspheres were authored against exactly that.

**Measured before deciding**: renormalizing changes **0.98% of the rendered
frame**, by up to **107/255** in a channel, concentrated where the matcap
gradient is steepest. Visible, not rounding. The `normalize()` is gone.

### An argument I built, tested, and had to throw away
The appealing case for keeping `normalize()` is that it makes shading
independent of tessellation. I tested it: base vs subdivided differs by **3.09%**
(reference) against **2.92%** (renormalized).

That is **confounded** — subdividing moves the silhouette too, so most of both
numbers is geometry, not shading. A 0.17pp gap proves nothing, and I am not
offering it as a reason. Parity is the reason.

Renormalizing stays available as a deliberate *quality* choice. It is simply not
parity, and the difference is now a number rather than an opinion.

### Why the guard is a source check, not a render
No rendered-image test can defend any of these three terms: every variant still
produces a plausible lit figure. That is exactly why the divergence survived
this long in a project with a render test suite. So `[render][litsphere]` reads
the shader source, strips comments so the header's prose cannot satisfy it, and
asserts the three terms literally — the same trade `.mhmat` writing makes
(project_context.md §8.0).

Both mutations fail it: restoring `normalize()`, and `0.495` -> `0.5`.

### A restore that caught itself
Reverting the `0.495` mutation asserted `count == 1` and **failed** — because
after the mutation `vec3(0.5)` appears **twice** (the scale and the `+0.5`
offset). Without the assertion the shader would have been left silently wrong
with the tests still red for a reason I would have misread. The assert-before-
write rule paid for itself again.

### Verification
ctest **395/395** in debug, release, ASan; TSan run separately. Format clean.
CI green on `239473c9`.

---

## 2026-09-01 01:26:01 — Session 079 · **skin textures: the renderer could not show one**

### Owner request (2026-09-01)
Varied human skin tones with MetaHuman-level detail, with four texture sources
named. Investigating it first found the blocker is ours, not the assets'.

### The finding
`data/skins/` contains exactly **one** material, `default.mhmat`, and it names
**no texture at all** — `shaderConfig diffuse false`, `autoBlendSkin true`.
`data/textures/` holds only `texture_notfound.png`. The three "Skin" entries in
the UI are **litspheres (matcaps)**: captured *lighting*, not skin colour.

And the renderer could not have displayed a skin texture anyway: every mesh
sampled **one shared 1x1 white** diffuse. `MeshInstance` carried only
`{mesh, litsphere}`; the material's seven parsed texture channels reached
exporters and nothing else.

### What shipped
- `MeshInstance` carries a `diffuse` path; each `Drawable` owns its texture.
- The app feeds each mesh **its own material's** `diffuseTexture` — body from
  the skin `.mhmat`, each proxy from its own. One shared map would paint the
  eyes with body skin.
- Empty keeps the 1x1 white (pure matcap) — what every mesh had before, so
  today's output is unchanged.
- A **named-but-unloadable** map is an error, not a silent fallback to white: a
  wrong path would otherwise render a plausible untextured body and read as a
  shading bug.

Tests generate their texture in-process. Nothing ships to test against, and it
keeps the suite free of any third-party asset licence.

### Licence triage, done before touching `data/`
- **`texturing.xyz` VFace — paid commercial, redistribution forbidden. It
  cannot go in this AGPL repo** (hard rule 6). Private local use only.
- `3dtextures.me`, `texturecan.com` — CC0, fine, record in `LICENSING.md`.
- `freepbr.com` — free to use, restricts redistributing raw files; per-asset.

I also cannot fetch binaries — the fetch tool returns text — so the images have
to come from the owner.

### Two stale M6 entries closed on the way
Quad->triangle already happens at buffer-build time (`RenderMesh.cpp:172`), and
the eye-space-fixed light is already how the camera works
(`SceneResources.cpp:277`).

### A mutation that did not compile
The first attempt at reverting to the shared white left a dangling `bindAll`
argument. The build failed, the **stale binary** ran, and all three tests
"passed". Caught by checking the error count rather than trusting the test line.
Redone so it compiles: **2 of 3 fail**, and the third correctly still passes
because it tests error reporting, not sampling.

### Verification
ctest **395/395** in debug, release, ASan; TSan separately. Format clean.
CI green on `72828a87`.

---

## 2026-09-01 01:45:22 — Session 080 · **autoBlendSkin: one asset set, a range of tones**

### The chunk
Continuing the owner's skin-variety request. Last session built the per-mesh
diffuse path; this one ports the mechanism that actually produces *variety*
without needing a texture per tone.

`mh::core::SkinTone` — a translation of `legacy/python/apps/autoskinblender.py`.
**Licence**: that file is AGPL, so the translation belongs in `core` and must
never land in `render`/`io`/`foundation` (Apache). Placed accordingly, and it
takes raw RGBA bytes so `core` needs no image library.

### What the reference actually does, and what is easy to get wrong
`ethnicDiffuseColor` is straightforward (`:116-118`, constants `:46-48`). The
image blend is not — four details each produce a plausible skin if botched:

- only weights **> 0** contribute, gathered caucasian, african, asian;
- **one** contributor returns the image **unmixed**, with no rounding pass;
- two are `w0*a + w1*b` — **both weights supplied, so not a lerp**;
- a third folds in at **weight 1.0 on the accumulator**, not a running average.

Rounding is `int(w1*d1 + w2*d2 + 0.5)`.

### Checked against the reference, not against myself
My hand-computed expectations (75, 40, 60) agreeing with my own code proves
nothing — the arithmetic and the port could share a misreading. So I ran
`image_operations.mixData` in the reference Python: it returns the same
**75, 40, 60**, and the same mid tone **[0.59033, 0.44, 0.338]**. Two
implementations agreeing.

### Honest limit
**Not yet wired to the viewport.** `MeshInstance` takes a litsphere *path*, and
a blended tone is an in-memory image. Feeding it through needs an
in-memory-texture path on `MeshInstance` — recorded as the next step rather
than claimed as done.

### Verification
ctest **400/400** in debug, release, ASan; TSan separately. Format clean.
CI green on `52c44896`.

---

## 2026-09-01 02:30:14 — Session 081 · **skin tone reaches the screen, and a wrong-but-plausible blend**

### The chunk
Finished what session 080 left explicitly undone: `autoBlendSkin` now drives the
viewport, so moving the ethnic sliders changes skin tone live. This is the
owner's "variety of human colours" working end to end from one asset set.

`MeshInstance` gained `litsphereRgba`/`Width`/`Height`. A blended tone has no
file behind it, and writing a temp PNG on every slider drag would put image
encode/decode on the interactive path. The three ethnic litspheres are decoded
**once**; the blend itself is a pass over bytes.

### The bug, and why it would have shipped
I first fed the blender `modifierValue("macrodetails/Caucasian")` — the **raw
slider**. Those are not renormalised: setting Caucasian to 1.0 leaves the other
two at 1/3 each, so the weights summed to **1.667** and a "pure" caucasian skin
blended as 1.0/0.33/0.33 of all three litspheres.

It looked completely fine. A rendered human with slightly-off skin is still a
rendered human.

What exposed it was insisting on a check that could **only** pass if the maths
were exact: with Caucasian at 1.0 the blend must reproduce the caucasian
litsphere *byte for byte*. It differed by 4.87% of the frame. `Human::factors()`
returns the renormalised values — what the reference reads
(`human.getCaucasian()`) — and with that:

| character | blend ON vs OFF |
|---|---|
| `Caucasian=1.0` | **0 pixels** of 4,096,000 |
| `African=1.0` | **214,693 (5.24%)**, worst 82/255 |

Pinned by `[core][macro][ethnic]`, which asserts the raw sliders sum past 1.5
while `factors()` sums to exactly 1.

### Two more traps hit on the way
- A mutation to isolate tone from geometry **failed to compile**, so the stale
  binary ran and produced a number identical to the unmutated one. Caught by
  counting build errors rather than reading the test line. Redone properly.
- My CI status filter, `select(.conclusion != "success")`, counts an
  **in-progress** job as a failure. It reported "failed: 1" on a run that was
  simply still going. Now filtering on `== "failure"`.

### A simplification
Adding fields to `MeshInstance` twice forced empty-brace padding through ~20
aggregate initialisations. Default member initialisers stop clang warning about
omitted fields, so all that padding is gone and the struct can grow again
without rippling. One blanket replacement over-reached into a different struct —
the build caught it.

### Verification
ctest **401/401** in debug, release, ASan and TSan. Format clean.
CI green on `a4d478fe` (0 genuine failures).

---

## 2026-09-01 02:53:08 — Session 082 · **normal maps, and a uniform that did nothing**

### The chunk
Surface detail — pores, wrinkles, fine skin structure — comes from a
tangent-space normal map, not the albedo. This is the mechanism behind the
owner's "MetaHuman-level detail" request.

The mesh already had **correct** tangents (Lengyel, with the reference's three
bugs fixed). They were simply never uploaded. Vertex layout is now
pos+normal+uv+tangent.

**One pipeline, not two.** `litsphere.vert`'s own comment planned a separate
shader per variant. A uniform branch is uniform control flow — every fragment in
a draw takes the same side — and avoids doubling pipeline state for a per-mesh
runtime choice. Whether a map exists is per MESH, so it needed a **per-mesh
uniform block**; the existing `Buf` is written once per frame.

A flat 1x1 placeholder cannot stand in for the branch: unpacking a flat map
gives `normalize(TBN * (0,0,1))` = `normalize(vNormal)`, and normalizing is
precisely what the no-map path must not do.

### The find: `normalmapIntensity` is dead in the reference
The intensity test failed with **0 differing pixels**. That was not a wiring bug.
The reference computes

    vec3 normal = (2.0 * normalH - 1.0) * normalmapIntensity;
    normal = normalize(tbnMat * normal);

A uniform scale followed by a normalize **cancels exactly**. The uniform has no
effect at any positive value; it would only matter under `CALC_NORMAL_Z`, which
the reference leaves commented out at `:64`.

Porting that faithfully would ship a control that silently does nothing, so it
is a deliberate divergence: scale **XY only, keeping Z** — the standard strength
idiom, and what `CALC_NORMAL_Z` was reaching for. Recorded in §8.0.

**Second defect, same area**: the reference's binormal is
`cross(vNormal, tang)`, discarding Lengyel's handedness sign. On a symmetric
human, mirrored UV islands then get inverted normal-map lighting on one side. We
carry `tangent.w` through.

### A guard of mine that had to get smarter
The litsphere source check banned `normalize(vNormal)` outright. Normal mapping
broke it — for a **correct** change, since a TBN basis needs a unit normal. It
now bans the *assignment* `normal = normalize(vNormal)`, which is the regression
it was written for, and I re-ran the mutation to confirm it still bites.

A blanket ban that fires on correct code is a bad test; this one earned its keep
only after being made precise.

### Verification
ctest **401/401** in debug, release, ASan; TSan separately. Format clean.
CI green on `d03c23fb`.

---

## 2026-09-01 03:15:59 — Session 083 · **AO, and materials finally reaching the viewport**

### The chunk
Ambient occlusion, and the wiring that lets a `.mhmat` actually drive what the
viewport draws.

AO is multiplied over the result **after** the additive term, matching
`litsphere_fragment_shader.txt:103-105`. The ordering is load-bearing: folding
it into `shading` earlier would also scale the additive contribution, which the
reference does not do.

**Roughness is not in this pipeline at all.** The `.mhmat` format has seven
channels — Diffuse, BumpMap, NormalMap, DisplacementMap, SpecularMap,
TransparencyMap, AoMap — and roughness is not among them. It belongs to the PBR
path, not the litsphere one. Recorded rather than faked.

### Testing AO directionally
"The image changed" would pass on a map that made the model *brighter*, which is
not what multiplying by an occlusion term does. So the test asserts the
direction: **0 pixels got brighter**. That is the property; the pixel count is
just corroboration.

### Materials now drive the viewport
`viewportMapsOf()` reads diffuse, normal, AO, intensity and `autoBlendSkin` in
**one** pass. It replaces two separate `.mhmat` loads per rebuild — which ran on
every slider drag. Body and each worn proxy read **their own** material, so
clothing detail cannot inherit the body's.

### The check that mattered
With no maps shipping, all of this must be **inert**. Comparing against a stale
screenshot showed 4.77% difference — but that baseline predated two intentional
changes (the litsphere renormalize fix, and tone blending), so it proved
nothing. The honest check is against the **previous commit**: stash, build,
render, restore, render.

Result: **0 differing pixels of 4,096,000**. The plumbing is genuinely dormant
until a material supplies maps.

### Verification
ctest **401/401** in debug, release, ASan; TSan separately. Format clean.
CI green on `91b110d4`. Stash list empty afterwards (checked — a previous session
lost time to an unverified stash).

### Still needed from the owner
The texture files. I cannot fetch binaries, and `texturing.xyz` VFace cannot
enter this repo at all (paid, redistribution forbidden). Everything downstream
of "drop a PNG in `data/` and name it in a `.mhmat`" now works.

---

## 2026-09-01 03:41:45 — Session 084 · **the headline metric, and a perf test that measured the sanitizer**

### 60 fps on the subdivided mesh: MET
The project's headline metric, finally measured rather than assumed.

Subdivided mesh, 55,784 render verts, 1280x960:
**2.5-4.6 ms median in release — 218 to 354 fps** against a 16.7 ms budget.
Base mesh 1.86 ms.

**Measured pessimistically on purpose.** The offscreen path builds a whole
`SceneResources`, uploads every buffer and texture, draws, AND reads the image
back — on every call. The interactive widget does none of that per frame:
`ViewportWidget.cpp:94` creates its resources once and `:113` re-uploads only on
change. So the interactive steady-state frame is strictly cheaper than the
number above, and an upper bound that fits the budget is the useful direction.

I also owed this measurement: last session added 4 floats a vertex (tangents),
a 50% larger vertex. The budget has 3.6x headroom even including full re-upload.

### The test I nearly shipped broken
I wrote it as `CHECK(median < 16.7)`. It passed in debug and release, then
**failed under ASan at 59.5 ms** — 24x slower, because every memory access is
instrumented.

A timing assertion under a sanitizer measures the **sanitizer**. It says nothing
about the renderer and fails for a reason unrelated to the code under test. The
timing is now skipped when `__has_feature(address_sanitizer)` or
`thread_sanitizer` holds; the render itself is still exercised for correctness
in every configuration.

Worth remembering as a rule: **never assert wall-clock under instrumentation.**

### A stale entry closed with it
"Persistent vertex/index buffers" was already true for the path that matters —
the widget keeps its resources across frames. `OffscreenRenderer` rebuilds per
call by design, being one-shot, which is precisely what makes its timing a
pessimistic bound. Partial dirty-range updates stay closed from M2: 0.7% of a
frame, and the reference's own "partial" path is a pessimisation.

### Still unmeasured
The real interactive swapchain path (`QRhiWidget` with a live surface). The
offscreen bound is strong evidence, not a substitute.

### Verification
ctest **401/401** in debug, release, ASan; TSan separately. Format clean.
CI green on `855aed96`.

---

## 2026-09-01 03:59:29 — Session 085 · **I broke CI with the fix for the last thing I broke**

### What happened
The previous chunk's fps guard asserted `median < 16.7` and I made it skip under
sanitizers, having watched ASan report 59.5 ms. I stopped there.

CI then failed the **debug** job: **35.5 ms on a CI runner**, ~10x the release
figure. Unoptimized code on shared, virtualised hardware is no more a valid
subject for a frame budget than instrumented code is. I had fixed one instance
of the error and assumed its sibling was fine.

| build | subdivided median |
|---|---|
| release, dev machine | **2.5-4.6 ms** |
| ASan, dev machine | **59.5 ms** (24x) |
| debug, CI runner | **35.5 ms** (~10x) |

### The rule, stated properly this time
A wall-clock budget is a claim about **the build people actually run**.
`timingIsMeaningful()` now requires `NDEBUG` **and** no sanitizer. Verified in
both directions rather than assumed: release prints `CHECK( median < 16.7 )`,
debug prints `timing not asserted in an unoptimized or instrumented build`.

The measurement is still printed everywhere, so the number stays visible where
it is not asserted.

### What I should have done
When ASan exposed the problem, the question was "where else is this wrong?", not
"how do I silence ASan?". Two configurations were already failing that test's
premise; I only looked at the one in front of me.

### Verification
ctest **401/401** in debug, release, ASan; TSan separately. Format clean.
`855aed96` was green; `db2e3c1c` failed debug and is fixed here.

---

## 2026-09-01 04:30:38 — Session 086 · **a production render the app could not reach**

### CI recovered
`5da99c58` green, all 9 jobs. The red I introduced with the frame-budget
assertion is cleared.

### The finding
`OffscreenRenderer` existed, was tested, and was **never used by the
application**. The "production render" capability had no user-facing entry
point at all — `--screenshot` grabs the *window* (chrome included), which is a
UI capture, not a render.

### What shipped
- `RenderSettings::transparentBackground` clears alpha to 0. The readback was
  already RGBA8888, so alpha was carried all along; the clear hard-coded it to 1.
  The body stays opaque because the shader writes `outColor.a` from the diffuse
  and the no-map stand-in is opaque white.
- `--render <png> [--transparent]`: headless, needs a GPU but **no window**, so
  it works where `--screenshot` cannot.
- The scene assembly is factored into `buildScene()` and **shared** by the
  viewport and the render. A duplicate would let the two quietly disagree about
  what a character looks like — which is exactly the bug a "production" path
  must not have.

### Verified by a third party, not by our own reader
Blender reading the PNGs: opaque render **1,048,576/1,048,576 opaque**;
transparent render **84,894 opaque (the figure) against 963,682 clear**, corner
alpha 0.00. Then looked at the image: a clean full figure on transparency.

The companion test matters as much: the **default** render must stay fully
opaque. A default that drifted transparent would break every screenshot and
export path downstream, and would look like a compositing bug somewhere else
entirely.

### Three stale entries closed
Quad->triangle already happens at buffer-build time (`RenderMesh.cpp:172`), the
eye-space-fixed light is already how the camera works
(`SceneResources.cpp:277`), and the cached bounding box is **not worth
building**: `Mesh::boundingBox()` has zero production callers and is one linear
pass. Same call as `findFaceGroup`.

### Verification
ctest **402/402** in debug, release, ASan; TSan separately. Format clean.

---

## 2026-09-01 04:48:59 — Session 087 · **third red run from one bad idea**

### What happened
`76c18bf6` failed the **release** job: **17.08 ms** on a CI runner against the
16.7 ms I was asserting. Debug passed, because I had already excluded it.

That is the same assertion failing for the third time:

| build / machine | subdivided median |
|---|---|
| release, dev machine | **2.5-4.6 ms** |
| ASan, dev machine | **59.5 ms** (24x) |
| debug, CI runner | **35.5 ms** (~10x) |
| release, CI runner | **17.1 ms** (over) |

Each time I narrowed the exemption — skip sanitizers, then skip debug — instead
of asking whether the assertion belonged in CI at all. It does not. **A
wall-clock budget is a claim about target hardware**, and a shared, virtualised
runner with a shared GPU is not the target. The premise was wrong from the
first commit; the two "fixes" were treating symptoms.

### The actual fix
The test now **measures and reports**, and asserts only what is
hardware-independent: that the subdivided mesh renders repeatedly and returns a
correctly sized image. The timing prints on every run.

The 60 fps claim lives in `memory/todo.md` as a measurement on **stated
hardware** — this machine, release, 1280x960, 2.5-4.6 ms — which is what such a
claim always was.

**Rule recorded: performance gates belong on controlled hardware. Never assert
wall-clock in CI.**

### Worth noticing about my own process
The mutation-testing and grounding discipline has been catching real bugs all
session, but it did not catch this, because the test *passed locally every
time*. What exposed it was CI hardware I do not control — three times. The
lesson is about where a check runs, not how hard it checks.

### Verification
ctest **402/402** in debug, release, ASan; TSan separately. Format clean.

---

## 2026-09-01 05:13:13 — Session 088 · **import returned a naked character**

### CI recovered
`fa50f20d` green, all jobs. The frame-budget assertion that went red three times
is settled: measured and reported, never asserted in CI.

### The asymmetry
Export has been multi-mesh for a while — a dressed character is written as the
body plus one entry per worn proxy. **Import read only `mMeshes[0]`.**

So a round trip silently returned a naked character. The clothes exported
correctly, the file was valid, and everything worn disappeared on the way back
in. Nothing failed; the result was just wrong.

`io::importScene` now reads every mesh, with its name. `importMesh` stays as the
single-mesh entry point and still reports `meshCount`, so a caller can tell what
it is dropping.

### Testing it against something that is not us
The round trip goes out through our writer and back through **assimp**, so
agreement is not self-confirming — and one case exports through our
**hand-rolled GLB writer**, which makes it a genuine cross-check rather than a
library agreeing with itself. Same reasoning that made `usdchecker` worth using
on the USD work.

Mutation-verified: restricting the loop to the first mesh fails 2 of 3 cases.

### A judgement call
A mesh with no triangles is **skipped, not fatal**. Real scenes carry empty or
non-triangular helper meshes, and rejecting a whole file for one of them would
make many usable assets unopenable. The scene is an error only when nothing
usable came back at all — so a genuinely broken file still fails loudly.

The trust-boundary handling is unchanged and deliberate: validation before any
step that dereferences indices (assimp segfaults otherwise — measured), and
non-finite coordinates refused rather than carried into every consumer.

### Verification
ctest **405/405** in debug, release, ASan; TSan separately. Format clean.

### Still open on import
Materials, skins and node transforms. Geometry and names only for now.

---

## 2026-09-01 05:35:36 — Session 089 · **every imported object was at the origin**

### The bug
A glTF/FBX/DAE scene places meshes with a **node graph**: the mesh data is in
local space and the node carries the transform. `importScene` read
`aiScene::mMeshes` directly and never walked `mRootNode`.

So every imported object came back **stacked at the origin**. No error, no
warning, valid file — a whole scene collapsed into one pile.

The previous chunk fixed import returning only the first mesh. This is the same
shape of bug one level down: the data was all there and quietly mis-assembled.

### Caught with a fixture that is not ours
Our own exports write meshes at identity, so a round trip through our writer
could never have exposed this. `tools/make_scene_fixture.py` has **Blender**
write two identical cubes whose only difference is a node translation of ±5.

Before the fix both imported at x ∈ [-1, 1]. That is the whole test: geometry
identical, placement the only variable.

The fixture is two Blender primitives — no third-party asset — so it is CC0 like
the rest of the tree, with provenance in `tests/golden/scene/README.md`.

### What the fix buys beyond the bug
Walking the graph gives **instancing** for free: one mesh referenced by two
nodes is two placed objects, which a mesh-array loop cannot express at all.
A file with no node graph still falls back to the mesh array.

Only positions need transforming — normals and tangents are not imported yet, so
there is no inverse-transpose to get wrong. Worth remembering when they are.

### The assert-before-write rule paid again
Two scripted edits failed their `count == 1` assertion because `clang-format`
had reflowed the target (`entry.name` onto two lines, differently from how I
wrote it). Nothing was written either time. That rule has now caught four
silent no-op edits this session.

### Verification
ctest **406/406** in debug, release, ASan; TSan separately. Format clean.

### Still open on import
Materials and skins. Geometry, names and placement now — no shading or rigging.

---

## 2026-09-01 06:03:40 — Session 090 · **exported characters had no skin, and nobody knew**

### The chunk
Materials on import. Which turned up a missing half of **export**.

### The find
Chasing why no texture path survived a round trip, I checked whether the
exported file contained the name at all. It did not. **Our exporter never wrote
texture paths to the assimp-backed formats.**

So a character exported to FBX or DAE arrived in a DCC tool with its colours and
**no skin** — the albedo reference simply absent from the file. That reads as a
broken exporter, not a moved texture, and it had been true the whole time.

Fixed in `fillMaterial`: `AI_MATKEY_TEXTURE_DIFFUSE` and `..._NORMALS`.

### Measuring instead of assuming
Round-tripping a fully specified material, then reading the exported bytes:

| | name | diffuse | specular | opacity | textures |
|---|---|---|---|---|---|
| FBX | yes | yes | **no** | yes | **written, not read back** |
| Collada | yes | replaced by texture | yes | yes | yes |

Two results needed understanding rather than working around:

- **FBX textures ARE written** — the path appears three times in the exported
  bytes — but assimp's FBX *importer* does not read material textures back.
  That is a reader limitation; a DCC tool opening the file gets the reference.
  So the test checks the **file**, not the round trip. Without looking at the
  bytes I would have recorded "FBX loses textures", which is false and would
  have sent someone hunting a non-existent exporter bug.
- **Collada replaces the diffuse colour with the texture** when one is present.
  That is the format's own semantics, not a loss on our side.

Shininess is deliberately **not** asserted: FBX returned a default and Collada a
0..128-style exponent. A single expected value would be wrong somewhere, and a
test that encodes a wrong expectation is worse than no test.

### A deliberate API choice
`material` is `optional`. **Absent means the file carried none** — not that it
carried a default — because a caller substituting its own default has to be able
to tell those apart.

### Verification
ctest **408/408** in debug, release, ASan; TSan separately. Format clean.

### Still open on import
Skins. Geometry, names, placement and materials now; no rigging.

---

## 2026-09-01 06:32:31 — Session 091 · **rigged exports came back unrigged**

### The last piece of import
Skins. All **163 joints** now survive a glTF round trip with their names and
inverse-bind matrices.

Before this, a character round-tripped through glTF kept its geometry AND its
bones — and **nothing connected them**. Both halves present, the binding gone.
That is the third variant of the same failure this milestone has produced:
data intact, assembly silently missing (first mesh only, then node transforms,
now skin binding).

### The test asserts the property, not the plumbing
Bone counts and names are easy to check and weak. The property a bad vertex-id
remap actually breaks is that **weights are a partition of unity per vertex**.
110,113 assertions: every weight in [0,1], every weighted vertex summing to 1
within 1e-3. Mutation-verified — halving the weights fails it.

Bone vertex ids are post-`JoinIdenticalVertices`; assimp remaps them, so they
index the vertices we just read rather than the file's originals. That is
exactly the kind of off-by-a-remap that a count-only test would wave through.

### Two deliberate choices
- `ImportedSkin` is **owning**, unlike `foundation::SkinView`. A view over
  assimp's scene dangles the moment the importer goes out of scope, and an
  importer that hands back dangling spans is a trap.
- A weight naming a vertex the mesh does not have is an **error, not a silent
  drop**. Dropping it leaves a body part unbound and moving with the wrong bone
  — which presents as a rigging bug, not a corrupt file.

### M7 import is functionally complete
Geometry, names, node placement, materials, skins. The remaining gaps are
format-specific and recorded: assimp's FBX importer does not read material
textures back (we do write them), and Collada replaces diffuse colour with its
texture.

### Verification
ctest **410/410** in debug, release, ASan; TSan separately. Format clean.

---

## 2026-09-01 07:03:34 — Session 092 · **USDZ, validated by Apple's own checker**

### The chunk
`io::writeUsdzScene` — one self-contained file, stage plus every texture.
**`usdchecker --arkit` reports Success**: Apple's own validator on its strictest
profile.

### Grounding the format in a real artifact, not the spec from memory
Rather than implement from what I remembered of USDZ, I had `usdzip` produce a
reference archive and read it:

    entry: ref.usda  compress=0  data offset 64 (mod 64 = 0)  extra_len 26
    extra: id 0x1986, size 22, zero payload   (30 + 8 + 26 = 64)

That gave three rules I would otherwise have guessed at:

- **STORED, never deflated.** A consumer memory-maps the archive and reads the
  stage in place, so compressed data is simply unreadable.
- **Data on a 64-byte boundary**, padded through the zip extra field with header
  id **0x1986**.
- The padding is a **well-formed TLV**, not loose bytes — which is why a 1..3
  byte gap must take another whole 64 rather than emit a malformed field. I
  would not have known to do that from the prose description.

### Verified twice, by things that are not us
- `usdchecker --arkit`: Success.
- Python's `zipfile`: CRCs valid, `stored=True`, data at offset 64, payload
  reads back as `#usda 1.0`.

The unit test then pins the structure so a regression is caught on a machine
with no USD tooling.

### No second definition of "self-contained"
`writeUsdaScene` already copies each texture beside the stage and references it
by filename. So the packager stages into a scratch directory and archives
**whatever is there** — there is no separate list of what a self-contained stage
needs, which is the thing that would drift.

### Two more stale entries closed
STL, Collada, 3MF and BVH export all exist; glTF embedded textures exist and are
tested, including the rejection path for a format GLB cannot carry. Draco
remains genuinely open and is now its own line.

### Verification
ctest **411/411** in debug, release, ASan; TSan separately. Format clean.
CI green on `8d9d1855`.

---

## 2026-09-01 07:29:01 — Session 093 · **UsdSkel, and a vacuous test I wrote myself**

### The chunk
UsdSkel: skeleton and skinning in USD. `usdchecker` reports **Success** on a
163-joint skinned stage — `SkelRoot`, a `Skeleton` prim with joints,
bindTransforms and restTransforms, and the mesh bound through `SkelBindingAPI`
with `primvars:skel:jointIndices`/`jointWeights`.

### Two traps usdchecker cannot see
Both produce a stage that **validates and poses wrongly**, so both are pinned:

1. **USD uses ROW vectors; this codebase uses COLUMN vectors.** They are
   transposes. An untransposed write emits every joint transposed and nothing
   complains. Verified positionally: the root bone's head
   `(0, 0.5639, -0.7609)` must be the **last ROW** of its bindTransform.
2. **`restTransforms` are LOCAL, `bindTransforms` are WORLD.** Emitting world
   for both compounds every joint by its ancestors.

A third trap `usdchecker` *did* catch: joint tokens are USD **paths made of
identifiers**, and MakeHuman bone names carry a dot and a dash
(`upperarm01.L`, `finger1-1.L`) — a dot being the path property separator. They
are sanitised to `clavicle_L/shoulder01_L/upperarm01_L`.

### The part worth remembering: my own test was vacuous
My first transpose check asserted the number `0.5639` appeared *somewhere* in
the first matrix. It appears either way — transposing moves it, it does not
remove it. **The mutation passed all 29 assertions.**

That is precisely the failure I have spent this session catching in other
people's absence, written by me, in a test whose entire purpose was to catch a
transpose. The lesson is not "mutation-test more"; it is that an assertion about
a VALUE is worthless when the bug is about POSITION. Strengthened to match the
whole last-row tuple, and to require that `(0, 0, 0, 1)` is absent — the
mutation now fails 2 assertions.

### Verification
ctest **413/413** in debug, release, ASan; TSan separately. Format clean.
CI green on `97282c55`.

---

## 2026-09-01 07:54:32 — Session 094 · **import had no unit contract: a 17 cm human**

### The finding
Our own GLB round-trips a **16.9455 dm** human back as **1.6946**. glTF is
metres, the internal unit is the decimetre, and import treats whatever it reads
as internal units. Fed straight back into the app that is a **17 cm doll** — the
same 10x class recorded against the reference's FBX in §8.

### The fix is a contract, not a silent conversion
Rescaling on import would have been the obvious move and would have been wrong:
`fbxHeight` in the unit tests measures a file *precisely because* import does not
rescale. Breaking that to fix the other use would trade one bug for another.

So import now **states** what it returns: coordinates in file units, plus
`ImportedScene::metersPerUnit`. `decimetres = units * metersPerUnit * 10`.

Sources, in order of trust:
- **glTF/GLB — 1.0 by specification.** Verified: assimp reports *no* unit
  metadata at all for glTF, so the spec is the only source there is.
- **FBX — the file's own `UnitScaleFactor`** (centimetres per unit).
- **Everything else — 0, meaning "you decide".** OBJ and STL are genuinely
  unitless, and reporting a made-up 1.0 would let a caller convert confidently
  and wrongly. Saying "unknown" is the useful answer.

### What I checked before assuming a problem
- `unitScale()` is defined **once** and used by all four writers; USD's second
  switch is a different quantity, not a duplicate.
- **A new unit cannot silently go wrong**: adding `Unit::Millimeter` produced
  three `-Werror,-Wswitch` errors, one per switch. That is what makes the
  writer-side item closable rather than merely believed.
- **Our FBX export is correct for real tools**: Blender reads it at
  **1.694 m**. assimp's own readback of the same file disagrees with Blender's,
  which is worth knowing but is not our defect — recorded as an observation, not
  a claim.

### Verification
ctest **415/415** in debug, release, ASan; TSan separately. Format clean.

---

## 2026-09-01 08:57:00 — Session 095 · **the OBJ was the one export shipping the helper cages**

### The chunk
`mh::core::bodyFaceMask(base, shown, worn)` — one function answering "which body
faces exist" — wired into the viewport, into OBJ export, and (through
`RenderMesh`) into every other writer.

### What was actually wrong
Two things, and the second was bigger than the one I set out to fix.

`todo.md` said proxy `delete_verts` were never applied: `visibleVertexMask` and
`Mesh::faceMaskForVisibleVertices` existed and were tested, but a grep of `src/`
found **`visibleVertexMask` had no caller at all**. True, and a no-op today —
all four shipped `.mhclo`/`.proxy` files declare zero `delete_verts`.

Wiring it up meant looking at where the body mask reaches the writers, and that
is where the measured defect was. Exporting the **same** default character:

```
--export mask.obj   body 18486 faces
--export mask.glb   body 13378 quads (26756 tris)
```

Every format takes its geometry from the `RenderMesh`, which has
`staticFaceMask` applied. OBJ alone wrote the `Mesh` directly, with `{}` for
its face mask — so the OBJ, and only the OBJ, carried the **5,108**
`joint-*`/`helper-*` faces. base.obj is 138 parts helper geometry to 1 part
body; that file opens as a figure in a solid skirt with a box over its face.
Nothing was broken enough to fail: the file parsed, the counts were plausible,
and no test looked.

### The fix is one answer, not two
`staticFaceMask` (helper groups) **AND** `visibleVertexMask` (what is worn),
computed in one place. `delete_verts` index the base mesh, so the mask can only
be built there; the subdivided case expands it 4:1, because child face `f*4+k`
comes from parent `f` and inherits its group (`Subdivider.cpp:258-277`).

The expansion is checked against an **independent** derivation rather than
itself: the subdivided mesh carries its own face groups, so `sub.staticFaceMask()`
is computed with no knowledge of the 4:1 layout. If the expansion is wrong they
disagree.

### Mutation, four ways
| mutation | result |
|---|---|
| `&` → `\|` in the combine | 3 of 4 unit tests fail |
| `expanded[f*4+k] = mask[f/2]` | the subdivision test fails |
| drop the OBJ mask argument | **does not compile** — `-Werror,-Wunused-parameter` |
| pass an empty span instead | `app_smoke` still **passes**; `app_smoke_obj_faces` fails, 19,506 vs 14,398 |

That last row is the whole reason `app_smoke_obj_faces` exists. Asserting on the
app's own announcement (`body: 13378 of 18486 faces visible`) proves the mask was
*computed*; only reading the file proves it was *written*, and it was the second
claim that had been false.

### Ponytail
Cut the per-group counting loop from the CMake checker — the regression is helper
faces leaking **in**, which moves the total just as surely. `file(STRINGS ...
REGEX "^f ")` + `list(LENGTH)`, 12 lines shorter. Kept the memoisation guard in
`applyBodyMask`, but rewrote its comment: 0.21 ms of `setFaceMask` does not earn
it, and suppressing a printf on every slider drag does.

### Verification
ctest **420/420** in debug, release, ASan and TSan. Format clean. Benchmarks
unchanged and all ahead of the Python baseline (`setFaceMask` 0.21 ms vs 1.85,
`faceMaskForVisibleVertices` 0.01 ms).

### Still open
Proxy-on-proxy masking (`transferVertexMaskToProxy`) — each worn proxy still
exports with an empty face mask. And the `delete_verts` half remains covered by
synthetic proxies only, because no shipped asset declares any.

---

## 2026-09-01 10:10:41 — Session 096 · **our FBX exports arrived in Blender as chrome**

### The chunk
Specular material data across the assimp-backed writers: shininess written and
read as an **exponent**, and metalness stated rather than left to a template.

### Two defects, and the second needed a third party to see
**One.** `fillMaterial` never wrote `AI_MATKEY_SHININESS` — while `importScene`
**read** it. Measured, not inferred: a 0.96 skin came back

| format | before | after |
|---|---|---|
| FBX | **0.2** (our own struct default; the file carried nothing) | 0.96 |
| Collada | **10** (assimp substituting a fixed exponent) | 0.96 |

10 in a field every consumer treats as 0..1 is not a small error. glTF and USD
roughness is `1 - shininess`, so a Collada round trip asked for roughness
**-9**, clamping to 0 — a perfect mirror. There is now a test that does exactly
that round trip and asserts `"roughnessFactor":0.04`.

An earlier session had *already seen* half of this and written it off in a
comment: "Shininess is deliberately not asserted: the conventions differ per
format (FBX returned a default, Collada a 0..128-style exponent)". The
conventions did not differ. The exporter was silent and the numbers were
invented downstream. A plausible explanation is how a measurement stops being
followed up.

**Two.** Blender 5.2, reading our own `makehuman --export x.fbx`:

```
before   DefaultSkin  roughness 0.0000  metallic 1.0000     <- chrome
after    DefaultSkin  roughness 0.0000  metallic 0.0000
GLB      DefaultSkin  roughness 0.0400  metallic 0.0000     <- always was right
```

assimp's FBX exporter fills its material template with `ReflectionFactor` 1, and
Blender reads that key straight into Principled `metallic`
(`import_fbx.py:2101`). `.mhmat` has no metalness concept at all — which is
exactly why our glTF writer already hard-codes `"metallicFactor":0` — so the two
exports of one material disagreed about whether skin is metal. One property
(`AI_MATKEY_REFLECTIVITY = 0`) closes it.

### What I did NOT change
Blender's FBX roughness stays 0. It reads `Shininess` as 0..100 through
`1 - sqrt(S)/10` (`import_fbx.py:2083`, whose own comment calls it "totally
empirical"), so anything above shininess 0.78 clamps. Bending our scale to that
curve would abandon the exponent's defined meaning to please one importer, and
would still not agree with the linear glTF conversion — the two are different
curves, not different constants. Recorded as an observation.

I also left `roughness = 1 - shininess` alone. The reference documents shininess
as "the inverse of roughness" (`material.py:686`) and `.mhmat` clamps it to
0..1 (`Material.cpp:242`); I have no oracle saying the physically-motivated
`sqrt(2/(exponent+2))` is what the assets were authored against.

### Mutation
| mutation | result |
|---|---|
| import without the exponent conversion | 2 tests fail |
| export the raw 0..1 number | 2 tests fail |
| `ReflectionFactor` back to 1 | the FBX property test fails |

The FBX property test reads the file with **assimp directly**, not through our
importer — the round-trip tests would pass just as happily if both ends were
wrong in the same way.

### Ponytail
`std::clamp` for a hand-rolled ternary; 11 comment lines cut to 5 by moving the
Blender measurement into the test that reproduces it.

### Verification
ctest **423/423** in debug, release, ASan and TSan. Format clean. Benchmarks
unchanged.

---

## 2026-09-01 10:42:02 — Session 097 · **a malformed-input sweep that found nothing, and two holes in itself**

### The chunk
`tests/unit/test_malformed_input.cpp`: 11 readers, one corpus of hostile bytes,
run in debug and under **ASan**.

### The result, stated as what it is
**It found no bugs.** Every reader we own already returns an error rather than
crashing or reading out of bounds, and so does assimp on a corrupted GLB. The
sweep's value is that this stays true, not that it fixed anything today.

Corpus is derived from **real shipped files**. Invented junk bounces off the
first `if` in a parser; a file that is valid for a hundred thousand lines and
then is not gets deep into the state machine first. Mutants: unmodified
(control), empty, truncated at 1/10/50/99%, valid prefix + 0xFF, valid prefix +
NUL, every digit turned to 9, and 8 x 32 scattered byte flips from a fixed
xorshift32 seed.

### Two holes, both found by mutating rather than by reading
**One — the corpus was vacuous for OBJ.** I capped samples at 256 KB for
wall-clock. An OBJ is *sectioned*: every `v`, then every `vt`, then every `f`.
The first face line of `base.obj` is at **line 40,511**, well past the cap, so
the entire face parser went unfuzzed. Proof rather than suspicion: I removed
**both** of the codebase's vertex-index bounds checks — `ObjReader.cpp:75` and
`Mesh.cpp:61` — and the sweep still passed.

Fixed by using `axis.obj` (6 KB, 100 faces, fits whole) and, so it cannot
recur, a per-reader `mustContain` string the capped sample must still hold. The
`.target` guard was `"\n"` at first, which matches a licence header and would
have guarded nothing; it is a real delta line now.

**Two — there was no control.** "Nothing crashed" is satisfied just as well by a
reader that rejects every input, and that is precisely what the OBJ reader does
to all 14 mutants. The unmodified sample is now the first mutant, so the
success-path assertions (a loaded mesh must be indexable, a skeleton's parents
must precede their children) actually run.

The codebase itself was never at risk: `test_obj_reader.cpp:109` catches that
mutation. The sweep is defence in depth, not the gate — and I would have
believed otherwise if I had not mutated.

### Ponytail
Kept the hand-rolled xorshift32 over `<random>` and said why in the comment:
`<random>`'s engines are reproducible but its **distributions are not
specified**, so libc++ and libstdc++ produce different sequences from one seed.
A corpus that differs per standard library is not a fixture.

### Verification
ctest **424/424** in debug, release, ASan and TSan. Format clean. Sweep costs
0.8 s in debug, 2.1 s under ASan.

---

## 2026-09-01 11:17:35 — Session 098 · **the production render was aliased and the header said it could not be**

### The chunk
MSAA in `OffscreenRenderer`, and one `render::kSampleCount` that the viewport
and the offscreen path both read.

### The defect
`OffscreenRenderer.h` says the offscreen renderer "shares SceneResources with
the interactive viewport, **so the two cannot drift**". They shared everything
except the one number that does not live in `SceneResources`: `ViewportWidget`
asked for 4x MSAA, `OffscreenRenderer` passed a hard-coded **1**.

So `makehuman --render out.png` — the production output, the one a picture
would ship from — was aliased, while the same scene on screen was smooth.
Measured at 1024x1024 with `--transparent`:

| | alpha 0 | alpha 255 | partial |
|---|---|---|---|
| before | 963,682 | 84,894 | **0** |
| after | 962,487 | 83,720 | **2,369** |

**Zero** partial-coverage pixels: a hard stair-step silhouette everywhere.

### Why partial alpha is the right assertion
Nothing else in this scene can produce it. The shader writes `outColor.a` from
the diffuse map, and the no-map stand-in is opaque white, so every fragment is
alpha 1. A pixel between 0 and 255 can only be coverage resolved from several
samples. A coverage or luminance metric would have been satisfied by a slightly
different camera.

### How it was found
Not by reading the code. The todo entry read "Qt RHI swapchain and MSAA — the
interactive widget, **not offscreen**", which I took for a scope note and
checked: the widget half was already done and stale, and "not offscreen" turned
out to be an accurate description of a bug.

### Implementation notes worth keeping
- The read-back texture stays **single-sample**. `readBackTexture` cannot
  resolve and a multisample texture is not readable, so drawing goes to a
  multisample colour buffer that the pass resolves into it at `endPass`.
- Depth must carry the same sample count as colour or the target is incomplete.
- `supportedSampleCounts()` is checked; where 4x is not offered it falls back to
  1, and the test **skips** rather than fails. It asserts MSAA is used when
  available, not that every machine has it.

### Cost: not measurable here
Three runs each of the subdivided render, release: **1x gave 2.67 / 2.99 /
4.79 ms, 4x gave 4.89 / 3.26 / 3.83 ms**. The ranges overlap; the difference is
below this machine's noise. Both sit far inside a 16.7 ms budget. Stated as
indistinguishable rather than free, because the measurement does not support
the stronger claim.

### Verification
ctest **424/424** in debug, release, ASan and TSan. Format clean.
Mutation-verified: forcing `samples = 1` fails the new test at partial 0.

---

## 2026-09-01 12:58:03 — Session 099 · **a dense morph target cost the same whatever it moved**

### The chunk
Sparse glTF morph accessors, chosen per target.

### The measurement
A morph target is a delta per RENDER vertex and almost all of them are zero.
`nose/nose-base-up.target` moves **305 of 19,158** mesh vertices — 1.6% — and
dense cost it the same **261,996 bytes** as a target that moves everything. The
three-target fixture GLB was 1,930,380 bytes with **785,988 of it, 41%, deltas
that were overwhelmingly zero**.

That is not just waste. It is what made exporting the modifier set impossible:
1,280 targets dense is **335 MB**.

| | morph bytes | file |
|---|---|---|
| dense | 785,988 = 3 x 261,996 | 1,930,380 |
| sparse | 133,744 = 35,200 + 93,840 + 4,704 | 1,278,576 |

**−83% of the morph payload, −34% of the file.**

### Verified three ways, because "smaller" is the easy half
1. **Byte-level**: the test decodes the sparse indices and values back out of
   the BIN chunk, rebuilds the delta array and compares it to the input delta
   for delta. A wrong index mapping shrinks the file just as well and writes a
   perfectly valid GLB that moves the wrong vertices. Mutation: shifting every
   index by one leaves 190 vertices wrong and fails.
2. **Blender 5.2**, dense file against sparse file: `head-oval` 2200,
   `head-trans-backward` 5865, `nose-base-up` 294 moved vertices, max deltas
   equal to six decimals. Identical.
3. Indices asserted **strictly increasing**, which the spec requires.

### The test that looked right and was not
`min`/`max` must describe the accessor's **effective** values — zeros included —
or glTF-Validator reports `ACCESSOR_MIN_MISMATCH`. My first assertion was "the
bounds straddle zero" on nose-base-up. It passes either way: that target's
deltas already go both directions on every axis, so commenting out the
zero-fold left every test green.

It took a **one-directional** morph to separate them — ten vertices moving only
+y, where bounds over the stored values alone give `min.y = 0.05` for an
accessor that reads zero at 21,823 of its 21,833 entries. Only then did the
mutation fail.

### Ponytail
Sparse doubled the per-target state and left **eight** parallel vectors on
`Packed`. Grouped into one `MorphBlock` and merged the sparse and dense write
loops into one, indexed either way.

### Still open, and it is the interesting part
**Nothing in the application exports morph targets.** `writeGlb` takes them,
`mh_export_fixture` passes three, and `main.cpp` passes none. Sparse accessors
are what make wiring that up feasible; the wiring itself is not done.

### Verification
ctest **427/427** in debug, release, ASan and TSan. Format clean.

---

## 2026-09-01 13:38:31 — Session 100 · **every export from the application was a statue**

### The chunk
Hand the rig the application already built to a writer.

### The defect
The app loads the skeleton, fits the joints to the morphed body, compiles the
weights, prints *"clamped 3,725 of 19,158 vertices to 4 influences"*, and poses
the mesh with them. Then it exports none of it. Measured on
`makehuman --rig mixamo_superset --pose tpose --export out.glb`:

```
skins: 0
nodes: 2
attributes: ['NORMAL', 'POSITION', 'TEXCOORD_0']
```

No skin, no joint hierarchy, no `JOINTS_0`, no `WEIGHTS_0`. Every export the
application has ever produced was a statue.

Everything needed already existed and was tested: `buildSkinData`,
`GltfSceneEntry::skin`, `writeGlb`'s skin path, and `mh_export_fixture`'s
`rigged.glb` validated in Blender. **Nothing connected them**, and
`app_rig_superset` passed the whole time because it only checked that the app
*announced* the rig. Same shape as the OBJ helper cages two sessions ago.

### A second defect, found doing it
`--rig` alone did nothing. `loadPoseRig` returned early for `"rest"`, so
`--rig mixamo_superset` with no `--pose` loaded no skeleton at all — and the
bind pose is precisely the most useful thing to export. The rig now loads
whether or not a pose is asked for.

### The decision worth recording: bind pose = the exported pose
The mesh written out is the **posed** one. Writing the REST globals with it
would let a DCC apply the pose a second time. So joint b's bind global is
`skinning[b] * restGlobal[b]`, which makes the file's skinning matrices
identity and the mesh arrive exactly as the app draws it.

Blender confirms both halves: armature with **179 bones**, body with an
ARMATURE modifier and **179 vertex groups**, and the armature-evaluated mesh
differing from the raw mesh by at most **1.2e-5**. No double transform.

One thing not to chase later: Blender lists a stray `Icosphere` after importing
a rigged GLB. It is Blender's own bone-shape object — importing a non-rigged
GLB produces none, and our file declares exactly two meshes, `body` and `eyes`.

### Limits, said out loud rather than silently
- **Only `.glb` carries a skin.** `GltfSceneEntry` has the field; the assimp
  and USD scene entries do not. Every other format now prints that it is
  dropping the skeleton, instead of writing a statue in silence.
- **A subdivided mesh is refused.** Weights are per BASE vertex while a
  subdivided `vmap` indexes subdivided vertices, so `buildSkinData` would
  silently weight the wrong points.

### Cost
Loading the rig unconditionally adds **~40 ms** to a headless run — 0.17 s
against 0.13 s, three runs each. Kept: it also removes the stall the first time
the pose chooser is used.

### Ponytail
`PoseRig` gained two bools that duplicated state its own members already
carried; they are `loaded()` and `posed()` now, derived from `skeleton.bones`
and `localPose`. The 28-line skin build moved out of `main()` into
`exportSkin()` beside the other export helpers.

### Verification
ctest **429/429** in debug, release, ASan and TSan. Format clean.
The new `app_rig_glb_skinned` reads the file for `skins`, `JOINTS_0`,
`WEIGHTS_0` and `inverseBindMatrices` — asserting on the app's announcement
alone is what let this survive four milestones.

---

## 2026-09-01 14:16:12 — Session 101 · **a dressed character was a statue in FBX and DAE**

### The chunk
`SceneEntry::skin`: the assimp scene path can carry a rig.

### The defect
The single-mesh overload took a `SkinView` from the start. The scene overload
took none at all — so the moment a character wore anything, its FBX and Collada
exports lost the skeleton. FBX is the format a rigged character is usually
handed over in, so this was the gap that mattered most after last session's.

### No second copy
`attachSkin` and `addJointNodes` are the single-mesh path's own blocks, lifted
out so both callers share them rather than the scene path growing a duplicate.

Verified as a **move** rather than a rewrite, which is the only way to believe
a 300-line diff: the fixture's `rigged.fbx` came out **byte-identical**
afterwards apart from **3 bytes** — the FBX header's Hour/Minute/Second.

### The extraction had a bug, and the test found it in one run
`addJointNodes` still *replaced* `root->mChildren`. Harmless for a single mesh
whose root has no children; a **SIGSEGV** for a scene that already has one
child node per mesh, because `mNumChildren` kept counting entries the new array
did not hold. I had even written a doc comment saying it grows the array — the
comment described intent, the code did not do it. It grows the array now.

### Measured, not assumed: the two formats disagree
FBX writes all **163** bones. Collada writes **139**. Not a bug either side:
`default_weights.mhw` names 139 of the rig's 163 joints, and assimp's Collada
exporter prunes bones with no weights. So the test computes the weighted set
and asserts `bonesOnBody >= weighted`, with the format-specific extra allowed,
instead of a magic number that is right for exactly one format.

The joint NODES survive in both — 163 of them — which is the half that cannot
be assumed: an aiBone with no node of its own name is what makes assimp's FBX
writer fail outright with "Failed to find node for bone: root".

### Third party
Blender 5.2 on a dressed, rigged FBX straight from the app
(`--rig mixamo_superset --eyes high-poly --export dressed.fbx`):

```
ARMATURE bones=179
MESH body verts=21833 groups=141 mods=['ARMATURE']
MESH eyes verts=1076  groups=0   mods=[]
```

The rig on the body, the eyes unskinned, exactly as intended.

### Ponytail
The scale-and-ground-offset of a rest matrix appeared three times — bone offset,
joint node, joint parent. One `placedRest()` now, because a ground offset
applied to two of the three would drift the rig off the body in a way that
still looks like a rig. Re-verified byte-neutral afterwards (2 differing bytes).

### Still open
OBJ has no skeleton concept; `UsdSceneEntry` has no skin field, though
`writeUsda` emits UsdSkel for the single-mesh path (`UsdWriter.cpp:100,135`).
Both now say what they are dropping instead of writing a statue in silence.

### Verification
ctest **430/430** in debug, release, ASan and TSan. Format clean.

---

## 2026-09-01 15:15:00 — Session 102 · **USD had taken a skin all along, and USDZ was unreachable**

### The chunk
The last two formats that dropped the rig: USD, and USDZ that the application
could not produce at all.

### I was wrong about the cause, and checking said so
Last session I recorded that USD needed a `UsdSceneEntry::skin` field. Reading
the header rather than trusting that note: **`writeUsdaScene` has taken a
`SkinView*` all along** — as a parameter, bound to the first entry, the same
one-skin rule glTF follows. The writer was complete. `main.cpp` passed
`nullptr`.

One argument. A dressed rigged stage now emits `def SkelRoot`, `def Skeleton`,
`primvars:skel:jointIndices` and `primvars:skel:jointWeights`, and
**`usdchecker` returns Success!**

### .usdz was implemented and unreachable
`writeUsdzScene` existed, was validated against Apple's own `usdzip` layout and
tested — and no branch in `exportMesh` could reach it. The format an Apple or
AR pipeline actually takes could not be produced by the application. Five lines.
**`usdchecker --arkit` passes** on a dressed, rigged archive.

### Two misreads of my own, both mine to correct
1. `--export x.usdz` looked like it failed silently. It did not: the app prints
   `unknown export extension` and exits 1. My `tail -2` cut the line off. Same
   mistake twice this session — a `tail` is not a reading of the output.
2. A scripted edit failed to match because **clang-format had reflowed the
   `std::fprintf` I committed last session** onto more lines. The assertion
   before the write caught it, which is why that assertion exists.

### One measurement I will not stand behind
The debug suite reported **507 s** in one run and **44.65 s** on a clean re-run,
with no single test above 3.9 s. Almost certainly contention with a background
build. Recorded as noise, not as a regression — but recorded, because a
507-second number left unexplained is how a real regression gets waved through.

### Test plumbing
`glb_has_skin.cmake` became `file_contains.cmake` taking a `KEYS` list, and now
serves GLB, USDA and USDZ. It greps a **USDZ directly**: the stage inside is
STORED uncompressed, which is a format requirement (a consumer memory-maps the
archive), not luck.

A detail worth keeping: CMake stops splitting a `-DKEYS=a;b;c` value on `;`
once it contains a `"`. The GLB keys were `\"skins\":[` and friends and arrived
as one string; dropping the quotes fixed it, and the bare names appear in a GLB
only when it carries a skin anyway.

Mutation-verified per format: passing `nullptr` to `writeUsdaScene` fails
`app_rig_usda_skinned` and leaves the USDZ test green, and vice versa.

### Verification
ctest **434/434** in debug, release, ASan and TSan. Format clean.

---

## 2026-09-01 15:47:00 — Session 103 · **a third of every export was vertices nothing referenced**

### How it was found
Not from todo.md. I went looking for the next instance of the session's
recurring shape — capability built, never connected — by asking what the
writers support that the app never sets, and found that the app never sets ANY
export option. Chasing whether that mattered, I measured the exported height of
the same character in every format through Blender:

```
OBJ   16.5937        GLB   1.6594
FBX    1.6940        USD   1.6940        STL 165.9377
```

Each unit is its format's own convention, so those are fine. But **GLB says
1.6594 and FBX says 1.6940 for the same body**, and that is not a convention.

### The defect
`RenderMesh::setFaceMask` filters the INDEX buffer and leaves the vertex buffer
alone. That is right for the renderer — it uploads the buffer once and a mask
toggle must stay cheap; there is even a test asserting it. Export inherited it.

Counted in the GLB itself: **21,833 body vertices, 14,517 referenced, 7,316
dead — 33.5%** of every position, normal, UV, tangent, joint and weight
written. glTF's importer drops unreferenced vertices, FBX's does not, which is
exactly why the two disagreed about the height: **1.6940 is the helper cages,
1.6594 is the body.** Anything that frames or scales by bounds inherits that 2%.

### The fix, and where it does NOT go
`io::compactUnusedVertices` plus `compactSkinAttributes`, called once at the
app's export site.

**Not inside the writers.** A writer renumbering vertices behind a caller's
back would break anyone round-tripping indices — and the tests asserting
`result.vertices == view.vertexCount()` are that caller. This is a step an
exporter takes, explicitly.

| | before | after | |
|---|---|---|---|
| GLB | 2,255,576 | 1,845,876 | **−18.2%** |
| FBX | 4,529,552 | 4,154,624 | **−8.3%** |
| USD | 3,833,150 | 2,831,381 | **−26.1%** |

Blender now reads the FBX body at **1.6594 m** with 14,517 vertices, and the
rigged GLB still binds exactly: 179 bones, 179 vertex groups, max shift
**1.2e-5**. The FBX's vertex-group count drops 141 → 126 — bones that only
weighted helper vertices now weight nothing, so Blender makes no group for
them. All 179 bones are still there.

### Ponytail cut something I had just written
`compactDeltas` had **no caller**: the application exports no morph targets. I
had written it "so the next person does not forget". That is precisely the
mistake this whole change exists to fix — a capability built and never wired —
so it is gone, with a comment in the header saying why and a todo entry
recording that morph deltas will need the same remap.

### Still open, measured
OBJ carries the same dead weight by a different mechanism: **20,222 `v` lines,
14,444 referenced**, 28.6% dead, plus unreferenced `vt`. The OBJ writer works
in mesh/UV index space rather than render-vertex space, so this function does
not apply; it needs the same remap inside `writeObjScene` against its face mask.

### Verification
ctest **438/438** in debug, release, ASan and TSan. Format clean.
Mutation-verified: reusing the new index on the read side of the skin remap
fails the skin test; leaving indices unremapped fails the compaction test.

---

## 2026-09-01 16:15:40 — Session 104 · **our own reader refused our own OBJ**

### The chunk
The OBJ half of last session's compaction — and it turned out to be a stronger
defect than the size number suggested.

### What I expected, and what I found
I expected file size. Measured on `makehuman --export x.obj`: **20,222 `v`
lines of which 14,444 were referenced** (5,778 dead, 28.6%) and **22,142 `vt`
of which 15,325**. The face mask skips FACES while the vertex and UV lists were
written whole.

Then I checked the round trip, and `loadObj` **rejected the file the
application had just written**:

```
vertex referenced by no face (vertex 13380)
```

`ObjErrorKind::LooseVertex` is a deliberate strictness in our reader — a loose
vertex in an OBJ usually means a shifted index — and our own exporter tripped
it on every masked export. Measured both directions: the pre-fix file fails to
load, the post-fix one loads with 14,444 vertices. That round trip is a test
now, and it is what turns "wasteful" into "wrong".

**Blender never noticed.** Its OBJ importer already discards unreferenced
vertices: 14,444 verts and identical dimensions to four decimals, before and
after. Only our own stricter reader caught it — a case where the third party
being lenient hid the bug rather than revealing it.

### Where the fix lives, and why not with the other one
Not in `io::compactUnusedVertices`. OBJ indexes vertices and UVs **separately**,
in mesh space, against its own face mask — a different index space from the
render-vertex path. So `writeObjScene` does its own two remaps.

`v` 20,222 → 14,444, `vt` 22,142 → 15,325, file **17.9% smaller**.

### Ponytail
The two remap arrays were built by the same three steps twice. One `renumber`
lambda now, so they cannot drift into different numbering rules. Verified
byte-neutral — and my first check said "differ at char 65" because I had
exported to `c2.obj` and the `mtllib` line names the file. Re-run with the same
filename in a different directory: identical.

### Verification
ctest **440/440** in debug, release, ASan and TSan — TSan re-run on the final
tree rather than trusting the pre-ponytail green, since a stale green is not a
green. Format clean.
Mutation-verified: emitting every vertex again fails 2 of the 10 `[objscene]`
cases.

---

## 2026-09-01 16:41:10 — Session 105 · **USD wrote a skeleton ten times the size of the body**

### How it was found
I was chasing something else. Every export puts the character half underground
(feet at −0.82 m, origin at hip height) while the reference defaults "Feet on
ground" to True for every exporter, so I went to add the option to USD — and
found `UsdWriteOptions` has no `feetOnGround` field, then noticed while reading
that `bindTransforms` emitted `globalRest` with no `* s` while `points` had one.

### The defect
`points` are multiplied by the unit scale. `bindTransforms` and
`restTransforms` were not. At the default unit — metre, s = 0.1 — the mesh came
out in **metres** and the rig in **decimetres**.

Blender, on our own export:

```
before   MESH body z −0.8178..0.8416      ARM 179 bones z −8.0385..8.4481
after    MESH body z −0.8178..0.8416      ARM 179 bones z −0.8038..0.8448
```

A skeleton ten times the size of the body. This is our own instance of the
class recorded against the reference's FBX in project_context §8.

### The uncomfortable part
**`usdchecker --arkit` passed the whole time**, before and after. It validates
the stage's structure, not whether the rig fits the mesh. That validator has
been the trusted third-party oracle for USD across three earlier sessions —
worth remembering what it does and does not check.

**And the existing test was asserting the defect.** `test_usd_writer.cpp` pinned
the root bind translation as the literal `(0, 0.5639, -0.7609, 1)` — the
unscaled value. That test was itself written as a fix for a *vacuous* test in
session 093, and it still encoded the bug. It derives the expectation from
`unitScale(Unit::Meter)` now; a literal there is exactly how this hid.

### The new check
Containment, not a scale factor: every joint's bind translation must lie inside
the mesh's own declared `extent`. **155 of 163 joints were outside** before the
fix. A 10x rig fails it by an order of magnitude, and the property survives a
change of unit — which a hard-coded 0.05639 would not.

### Still open, and measured
Feet-on-ground, the thing I set out to do. Every export leaves the origin at
hip height: feet **−0.82 m**, head **+0.84 m**, in OBJ, GLB, FBX and USD alike.
The reference defaults it on for every exporter
(`legacy/python/core/export.py:58`); our app passes `{}` everywhere, and
`UsdWriteOptions` has no such field, so USD could not be grounded even on
request.

### Verification
ctest **441/441** in debug, release, ASan and TSan. Format clean.
`usdchecker --arkit` still Success on a dressed, rigged USDZ.

---

## 2026-09-01 17:27:04 — Session 106 · **feet on the ground, and two correct features that were wrong together**

### The chunk
`feetOnGround` for USD, and on by default for every format in the app.

### The defect
Measured in Blender on the app's own output in four formats: feet at
**−0.82 m**, head at **+0.84 m**. The origin sits at hip height, so a character
imported anywhere arrived buried to the waist. The reference defaults
"Feet on ground" to **True** for every one of its exporters
(`legacy/python/core/export.py:58`); our app passed `{}` so it was false
everywhere, and `UsdWriteOptions` had no such field at all — the "one exporter
that cannot be set like the rest" its own `unit` comment says M7 exists to
remove.

All four now land at **0.0000**, and the USD armature moves with the body:
mesh z 0.0000..1.6594, armature 0.0139..1.6626, `usdchecker --arkit` Success.

### The interesting part: two correct features, wrong together
Compaction (last session) drops vertices no surviving face names. `feetOnGround`
levels the scene by its lowest point. Each is right. Together they were not:
the offset was taken over **all** vertices, so the OBJ levelled by a vertex it
then dropped — and the body's helper cage reaches below the visible feet, so the
character came out **floating 0.27 m above the ground**.

Neither test suite caught it. Both features passed their own tests. It showed up
only in the finished file, in Blender, because I checked the result rather than
the change. `writeObjScene` now does its reachability pre-pass **before** the
ground offset and levels only by what it will write, and there is a test with a
buried hidden quad that fails if the offset ever looks at dropped vertices
again.

### A flake I chased rather than waved away
"all writers agree at the same unit" failed once in a full ASan run and passed
in isolation, then passed 444/444 serially twice. Cause found, and it is mine:
I had an ASan suite and a background TSan suite running at the same time, and
the test binaries share **fixed** temp filenames (`mh_units_agree.obj` under
`temp_directory_path()`). Two concurrent runs of the same tests fight over the
same files. CI is unaffected — one preset per runner — so it is a local-workflow
hazard, recorded in todo.md rather than fixed here.

The lesson I am taking: stop launching a sanitizer suite in the background and
then editing or running another. It has also produced two stale greens this
session.

### Ponytail
The pre-pass's `renumber` computed the kept count and threw it away, so the
emission loop re-derived it with two `count_if` scans over 19k elements per
entry. Kept instead. (And the edit had to be retried: clang-format had
reflowed the `count_if` I was matching against — the third time that has caught
me, and the assert-before-write is what makes it cheap.)

### Verification
ctest **444/444** in debug, release, ASan and TSan — TSan run alone on the
final tree, for the reason above. Format clean.

---

## 2026-09-01 18:30:48 — Session 107 · **Collada was lying about its own unit**

### The chunk
`tests/golden/test_roundtrip.cpp` — every format we can write AND read, exported
and read back in one table: FBX, DAE, STL, 3MF, GLB, OBJ. The property is that
the CHARACTER survives: same triangles, same real-world size.

Vertex counts are deliberately not compared. Importers weld and split
differently, and demanding equality would pin whichever behaviour assimp happens
to have rather than anything true about our files.

### What it found immediately
assimp's Collada exporter writes `<unit name="meter" meter="1"/>`
**unconditionally**, whatever coordinates it is handed. Ours are centimetres by
default, so the file declared a head vertex at `155.593674` to be **155 metres**
up. A spec-conforming Collada consumer reads our character as 155 m tall — the
same 100x class as the reference's FBX defect, in a file we produce, and worse
than declaring no unit at all.

Fixed by rewriting the element to the unit actually used, **not** by forcing the
export to metres. Forcing would have made Collada the one format that ignores
the caller's `unit`, which is the per-format exception this milestone keeps
paying for.

### It also made the import contract truer
assimp's Collada reader **applies** the declaration. Measured: the same
character written at the metre, decimetre and centimetre now all import at
**1.69455** — so `importScene` reports `metersPerUnit = 1.0` for `.dae` instead
of 0 ("you decide"). That only became true once the declaration stopped lying;
before, the reader saw `meter="1"` and handed back raw centimetres with nothing
saying so.

### Two of my own claims, corrected by checking rather than shipped
1. I wrote in a comment that 3MF's importer rejects what assimp writes, and that
   it was "verified rather than assumed". It was neither — I had not tested it.
   It round-trips fine, so it is a row in the table now.
2. I expected the masked OBJ to round-trip to `Mesh::heightCm`. It round-trips
   to the **visible** height: 166.589 cm against 169.455. The helper cage again.
   The test asserts the visible height, computed from the mask.

Both were caught because the test failed, not because I re-read the comment.

### Verification
ctest **448/448** in debug, release, ASan and TSan — TSan run alone. Format clean.

---

## 2026-09-01 19:04:36 — Session 108 · **the shared-options type was the wrong answer; the test was the right one**

### The chunk
`[units][io][options]`: every setting the four writers claim to share, toggled
on all four, checked in the file.

### Why, and why not the refactor
`ObjWriteOptions`, `GltfWriteOptions`, `UsdWriteOptions` and
`SceneExportOptions` carry five identically-named, identically-meaning fields:
`unit`, `scale`, `feetOnGround`, `writeNormals`, `writeUVs`. Nothing made that
true except each writer happening to read the field — and this session found
**six** defects that were exactly one writer diverging from the others: import
units, skins, morph targets, `feetOnGround`, vertex compaction, and Collada's
unit declaration.

The obvious fix is a shared base, and ponytail pointed at it twice. I did not
build it, and the reason is not effort:

* it **expresses** the relationship without **enforcing** it — a writer can
  inherit a field and ignore it, which is precisely what all six defects were;
* the per-format `unit` defaults (dm for OBJ, m for glTF and USD, cm for FBX)
  are format conventions that have to survive, and re-defaulting an inherited
  member needs a user-declared constructor, which costs the aggregate
  initialisation the option structs are used with.

Only a test that reads the FILE catches a writer ignoring a field. So the
mechanism is the test, not the type.

### The result
**All four writers already honoured all five options.** A null result on the
product, reported as one; what it buys is that this stays true. Per format the
marker differs — `vn `/`vt `, `"NORMAL"`/`"TEXCOORD_0"`,
`normal3f[] normals`/`primvars:st`, and an assimp readback for binary FBX — but
the property is one property.

Mutation-verified in two writers: dropping `options.scale` from the USD writer,
and `options.writeUVs` from the OBJ writer, each fails it.

### Where the loop stands
The export path is swept. What remains in M1-M8 is blocked on the owner
(SonarQube credentials, skin textures, bone naming, which blendshape set),
measured-and-deferred with reasons (GPU skinning at 0.11 ms, ID-buffer picking
with no consumer, four asset-less features), or genuinely open but not
defect-backed (PBR with no roughness maps to drive it, Draco/KTX2 needing new
dependencies, FBX-from-spec against a working assimp path, the `mh::io::Scene`
IR whose motivation this session just reduced).

### Verification
ctest **449/449** in debug, release, ASan and TSan — TSan run alone. Format clean.

---

## 2026-09-01 20:18:48 — Session 109 · **SonarQube unblocked, and it cannot see the C++**

### The chunk
The owner pointed at Docker. SonarQube **26.8.0 Community** was already running
as `m2m-sonarqube` for the mesh2motion project — started it, created a
`makehuman` project, minted a **PROJECT_ANALYSIS_TOKEN scoped to this project
only**, and wired up `sonar-project.properties`. Secrets live in `./.sonar-token`
(gitignored, 0600).

### The thing that matters more than the setup
**Community Edition ships no C or C++ analyser.** Verified against the server
rather than assumed: `GET /api/languages/list` returns 26 languages and neither
`c` nor `cpp` is one. Confirmed independently by the measures —
`ncloc_language_distribution = py=1956` for a repo of ~60k lines of C++23.

So **a green Sonar gate says nothing about the C++**. What it does cover is the
Python in `tools/` and `benchmarks/` (which gates three CI jobs), the workflow
YAML, and the `secrets` analyser over every indexed file — which is the reason
`src/` and `include/` are in `sonar.sources` at all. That is written at the top
of `sonar-project.properties` so nobody reads a green badge as C++ assurance.

### 70 findings to 8, and 1 bug to 0
* **36 were one false-positive class.** `capture_fixture.py` stubs the
  reference's own API — `getRestposeCoordinates`, `callAsync`, `addSetting`,
  `zoomFactor` — and Sonar flagged each for not being snake_case. Renaming would
  stop the stubs matching the thing they exist to mirror. Suppressed per rule,
  per file, with the reason in the properties file.
* **Real, and fixed**: a lambda capturing a loop variable (the one *bug*); an
  `assert` inside `except Exception`, which both swallows AssertionError and
  disappears under `python -O`; a `# noqa: BLE001 - prose` whose trailing text
  can make the suppression silently inert; three chained
  `endswith`/`startswith`; 19 duplicated literals named.
* **One rejected with a reason rather than deferred**: `python:S6353` wants
  `\w` for `[A-Za-z0-9_]`. Python's `\w` is Unicode-aware, so it would widen
  the class to accented and non-Latin letters. Mixamo bone names are ASCII and
  spelling it out is the point.

**Behaviour preservation proved, not assumed**: after extracting 19 constants
from the oracle generator I re-captured **every** golden fixture. All `.bin`
blobs byte-identical.

### The gate, and the honest bit about coverage
A `MakeHuman` gate copying the pattern the owner already accepted for
Mesh2Motion: `new_violations = 0`, hotspots reviewed 100%, new duplication
<= 3%. The default gate's `new_coverage >= 80` is **removed** — this server
cannot see the 449 C++ tests, and there are no Python tests, so the condition
would read 0% forever and make the gate uninformative. That is not weakening a
test: Python coverage genuinely is 0, and it says so in `todo.md` and
`test.md` rather than being hidden by the removal.

### Found on the way, not fixed
The `character` fixture is **not reproducible**: re-capturing rewrites
`cases.json`'s stack keys from `../../data/...` to `../../../data/...`.
Verified environmental by re-running the ORIGINAL script — it does the same, so
it predates today. Benign (the parity test reads only `name`, `settings` and
`stack_size`, and every `.bin` matches) but an oracle whose content depends on
where the repo sits is a fragile one.

### Verification
Gate **OK**. ctest **449/449** in debug. ASan and TSan not re-run: **no C++ file
changed** — the diff is Python tooling, `.gitignore` and the Sonar config — and
the only coupling to C++ is the golden fixtures, which were re-captured and are
byte-identical.

---

## 2026-09-01 20:47:45 — Session 110 · **the tangents were computed by nobody**

### The chunk
`TANGENT` in the glTF export — and the reason there was none to export.

### The defect, and it was one level deeper than todo.md said
todo.md recorded "`RenderView::vtang` exists but is never written". The truth is
worse: **`Mesh::calcVertexTangents()` had no caller anywhere in `src/`.** The
tangent array was always empty, so:

* the viewport's normal-map branch had no basis to build a TBN from — session
  082 wired the vertex slot and the upload, and nothing ever filled it;
* no export carried a tangent.

These are the good tangents: Lengyel's method with the reference's three bugs
fixed. That is precisely why a consumer should get ours rather than regenerate
its own basis.

### The w, and a test that could not see it
glTF's TANGENT is VEC4 — xyz plus handedness w of exactly ±1. The w is the half
the reference throws away (`cross(normal, tangent)`, unsigned), and losing it
inverts normal-map lighting on the mirrored half of a symmetric body.

Measured in our own file: **16 of 14,517 body vertices and 256 of 1,076 eye
vertices carry −1.**

My first test asserted `w ∈ {+1, −1}`. That **passes on a writer that emits +1
everywhere** — verified by replacing the sign with a literal `1.0F`, which left
all 21,833 at +1 with every assertion green. Same shape as the one-directional
morph two sessions ago. It asserts the sign VARIES now, and the mutation fails.

### Something I nearly claimed and did not
I imported the GLB into Blender and read `loop.bitangent_sign`: body all +1,
eyes a mix. That is **not** validation of our values — `calc_tangents()`
recomputes Blender's own basis from the UVs and does not read the imported
TANGENT. The real evidence is the file itself, read back byte for byte.

### Code written, measured to do nothing, and removed
I also filled `aiMesh::mTangents`/`mBitangents` for the FBX/Collada path. assimp
does not write them: our exported FBX carries `LayerElementNormal` and
`LayerElementUV` and **no** `LayerElementTangent`, and the Collada carries
`semantic="NORMAL"`/`"TEXCOORD"` and **no** `TEXTANGENT` — grepped from our own
output, both formats, with the elements that ARE written as the control that
the grep works.

Twenty-one lines removed rather than left in. Adding a capability that provably
does nothing is the exact bug class this whole run has been fixing.

### Cost
`calcVertexTangents` is **0.18 ms** on the base mesh, per rebuild. Headless
export: 0.15 s before and after, unchanged within noise.

### Verification
ctest **450/450** in debug, release, ASan and TSan — TSan run alone. Format clean.
SonarQube gate **OK** (8 findings, all pre-existing `S3776` in tools).

---

## 2026-09-01 21:16:03 — Session 111 · **the shader wrote the alpha and the pipeline threw it away**

### The chunk
Alpha blending in the viewport: a second pipeline and a two-pass draw.

### The defect
`litsphere.frag:120` writes `outColor.a = diffuse.a`, and has for several
sessions. `QRhiGraphicsPipeline`'s default target blend is **disabled**, so the
alpha reached the framebuffer and was discarded. Meanwhile the GLB export
already wrote `alphaMode: BLEND` for the same material, so the file and the
screen disagreed about it.

Fixed with a second pipeline — SrcAlpha / OneMinusSrcAlpha, **depth write off,
depth test on** — and a two-pass draw: opaque, then blended. Order is
load-bearing: a transparent surface drawn before the opaque geometry behind it
blends against the clear colour instead, which reads as the transparency simply
not working.

No back-to-front sort inside the transparent set. One shipped material is
transparent, so the question does not arise yet; the sort belongs when a second
one lands, not as machinery nothing exercises.

### The part I will not overstate
**It changes nothing visible today.** The shipped `brown.mhmat` is
`transparent True` over an RGBA `brown_eye.png` — colour type 6, with **13,282
of 1,048,576 texels below alpha 255 and 13,238 of them fully clear** — but the
eye mesh's UV island never reaches them. Enabling blending for the eyes moves
**0 of 1,048,576 pixels**, measured before and after.

So the fixture is synthetic, and the writeup says so. What changed is that a
material with real transparency now renders as one; the shipped asset does not
happen to be that material.

### The test asserts direction, not difference
"Blending changed some pixels" would pass on a pipeline that changed any other
state. It asserts the mean green over the model **drops** when a half-alpha map
is blended over the dark background — the direction blending must move it.
Mutation-verified: `blend.enable = false` gives 0.120844 against 0.120755, and
fails.

### Verification
ctest **450/450** in debug, release, ASan and TSan — TSan run alone. Format
clean. Render timing unchanged: subdivided median **2.62 ms** (381 fps), within
the spread of the 2.67-4.79 ms range measured for this scene.

---

## 2026-09-01 21:49:28 — Session 112 · **the application could not read a mesh**

### How it was found
By scanning instead of guessing. This run has found the same bug shape eight
times — capability built, tested, never connected — so I looked for the rest of
them mechanically: every function name declared in `include/`, counted against
its occurrences in `src/`.

**274 declared names; 57 appear at most once**, i.e. only at their own
definition. Most are accessors used from tests, or Qt event overrides Qt itself
calls. Two were real capabilities with no application path: **`importScene`**
and **`writeBvh`**.

### `--inspect <file>`
`io::importScene` is five sessions of work — multi-mesh, node transforms,
materials, skins, and a unit contract — and **nothing in the application called
it**. Its only consumers were tests. Every "what does our reader actually see in
this file?" across those five sessions was answered with a throwaway probe I
wrote and threw away.

It prints meshes, vertices, triangles, per-mesh UVs / material / skin, and what
the file says about its unit. `metersPerUnit == 0` stays **"the format does not
say"** rather than becoming a guess — the contract from session 094, surfaced.

Side by side it makes that contract visible:

```
t.glb : units: 1 = 1 m,    so 1.6594 m tall   body 179-bone skin
t.fbx : units: 1 = 0.01 m, so 1.6594 m tall   body 179-bone skin
h.obj : units: the format does not say
```

### The test that could not fail
My first `--inspect` test used the GLB. glTF is metres, so
`height * metersPerUnit` is a **no-op** there: deleting the multiplication left
it green. The FBX is written in centimetres, so only it can tell the difference
— and the mutation fails that one. Third time this run that a first test passed
on a mutation; the pattern is always the same, a fixture whose value happens to
be the identity.

### Not done, and why it is not the same job
`writeBvh` also has no application path, but wiring it is not a connection: it
takes a `BvhFile`, so exporting the current pose means BUILDING one from the
skeleton and local pose — hierarchy, offsets, channels, a frame. That is a
feature, recorded rather than half-started.

### Verification
ctest **454/454** in debug, release, ASan and TSan — TSan run alone. Format clean. SonarQube gate OK.

---

## 2026-09-01 22:31:13 — Session 113 · **MOTION and HIERARCHY were in different orders**

### The chunk
`--export x.bvh`: `rig::toBvhPose` builds a single-frame `BvhFile` from a
skeleton and a pose, which is the piece `writeBvh` never had. The app could READ
a pose and never write the one it was showing.

Conventions taken from the reference (`shared/bvh.py:369-428`) rather than
invented: root channels `Xposition Yposition Zposition Zrotation Xrotation
Yrotation`, others `Zrotation Xrotation Yrotation`, an `End Site` of
`tail - head` for every childless bone, offsets relative to the parent's head.
163 bones become 212 joints.

`dummyJoints` is deliberately not done — the reference's `__name` joints exist
because tools disagree about where a bone ends when a parent has several
children, and omitting them is a supported reference mode that keeps one joint
per bone, which is what lets the file round-trip onto the same skeleton.

### The defect it uncovered
`writeBvh` emitted the HIERARCHY depth-first and the MOTION values **in array
order**.

`readBvh` always produces joints depth-first, so for every file the round-trip
tests had ever seen — including the 212-joint, 60-frame face pose units — the
two orders were the same and the bug was invisible. A `BvhFile` built from a
skeleton is in the skeleton's parents-first order, where they are not. The file
parses, every joint gets three plausible angles, and **they belong to other
joints**: 0.96 on a matrix element, on the arms of a T-pose.

Fixed in the writer, not the builder: HIERARCHY and MOTION must agree by
definition, whatever order the caller's array happens to be in.

### Four wrong hypotheses, each killed by measurement
1. **Euler order** — derived `syxz` from the channel list and confirmed the
   reader derives the same.
2. **Angle-to-channel mapping** — the reference maps `euler_from_matrix`'s
   outputs differently from our writer, so I swapped ours to match. It broke 4
   of 5 existing round-trips and did not fix mine. Reverted.
3. **Up axis** — forced `UpAxis::YUp` on the read. No change.
4. **Local vs global pose** — passed the global matrices instead of the
   bone-local ones. No change.

What settled it: the Euler round trip is exact (**1.19e-07**) for the offending
matrix with **no file in the loop**. So the loss was in the file, not the maths,
and the only thing between them is the ordering.

The general lesson: a self-consistent reader/writer pair can hide an ordering
bug indefinitely, because every test feeds the writer what the reader just
produced. It took a `BvhFile` from a third source to expose it.

### Third party
Blender imports the file as a **163-bone armature with 40 bones posed** — the
End Sites become bone tails, as they should.

### Verification
ctest **457/457** in debug, release, ASan and TSan — TSan run alone. Format clean. SonarQube gate OK.
Mutation-verified: reverting MOTION to array order fails the new test and leaves
the other 11 BVH tests green.

---

## 2026-09-02 00:13:17 — Session 114 · **saving a dressed character saved nothing about the clothes**

### The defect
Measured, both directions:

```
--eyes low-poly --save x.mhm  ->  reopen  ->  wearing HighPolyEyes
--eyes none     --save y.mhm  ->  reopen  ->  wearing HighPolyEyes
```

The choice is lost and the default is substituted **silently** — no warning, and
the character looks fine, just not like the one that was saved.

It was masked at first: my initial check saved with the DEFAULT selection and
reloaded it correctly, because the default is what the chooser falls back to.
Only a non-default choice exposes it.

### Two causes, both the shape this run keeps finding
1. **`--save` ran before the choosers.** It could not know what was worn, so it
   wrote no proxy line whatever the selection.
2. **Nothing wrote or read the line.** `AssetIndex::findByUuid` exists for
   exactly this resolution and had **no caller in `src/`** — it was on the
   uncalled-API list from session 112 and I had passed over it as an accessor.

### Taken from the reference, not invented
`apps/gui/proxychooser.py:554-556` writes `<slot> <name> <uuid>` from the save
handler, after the modifiers. `:550-552` resolves the **UUID** and refuses a
filename outright — "Loading proxies from filename is no longer supported" — so
a two-token line is now reported the same way rather than guessed at.

Carried in `MhmFile::unhandled` rather than a new typed field. The writer
already emits those lines exactly where the reference's handlers run, so the
byte-exact `.mhm` round-trip fixture is untouched — a new field would have
risked it for nothing. `recordProxy` REPLACES rather than appends: a document
loaded with one selection and saved with another would otherwise carry both
lines, and the loader takes the first.

### One thing that is NOT a gap
A file with no `eyes` line still reopens with the default, so "wearing none"
cannot be saved. That is reference behaviour: its chooser selects high-poly on
reset and only overrides when a line exists, so the same asymmetry exists
upstream. Recorded rather than "fixed" by inventing a line format.

### Verification
ctest **461/461** in debug, release, ASan and TSan — TSan run alone. Format clean. SonarQube gate OK.
Mutation-verified: ignoring the document's line fails `app_reload_eyes`.
Low-Poly is 96 verts against High-Poly's 1,064, so the fixture cannot pass by
confusing them.

---

## 2026-09-02 00:51:39 — Session 115 · **the rig and the pose were lost too**

### The chunk
Same defect as the clothes, two more fields. Having fixed the proxy line I asked
what else a save drops, and measured it rather than assuming:

```
--rig mixamo_superset --pose tpose --skin african --eyes low-poly --save
  file records:  eyes Low-Poly <uuid>          ... and nothing else
  reload gives:  rig default (163 bones), rest pose
```

The 179-bone rig and the T-pose were both gone.

### Fixed, with the reference's own lines
`skeleton <relative path>` (`skeletonlibrary.py:336-339`) and
`pose <relative path>` (`3_libraries_pose.py:265-268`). `setRigName` necessarily
runs before the document loads — the flag has to be available first — so the
file's choice is applied immediately after the load, and only when the flag was
not given. Same precedence for the pose, and both halves are tested.

Measured after: the file carries `skeleton mixamo_superset.mhskel` and
`pose tpose`, the reload reports **179 bones**, and the arm span goes
**10.516 dm → 16.863 dm**. That last number is the one that matters — it shows
the pose actually applied rather than just the rig name being restored.

### `--skin` deliberately left out
The reference's line is `skinMaterial <path to a .mhmat>`
(`3_libraries_material_chooser.py:304`). Our `--skin` names a **litsphere PNG**,
not a material, so writing it as `skinMaterial` would be a lie about what the
value is. It stays unsaved and recorded, blocked on the `--skin`/`--litsphere`
naming decision already open for the owner.

### Ponytail
`recordProxy` and the new rig/pose writers were the same replace-or-append
logic three times. One `recordLine(doc, key, value)` now, with `recordProxy` a
one-line caller.

### Verification
ctest **465/465** in debug, release, ASan and TSan — TSan run alone. Format clean. SonarQube gate OK.
The byte-exact `.mhm` round-trip fixture is untouched — these lines live in
`unhandled`, which the writer already emitted in the right place.

---

## 2026-09-07 00:45:00 — Session 157 · **the safeguard I described was a no-op, and the tests said so**

### The chunk
Continued the Draco work (the previous fire left it `[~]`): the codec now
carries **every** attribute a rigged primitive has — TANGENT, JOINTS_0 and
WEIGHTS_0 as GENERIC, alongside POSITION/NORMAL/TEXCOORD_0.

That is the extension's rule, not thoroughness: `KHR_draco_mesh_compression`
has no partial mode. A consumer reads the compressed buffer and has nowhere
else to get an attribute from, so a rigged primitive shipping position, normal
and UV only would load as an unrigged statue with a skeleton attached.

Generic rather than draco's own TANGENT/JOINTS/WEIGHTS types: those are behind
`DRACO_TRANSCODER_SUPPORTED` and are a bitstream change. The extension maps
glTF NAMES to draco unique ids, so a generic attribute is exactly as
addressable as a named one.

### The correction that matters
I wrote — in a doc comment, confidently — that **not** quantising the GENERIC
type is what keeps joint indices exact, since one off by a single unit weights
a vertex to the wrong bone.

The mutation that should have proved it **did not fail**. So I pushed harder:
`SetAttributeQuantization(GENERIC, 2)` — two bits — and weights STILL came back
summing to exactly 1. The option is honoured for named types only; on GENERIC
it does nothing at all in draco 1.5.7. My "safeguard" was describing a decision
the library never consulted.

What actually protects joints is the declared data TYPE. So I made that
testable: the fixture now carries joint index **300**, past what a byte holds,
and declaring `DT_UINT8` truncates it to 44 and fails the test. The comment now
says what was measured, and flags that draco's named JOINTS/WEIGHTS types ARE
quantisable if anyone moves to them.

Worth naming as a pattern: a surviving mutation is not always a weak test. Here
it was a wrong belief about the dependency, and the test was right to be
unmoved.

### Mutations
Killed: skin attributes omitted from the map; WEIGHTS_0 mapped to the joint id
(duplicate ids decode without error and put the wrong data in both); skin
stride 3 instead of 4; joints declared UINT8. Deliberately NOT killed and
recorded as such: quantising GENERIC, which is a no-op.

### Verification
ctest green in debug, release, ASan and TSan. Format clean. Sonar OK.

---

## 2026-09-06 23:45:00 — Session 156 · **the Draco codec, and a dependency check that was watching the wrong door**

### The chunk
M7's first open item: glTF Draco compression. This fire delivers the CODEC and
its licensing, not the glTF wiring — the extension has rules about attributes
and accessors that deserve their own pass rather than a rushed one.

`io::dracoEncode` compresses a primitive and returns the bitstream plus the
glTF-name -> draco-unique-id map that `KHR_draco_mesh_compression` carries. The
unique id, not the build-time attribute index: the encoder may reorder
attributes and the unique id is what survives that.

### Optional, like assimp
`find_package(draco QUIET)`. A build without it writes glTF uncompressed rather
than failing to configure, and **CI stays green without installing draco** —
verified by configuring with `-DCMAKE_DISABLE_FIND_PACKAGE_draco=ON`, building
the tests and running them, not by assuming the `#if` was right.
`dracoAvailable()` reports which build this is, so the writer cannot declare an
extension with no encoder behind it.

### Tested by decoding, not by inspection
Every test round-trips through draco's own decoder — a separate code path from
the encoder. Measured: 226,328 -> **7,716 bytes (29.3x)** on a 64x64 displaced
grid; worst position error **4.6e-05** on a 2-unit span at 14 bits.

The position test asserts the error is **greater than zero as well as small**.
Exact positions would mean the quantisation setting never reached the encoder,
and a one-sided bound would have called that a pass.

Mutations killed: TEXCOORD_0 dropped from the attribute map; quantisation
disabled; 4-bit positions; normals never added.

### The dependency check was watching the wrong door
CI verified that every `FetchContent_Declare` name appears in `LICENSING.md`.
It said nothing about `find_package` — so Qt, assimp and now draco could be
linked with nothing recorded about their licences, and that is the half more
likely to be added casually, since it needs no version pin to work.

Extended to cover both. One wrinkle worth keeping: `find_package(Qt6)` against
a `LICENSING.md` row that says "Qt" would report the project's oldest
dependency as unrecorded, so the check also tries the name with trailing digits
stripped. Mutations killed: an unrecorded `find_package` added; the draco row
deleted from `LICENSING.md`.

### Verification
ctest green in debug, release, ASan and TSan, and separately in a
draco-disabled configuration. Format clean. Sonar OK.

---

## 2026-09-06 22:45:00 — Session 155 · **"verifying it needs the window" was wrong; exporting twice was enough**

### The chunk
Closed the last two M6/M8 items I had been walking past, one by measurement and
one by finding that its stated blocker was not real.

### The live-rig export restore is testable from the CLI after all
`exportTo` swaps the mesh to REST positions before writing a rig-carrying
format, then puts the posed ones back. The note said verifying that "needs the
window", because the CLI exits straight after exporting and has nothing left to
observe.

**Export twice and it has something to observe.** `--export` is now repeatable
(useful in itself: one character, several formats). An OBJ carries no rig, so it
always gets the baked POSED mesh — and must be byte-identical to an OBJ written
with no `.glb` before it.

First run FAILED, and the failure was informative: the only differing line was
`mtllib`, which names the file after itself. Every vertex already matched. Same
basename in two directories fixes it — and that near-miss is worth keeping,
because "the files differ" would otherwise have read as a restore bug.

Mutations killed: the restore skipped entirely; normals and tangents not
recomputed after it. **One survived and stays surviving on purpose:** removing
the worn-proxy refit inside the restore changes nothing, because every export
refits the proxies at its top. That line is there for the SCREEN — File > Export
leaves the window showing the character — and only a rendered frame after an
export would catch it. Recorded as the precise remaining limit rather than
deleted or papered over.

### GPU skinning: measured, not built
Timed the whole interactive rebuild in release — the path every slider drag
takes through `buildScene`:

    applyStack 0.07, updateJoints 0.01, buildRestMatrices 0.02,
    computeSkinningMatrices 0.00, skinPositions 0.12, calcNormals 0.09,
    calcVertexTangents 0.22   -> about 0.51 ms

Skinning is **0.12 ms of a 16.7 ms frame — 0.7%**. Moving it to the GPU buys
back under one percent of a frame and costs a matrix-palette upload and a second
vertex path. It would also make normals worse: a vertex shader has no adjacency,
so normals would have to be skinned rather than recomputed, which this port
already rejected as less accurate (M5).

Incidentally the largest item is `calcVertexTangents` at 0.22 ms, nearly twice
skinning. I checked whether it was dead work — it is not: eight shipped skins
carry a normal map.

Reopen if something poses per FRAME. Nothing does.

### Verification
ctest green in debug, release, ASan and TSan. Format clean. Sonar OK. Benchmark
numbers above are from `mh_bench`, release build, run this session.

---

## 2026-09-06 21:45:00 — Session 154 · **picking: the item described the wrong feature and the wrong technique**

### The chunk
"ID-buffer picking with async readback (replaces the full-window sync readback)"
— M6's last real item. Both halves of the parenthetical are wrong, and the
feature underneath is not the one named.

- **There is no full-window sync readback to replace.** The only readback in
  the project is `OffscreenRenderer`'s, and it exists to produce a PNG.
- **`selectedGroup` is dead in the reference**: `mhmain.py:293` and `:433` are
  its only two occurrences, assigned and never read. Face-group selection is
  not a feature to port.
- **The live consumer of picking is click-to-focus** (`camera.py:774
  mousePickHumanCenter`), and even there the ID buffer is only a hit test — the
  real work is a SEPARATE depth readback (`glmodule.py:226`) unprojected to a
  world position.

So: a CPU ray cast. `render::rayThroughPixel` + `render::intersect`
(Moller-Trumbore) + `render::focusOn`, on double-click. No readback, no extra
pass, no GPU round trip, exact rather than quantised to a depth buffer — and it
runs before the RHI exists, which is what makes it testable at all. Linear over
36,972 triangles once per double-click; an acceleration structure belongs to
whatever casts rays per frame, and nothing does.

The eye-space ray is built from the field of view and carried back through the
inverse model-view, deliberately not through the inverse MVP: that would drag in
`clipSpaceCorrMatrix()` and the backend's NDC depth range (Metal [0,1], GL
[-1,1]) for nothing.

### Two mutations survived the first pass, and both were my test's fault
- **"nearest of several hits wins" passed with the nearest-check removed.** I
  had listed the NEAR quad last, so "keep whatever was seen most recently"
  returned the right answer by accident. Reordered.
- **"nearest across MESHES" was untested** — every UI test used one mesh. Added
  a two-plane test with the expected pan derived from the geometry
  (ndcX 0.15 x aspect 4/3 x tan 15 deg = 0.05359 per unit of depth, near plane
  42 away), so it pins the value rather than a direction.

### And one mutation reported a false pass
`M1: aspect factor removed` came back green. The build had FAILED
(`unused variable 'aspect'`) and the stale binary ran. My mutation loop piped
the build to /dev/null. **A mutation harness must check that the mutation
compiled** — a build error is not a passing test, it is no test at all. The
loop now greps for `error:` and says "inconclusive". Re-run compiling, it kills.

### Shared, not duplicated
`modelMatrixOf` now lives once in `SceneResources` and both the renderer and the
picker use it. My first version copied it into `Picking.cpp` with a comment
claiming a test would catch divergence — it would not have: both sides of that
test used the picker's own copy.

### Verified by looking
The render probe takes a pixel, focuses, and renders. Picked the chin at
(320,150): hit (0.0000, 6.2014, 1.4350), pan (-0.0000, -6.2014), and the chin
sits under the centre crosshair in the after image.

### Verification
ctest green in debug, release, ASan and TSan. Format clean. Sonar OK. Ten
mutations killed across the maths, the mesh loop and the viewport wiring.

---

## 2026-09-06 20:45:00 — Session 153 · **the other four shaders: not ported, and now it stays that way**

### The chunk
Closed the M6 shader-port item. It had sat at `[~]` reading "Remaining: phong,
normalmap, skin, toon" as though four ports were queued. Reading the four
settles it, and the answer is that none of them should be written.

**Nothing in `data/` asks for them.** Every `shader` line across the 1,787
shipped files names one of two stems: `litsphere` (`default.mhmat`,
`brown.mhmat`) and `xray` (`materials/xray.mhmat`). That is the whole demand.

- **phong** — Blinn-Phong over diffuse + AO. The PBR path consumes the same
  `MaterialDesc` fields through a better BRDF, and `metallicRoughnessOf` is the
  recorded conversion between the two models.
- **normalmap** — tangent-space normal mapping against ONE light
  (`normalmap_fragment_shader.txt:9-17`); PBR does it against three.
- **skin** — Blinn-Phong + AO + a **1D gradient map** for light colour
  temperature (`skin_fragment_shader.txt:5,17`). **Unportable as shipped:
  `find data -iname '*gradient*'` returns nothing**, so its central input does
  not exist in the tree. Real subsurface is M9, not a port of this.
- **toon** — genuinely distinct, and nothing requests it.
- **xray** — forbidden to translate (session 152: MeshLab, GPL-2.0-or-later).

### The part that is not documentation
A `.mhmat` names a shader and **this port ignores the name** — the viewport
picks its model from settings, and `Material.cpp:59` only strips the stem. So an
asset asking for `toon` today renders as PBR or the matcap and nothing says why.
Harmless while both stems are accounted for; a trap the moment a new asset
arrives, and the seven empty proxy choosers mean new assets are expected.

`tools/audit_shading.py` fails unless every stem in `data/` is either
implemented or excluded with a reason that still appears in the file which
recorded the decision. Same shape as `audit_licences.py` from the last chunk,
and it now sits beside it in CI's stdlib-only `inventories` job. Reopening any
exclusion means deleting its line, which fails until the model exists.

Three mutations, all killed: a material asking for `toon`; the `LICENSING.md`
section renamed so xray's exclusion loses its record; `litsphere` removed from
IMPLEMENTED.

### Verification
ctest green in debug, release, ASan and TSan. Format clean. Sonar OK. Audit
reports 2 stems: litsphere implemented, xray excluded.

---

## 2026-09-06 20:00:00 — Session 152 · **the xray shader is MeshLab's, and LICENSING.md said otherwise**

### The chunk
Next open M6 item: "Shader port to `.qsb` — remaining: phong, normalmap, skin,
toon, **xray**". Before porting anything the question is which of the five we
are allowed to touch, so I read their headers.

Four carry `**Licensing:** AGPL3` and the MakeHuman Team copyright
(`litsphere_fragment_shader.txt:16`). **`xray` does not.** It is MeshLab —
© 2005, 2009 Visual Computing Lab, ISTI - Italian National Research Council —
granting "GNU General Public License ... either version 2 of the License, or
(at your option) any later version" (`xray_fragment_shader.txt:5-19`).

`LICENSING.md` row 42 said, without qualification, "UI images, icons, themes,
**GLSL shaders** | AGPL-3.0". For this pair that was **wrong**.

### What follows, and what does not
- **Shipping it is fine.** GPL-2.0-*or-later* upgrades to GPL-3.0, compatible
  with AGPL-3.0 (§13 of both). That is exactly the analysis §3.1 already
  records for pyFBX, so the conclusion is precedent, not a new argument.
- **Translating it is not.** `resources/shaders/rhi/` is what the Apache-2.0
  `mh_render` reads. `xray` is struck off the port list; if the look is ever
  wanted it gets written from the technique (one minus a power of the
  view/normal dot product), which is the call the project already made for FBX.

### The gate, because a blanket row is the easiest thing here to get wrong
`tools/audit_licences.py` scans every bundled text file — **1,364** of them,
1.5 s — for a GPL or non-commercial GRANT (not a mention: the AGPL notice
contains the words "GNU General Public License" too, so a version number must
follow). Anything not in an ALLOWED table, whose reason string must itself
appear in `LICENSING.md`, is an offence.

It **failed on the tree as it stood**, which is how the finding got confirmed
rather than argued. Now in CI's `inventories` job, which is stdlib-only.

Three mutations, all killed: an unrecorded GPL-2 grant planted in
`default.mhmat` (exit 1); the `LICENSING.md` section renamed so the record
disappears; xray recorded under the wrong family (GPL-3.0 against a GPL-2.0
file).

### Note for whoever ports the remaining four
phong, normalmap, skin and toon are clean — normalmap and skin carry no licence
header of their own and fall under `LICENSE.md` §B with the rest of the
MakeHuman tree. The audit will say so on every run.

### Verification
ctest green in debug, release, ASan and TSan. Format clean. Sonar OK. Licence
audit 1,364 files, 2 recorded grants, 0 unaccounted.

---

## 2026-09-06 19:15:00 — Session 151 · **a shader could be added and silently not built**

### The chunk
The next open M6 item was "wire `qt6_add_shaders()` into CMake — deferred until
Qt is a real build dependency". Both halves were stale, and the interesting part
was neither of them.

- Qt has been a real build dependency for a while, and the shaders have been
  compiled by `add_custom_command` + `Qt6::qsb` since M6 landed. The item was
  already done in substance.
- `qt6_add_shaders()` is the **wrong tool** here, measured rather than assumed:
  it always packages its output into a Qt resource under a `PREFIX`
  (`Qt6ShaderToolsMacros.cmake:5,50`) and has no mode that leaves loose files on
  disk. This port loads `.qsb` from a resolved directory
  (`foundation::resolveShaderDir`), which is what lets a relocated `.app` carry
  its own shaders in `Contents/Resources` — session 147's work. Adopting the
  macro would mean adopting qrc and unpicking that.

### The actual defect, one line away
The stage list was a literal `foreach(stage litsphere.vert litsphere.frag
pbr.vert pbr.frag)`. Add a fifth shader and **nothing builds it and nothing
says so** — while `tools/compile_shaders.sh`, which globbed, did build it. Two
implementations of the same step, disagreeing about what the shader set even is.

Measured, not reasoned: dropping a `_probe.frag` into `resources/shaders/rhi/`
and reconfiguring produced no `.qsb` and no warning. The new test fails on it;
after switching to `file(GLOB ... CONFIGURE_DEPENDS)` it passes with the probe
still there, and passes again once the probe is removed.

`tools/compile_shaders.sh` is deleted rather than left to drift. Its header
comment still claimed "Not wired into CMake yet", which had been false for
months.

### The test rejects more than absence
An empty or truncated `.qsb` exists too, and a pipeline built from one fails at
`create()` saying nothing useful. The test opens each file and requires
`QShader::fromSerialized(...).isValid()`. Truncating `pbr.frag.qsb` to zero
bytes fails it.

Also a `message(FATAL_ERROR)` if the glob comes back empty: that would
configure and build cleanly and fail at the first pipeline, which is the worst
place to find out.

### Verification
ctest green in debug, release, ASan and TSan. Format clean. Sonar OK.
Mutations killed: a shader source with no `.qsb` (proved with a real probe
file), and a zero-byte `.qsb`.

---

## 2026-09-06 18:30:00 — Session 150 · **the exporters get V right, and nothing was checking**

### The chunk
The open M6 item asked whether the EXPORTERS get the UV V origin right, and
said our writers "pass the OBJ's V through unchanged". That premise was stale:
true of OBJ and USD, never of glTF. `GltfWriter.cpp:300-302` has always emitted
`1 - v`, with a comment explaining why.

Nothing held the comment up. Dropping the `1.0F -` passed **114 other `[io]`
test cases and their 401,956 assertions** — the only quad they export has UVs of
0 and 1, which is its own mirror in V, and the vertex order matches too. So the
one convention an exporter actually gets wrong was invisible to the entire
suite.

### Two independent checks now
- A unit test exports a **0.25/0.75** quad and reads TEXCOORD_0 back out of the
  BIN chunk, comparing against the RenderMesh's own UVs (not the Mesh's — the
  writer walks the RenderMesh, whose vertex order is the packed key).
- `tools/blender_validate.py` reports the UV of the topmost vertex, and
  `blender_check.py` requires `base.obj`, `base.glb`, `base.fbx` and
  `base.usda` to agree. Blender is a third implementation with two independent
  importers; agreement across four formats is a much stronger statement than
  any one of them matching a number we wrote down.

All four put the top-of-head vertex at **(0.911166, 0.881046)**. Dropping the
glTF flip moves the GLB to **0.118954** — exactly `1 - v`. Adding a flip to USD
moves the USDA to the same place. Both mutations fail the harness.

### The anchor needed the whole sort key
First version anchored on max world Z alone and immediately reported base.fbx at
u **0.9205** against base.obj's **0.9112**, mirrored about the seam, v identical
to 1e-6. Not a convention bug: the crown of the head carries a left/right pair
at the same height (and both feet sit flat at z=0), so "whichever tied vertex
the importer listed first" is not comparable across formats. Anchoring on
`(-z, x, y)` fixes it. A near-miss worth naming — that finding, believed, would
have been a false FBX bug report.

### Rendering it proved nothing, and that is the lesson
I exported the correct and the V-mirrored body, rendered both in Blender and
looked. **Indistinguishable** — the shipped skin textures are smooth tone maps
with no vertical structure, so a mirror moves nothing the eye can catch. The
eyes gave it away only at a 30 px crop: iris and pupil in one, blank white
ovals in the other, which is the session-145 defect reproduced through the
exporter. The standing rule is "render it and look", and I did; here the numbers
were the decisive evidence and the image was not. Both are worth having.

### Verification
ctest 549/549 in debug, release, ASan and TSan. Blender harness 11/11 with the
new UV rule. Format clean. Sonar OK. Mutations killed: glTF stops flipping
(unit test AND harness), USD starts flipping (harness).

---

## 2026-09-06 17:30:00 — Session 149 · **the fourth one: opacity, and a test that could not see it**

### The chunk
`MaterialDesc::opacity` now reaches the PBR alpha. It is the FOURTH material
property built for export and never connected to the renderer, after
metallic/roughness, `transparent` and the base colour — the pattern named in
Session 148 held on the very next look.

The glTF writer emits it as `baseColorFactor`'s alpha (`GltfWriter.cpp:200,929`).
The renderer used it only in `maps.transparent = ... || opacity < 1.0F` — as a
BOOLEAN to pick the blend pass — and then wrote the texture's own alpha, so a
uniformly translucent material with no alpha channel came out solid.

Not hypothetical: `data/materials/xray.mhmat` ships `opacity 0.1` and rendered
fully opaque. Rendered through the release app it is now a faint ghost — 85,743
non-background pixels solid, **529** at 10% opacity — and I looked at the image.

### No new uniform space
`MeshBuf`'s third vec4 is `base`, whose `w` was already being written as a
constant `1.0F`. The opacity goes there. `pbr.frag` multiplies:
`texel.a * mbuf.base.w` — the texture's alpha for cut-outs AND the material's
opacity for a flat translucent one.

### The litsphere test proved nothing until it was blended
The parity test asserts opacity moves zero pixels under the litsphere (the
reference writes `outColor.a = diffuse.a`, `litsphere_fragment_shader.txt:93`).
Mutating the litsphere to honour opacity did **not** fail it: the instance was
opaque, so the draw went through the pipeline that discards alpha, and the
mutation was invisible. Setting `transparent = true` on BOTH renders kills it.
Same failure class as the earlier clamp-vs-reflect test — a test that exercises
the wrong pipeline cannot see the thing it asserts about.

### One slip
My first edit to the uniform initialiser did not match the formatted source, so
`1.0F` stayed hardcoded and the test failed with **identical** pixel counts on
both renders. Identical, not merely different, is the signature of "the change
never reached the GPU" — worth recognising on sight.

### Verification
ctest 551/551 in debug, release, ASan and TSan. Release `.app` run and its
render inspected. Format clean. Sonar OK. Mutations killed: shader ignores
opacity, opacity not uploaded, litsphere honours opacity.

---

## 2026-09-06 16:20:00 — Session 148 · **the third material property plumbed for export and not for the screen**

### The chunk
The white body: `--shading pbr` with the default skin rendered pure white,
including in the DMG's own render.

### Cause
`default.mhmat` names no diffuse texture — `shaderConfig diffuse false`, because
it is the litsphere-shaded original where the matcap carries the lighting AND
the colour. Under a real BRDF that left albedo at the 1.0 default.

But the deeper finding is the pattern: **`MaterialDesc::diffuse` never reached
the viewport at all.** The glTF writer has emitted it as `baseColorFactor` since
M7 (`GltfWriter.cpp:926-928`), so the file and the screen disagreed about the
same material. That is the THIRD material property built for export and never
connected to the renderer, after metallic/roughness and `transparent`. Worth
naming as a pattern rather than three coincidences: the exporters were written
against `MaterialDesc` and the viewport against `MeshInstance`, and nobody owned
the bridge.

### PBR only, and the test says so
The reference multiplies its matcap by the diffuse TEXTURE alone
(`litsphere_fragment_shader.txt:90-91`; the `gl_Color` on `:85` is VERTEX
colour, behind a define we do not port). Tinting the litsphere would be a
divergence, not a fix, so a test asserts a violently green base colour moves
**zero** pixels under it. Mutating the litsphere to apply the tint fails that
test.

`default.mhmat` now carries `diffuseColor 0.76 0.62 0.53`, read only by PBR and
the exporters.

### Two self-inflicted slips, both caught
- I declared `material[12]` but left the initialiser at 8 entries, so `base`
  was zero-filled. The symptom was two IDENTICAL renders rather than an obvious
  black body, and the test caught it — "differs" would not have been enough on
  its own, which is why the assertions check direction (darker, red-dominant)
  rather than difference.
- `git checkout resources/shaders/rhi/litsphere.frag`, used to undo a mutation,
  also reverted the uncommitted `vec4 base;` declaration that keeps the two
  shaders' uniform blocks the same size. Caught by re-running the tests rather
  than trusting the revert. **Never `git checkout` a file with uncommitted work
  in it to undo a mutation** — copy it aside first, as I did for the others.

### Verification
ctest 549/549 in debug, release, ASan and TSan. Release `.app` run. Format
clean. Sonar OK. Mutations killed: shader ignores the factor, factor not
uploaded, litsphere tinted.

---

## 2026-09-06 15:30:00 — Session 147 · **the DMG shipped an app with no assets, and my last note was wrong about why**

### A correction first
Last session I wrote "there is no DMG or CPack target at all — `packaging/`
holds one empty directory". **There IS a `dmg` target**, at
`src/app/CMakeLists.txt:41-67`. I looked in `packaging/`, found it empty, and
concluded rather than searched. It runs `macdeployqt` and `hdiutil`.

### What was actually broken
`macdeployqt` bundles **Qt and nothing else**. So the DMG contained an
application with no data, no compiled shaders and no icons — it ran only on a
machine that still had the source and build trees at the paths compiled into it.

Two halves to the fix:
1. `foundation::resolveShaderDir` and `resolveResourceDir` join the existing
   `resolveDataDir`, sharing one search so the candidate ORDER is one policy
   rather than three that drift. `MH_SHADER_DIR` was the worse of the two —
   `${CMAKE_BINARY_DIR}/shaders`, inside the BUILD tree, so it does not even
   survive deleting the build directory on the machine that made it.
2. The `dmg` target now copies `data/`, `shaders/` and `resources/` into
   `Contents/Resources`.

### The test that mattered
Build the DMG, copy its `.app` out, **rename the source `data/` away**, render.

First attempt: **exit 1** — "cannot read .../data/litspheres", poses, eyes.
`buildAssetGroups` scanned `MH_DATA_DIR` directly for those three while
everything else went through `dataDir()`. Completely invisible on the build
machine, fatal anywhere else, and precisely the class of bug a packaging step
ships.

Second attempt after routing them through `dataDir()`: renders, green eyes and
all, with the source data tree gone.

I nearly accepted an earlier, weaker version of this test. Copying the release
`.app` to /tmp and running it "worked" — but only because it fell back to
absolute paths that still existed. `Contents/Resources` held one file,
`AppIcon.icns`. Hiding the source tree is what turned a test that could not fail
into one that did.

### The durable guard
`tools/audit_runtime_paths.py`, in the stdlib-only `inventories` CI job: fails
if `MH_DATA_DIR`/`MH_SHADER_DIR`/`MH_RESOURCE_DIR` appear in code outside a
small, budgeted set of resolver fallbacks. It ignores comments and string
literals — the env-var names are themselves strings in `DataDir.cpp` — and
re-introducing the poses regression makes it fail.

### Verification
ctest green in debug, release, ASan and TSan. DMG built (116 MB) and its app run
from outside the repo with `data/` renamed away. Format clean. Sonar OK.

---

## 2026-09-06 14:30:00 — Session 146 · **six eye colours, and the production bundle joins the gate**

### The chunk
Owner request: eye colours. Six — `brown` as shipped plus `amber`, `hazel`,
`green`, `blue`, `grey` — generated by `tools/make_eyes.py`.

### Recolour, not regenerate
`brown_eye.png` is CC0 like the rest of `data/` and carries three things that
are tedious to reproduce: the sclera's blood vessels, the iris's radial fibre
structure, and an **alpha-0 disc** for the high-poly proxy's cornea. Shifting
only the iris hue keeps all three exactly.

**The mask is measured, not eyeballed.** At saturation > 0.35 there are 79,164
pixels and **zero** outside the two iris discs — so the sclera's reddish vessels
are untouched by construction rather than by luck. `--check` re-verifies that on
every run, because a future retouch of the source could break the assumption
silently and the failure would be a recoloured vein nobody notices.

Luminance is preserved per pixel, so the fibre detail survives; setting a flat
colour gives a plastic disc. Verified by building a contact sheet of all six
irises and looking at it, then rendering three of them on the model.

### Independent of the Eyes group, deliberately
Eyes picks the GEOMETRY (high- or low-poly); Eye colour picks the material worn
on it. Two separate choices, two separate `.mhm` lines, both restored on load.
`--eye-colour` refuses an unknown name rather than falling back to brown.

### Three shipped counts moved, deliberately
Adding five materials moved `every shipped material parses` 11 → 16, the uuid
index 15 → 20, and the asset-group count 5 → 6. Each is pinned with the reason
in its comment; that is the third time these counts have moved with an asset
change, and pinning them is what makes the change visible rather than silent.

### Owner request mid-session: test the PRODUCTION build
Now part of the standing gate — after the four presets, the release-preset
`.app` bundle is run end to end as an artefact, not merely via its unit tests.
Worth being precise about what that does and does not cover:
- The `app_*` ctest entries already invoke that bundle's executable, so release
  BEHAVIOUR was covered; the bundle as an artefact was not.
- **There is no DMG or CPack target at all.** `packaging/` holds one empty
  directory. "Production build" today means the `.app`, not an installable.
- The bundle is **not relocatable** — `MH_DATA_DIR` and friends are
  compile-time absolute paths, so copying the `.app` elsewhere breaks it. That
  is the real blocker for a shippable DMG and it is still open, now recorded
  next to this gate rather than buried.

### Verification
ctest 546/546 in debug, release, ASan and TSan. Release `.app` bundle run
directly. Format clean. Sonar OK.

---

## 2026-09-06 11:45:00 — Session 145 · **the eyes are fixed: two bugs, and my retraction was the wrong call**

### Outcome first
The eyes render with irises, pupils and sclera. **Two independent bugs**, both
needed:

1. **The V flip WAS missing.** OBJ's V origin is the image BOTTOM; QRhi samples
   v=0 at the FIRST row. Diffuse, normal and AO are now flipped at upload — the
   same correction the reference makes at `lib/texture.py:167`, for the same
   reason. NOT the litsphere: a matcap is indexed by the view-space normal, not
   a mesh UV, so it is not in this coordinate system.
2. **`ViewportMaps::transparent` was never assigned.** Every worn thing drew in
   the OPAQUE pass with its alpha discarded. `MeshInstance::transparent`, the
   blend pipeline and `MaterialDesc::transparent` all existed; nothing connected
   them. That field's own doc-comment predicted the failure — "a cut-out cornea
   over `opacity 1.0` renders solid".

### I retracted a correct finding, then had to retract the retraction
Session 143 concluded the flip was missing. Session 144 "corrected" that to
"the flip is already handled", on the strength of a quad test whose expectation
I had written backwards — v=1 should sample the image's TOP row under OBJ, and
I asserted the bottom. The test passed because the code had the same error, so
it confirmed the bug rather than catching it.

**What broke the tie was the alpha channel**, which I had never looked at. The
eye's front is a cornea: correct reading gives `alpha 0` (invisible), upside
down gives `alpha 255` over a pale disc. Only one of those can produce a blank
white oval. No amount of reasoning about conventions settled it; one number did.

The lesson is narrower than "measure more". Both sessions measured. The
difference is that a self-authored test only checks that code matches MY
expectation — when the expectation is the thing in doubt, it proves nothing.
The alpha was external evidence: the asset author's intent, not mine.

### A second instance of the same trap, caught by mutation
My first attempt to test the transparency fix printed `worn.material->transparent`
— the material's own flag — while the bug was that the flag never reached
`MeshInstance`. Mutation showed it: deleting the fix left the test green. It now
counts `m.transparent` over the built scene, which is the field the renderer
reads, and the mutation fails.

Twice in one session, an assertion read from a different place than the code
under test.

### Verification
ctest green in debug, release, ASan and TSan. Two mutations killed: the flip
removed (orientation test), and the plumbing removed (`app_eye_is_blended`).
Format clean. Sonar OK. Rendered and looked at the result rather than trusting
the suite.

### Owner asked for eye COLOURS mid-session
Recorded as the next chunk. This fix is its prerequisite — colour choices are
meaningless while every eye renders as an opaque blob.

---

## 2026-09-06 11:00:00 — Session 144 · **I was wrong about the V flip, and the test that proved it is worth keeping**

### The chunk
Last session's next item: apply the OBJ→image V flip, re-cut the golden
baselines, cross-check an export in Blender.

**The flip was not missing.** I had it backwards, and the correction is the
result of this session.

### How I found the reference's answer first, and still got it wrong
`legacy/python/lib/texture.py:167,180` do `pixels = image.flip_vertical().data`
before every GL upload. I read that as "the reference flips, we do not,
therefore we are broken". It is the right observation and the wrong inference:
**GL's texture origin is bottom-left, QRhi's is not.** The reference flips to
get from a top-origin image into a bottom-origin API. We are already in a
top-origin API, so the same correction is already made.

### The test that settled it
A quad with `v=0` at its bottom vertices and `v=1` at the top, drawn with a
texture that is red on its top half and blue on its bottom. Under the OBJ
convention `v=1` must sample the image's BOTTOM row, so the quad's top must come
out blue. It does — with the code exactly as shipped. Adding
`.flipped(Qt::Vertical)` at diffuse upload makes it fail.

Before trusting it I checked the frame the test itself depends on: a quad
occupying only world `+Y` covers 3,698 pixels in the output's upper half and
**zero** in the lower, so "top of quad" really is "small y in the image". Both
directions of the argument now have a measurement.

### What this cost, and the lesson
I recorded "the cause: we never apply the OBJ→image V flip" in `todo.md` and in
a commit message as a **finding**, on the strength of a PIL sample and one
render. That is exactly the failure I had criticised in the note I was
replacing, one session earlier. Reading the reference's flip felt like
confirmation and stopped me looking further.

What actually distinguishes the two cases: the PIL check sampled a texture in
isolation, while the quad test drives the real renderer end to end with a
controlled input. Only the second can answer "what does OUR sampler do".

### Where the eye stands
Still unexplained, and `todo.md` now says so. The V flip joins the rejected
list. Also flagged there: my analysis leaned on "the eyeball's frontmost
vertices in the OBJ", but `fitProxy` MOVES those vertices before they are drawn,
so frontmost-in-the-asset is not visible-on-screen. That assumption needs
instrumenting on the fitted proxy, not the raw file.

### What landed
The orientation test — which pins a convention nothing checked, in either
direction — and the corrected record. ctest 539/539 in debug, release, ASan and
TSan — the count is unchanged because the new case runs inside the single
`render` ctest entry rather than as its own row. Format clean. Sonar OK.

---

## 2026-09-06 10:15:00 — Session 143 · **why the eyes are blank, measured rather than guessed — and why I did not fix it yet**

### The chunk
The PBR follow-up: the eyes render as flat pale ovals. My own earlier note said
"the eye geometry in the base mesh takes the BODY albedo, and its UV island
lands on a bright region of the generated skin texture", and filed it under PBR.
**Every part of that was wrong**, and it was written as a guess.

### What it actually is
OBJ's V origin is bottom-left; QImage's row 0 is at the top. **We never apply
the flip.** Measured on the asset: the eyeball's frontmost 130 vertices carry
UVs `u 0.921–0.953, v 0.052–0.084`, which in image space is the texture's
BOTTOM-right — a small blue disc. Unflipped we sample the TOP-right instead,
which is the pale grey background, RGB (171,171,167). That is the colour on
screen.

Confirmed by rendering the raw sampled albedo with the lighting removed: flat
(171,171,167) unflipped, and the blue disc once `.flipped(Qt::Vertical)` is
added at diffuse upload.

### Five hypotheses killed by experiment, so nobody repeats them
- **Shininess/roughness.** `shininess 1.0` → roughness 0.045, a near-mirror,
  looked like an obvious specular blowout. Tested 0.55 and 0.30: unchanged.
  I first dismissed this on a LOW-RESOLUTION look, which was sloppy — I redid
  it at 10x zoom before ruling it out.
- **Render resolution.** At 1024 full-body each eye is ~10 px, so "no iris"
  could have been sampling. Rendered and zoomed 10x: the white region is ~30 px
  and genuinely featureless.
- **Eyeball orientation.** The iris is at the FRONT — Z 1.417 in a 1.221–1.463
  mesh, model faces +Z. Not backwards.
- **Missing material or texture.** The whole chain resolves; now asserted by a
  test that walks proxy → material → texture and requires the file to exist.
- **Missing UVs.** Mesh 808 UVs / 1064 verts (per-corner), RenderView 1076
  texco spanning 0.011–0.991, `texco.size() == vertexCount()` so the interleave
  writes them. The eye's raw albedo varies (std ≈ 35, range 25–171).

### Why I did not land the fix
`.flipped(Qt::Vertical)` at diffuse upload is one line, and it changes EVERY
sampled texture. That moves the M6 litsphere parity images and the golden render
baselines, and it raises a question I have not answered: glTF specifies a
top-left UV origin, so if we pass OBJ's bottom-left V straight through, the
**exporters may be wrong too**. Flipping only the viewport would then hide that
rather than fix it.

That deserves its own chunk with the baselines re-cut deliberately and an export
cross-checked in Blender — not a one-liner bolted onto the end of an
investigation. The full measurement set is in todo.md so the next iteration
starts from evidence instead of from my earlier wrong guess.

### What did land
The proxy → material → texture assertion, and the corrected note. ctest 539/539
in debug, release, ASan and TSan. Format clean. Sonar OK.

---

## 2026-09-06 09:05:00 — Session 142 · **a fixture that moved when the repo did, and the parity test that never ran production's code path**

### The chunk
M4's oldest open item: `capture_fixture.py character` was not reproducible.

### The cause, in one line
`capture_fixture.py:450` anchored the stack keys at `os.path.abspath("data")` —
the CURRENT WORKING DIRECTORY, which line 40 sets to `legacy/python`, where
`data` is a symlink to `../../data`. So the relative depth depended on where the
repo sat and whether the symlink got resolved. Re-running rewrote all 234 key
lines with not one byte of geometry changing.

Now anchored at the repo's `data/targets`, resolved through the symlink.
Verified by re-running the capture from `/tmp` and diffing: identical.

### The anchor was a choice, and it paid twice
`data/targets` rather than `data/` because that makes the fixture keys EXACTLY
what `Human::stack()` produces. So the test can compare the reference's stack
instead of only counting it — which it had been doing, reading `stack_size` and
ignoring 23 keys and weights per case.

Mutating a weight or deleting a target in the fixture now fails the test. Before
this, either would have been invisible until the geometry check, and then only
as a wall of bad vertices rather than "this target should not be here".

### What that exposed
With the stack actually compared, every case disagreed on every key. The keys
were byte-identical when I dumped them by hand, which is what made it worth
chasing rather than assuming I had the anchor wrong.

`test_character_parity.cpp` built `TargetIndex::build(MH_DATA_DIR)` and
`TargetLibrary(MH_DATA_DIR)`. `main.cpp:1441,1445` build both at
`data/targets`. The test's pairing was internally consistent — which is why it
passed — but it produced `targets/…`-prefixed keys that the application never
produces. **The parity oracle had never exercised production's code path.**

I changed the index root first and broke the geometry, because the library root
has to move with it: the index decides the key form, the library resolves it.
Both now match `main.cpp`.

### Also
The hand-rolled `cases.json` scanner is gone. Its comment said "a targeted scan
beats adding a JSON dependency"; the dependency was already there — `mh_json`
is linked into that target — and the shape stopped being shallow the moment the
stack became worth reading.

### Verification
ctest 538/538 in debug, release, ASan and TSan. Format clean. Sonar OK.
`audit_taskviews.py` unchanged. Fixture mutations killed: a corrupted weight and
a dropped target.

---

## 2026-09-06 08:20:00 — Session 141 · **the loop came home, and a directory stopped being a valid asset**

### The loop moved local
The owner disabled the cloud routine. It had fired **ten times** — 16:27
through 07:27 — done real work every time, and pushed none of it: the Claude
GitHub App is read-only on `SeedeXR/makehuman`, so every run ended in a 403 and
died with its container. `trig_01Q73g2iWMsqUe9T6kzoKo4a` is now `enabled:false`.

Replaced with a local hourly cron (`853ce738`, :23). **Session-only** — it dies
when this session exits and auto-expires after 7 days. The cloud was the
durable option; the 403 made it worthless, so this is the better trade, but it
is not durable and the next session must re-create it.

### The chunk, which the last cloud run found and could not land
`todo.md` said "four JSON readers crash on a directory". I measured rather than
trusting it, and it was wrong in both directions. On libc++, given
`data/targets`:

| Reader | Before |
|---|---|
| `loadMaterial`, `loadMhm`, `loadProxy` | **returned SUCCESS** — a valid, empty asset |
| `loadModifiers`, `loadSliderLayout`, `loadSkeleton`, `loadPoseUnitNames` | `Malformed` |
| `loadObj` | `EmptyMesh` |

Eight readers, one guard, textually identical in every file:

    if (!exists(path)) return NotFound;
    std::ifstream in(path);
    if (!in) return Unreadable;

`exists()` is true for a directory and `ifstream` **opens** one, so `!in` never
fires. A parser calling `sbumpc()` throws; a parser calling `get()`/`getline()`
sees end-of-input and succeeds. The three that "succeeded" are the worse half —
an exception is at least loud — and `data/` is full of directories sitting
beside the files these readers want.

Fixed with `foundation::openForRead`: one guard, refusing any non-**regular**
file rather than just directories. A FIFO also exists, also opens, and then
blocks forever — and a hang is the one failure a test cannot assert on.

### My own test hid three of them
The first version used `SECTION`s with `REQUIRE`. Catch2 abandons a section at
its first failed `REQUIRE`, so `loadMaterial` failing hid `loadMhm` and
`loadProxy` — which were doing exactly the same thing. I only saw all eight by
writing a throwaway probe that reported each reader's answer independently.

Rewritten as one flat case with `CHECK`, so every reader is asserted on its own.
The pre-existing `test_malformed_input.cpp` had handed readers a directory since
it was written and saw none of this, because it discarded the result.

### Verification
ctest 538/538 in debug, release, ASan and TSan. Format clean. Sonar OK.
Mutations killed: the regular-file check removed, and `NotAFile` collapsed into
`NotFound` — the second matters because "not found" about a path that exists
sends whoever is debugging it looking in the wrong place.

---

## 2026-09-06 02:35:00 — Session 140 · **units, and my own audit catching me**

### The chunk
`SettingsTaskView`, first slice. The status line hardcoded centimetres; the
reference has a units setting and converts at DISPLAY time
(`guimodifier.py:174-181`), keeping the stored value in centimetres. A
preference that changed the model's units rather than their presentation would
be a very different and much worse idea, so the conversion lives in one place.

Settings > Units, exclusive, persisted to QSettings **immediately** rather than
at shutdown — a crash should not lose a preference the user has already watched
take effect.

The menu holds exactly one setting. That is deliberate: it is the only one that
currently changes anything on screen, and a Settings menu full of controls that
do nothing would be worse than a short one.

### The icon audit caught me
`[ui][icons]` failed the moment I added the two unit radio items:

    actions with no icon: settings.units.imperial, settings.units.metric

Which is the audit working. The question was whether they SHOULD have icons,
and the answer is no — an exclusive checkable action shows its state with a
check mark, and macOS menus do not pair a check with an icon.

**The exclusion is deliberately narrow**: `isCheckable() && actionGroup() &&
actionGroup()->isExclusive()`. `isCheckable()` alone would also excuse a
checkable TOOLBAR toggle, which is icon-only and absolutely needs one. Verified
by mutation that a genuinely blank action still fails.

Worth noting the sequence: I added a feature, my own test refused it, and the
right response was to narrow the rule rather than delete it. That is the audit
earning its place a second time.

### What I did NOT add
`real_weight` (kg/lb), which the reference also offers. It needs
`getWeightKg()`, derived from body surface area (`human.py:638`,
`bsa*bsa*3600/heightCm`) — a computation we do not have, not a formatting
choice. Weight stays a percentage. Recorded rather than half-built.

### Verification
ctest 536/536 in debug, release, ASan and TSan. Format clean. Sonar OK.
Mutations killed: conversion ignored, units not persisted, same-unit retrigger
still emitting (which would rebuild the scene on every menu open).

---

## 2026-09-06 01:50:00 — Session 139 · **245 changes as one undo step, and counting the calls**

### The chunk
The half of Randomise I deliberately deferred last session: the button.

### Why not a QUndoStack macro
The obvious wiring is `beginMacro` over 245 `ValueChangeCommand`s. It would be
correct and unusable — each of those triggers a full mesh rebuild, so one undo
would rebuild the mesh 245 times.

`MultiValueChangeCommand` instead: it holds the whole change list and hands it
to ONE callback, which moves every slider, sets every modifier, and then
rebuilds once.

### The property worth testing is the call COUNT
"It worked" and "it worked 245 times too slowly" look identical from outside,
so the test counts invocations rather than only checking values: one call per
undo and one per redo, never 245. Mutation-verified — changing the
implementation to `for (v : values) apply_({v})` keeps every value correct and
fails the test.

That is the third assertion this session where the interesting property was
*how* something happened rather than *what* it produced. The first two I got
wrong and mutation caught; this one I wrote that way from the start.

### One ordering trap
`randomize()` mutates `human` in place, so the BEFORE values have to be read
from the panel before the call — asking `human` afterwards would record the new
value as the old one and undo would silently do nothing.

The button seeds from the clock rather than a fixed value: `--random <seed>` is
where reproducibility lives, and a button that always produced the same person
would be pointless.

### Verification
ctest 536/536 in debug, release, ASan and TSan — unchanged from the last
session, because the three new cases run inside the single `ui` ctest entry
rather than as their own rows. Format clean. Sonar OK.
Mutation: apply one-at-a-time fails; the action test pins Ctrl+R, the icon, and
that it sits in a real menu.

---

## 2026-09-06 01:05:00 — Session 138 · **a randomiser, a reference bug not ported, and a test that could not tell reflect from clamp**

### What I did not build, and why
I went for `ExpressionTaskView` first and stopped after measuring. **Zero
`.mhpose` files ship**, so the chooser has no content — the same wall as the
seven proxy choosers. Worse, the two expression views are different systems:
the mixer drives a FACE RIG (`data/poseunits/face-poseunits.bvh` + `.json`, 60
framemappings) while `buildExpressionBlendshapes` uses MORPH TARGETS
(`data/targets/expression/units/`, 34 units × 3 ethnicities). Choosing which
the port should offer is design work, not a chunk. Recorded.

`RandomTaskView` instead: pure logic over the modifiers that already exist, no
assets, no chrome decision.

### A reference bug, deliberately not ported
`0_modeling_8_random.py:172-174`:

    if Gender > 0.5 or Age < 0.2 or Age < 0.75:
        # No pregnancy for male, too young or TOO OLD subjects

The third clause is `<` where the comment says "too old". It is therefore true
for every age below 0.75 — the guard fires on nearly every character, and the
second clause (`Age < 0.2`) is entirely dead because it implies the third.
Pregnancy is effectively always zeroed.

CLAUDE.md hard rule 3 says never port a known-broken behaviour and exclude it
explicitly with a comment. Implemented the stated intent (`Age > 0.75`), with
the divergence written up in the header, the `.cpp` and a `[divergence]`-tagged
test that pins it.

### The test that could not tell reflection from clamping
The reference draws Gaussian and **reflects** at each bound rather than
clamping. My first test only asserted the result was in range — which the
clamp alone guarantees. Mutation confirmed it: deleting the reflection left
every assertion green, while my comment claimed reflection mattered.

The fix measures WHERE the probability goes, and getting the threshold right
took a measurement rather than a guess. At sigmaFactor 2.0 the two converge
(45% vs 80% on a bound) because a draw more than a full range out reflects past
the *other* bound and gets clamped anyway. At 0.3 — the real macro spread —
they separate cleanly: **0.00% reflecting, 9.50% clamping**. The test uses 0.3,
and the comment records why the wide-sigma loop cannot do this job.

Second time this session an assertion of mine restated the implementation
instead of checking it. Both were caught by mutation, neither by review.

### Shipped as core + CLI, not the button
Wiring it to the UI is a separate design problem I did not want to rush: one
randomisation changes **245 modifiers**, so the undo entry has to be a
`beginMacro` group and 245 `ValueChangeCommand`s must not each trigger a mesh
rebuild. `--random <seed>` is useful on its own (crowds, test fixtures) and
fully testable; the button is next, in todo.md with the constraint written down.

### Verification
- ctest 536/536 in debug, release, ASan and TSan.
- Same seed → byte-identical export; different seed → different export. Both
  ctest entries. The first version of the reproducibility test compared
  `app_random_a.obj` against `app_random_b.obj` and failed on the `mtllib`
  line, which names the output file — the geometry was identical all along.
  Now both write `person.obj` into different directories.
- Mutations killed: clamp instead of reflect (after the fix), seed ignored
  (`-Werror` on the unused parameter), symmetry never mirrors, pregnancy guard
  restored to the reference bug, side test by prefix instead of component.
- Rendered two random characters and looked at them: coherent, distinct people
  rather than caricatures, which is what the per-group sigmas are for.

---

## 2026-09-06 00:10:00 — Session 137 · **the rig had no picker, and two assumptions I checked before acting on**

### The chunk
`SkeletonLibrary`, from taskviews' `todo` list. Two rigs ship — the reference's
163-bone `default` and our 179-bone `mixamo_superset` — `--rig` has always
chosen between them, and the window could not.

### Two things I assumed were broken and were not
Worth recording because in both cases acting on the assumption would have
produced pointless work:

1. **"The `.mhm` doesn't record the skeleton."** It does — written at
   `main.cpp:1619` and read back at `:1445-1449`, resolved by stem, with
   `--rig` taking precedence over the file. The round trip was already
   complete; the picker was the only missing piece.
2. **"`availableRigs()` gives me the list."** It returns a comma-joined STRING
   for the `--rig` error message. Splitting that would have been silly, so it
   is now `rigStems()` returning the vector, with `availableRigs()` joining it
   — one source of truth, where otherwise the error message and the picker
   would have scanned the directory separately.

### Switching rig keeps the pose
`loadPoseRig` reads the rig from `rigNameRef()`, so the branch sets the name,
reloads with the CURRENT pose, and puts both the name and the picker back if
the load fails — the same "try it first, do not create an undo entry that does
nothing" shape the Pose branch already had. A user changing rig mid-pose
expects to keep the pose, not be reset to rest.

### Something I made worse, and left
The dock is titled **Materials** and now holds Skin, Pose, Eyes, Skin material
and Skeleton — three of which are not materials. I did not rename it:
`dockObjectName()` lower-cases the category and `saveState` keys on it, so a
rename silently invalidates every saved workspace. Recorded in todo.md to be
done alongside the docks-vs-tabs decision, since that may move these controls
anyway.

### Verification
ctest 522/522 in debug, release, ASan and TSan. Format clean. Sonar OK.
Screenshotted the window: five pickers, "Mixamo superset" selected, PBR body in
`african_warm`.

---

## 2026-09-05 21:00:00 — Session 136 · **the renderer had no user interface, and a test that proved nothing**

### The chunk
`OpenGLTaskView` — the reference's Render tab, whose label is literally
"Render" (`plugins/4_rendering_opengl/__init__.py:53`). Same shape as the
Export gap closed two commits back: `OffscreenRenderer` has been in the tree
since M6, `--render` has worked for as long, and **nothing in the window
reached either**.

`RenderDialog` (width, height, transparent background, shading model) plus one
`renderTo` lambda that `--render` and File > Render both call. The dialog's
defaults ARE the CLI's defaults — 1024 square, opaque — so pressing Enter twice
produces exactly what the command line would. Asserted.

**No anti-aliasing toggle**, which the reference has. MSAA is not optional here:
`render::kSampleCount` is requested for every target in the application and the
backend may clamp it, so a checkbox would be a control that sometimes silently
does nothing — the same reason the toolbar has no wireframe button. The shading
model takes its place, which the reference could not offer because it has no
PBR path.

### A test of mine that proved nothing, caught by mutation
I wrote that driving the shading combo "proves the mapping" between its index
and `ShadingModel`. It does not. `request()` casts the *index*, and the
constructor sets the index from the enum — so the two are consistent no matter
what the labels say. Reversing the two `addItem` calls left the test **green**
while the dialog would show "PBR" and render a litsphere.

Mutation testing found it. The fix pins the LABELS to their positions:
`itemText(static_cast<int>(ShadingModel::Litsphere))` must contain "Litsphere".
Re-ran the same mutation afterwards and it fails now.

This is the same family as the earlier "test asserted the printed message, not
the effect" — an assertion that restates the implementation rather than
checking it. The comment claiming the coverage was as wrong as the test.

### Also caught: two icons for two different things
Render and Grab Screen are different — one draws the character offscreen at a
chosen resolution, the other captures the window — and the obvious glyph for
both is a camera. Render uses `image`; a test asserts the two icons differ. It
caught nothing when written, which is why it says so.

### Verification
- ctest 521/521 in debug, release, ASan and TSan.
- `--render` output is byte-identical in wording to before the extraction:
  "rendered ... (1024x1024, litsphere)" and "(1024x1024, transparent, pbr)".
- Mutations killed: reversed combo (after the fix), removed spin-box floor,
  render action not added to a menu.

---

## 2026-09-05 20:15:00 — Session 135 · **eight skin materials nobody could pick, and seven choosers with nothing to choose**

### Where I started, and why I changed direction
`taskviews.md` files eight proxy choosers as "blocked on the viewport drawing
exactly one mesh". Multi-mesh rendering IS done, so I set out to build them —
then counted the assets before writing anything:

| Directory | `.mhclo` files |
|---|---|
| `data/eyes/` | 2 |
| `clothes`, `hair`, `teeth`, `tongue`, `eyebrows`, `eyelashes`, `proxymeshes` | **0** |

Three of those hold exactly one file: `clear.thumb`, a placeholder. Upstream
ships them as separate downloadable asset packs. So seven of the eight are
blocked on **content, not code** — building them would ship seven empty
dropdowns, the same painted no-op I refused for the toolbar's mesh-display
group. `taskviews.md` is corrected in place; the audit still reports 51 and the
same buckets, because I added the measurement rather than reclassifying.

That is an owner decision now in todo.md: asset packs, or generate proxies
procedurally as we did the skin textures.

### What was actually reachable and broken
The eight skin materials I generated two sessions ago had **no picker**.
`--skin-material` set them, the `.mhm` saved them, the exporters wrote them, and
the window could not choose one — so four shipped African tones were
unreachable to anyone not using the command line.

Added as a "Skin material" group. Deliberately NOT "Skin": that group lists
LITSPHERES, viewport matcaps with no PBR data, and having two groups called
Skin is precisely the collision the pending `--skin` → `--litsphere` rename is
about. Spelled out in full rather than competing for the short name.

It is built from `skinMaterialRef()`, not the raw option, so the picker starts
on the material actually in use — including one restored from a loaded `.mhm`.
Both are asserted.

### Then I looked at it, and the label was wrong
The picker read **"African_rich"**. `prettyName` capitalised the first letter
and never touched underscores, because no shipped stem had ever contained one —
the litspheres are `skinmat_caucasian`, and stripping the prefix leaves nothing
internal to convert. Nothing asserted a label anywhere in the codebase.

Fixed by moving it to `ui::prettyAssetName`, which is tested: `main.cpp` has no
test seam for a display string, and this is presentation logic that belongs
beside the widget that shows it. Underscores become spaces; **hyphens do not**,
and that is asserted — "High-poly" and "A-pose" are how those assets are
written, and "High poly" would be a regression.

This is the fifth visual defect this session that a green suite did not see. I
have written it up as a memory rather than rediscovering it a sixth time.

### Verification
ctest 521/521 in debug, release, ASan and TSan. Format clean. Sonar OK, 0 open
issues. `tools/audit_taskviews.py` still reports 51 with unchanged buckets, so
the inventory CI job stays green.

---

## 2026-09-05 19:35:00 — Session 134 · **the roadmap said Export was covered; the menu had no Export**

### The chunk
Owner directive 8, continued. `memory/taskviews.md` files `ExportTaskView` in
its "covered (2)" bucket — "the reference makes these tabs; we make them File
menu actions". For Load and Save that is true. For Export it was not: the File
menu had Open, Save and Save As and **no Export at all**, so every writer we
have shipped since M7 was reachable only from the command line.

The file itself already says this is "the worst kind of error in a roadmap: it
labelled missing work as done" — about Export specifically — and had moved it
to `todo`. It had stayed there. The new test asserts the action exists rather
than trusting any of that.

### One export path, two triggers
The CLI block was ~90 lines: live-rig rest restore, proxy refit, vertex
compaction, skin remapping, blendshape building, then `exportMesh`. It is now a
single `exportTo(path, wantBlendshapes)` lambda that `--export` and
`MainWindow::exportRequested` both call. Duplicating it would guarantee the
menu and the command line drift into producing different files.

The extraction is verified by what did NOT change: every existing `app_*` export
test passes untouched, and those cover OBJ face counts, GLB rig contents, USD
units and the `.mhm` round trip.

### The one thing the interactive path needed that the CLI never did
`exportTo` swaps the mesh to its REST positions before writing a format that
carries a rig. The CLI exits immediately afterwards, so nothing noticed. With
the window open, leaving the body un-posed after an export would look like the
export had broken the model. So the posed vertices are backed up and restored,
and the viewport is rebuilt because it holds spans over them.

**Stated rather than glossed**: `app_live_rig_export` proves that branch RUNS —
and under ASan and TSan, so a bad size or a use-after-move in the restore is
caught — but nothing asserts the right vertices came back. The CLI has nothing
left to observe after it exports. That needs the window, and it is in todo.md.
`.obj` cannot carry a rig, so the pre-existing posed tests never reached this
branch at all; the new test uses `.glb` deliberately.

### Two roadmap corrections, both recorded not fixed
- **`taskviews.md` is stale on its biggest claim.** Eight proxy choosers are
  filed as "blocked on the viewport drawing exactly one mesh". Multi-mesh
  rendering is done — `ViewportWidget::setMeshes`, `render::MeshInstance`, and
  `test_proxy_render.cpp` draws a body and a worn proxy together. Those eight
  are `todo`.
- **An owner question, not a blocker.** The reference screenshot is a two-level
  TAB bar; this port deliberately uses a dockable layout with workspace presets
  (`design.md` §6.4). Matching the screenshot literally means discarding
  docking. The task-view CONTENT is needed under either chrome, so that is what
  I am building; the chrome is worth an explicit decision first. I did not stop
  for it, because there is plenty of work that is right either way.

### Verification
ctest 519/519 in debug, release, ASan and TSan. Format clean. Sonar OK.

---

## 2026-09-05 18:50:00 — Session 133 · **the status line, and a height that measured the helper cages**

### The chunk
Owner directive 8: the reference's persistent bottom stats line
`Gender: … Age: … Muscle: … % Weight: … % Height: … cm`.

### Two format rules a guess would have got wrong
Read out of `legacy/python/apps/gui/guimodifier.py:152-185` rather than
invented, and both are pinned by test:

- **Weight displays `50 + 100 * w`, not `100 * w`.** The 0..1 slider maps onto a
  50 %..150 % display range, so a default character reads "100.00 %". I would
  have written 50.00 %.
- **The gender endpoints are compared EXACTLY** — 0.0 is "female", 1.0 is
  "male", within 0.01 of 0.5 is "neutral", everything else is the split
  percentage. So 0.999 reads "0.10 % female, 99.90 % male" and not "male". A
  tolerance on the endpoints would swallow that.

### Licence boundary held
`src/ui` links only `mh::render` and Qt — it does NOT depend on `core`, and
must not (CLAUDE.md hard rule 4: Apache may never depend on AGPL). So
`macroStatusLine` takes a `MacroStats` of plain floats and the app, which owns
the `Human`, fills it in. Same shape as the File-menu signals.

It is a PERMANENT status-bar widget, not `showMessage`: the latter is transient
and the next "Saved workspace" or viewport error would wipe the stats forever.
The reference calls its own equivalent `statusPersist` for the same reason.

### The bug the stats line exposed: heightCm measured the helper cages
I first wrote `10 * (bbox.max.y - bbox.min.y)` in `main.cpp`, then checked what
the reference actually does before trusting it. `getHeightCm` calls
`calcBBox(fixedFaceMask = self.staticFaceMask)` and the docstring says "the
bounding box of the basemesh **without the helpers**"
(`legacy/python/apps/human.py:701-706`).

Ours iterated every vertex. **Measured, not reasoned about**: 169.455 cm with
helpers, 166.589 cm without — the skirt and tights cages stick out past the
body by 2.87 cm. I ran a throwaway probe to get both numbers before changing
anything, and the parity test now asserts both so a silent revert to the full
bounding box reproduces the larger figure and fails.

`boundingBox()` stays deliberately unmasked: the exporters ground the model on
it and every vertex they write has to be inside it.

### Four tests broke, and they were right to
`test_roundtrip.cpp` (×3) and `test_unit_correctness.cpp` compared an unmasked
EXPORT against `mesh.heightCm()`. They passed only because heightCm used to be
the full bounding box too. They mean "the exported geometry's extent", so they
now say `boundingBox()` explicitly, via a `fullExtentDm` helper that spells out
why. **The asserted values are unchanged** — this is not a weakened test, it is
the same assertion against the thing it was always about.

Worth noting `test_roundtrip.cpp:255-263` had already hand-rolled the masked
extent, so someone had met this distinction before without generalising it.

### Verification
- ctest 518/518 in debug, release, ASan and TSan.
- Mutation: dropping `if (visible[f] == 0U) continue;` from `heightCm` fails the
  new parity test on both of its measured numbers.
- Screenshotted the window and read the line: `Gender: neutral  Age: 25
  Muscle: 50.00 %  Weight: 100.00 %  Height: 165.94 cm`. 165.94 rather than the
  raw mesh's 166.589 because the app has applied the default macro stack — the
  two are consistent, not contradictory.

---

## 2026-09-05 18:05:00 — Session 132 · **every icon in the app was clipped, and no test could see it**

### The chunk
Owner directive 8, first structural piece: the icon audit the directive asks
for, and the top toolbar.

### The audit, written first, found three blanks immediately
`theme::icon()` returns a NULL QIcon for a name it cannot find — deliberate, so
a missing file shows nothing rather than a black square. The cost is that a
typo, or an action nobody gave an icon, ships a blank and logs nothing.

`[ui][icons]` now walks every named QAction in the real window. First run:
`edit.undo`, `edit.redo` (Qt builds those two, so they are the easiest pair to
forget) and — after I fixed an exclusion that was too broad — `workspace.reset`,
which had been shipping as bare TEXT between the toolbar icons.

That exclusion is worth remembering: I wrote `if (name.startsWith("workspace."))
continue;` meaning "skip the named layouts", and it silently also skipped two
real commands. The test was green and the screenshot showed the word "Reset
Workspace" sitting in the toolbar.

### The real find: every icon in the application was clipped on retina
Looking at the screenshot, the glyphs were fragments — `undo-2` rendered as a
bare `←`, `save` as a corner bracket. Not a toolbar problem: the panel title
bars had it too.

`theme::icon` called `QSvgRenderer::render(&painter)` with **no target rect**.
That uses the painter's viewport, which is in DEVICE pixels, while the
painter's transform already carries the device pixel ratio. Fix is one
argument: `render(&painter, QRectF(0, 0, px, px))`.

**It is invisible at DPR 1**, where the two rects coincide. The entire suite was
green, and had been for as long as the icons have existed. So the test had to
create the condition: `ui_icons_hidpi` runs the icon cases a second time under
`QT_SCALE_FACTOR=2`. Mutation-verified — reverting the fix fails
`ui_icons_hidpi` and leaves `ui` passing.

The assertion that catches it is a centre-of-mass check, not a pixel count: a
clipped icon is still mostly opaque, so "did it draw anything?" cannot see this.
A lucide glyph sits centred in a 24x24 viewBox; at DPR 2 the centre of mass of a
symmetric `x` measured (0.72, 0.72) of the pixmap instead of (0.5, 0.5).

**A correction I should record**: I predicted the clipping would push the ink to
the TOP-LEFT and wrote that reasoning out before measuring. The measurement said
bottom-right. The fix is the same either way, and the comment in `Theme.cpp` now
states only what was measured.

### The toolbar
`toolbar.main`: open, save, save-as | undo, redo, reset workspace | grab screen.
It re-uses the menus' QActions rather than building parallel ones — two actions
for one command is how a button drifts out of step with its menu item, each
keeping its own enabled state, shortcut and translation registration. Asserted
by looking up each objectName and requiring exactly ONE match, plus checking
undo starts disabled and the toolbar holds that same object.

`screenshotRequested()` is new and wired in `main.cpp`, so the camera button
does something. Every other group from the reference screenshot is deliberately
absent — see below.

### What I did NOT build, and why
The reference's mesh-display (smooth, wireframe, subdivide), symmetry (3) and
body-part camera views (~8) are not here. `src/ui/` has no wireframe, smooth,
subdivide or mirror anything, so all of those would be painted no-ops. A button
that looks live and does nothing is worse than an absent one. The body-part
views additionally need CUSTOM icons — lucide ships no anatomy glyphs, which is
what the empty `resources/icons/custom/` is for. Recorded in todo.md.

### Verification
- ctest green in debug, release, ASan and TSan, including the new
  `ui_icons_hidpi` entry.
- All 56 vendored SVGs asserted to rasterise to a non-empty, centred pixmap.
- Looked at three screenshots of the real window. The clipping was only ever
  visible that way; no assertion I had would have found it.

---

## 2026-09-05 17:20:00 — Session 131 · **viewport PBR, and a skin picker that never reached the screen**

### The chunk
Owner directive 4 ("number 4, let's make view port also PBR"), plus the two
portability bugs the cloud loop kept rediscovering every hour.

### PBR as a second model, not a replacement
`render::ShadingModel` picks between `Litsphere` and `Pbr`. The litsphere stays
the default and stays byte-identical: it is the M6 parity path, and a test
asserts that setting `metallic`/`roughness` on a mesh moves **zero** pixels
under it.

`resources/shaders/rhi/pbr.{vert,frag}` are **Apache-2.0 original work**, not
ports — the reference has no PBR path to translate, so there was nothing to
derive from. GGX + height-correlated Smith + Schlick, ACES tonemap, sRGB decode
on the way in and encode on the way out (the target is plain RGBA8,
`OffscreenRenderer.cpp:66`, so nothing linearizes for us).

Two decisions worth keeping:
- **No IBL.** Every usable HDRI is CC-BY (attribution we cannot honour inside a
  binary asset) or non-commercial. The ambient is an analytic hemisphere. It is
  not energy-accurate and the shader header says so; it exists so surfaces
  facing away from all three lights are shaded rather than black.
- **Lights in VIEW space.** The camera is fixed and the model rotates, so a rig
  bolted to the camera is what a modelling viewport wants — and it is what the
  matcap already implies, a matcap being lighting locked to the eye.

Metallic and roughness come from `foundation::metallicRoughnessOf()`, the same
call the glTF and USD writers make, so the viewport and the exported file
cannot disagree about the same material.

### The bug PBR exposed: `--skin-material` never reached the viewport
`main.cpp:1803` read `skins/default.mhmat` **by name**. So the picker, the
`.mhm` field and the exporters all honoured the choice, and the screen never
did: all eight shipped tones rendered as the same untextured body. Both renders
printed "rendered ... (1024x1024, pbr)" and exited 0.

Nothing catches this except comparing the two images, which is what
`tests/files_differ.cmake` now does. I wrote the test first, watched it fail
with the two PNGs byte-identical, then changed the line to `skinMaterialPath()`.

This had been true since the skins landed in `a8436474`. The litsphere hid it —
an untextured matcap body and a textured one shaded by a matcap look similar
enough that nothing looked wrong.

### A tuning value I had got wrong, visible only once it was rendered
The generated skins shipped `shininess 0.65` → roughness 0.35. Nothing in the
exported file looked wrong at 0.35. Putting the same material through a real
microfacet BRDF made it obvious: every tone read as **oiled skin**. Retuned to
`shininess 0.42` → roughness 0.58, in `tools/make_skins.py` and all eight
`.mhmat` files. Measured photographic skin sits around 0.5–0.7 for a
single-lobe GGX; below 0.4 is what a specular lobe does when it is the only
lobe and there is no subsurface term under it.

### The two portability bugs, fixed here rather than in the cloud
Both were found independently by the hourly cloud runs, which cannot verify a
fix as fast as I can locally.
- `src/rig/Skeleton.cpp:218,228` iterated `.items()` on the temporary returned
  by `root.value(...)`. Apple clang implements P2718R0 so it works here; clang-18
  with libstdc++ segfaults — 54 Linux failures. Both loops now name the object.
- Nine test files used `std::sqrt`/`std::isfinite`/`std::abs` without
  `<cmath>`. libc++ pulls it in transitively through Catch2; libstdc++ does not.

### Verification
- ctest **516/516** in debug, release, ASan **and** TSan. (The new
  ViewportWidget case runs inside the single `ui` ctest entry rather than as
  its own row, so the total moves by the four app-level tests only.)
- Mutations killed: reversing the plane triple, dropping the joint map,
  hard-coding roughness, hard-coding metallic, making `setShadingModel` a no-op.
  Each was killed by the test written for it, not incidentally.
- Rendered and **looked at** three images rather than trusting pixel counts.
  That is how the oiled-skin roughness was caught; no assertion would have.
- clang-format gate run with CI's exact command.

### Follow-ups recorded in todo.md, not fixed here
- The eyeballs blow out to white under PBR: the eye geometry takes the body
  albedo and its UV island lands on a bright region. Pre-existing asset/UV
  issue that the matcap was flattening.
- `default.mhmat` names no albedo at all, so `--shading pbr` with the default
  skin renders a near-white body. Correct for albedo 1.0, but a poor default.

---

## 2026-09-05 16:44:29 — Session 130 · **the logo, and what "looks like a macOS app" actually means**

### The chunk
Owner supplied a 3966x3966 brand logo mid-turn: put it somewhere sensible and
make sure both the DMG-packaged app and the dev app carry it, *"properly warped
just like how other apps look like in macos"*.

### The geometry IS the request
A macOS app icon is not a full-bleed square. Since Big Sur every system icon is
a squircle occupying **824 of a 1024pt canvas**, centred, with a 100pt
transparent margin all round and a corner radius of 185.4. Shipping the raw
square would render visibly larger than every neighbour in the Dock and square
where they are round — which is exactly the complaint.

`tools/make_appicon.py` masks the logo into that geometry and emits the iconset
plus `AppIcon.icns`. The mask is supersampled 4x and downsampled, because PIL's
rounded rectangle is aliased at 1x and the corners come out visibly stepped at
512pt.

### Both apps, for different reasons
- **The bundle** takes its icon from `Info.plist`'s `CFBundleIconFile`.
- **A bare build-tree run has no Info.plist** — and that is the binary a
  developer looks at all day — so `QApplication::setWindowIcon` covers it.

There was no `.app` bundle at all before this, so M11's first item came with it:
`MACOSX_BUNDLE`, a generated `Info.plist`, and the icns installed into
`Contents/Resources`.

**Trap, pinned by a test**: `CFBundleIconFile` must be `AppIcon` **without** the
extension. `AppIcon.icns` there resolves to `AppIcon.icns.icns` and falls back to
the generic icon with no error anywhere. `app_bundle_icon` and
`app_bundle_plist` assert both the file's presence and the plist's value.

### The `.gitignore` trap that nearly shipped a blank icon
`*.png` is ignored repo-wide with only `!data/**` re-included, so **both** the
master logo and the runtime `AppIcon-1024.png` were silently excluded from the
commit — `git add -A` reported success and staged only the `.icns`. A clone
would have had no Dock icon and no error to explain it. Caught by reading
`git status` rather than trusting the add; fixed with `!resources/**`.

The `AppIcon.iconset/` intermediate stays ignored: it regenerates byte-identically
(verified) and only the `.icns` and the 1024 PNG are consumed.

### DMG
`cmake --build … --target dmg` produces `MakeHuman.dmg` (41 MB) with the usual
drag-to-Applications layout. **Verified by mounting it and running the packaged
binary**, which exported correctly and carried the icon.

**Two caveats recorded rather than hidden:**
- `macdeployqt` reports `Cannot resolve rpath` for QtVirtualKeyboard. Non-fatal
  — nothing uses it — but the deploy is not clean.
- **The bundle is not relocatable.** `MH_DATA_DIR`, `MH_SHADER_DIR` and
  `MH_RESOURCE_DIR` are compile-time absolute paths into the source tree, so the
  DMG runs on THIS machine only. That is M11's open "runtime data location"
  question and must be settled before the DMG is given to anyone.

### Licensing
Pillow (HPND) recorded as **developer tooling** — it runs once when the logo
changes; the committed `.icns` is what the build consumes, so a machine without
it builds fine. The same table now records the **FBX SDK and Maya as validators
only**, out-of-process like Blender, never linked, with a note that linking
either would require hard rule 6 to be amended first.

### Verification
- 503/503 in debug, release, ASan and TSan.
- Sonar gate OK, 0 open issues. Blender harness 11/11 (its hardcoded app path
  updated for the bundle, with the bare path still tried as a fallback).
- Master PNG losslessly recompressed 7.09 -> 4.92 MB, pixels verified identical.

---

## 2026-09-05 01:41:57 — Session 129 · **my probe was one statement too early**

### The chunk
Closing the "unexplained" note session 121 left behind: why do exported joint
nodes follow the pose when the only re-fit in the export path runs BEFORE
posing?

### The answer was in the function's own doc comment
`exportSkin` composes the pose into the bind globals
(`main.cpp:874-875`):

    skin.globalRest[b] = skinning[b] * skin.globalRest[b];

and its doc comment says exactly why: *"The exported mesh is the POSED one, so
the bind pose is the pose … handing over the REST globals instead would let a
DCC apply the pose twice."*

So there was never a defect and never a mystery. Session 121's note was wrong to
call it unexplained, and that record is now corrected rather than left standing.

### Four wrong theories before the measurement that mattered
1. **The ground offset.** Killed by splitting the file comparison into rotation
   and translation parts: the rotation differed by 0.62, which a translation
   cannot cause.
2. **Something mutating the skeleton.** Killed by grep: every rig function takes
   `const Skeleton&`.
3. **Re-fitting not being idempotent.** Killed by writing the test — and the
   test is worth keeping, so it stayed: *"fitting the rig twice to one mesh
   changes nothing"* in `test_skeleton_parity.cpp`.
4. **Stale or mismatched files.** Killed by re-comparing freshly written pairs
   and matching nodes by NAME rather than position.

### The mistake that cost the most
My first probe printed `skin.globalRest` immediately after `buildSkinData` --
**one statement before the line that changes it**. It showed all 163 matrices
byte-identical between a rest and a posed export, which is true and completely
misleading. Three sessions of confident reasoning rested on it.

A second probe at the handover point, a few lines later, showed the values
differing in the same run. That single before/after pair located the composition
immediately.

**Rule taken from this**: when a measurement contradicts the code, suspect the
measurement's POSITION before suspecting the code. A probe proves something only
about the point it sits at.

There was also a false negative on the way: a probe build failed on
`-Wsign-conversion`, both output files came back empty, and `diff` cheerfully
reported them identical. Caught by counting the lines rather than trusting the
diff.

### Verification
- 500/500 in debug, release, ASan and TSan (the new idempotence test is the
  500th).
- Every probe reverted; `grep -c PROBE` is 0 in both touched files.
- SonarQube gate OK, 0 open issues. CI green at `181dfb36`.

### Files changed
`tests/golden/test_skeleton_parity.cpp`, `memory/todo.md`,
`memory/handover_session.md`.

---

## 2026-09-05 01:09:52 — Session 128 · **twenty language files shipped, and nothing read them**

### The chunk
Live language switching and working RTL. The last M8 item that needed neither an
owner decision nor hardware.

### The starting state
`data/languages/` has shipped **20 JSON dictionaries** since the data import,
one of them RTL (Arabic). Grepping `src/` and `include/` for `QTranslator`,
`setLayoutDirection` or `LayoutDirection` returned **nothing**. The assets were
there; the code was not.

### QTranslator, not a lookup helper
Qt already owns live switching: `installTranslator` posts
`QEvent::LanguageChange` to every top-level widget, and every `tr()` call routes
through the installed translator with no change at the call site. A bespoke
lookup would have meant editing all 38 call sites and reimplementing the event.

**Context is ignored deliberately.** Qt keys translations by (class, source);
these files are one flat map keyed by source alone. Honouring context would make
every single entry miss.

### The measurement that decided where the work went
That flatness is also what lets **data** strings translate — and the data is
where the text actually is:

| surface | translated (German) |
|---|---|
| slider labels | **135 / 192** |
| task views | 5 / 7 |
| sections | 15 / 49 |
| our own `tr()` strings | **5 / 38** |

So the wiring went to slider captions, section headings, tab names and dock
titles, all through `QCoreApplication::translate("", …)`. Had I only wired
`tr()`, a language switch would have changed five words.

The five that do exist are `Close`, `Redo`, `Reset`, `Save`, `Undo`. Our menu
vocabulary (`&File`, `Open…`, `Save As…`) is absent, because these dictionaries
carry the REFERENCE's strings. **Recorded rather than hidden**: the mechanism is
done, the vocabulary is data someone can add.

### Two details that are the difference between working and demo-working
- `setLanguage` loads the new file **before** removing the old one. A failed
  switch leaves the running language alone instead of dropping the user into raw
  English — which, mid-switch in a script you cannot read, is unrecoverable.
- RTL is **reset** when switching away. Leaving the application right-to-left
  after leaving Arabic is exactly why the item said "**working** RTL".

The menu offers "English" first — the untranslated source every file is a
translation of, and the way back.

### Mutations, all five caught
Honour Qt's context; skip the clear on a failed load; let `__options__` into the
string map; never reset the layout direction; make `retranslateUi` a no-op.

### A regression I caused and caught
Routing dock titles through `registerText` alone broke two accessibility
assertions: `makeDock` derives `accessibleName` and the placeholder body from
the title it is CONSTRUCTED with, and neither follows a later
`setWindowTitle`. The title is now passed in as well as registered, with the
reason written down.

Also moved `Translatable` inside the anonymous namespace — I had left it with
external linkage in `mh::ui`, a collision waiting for a second definition.

### Verification
- 499/499 in debug, release, ASan and TSan.
- SonarQube gate OK, **0 open issues**.
- CI green at `1f125d49` before pushing.

### Files changed
`include/makehuman/ui/Language.h` (new), `src/ui/Language.cpp` (new),
`tests/ui/test_language.cpp` (new), `include/makehuman/ui/MainWindow.h`,
`src/ui/MainWindow.cpp`, `src/ui/ModifierPanel.cpp`, `src/ui/CMakeLists.txt`,
`src/app/main.cpp`, `tests/CMakeLists.txt`, `memory/todo.md`,
`memory/handover_session.md`.

---

## 2026-09-05 00:35:07 — Session 127 · **two items closed by deciding, one by sharing**

### The chunk
Two M5/M6 items, and only one of them wanted code.

### Skin normals/tangents (w=0): deliberately NOT built, with the evidence
`skinMesh` uses the homogeneous coordinate as a switch -- **w=1 for positions,
w=0 for directions** (normals, tangents, targets), so translation never moves a
direction (`shared/animation.py:1129-1145`).

Building `skinDirections` today would give it **zero callers**. Checked, not
assumed: every `poseInPlace` site recomputes normals and tangents from the
DEFORMED geometry immediately afterwards (`main.cpp:1305-1306`, `1556-1557`),
`refitProxy` does the same for every worn proxy (`422-423`), and `skinPositions`
has exactly one caller in `src/`.

And recomputing is the **better** answer: it is exact for the deformed surface
where skinning the rest normals only blends them. A deliberate divergence from
the reference, not an omission. Reopen when GPU skinning lands -- a vertex
shader has no adjacency to recompute from, and that is the first real consumer.

Same call as the cached bounding box and `findFaceGroup` before it.

### Blinn-Phong -> PBR: it already existed, twice
`GltfWriter.cpp` and `UsdWriter.cpp` each computed `clamp(1 - shininess, 0, 1)`
independently, and `SceneIO.cpp` commented about it a third time. Worse than the
duplication: the REASONING for `metallic = 0` lived in only one of the two
comments, and the other said "the same conversion every other writer uses" --
pointing at a rule the reader then has to go find.

`foundation::metallicRoughnessOf` is now the one conversion, and it states the
thing that was implicit: **metallic is 0 because everything this application
produces is dielectric** -- skin, cloth, hair, eyes -- and `.mhmat` has no field
that could say otherwise. A writer inventing its own value is how two exporters
disagree about one character.

glTF's `"metallicFactor":0` was a hardcoded literal in the JSON string; it now
comes from the same struct as the roughness beside it. Verified the output is
unchanged: skin roughness 0.04000002, eye 0, metallic 0, in both `.glb` and
`.usda`.

Two mutations, both caught: metallic 1 (5 failures), unclamped roughness (2).

### The viewport half is an owner decision, and is now recorded as one
"PBR metallic-roughness path" also implies the VIEWPORT, and that is not an
addition to what is there -- it is a different lighting model. The viewport
shades with a litsphere/matcap, which is what the reference does and what makes
every shipped skin look the way it does. Metallic-roughness needs light rigs, an
environment or IBL, and it retires the litsphere as the thing that defines a
material. Nothing downstream is blocked: the export side is already PBR.

### Verification
- 499/499 in debug, release, ASan and TSan.
- Exported material numbers unchanged in both formats, read back from the files.
- SonarQube gate OK, **0 open issues**.
- CI green at `84ddadca` before this was pushed.

### Files changed
`include/makehuman/foundation/Geometry.h`, `src/io/GltfWriter.cpp`,
`src/io/UsdWriter.cpp`, `tests/golden/test_scene_io.cpp`, `memory/todo.md`,
`memory/handover_session.md`.

---

## 2026-09-05 00:13:14 — Session 126 · **the blocker in the note had already been removed**

### The chunk
Proxy-on-proxy masking — clothes hiding clothes. The last open M4 item.

### The recorded blocker was stale, and checking cost one grep
`todo.md` said *"Needs the render-order stack"*. But `z_depth` has been parsed
since the proxy reader landed (`Proxy.h:73`, `Proxy.cpp:202-203, 336-339`), so
the ordering data was already there and the item had been implementable for
some time. Memory is point-in-time; state is truth.

### Two rules that are not the same rule
`shared/proxy.py:960-983`:

* a proxy vertex fitted to ONE base vertex (`weights[1]` and `[2]` both zero)
  copies that vertex's visibility;
* an interpolated one is hidden only when at least **two** of its three
  references are hidden.

The second is the one worth pinning. The natural guess — hide as soon as any
reference is hidden — erodes a much wider band around every hole than the
reference produces, and would look like a fitting bug rather than a mask bug.

### The order IS the feature
`3_libraries_clothes_chooser.py:92-99, 125` walks `reversed(sorted by z_depth)`
— outermost first — handing each garment the mask accumulated by the layers
**above** it and folding in its own `delete_verts` only afterwards. So a garment
is masked by what is over it and never by itself.

Ties fall to the uuid, as the reference's `(z_depth, uuid)` sort does, so the
answer does not depend on the order the caller happened to collect the proxies
in — asserted by running the same two garments through in both orders.

### The mutation that survived
Four of five mutations were caught at once. The fifth — folding a proxy's own
deletions in **before** taking its mask, so every garment erases itself wherever
it cuts the body — passed everything, because my jacket's own vertex sat away
from its own deletions. Fixed by a test that puts a garment's vertex exactly on
a body vertex it deletes. That is the second session running where the surviving
mutation pointed at a case the tests had merely not arranged.

### A performance finding in my own first draft
I put the per-garment loop **above** `applyBodyMask`'s early-out, so it ran on
every slider drag — allocating a mask per garment per frame for a result that
only changes when something is put on or taken off. Moved below the early-out,
where the body mask already sits for exactly that reason.

Also: my first version discarded `setFaceMask`'s failure with `(void)`. Now
announced — a garment that silently keeps rendering through a hole reads as a
modelling problem rather than a bug.

### Verification
- 498/498 in debug, release, ASan and TSan.
- Blender **11/11**; the shipped eye proxy declares zero `delete_verts`, so the
  exports are byte-for-byte what they were, which is the correct outcome.
- SonarQube gate OK, **0 open issues**. Benchmarks unchanged.
- Still **no shipped asset exercises this** — all four `.mhclo`/`.proxy` files
  declare zero `delete_verts`, so coverage is synthetic, as `visibleVertexMask`'s
  already was.

### Files changed
`include/makehuman/core/Proxy.h`, `src/core/Proxy.cpp`, `src/app/main.cpp`,
`tests/golden/test_face_mask_parity.cpp`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
M4 is now closed. M5's remaining items need the owner or assets, except the
skin normals/tangents w=0 path.

---

## 2026-09-02 19:09:26 — Session 125 · **two pan mutations survived, and that was the finding**

### The chunk
Camera pan. The viewport could orbit and zoom but never move the model off
centre, and the `.mhm` camera line's translation was written as zeros and read
as nothing.

### The binding was a real choice, made from the reference
The reference binds pan to the **arrow keys** (`core/mhmain.py:178-181`). Those
already orbit here -- a control added deliberately in an earlier session. Taking
them back would remove something that works to match a convention, so pan went
on **middle drag**, which was free and is what every DCC uses. Nothing was lost
either way, and the reasoning is in the code.

### The format's units are not ours
`lib/camera.py:544-546` multiplies the stored translation by the human's
half-extents, so the file holds a **fraction of the model's size**, clamped to
[-1, 1] (`camera.py:608-610`). `OrbitView::translation` stores it that way and
the app converts using the mesh bbox. Storing decimetres would have written a
file MakeHuman 1.x reads as a pan of many body-widths -- a bug invisible from
inside this port, since our own round trip would have been perfect.

Slot 4 (z) is still carried from the loaded file rather than zeroed: the
viewport pans in x and y only, so writing a zero would discard a depth offset
the file legitimately holds.

The **sign is negated on purpose** -- the reference pans by moving the camera's
CENTRE, so +x aims right and the model appears to move left, while our pan
translates the view. Reasoned from `camera.py:544`, **not measured against a
running MakeHuman 1.x**, and labelled as such: the same standing as the zoom
mapping, which is anchored on defaults and documented as "a sensible framing,
not the identical one".

### Two mutations survived, and both exposed the same gap
This is the part worth remembering.

1. **Making the renderer ignore `panX`/`panY` entirely left all 493 tests
   green.** The viewport tests checked that dragging changes the *camera*;
   nothing checked that the camera changes the *picture*. That is precisely the
   "built, never connected" class this port has hit again and again -- and this
   time the seam was inside a feature I was actively writing.
2. **Wiring pan into the model matrix instead of the view passed too.** At the
   default yaw the two axes coincide, so every check agreed. A model-space pan
   would move the body along its own axes -- dragging a turned model would push
   it away from the camera instead of across the screen.

Both are now covered by render tests that compare left/right coverage of the
actual image, the second with the camera turned 90 degrees. Five mutations
total, all five now caught.

### Verification
- 493/493 in debug, release, ASan and TSan.
- Render tests assert the IMAGE moves, and in the right direction: positive
  panX shifts coverage out of the left half into the right.
- App-level save/reload ctests still green; benchmarks unchanged.
- Sonar gate OK, 0 open issues. CI green at `f378ddd4` before pushing.

### Files changed
`include/makehuman/core/Mhm.h`, `src/core/Mhm.cpp`,
`include/makehuman/render/SceneResources.h`, `src/render/SceneResources.cpp`,
`include/makehuman/ui/ViewportWidget.h`, `src/ui/ViewportWidget.cpp`,
`src/app/main.cpp`, `tests/golden/test_mhm_parity.cpp`,
`tests/render/test_offscreen_render.cpp`, `tests/ui/test_ui.cpp`,
`memory/todo.md`, `memory/handover_session.md`.

### Next
This was the last M1-M8 item actionable without the owner. Everything still
open needs a decision, an asset, or hardware -- see the report.

---

## 2026-09-02 18:44:18 — Session 124 · **the same eight lines, in five places**

### The chunk
`io::Transform`, the last M7 item that did not need the owner.

### The duplication was real, and I checked before building the abstraction
`scale = unitScale(options.unit) * options.scale` plus a "lowest scaled y,
negated" loop appeared in **five** places: `GltfWriter`, `SceneIO` (twice),
`UsdWriter`, `ObjWriter`. Four were character-for-character identical. Net
**-56 lines** across the writers once shared.

That count is why this was worth doing rather than a speculative abstraction --
the ladder's first rung is "does this need to exist at all", and five copies
answer it.

### What shipped
`include/makehuman/io/Transform.h`: `Unit`, `unitScale`, and a
`Transform{scale, groundOffset}` with `place()` / `placedY()`, plus
`sceneTransform()` and `meshTransform()`.

A **template** over the entry type, because `GltfSceneEntry`, `SceneEntry` and
`UsdSceneEntry` agree only on having a `.mesh`. Converting them to a common
span would allocate on every export purely to satisfy a signature.

### The smell that made it obvious
`UsdWriter.h` was including `ObjWriter.h` **purely for `Unit`/`unitScale`** --
a unit type living in one format's header because there was nowhere shared to
put it. Both moved to `Transform.h`, which `ObjWriter.h` now includes, so every
existing includer still sees `Unit` and nothing downstream changed.

### ObjWriter is the deliberate exception
It keeps its own loop, now with a comment saying why: every other writer levels
by the lowest vertex in the buffer, but OBJ writes only the vertices its kept
faces reference, so it must skip the dropped ones. Levelling by a vertex that
never reaches the file lifts the model off the floor by however far the hidden
helper cage hangs below it.

Recording the exception matters more than sharing the code -- the next person to
see four callers and one hold-out will otherwise "finish the job".

### Mutations, all three caught
- Take the minimum BEFORE scaling. This is the dangerous one: it looks correct
  at the default scale of 1 and is wrong at every other.
- Drop the `isfinite` guard. `lowest` starts at +infinity, so an empty scene
  would be lifted by -inf and write a file of NaNs rather than an empty one.
- Offset x and z as well as y.

### Verification
- 491/491 in debug, release, ASan and TSan.
- Blender **11/11** -- the end-to-end check that matters here, since a scale or
  offset that shifted anywhere would move a bounding box.
- Benchmarks unchanged. Sonar gate OK, 0 open issues.
- CI green at `f78e5aca` before this was pushed. **Waited for it deliberately**:
  `cancel-in-progress` means an early push destroys the evidence.

### Files changed
`include/makehuman/io/Transform.h` (new), `src/io/Transform.cpp` (new),
`tests/unit/test_transform.cpp` (new), `include/makehuman/io/ObjWriter.h`,
`include/makehuman/io/UsdWriter.h`, `src/io/GltfWriter.cpp`,
`src/io/SceneIO.cpp`, `src/io/UsdWriter.cpp`, `src/io/ObjWriter.cpp`,
`src/io/CMakeLists.txt`, `tests/CMakeLists.txt`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
Camera pan (M8) is the last unattended-actionable item I know of. Everything
else in M1-M8 needs the owner.

---

## 2026-09-02 18:19:28 — Session 123 · **the last 8 Sonar findings, each proved by output diff**

### The chunk
The 8 `python:S3776` cognitive-complexity findings across 7 developer tools.
SonarQube now reports **0 open issues** on the project.

### Docker was down first
SonarQube was unreachable (`http=000`) because the Docker daemon had stopped --
also why `MCP_DOCKER` failed to connect at session start. Started Docker
Desktop, restarted the `m2m-sonarqube` container, waited for `status: UP`.
Worth knowing: the container does **not** come back on its own.

### The method, which is the point
Every refactor is a behaviour-preserving extraction into named helpers, one per
independent responsibility. None of it was verified by reading:

| tool | was | proof |
|---|---|---|
| `blender_validate.py` | 32 | 11 files, byte-identical JSON |
| `mixamo_mapping.py` | 55, 22 | identical stdout; `--check` still clean |
| `audit_taskviews.py` | 32 | identical stdout |
| `audit_poseunits.py` | 20 | identical stdout |
| `build_mixamo_superset.py` | 16 | identical stdout; regenerated `.mhskel` byte-identical in git |
| `baseline_python_core.py` | 17 | all 12 sections identical in name, order, every non-timing field |
| `capture_fixture.py` | 40 | `layout.json` byte-identical |

`baseline_python_core.py` rewrites the committed `baseline_python.json`, so the
original was backed up and restored; timings vary by nature, so that one diff is
structural and the note says so rather than pretending it is a byte diff.

### capture_fixture.py, with the proof this item demanded
`todo.md` had deliberately excluded it -- *"it generates the oracle fixtures, so
refactoring it trades a style metric for risk to every parity test. Reopen with
the re-capture-and-diff proof."*

Measured the baseline FIRST: re-capturing `slider_layout` with **no code change
at all** already rewrites `MANIFEST.json`. Not a defect -- the only fields that
move are `captured_at` and `reference_commit`, which are provenance by design.
Knowing that, the proof is exact: after the refactor, `layout.json` is
byte-identical and `MANIFEST.json` shows *that same two-field delta and nothing
else*. Without measuring the baseline first I would have had a diff I could not
interpret.

### Three things the re-scan caught in my own work
1. **An unused parameter** (`python:S1172`): my `_chain_root_problems(mixamo,
   makehuman)` never used `mixamo`. Removed.
2. **A function still over the limit**: `_mapping_problems` came out at 27, so
   it split again into coverage / injectivity / laterality / ancestry.
3. **A lost edge case**, found by reading my own diff rather than by a tool:
   `max(generator)` raises on an empty `RIGS` where the old accumulator stayed
   at 0. Restored with `default=0` and a comment saying why.

### And a measurement bug of mine, not a code bug
`for c in "tools/x.py --check"; do python3 $c; done` reported **exit 2** for both
`--check` tools. **zsh does not word-split unquoted parameters**, so the whole
string was passed as one filename. Run directly, both exit 0. Checked before
reporting a CI failure that did not exist.

### Verification
- SonarQube: **8 open issues -> 0**. Gate OK, duplication 0.0%.
- 484/484 in debug, release and ASan. **No C++ changed**, so the previous TSan
  run covers identical code.
- Blender harness still **11/11**.
- All five tools CI runs re-run with **system `python3`**, not the venv: all
  exit 0, and no generated file drifted in git.

### Files changed
`tools/blender_validate.py`, `tools/mixamo_mapping.py`,
`tools/audit_taskviews.py`, `tools/audit_poseunits.py`,
`tools/build_mixamo_superset.py`, `tools/capture_fixture.py`,
`benchmarks/baseline_python_core.py`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
M5's remaining items need the owner or assets. Still unattended-actionable:
camera pan (M8) and the shared `Transform` refactor (M7).

---

## 2026-09-02 04:57:19 — Session 122 · **Cmd+Z after Cmd+1 undid the wrong thing**

### The chunk
Undo for workspace changes. `applyWorkspacePreset` rewrote the whole layout and
pushed nothing, so ⌘1 followed by ⌘Z left the new layout in place and undid
whatever slider the user had touched before it -- silently, and the wrong thing.

### The todo item's framing was half wrong
It read *"presets and Save As bypass the stack"*. Only the preset half is a bug.
`saveWorkspaceAs` writes a file and changes **no window state**; "undoing" it
would mean deleting a file the user asked to save, which is not what an undo
stack is for. Fixed the preset, and recorded why the other half is correct as it
stands rather than quietly implementing it.

### What shipped
`ui::LayoutChangeCommand` -- two `saveState()` blobs and a restore callback,
knowing nothing about docks or presets, so it stays in the Apache-2.0 module.

That design rests on one fact I asserted rather than assumed: **`saveState()`
carries dock VISIBILITY, not just geometry.** If it carried only geometry, an
undo would restore positions and leave docks hidden -- a plausible-looking bug
nobody would trace back to the undo command. There is now a test whose only job
is that property.

The state is captured *after* every refusal path, so a preset resolving to no
live dock still pushes nothing -- the rule the pose commands already follow
("probed before the command is pushed").

### A test trap worth remembering
`isVisible()` is **false for every child of a window that was never shown**, so
the first version of the visibility test failed on its own setup. `isHidden()`
is the flag `setVisible` actually writes and `restoreState` restores, and is
what the assertions use.

### A guard I deleted rather than kept
Three mutations, two caught (never pushing the command; capturing `before` after
the layout already moved). The third -- removing a first-call guard in `redo()`,
there because `QUndoStack::push` calls `redo()` when the layout is already
applied -- changed **nothing**, because `restoreState` is idempotent. The
comment I had written to justify it ("keeps a push from stealing focus or
resizing a dock the user is mid-drag on") was a claim I had not verified. Guard
and claim both removed.

### CI: two runs were cancelled, and it was my doing
`b4b4f890` and `aa2f9d32` both show **cancelled**, not green. The workflow sets
`concurrency: cancel-in-progress: true` on `${{ github.workflow }}-${{ github.ref }}`,
so pushing three commits inside 40 minutes cancelled each in-progress run. Not a
failure, but it means those two commits were never CI-validated on their own;
`87cff591` is the cumulative head and covers them. **Push spacing matters here**
-- wait for the previous run before pushing again, or the evidence is destroyed
rather than gathered.

### Verification
- 484/484 in debug, release, ASan and TSan.
- Sonar gate OK, duplication 0.0%.
- Benchmarks untouched (UI-only chunk).

### Files changed
`include/makehuman/ui/UndoCommands.h`, `src/ui/UndoCommands.cpp`,
`src/ui/MainWindow.cpp`, `tests/ui/test_ui.cpp`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
See the report: what remains in M1-M8 needs the owner.

---

## 2026-09-02 04:36:59 — Session 121 · **the check I set out to write turned out to be impossible**

### The chunk
"Extend Blender validation to a posed mesh, so LBS output is checked by a third
party rather than only against the reference." The last M8 item actionable
without the owner.

### It cannot be done from our exports, and that is the finding
I set out to have Blender skin our rig and compare its answer to our CPU LBS.
Measured instead that **Blender's skinning is a no-op on every file we write**:
applying the armature moves the mesh by **9e-06**.

That is not a bug -- it is correct, and by design. `GltfWriter` derives the
joint node transforms AND the inverse-bind matrices from one scaled-global array
(`GltfWriter.cpp:330-374`) *"so they cannot disagree"*. A posed export bakes the
pose into the vertices and exports the skeleton in the same state, so the posed
state IS the bind pose and the skinning matrix is identity.

Confirmed three ways: the posed GLB's evaluated mesh equals its raw mesh; its
bbox matches the same pose exported to OBJ exactly (1.6863 x 0.3009 x 1.6630 m
against 16.8628 x 3.0088 x 16.6301 dm -- the 10x is dm vs m); and 41 of 163
joint node matrices differ from the rest export, so the skeleton genuinely
followed the pose rather than being written at rest.

**Consequence:** third-party LBS validation needs an export carrying **rest
geometry with a posed armature**. That is an interchange-semantics decision --
do exports bake, or ship a live rig? -- and belongs to the owner, not to me.

### What shipped instead
`posed.glb` in the harness (written by the **application**, since posing is an
app-level sequence and the point is the file a user actually gets), plus a
generic `armature_shift` check applied to **every** file: applying the armature
must not move the mesh.

**I verified it has teeth rather than assuming so.** My first mutation --
deleting the re-fit in `poseInPlace` -- changed the output not at all, so the
check looked like it might pass by default. The mutation that does break it is
the one the design guards against: unscaled inverse-bind matrices against scaled
node transforms. That yields a shift of **8.38** and two FAILs. A file failing
this is double-deformed in every DCC while our own tests, which never apply an
armature, all stay green.

Harness: **11/11 exports agree** (was 10/10).

### A comment I wrote and then had to correct
My first version of the check said the failure meant *"the skeleton was not
re-fitted to the posed geometry"* -- a mechanism I never confirmed and which my
own mutation contradicted. Rewritten to say what was actually measured: the node
transforms and the inverse-bind matrices disagree. The same correction went into
the harness and validator comments.

### Left explicitly unexplained
The exported joint nodes follow the pose, but the only re-fit in the export path
(`main.cpp:265`) fits the skeleton to the mesh *before* posing it -- and deleting
it changes the exported file not at all. So `skin->globalRest` differs between a
rest run and a posed run for a reason I did not locate. **Not a defect** -- the
output is verified correct -- but recorded in `todo.md` as a thing to understand
before anyone edits the pose path, rather than papered over with a plausible
story.

### Verification
- 484/484 in debug, release and ASan. **No C++ changed** in this chunk (tools
  only), so the previous TSan run covers identical code.
- Sonar gate OK, duplication 0.0%.
- `t-pose` note: `tallest_extent` for a T-pose is the arm SPAN (1.6863), not the
  height (1.6630) -- recorded in the expectation, because reading it as a height
  looks like a 1.4% error that is not there.

### Files changed
`tools/blender_validate.py`, `tools/blender_check.py`,
`tools/run_blender_validation.sh`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
Nothing in M1-M8 is now actionable without the owner. See the report.

---

## 2026-09-02 04:11:16 — Session 120 · **the validator and the DCC disagreed, and the DCC was wrong**

### The chunk
UsdSkel `BlendShape`. The last format carrying no expressions, and unlike FBX
last session this was a real writer gap rather than a missing struct field.

### The finding, and why probing first mattered
Before generating anything I hand-wrote a three-vertex stage and ran it past
two independent tools. They disagreed:

* **`usdchecker` REJECTS** `SkelBindingAPI` on a prim not rooted at a
  `SkelRoot` -- *"as required by the UsdSkel schema"* -- **even when there is no
  skeleton at all and only blend shapes**.
* **Blender imports that same invalid stage happily**, shape key and all.

Our writer emitted `SkelRoot` only when a skin was present. Had I validated with
Blender alone -- the habit this repo has built -- I would have shipped a stage
Apple's own validator rejects, and every check would have been green. This is
the **second** time Blender's lenience has hidden a defect; the first was loose
vertices sailing through its OBJ importer.

The root is now a `SkelRoot` whenever anything is bound, skeleton or not.

### What shipped
- `UsdSceneEntry::morphTargets`; `BlendShape` child prims with
  `uniform token[] skel:blendShapes` and `rel skel:blendShapeTargets`.
- **Sparse** via `pointIndices` -- the same reason glTF uses sparse accessors. A
  dense `offsets` array is equally valid USD and animates identically, and would
  make a 34-target body 34 copies of the vertex buffer.
- Same one-entry rule as the skin and the other two writers; a second set and a
  mismatched delta count are both refused (`InvalidMorphTarget`).
- Prim names take the one substitution USD forces: an identifier cannot hold a
  hyphen, so `eye-left-closure` becomes `eye_left_closure`.
- **OBJ is now the only format that says it drops blendshapes.**

### A gap my own mutation testing exposed
Mutating `needsSkelRoot` back to `skin != nullptr` was caught by the unit test
but **NOT** by the app-level `usdchecker` ctest -- because the application always
builds a rig, so its USD export has a skin and is a SkelRoot regardless. The
automated check did not cover the case the finding was about.

Closed by giving `mh_export_fixture` an `expressions.usda` with **no skeleton**
and having `run_blender_validation.sh` run `usdchecker` on it. Verified by
re-applying the mutation: the harness now fails.

### Three counts that looked wrong and were not
Blender read `eye_left_closure` as moving **115** vertices where the fixture's
GLB says **186**. I measured instead of assuming: the app's own GLB reports
**115** too. 186 is the uncompacted fixture mesh; 115 is the compacted export.
All three formats agree exactly, on all 34 keys, once the `-`/`_` substitution
is applied -- which is how the expectation is now written, derived from one
table rather than a third copy of 34 literals.

### Also
I made the same unit-scale mistake as last session in a first draft of the USD
test -- asserting an unscaled literal offset. Caught by the test, fixed by
asserting the SHAPE (exactly one offset tuple, exact `pointIndices`) rather than
a scaled value that would pin the default unit. And I hit the file's own
documented trap: searching for `']'` from `"uniform vector3f[] offsets = ["`
finds the type's bracket, not the array's.

### Verification
- `usdchecker`: **Success** on `.usda` and `.usdz`, and `usdchecker --arkit`
  passes on the `.usdz` -- Apple's own profile. Now a ctest
  (`app_blendshapes_usda_valid`, guarded on `find_program`).
- Blender: **10/10 exports agree** (was 9/9). 34 keys on the body, **0 on the
  worn eyes**, all 34 deforming.
- Three mutations caught: Xform root; dense offsets; omitted
  `blendShapeTargets`.
- 484/484 in debug, release and ASan. Benchmarks unchanged.
- Sonar gate OK, duplication 0.0%.
- Cost: 0.21 s and 3.2 MB (text; the GLB is 2.3 MB).

### Files changed
`include/makehuman/io/UsdWriter.h`, `src/io/UsdWriter.cpp`, `src/app/main.cpp`,
`tests/golden/test_usd_writer.cpp`, `tests/CMakeLists.txt`,
`tests/mh_export_fixture.cpp`, `tools/blender_check.py`,
`tools/run_blender_validation.sh`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
Blendshapes are complete across every format that can carry them. Everything
still open in M8 needs the owner: skin texture files, the `mixamorig:*` naming
decision, the `--skin` -> `--litsphere` rename, and the two accessibility items
(`reduceMotion()`'s true branch and the duplicate readout announcement), both of
which need hardware this session cannot drive.

---

## 2026-09-02 03:56:04 — Session 119 · **the dressed character was the one that lost its expressions**

### The chunk
Blendshapes through FBX and Collada. Session 118 shipped `--blendshapes` for
GLB only, because `GltfSceneEntry` had a `morphTargets` field and
`io::SceneEntry` did not.

### The gap was worse than "FBX is missing a feature"
The single-mesh `exportScene` overload has taken morph targets all along
(`SceneIO.h:142`), and `mh_export_fixture` has proved since session 029 that
assimp's FBX writer carries them and Blender reads them back. But `main.cpp`
routes through `sceneEntries()` the moment the character wears anything -- and
the default character wears eyes. So the working single-mesh path was
unreachable in practice: every FBX the app could actually produce was
expressionless.

Same shape as the skin gap fixed earlier: `writeUsdaScene` took a skin all
along and `main.cpp` passed none.

### What shipped
- `SceneEntry::morphTargets`, and the `aiAnimMesh` writer extracted out of the
  single-mesh path into `attachMorphs` so both share one implementation rather
  than diverging.
- One entry may carry morphs, exactly as one may carry the skin -- a worn proxy
  is re-fitted to the body, not blended. A second set is refused, and so is a
  delta array not parallel to its entry's mesh (the same check the single-mesh
  path already made).
- `main.cpp` names the formats that genuinely cannot carry them. USD is now the
  only one: `writeUsdaScene` takes no morphs, and UsdSkel `BlendShape` is a real
  writer gap, not plumbing.

### Two things the tests caught that I had wrong
1. **My arithmetic, not the code.** I asserted the shape key sits `base.y + 2`
   above the base and got 20. `SceneExportOptions::unit` defaults to
   **centimetres**, so everything is scaled x10 and the right answer is
   `base.y + 2*scale`. The test now DERIVES scale from the exported quad's known
   2-unit width, so it survives a change to the default instead of pinning it.
2. **assimp round-trips the channel name as `"smile.smile"`** -- FBX names the
   channel after the deformer holding it. Matched as a prefix, the way the mesh
   names already are, rather than pinning assimp's naming as if it were ours.

### Read the output, not the tail of it
I concluded `--export x.usda --blendshapes` printed no warning. It did: stderr
is unbuffered, so the line appeared near the TOP of the interleaved output and
`tail -5` cut it. **Third time this session** that truncating output produced a
false conclusion (sessions 106 and 111 have the other two). Rule for next time:
`grep` for the line, never `tail` for it.

### Verification
- Blender, third-party: **9/9 exports agree** (was 8/8). `expressions.fbx`
  matches `expressions.glb` **key-for-key on all 34** -- two independent writers
  agreeing is stronger than either matching an expectation, which is why the
  expectation table is now defined ONCE and shared by both entries rather than
  written twice where it could drift.
- Body carries 34 deforming keys; the worn eyes carry **none**.
- Three mutations, all caught: scene never attaches morphs; morphs attached to
  every entry from `entries[0]`; delta-count validation removed.
- 479/479 in debug, release, ASan and TSan. Benchmarks unchanged.
- SonarQube: the first scan **failed** on `new_duplicated_lines_density` at
  **35%** -- the two 34-key expectation dicts were byte-identical. A real
  finding, and the fix made the intent explicit rather than suppressing it:
  one table, referenced twice, because the two formats MUST agree. Now 0.0%,
  gate OK.
- CI green at `62a01c76`.

### Files changed
`include/makehuman/io/SceneIO.h`, `src/io/SceneIO.cpp`, `src/app/main.cpp`,
`tests/golden/test_scene_io.cpp`, `tests/CMakeLists.txt`,
`tests/mh_export_fixture.cpp`, `tools/blender_check.py`,
`tools/run_blender_validation.sh`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
USD blendshapes are a real writer gap (UsdSkel `BlendShape` +
`skel:blendShapeTargets`), not plumbing. Everything else open in M8 needs the
owner: skin textures, the `mixamorig:*` naming decision, the `--skin` ->
`--litsphere` rename, and the two accessibility items.

---

## 2026-09-02 03:29:12 — Session 118 · **the app exported no expressions, and 102 files that are really 34**

### The chunk
`--blendshapes`. `writeGlb` has taken morph targets since session 029 and
`main.cpp` passed none -- the same built-and-never-wired pattern as
`visibleVertexMask`, `writeUsdzScene`, `importScene` and `findByUuid`.

### The part that was a decision, and why it wasn't
`todo.md` parked this as "an open question the owner may want to settle": are
there 34 blendshapes or 102? It is not a preference. `data/targets/expression/
units/` holds 102 `.target` files as **34 units x 3 ethnicities** (african,
asian, caucasian), confirmed three ways -- `ls` per directory (34/34/34), and
`tests/golden/target_groups.txt`, which already recorded **34
`expression-units-*` groups with 3 components each**, written long before this
session. A character IS a blend of the three, so exporting 102 would hand a DCC
three near-duplicate keys per expression, none of them the character's shape.

Each key is therefore

    delta = SUM over race of factors.value(race) * delta(race)

which is `targetWeight`'s ordinary rule (`humanmodifier.py:644-652`) applied to
a group whose only macro dependency is race. The three weights sum to 1, so it
is a convex combination and needs no renormalisation. No new rule was invented.

### What shipped
- `core::buildExpressionBlendshapes` (`include/makehuman/core/Blendshape.h`) --
  group discovery is data-driven, so a unit added to `data/` appears with no
  code change and the count is asserted against the golden fixture, not
  hardcoded.
- `io::compactDeltas`. `Compact.h` had explicitly deferred it: *"nothing exports
  morph targets yet, so there is no compactDeltas here. Adding one now would be
  another capability built and never wired."* It now has a caller.
- **GLB only.** `GltfSceneEntry` has a `morphTargets` field; `io::SceneEntry`
  does not. FBX/USD/OBJ print what they are dropping rather than writing an
  expressionless mesh in silence -- the pattern already used for the rig.
- **Refused with `--subdivided`**, and not only by the caller: a subdivided vmap
  names vertices past the base mesh's count, which `expandTargetToRenderVertices`
  rejects (`Target.cpp:203`), so the function returns empty rather than deltas
  on the wrong vertices. Tested directly, not just documented.

### Two tests that passed on their own mutation
Both found by mutating, not by reading.

1. **An identity remap in `compactDeltas` was undetectable on the shipped
   mesh.** Every vertex compaction drops sits at the END of the base mesh's
   buffer, so `remap` IS the identity on survivors and `out[old]` equals
   `out[new]` for every one of them. Even distinct per-vertex deltas could not
   separate them. Caught only by the four-vertex synthetic mesh in
   `test_compact.cpp` that drops from the MIDDLE. The real-data test that
   proved nothing was deleted rather than kept for comfort.
2. **A `std::ranges::sort` I added was dead code** -- `groupNames()` already
   sorts (`TargetIndex.cpp:121`). Worse, the comment I wrote to justify it said
   groupNames walks an unordered_map unsorted, which is false. Both removed; the
   `is_sorted` assertion stays, since the guarantee matters wherever it comes
   from.

A third mutation (dropping `compactDeltas`'s bounds clamp) is invisible in
debug and caught by **ASan** as a heap-buffer-overflow. Recorded because it
means the clamp's test only has teeth under the sanitizer preset.

### The amplitude scare, resolved by measurement
Blender read `eye-left-closure` in the exported GLB as **0.0102** at 1 unit =
1 m, where the `.target` files say **0.15915 dm**. That is 64%, and it looked
like a silent loss -- 34 correctly-named keys that barely move.

It is not. The builder produces exactly 0.15915 dm (asserted). The gap is the
body face mask: the largest-moving eyelid vertices sit on helper geometry the
mask hides, so compaction drops them with the faces that named them. The
**visible** mesh's maximum is 0.102 dm -- exactly Blender's number x10. Both are
now asserted, so a real loss would fail instead of shipping quietly.

### Verification
- Blender, third-party: **8/8 exports agree** (was 7/7). `expressions.glb` is
  new and pinned per key -- all 34 present, all 34 deforming, left/right pairs
  agreeing where the data is symmetric (186/186) and differing where it is not
  (72/79), so a symmetry bug shows rather than hiding behind a total.
- 474/474 in debug, release and ASan. TSan 474/474 -- run before the
  `mh_export_fixture` edit, which touches a standalone tool binary that is not
  in ctest and no library source.
- Benchmarks unchanged (4.2x-59.5x over the Python baseline).
- `--blendshapes` costs **+10 ms and +210 KB**. Dense would be 5.9 MB; the
  sparse accessors from session 029 are what make 34 targets affordable.
- SonarQube gate **OK**. CI green at `c2fae127`.

### Files changed
`include/makehuman/core/Blendshape.h`, `src/core/Blendshape.cpp`,
`include/makehuman/io/Compact.h`, `src/io/Compact.cpp`, `src/app/main.cpp`,
`src/core/CMakeLists.txt`, `tests/golden/test_blendshapes.cpp`,
`tests/unit/test_compact.cpp`, `tests/mh_export_fixture.cpp`,
`tests/CMakeLists.txt`, `tools/blender_check.py`,
`tools/run_blender_validation.sh`, `memory/todo.md`,
`memory/handover_session.md`.

### Next
Blendshapes through FBX and USD is now a `SceneEntry` field plus plumbing, not
new capability -- `exportScene`'s single-mesh overload already takes them and
the fixture proves assimp carries them. Everything else open in M8 needs the
owner.

---

## 2026-09-02 02:41:59 — Session 117 · **the slider announced 500 for a value of 0.00**

### The chunk
The last M8 accessibility item I could act on without the owner's hardware.

### Measured before writing any fix
`memory/todo.md` recorded one known gap here (the readout label announcing the
value twice). Rather than trust the note, I built the shipped panel in a test
and printed what Qt actually hands the Cocoa accessibility bridge:

```
slider name : "Neck circum, Neck"
slider value: "500"        <- the raw TICK
readout name: "0.00"
readout text: "0.00"
```

The recorded gap was real, but it was the *smaller* of two. The slider's
announced **value** was `500` -- Qt's `QAccessibleAbstractSlider` default is
`QString::number(value())`, and our sliders run 0..1000 ticks regardless of the
modifier's own range. A screen-reader user heard a number with no meaning, one
that contradicted the readout label sitting right beside it.

### What shipped
`SliderAccessible : QAccessibleWidget` in `src/ui/ModifierPanel.cpp`, installed
once per process with `QAccessible::installFactory`. It overrides
`text(QAccessible::Value)` to map tick -> modifier value and format it with the
same two decimals the readout uses, so what is heard and what is seen are the
same number.

The factory claims only sliders carrying the `mh.sliderMin` / `mh.sliderMax`
dynamic properties the panel sets; every other QSlider in the app falls through
to Qt's own interface.

### A finding from my own review
My first version kept a file-static `std::map<const QObject*, SliderSpec>` plus
a `QObject::destroyed` connection to evict entries. `fromTick` uses only
`minValue` and `maxValue`, so two Qt dynamic properties on the widget replace
all of it -- and delete the hazard rather than guarding it: properties die with
the widget, so no stale pointer-keyed entry can hand a dangling range to the
next QSlider allocated at the same address. Net **-31 lines** against my own
first draft.

I also deleted a second test I had written that asserted the readout label
still announces itself. It characterised Qt's own default QLabel behaviour and
would have failed the day someone improved it -- a test that blocks a fix.

### What I did NOT ship, and why
Suppressing the duplicate announcement needs the label's accessible interface
to report itself invisible. **Whether macOS then drops it cannot be established
from the Qt API** -- it needs VoiceOver on a real device. Left recorded in
`todo.md` rather than shipped unverifiable. The same reason still parks
`reduceMotion()`'s true branch.

### Also fixed
`tests/CMakeLists.txt`: the `render` test had two `set_tests_properties(...
ENVIRONMENT ...)` calls. The second **replaces** the property rather than adding
to it, so the first was silently dropped -- self-inflicted in `1faca7ab`, which
had thereby lost the `TMPDIR` isolation that commit existed to add. One call now.

### Verification
- Two mutations, both caught (3 assertions each): factory returns `nullptr`;
  announce the raw tick.
- 465/465 in **all four** presets -- debug, release, ASan, TSan (TSan alone,
  after all edits, per the lesson in Session 112).
- Benchmarks unchanged: 4.2x-59.5x over the Python baseline. UI-only chunk.
- SonarQube quality gate **OK** (`new_violations` 0, duplication 0.0%).
- CI green at `1faca7ab` before this commit.

### Files changed
`src/ui/ModifierPanel.cpp`, `tests/ui/test_ui.cpp`, `tests/CMakeLists.txt`,
`memory/todo.md`, `memory/handover_session.md`.

### Next
M8 has no remaining item that does not need the owner: skin texture files, the
`mixamorig:*` naming decision, the blendshape set to export, the `--skin` ->
`--litsphere` rename, and the two accessibility items above all wait on them.

---

## 2026-09-02 01:38:58 — Session 116 · **a fix I could not prove, stated as one**

### The chunk
Each preset's tests get their own temp directory.

### The hazard
The suite writes ~80 fixed names under `temp_directory_path()` --
`mh_units_agree.obj`, `mh_fuzz_obj`, `mh_glb_*.glb` -- through a dozen little
per-file helpers. Two ctest runs of the same binary share them, which is how an
ASan run and a background TSan run collided two sessions ago.

Renaming 80 call sites is the large fix. `TMPDIR` is the small one:
`std::filesystem::temp_directory_path()` honours it, so one
`catch_discover_tests(... ENVIRONMENT "TMPDIR=...")` per target makes every
existing name unique per preset without touching a test.

### What I verified, and what I did not
**Verified, on the mechanism:** after a full debug run the system temp's `mh_*`
count is unchanged (**15 before, 15 after** -- those are leftovers from earlier
sessions) while **11** files land in `build/macos-arm64-debug/tests/tmp/`. The
suite no longer writes to the shared location, so two presets cannot collide.

**Not verified:** the flake itself. I ran the offending test in both presets
concurrently, five rounds, with the fix — all passed. Then I stashed the fix and
ran the same five rounds as a control — **those all passed too.** The collision
needs a wider timing window than two short filtered runs give, so I have not
reproduced it on demand.

So this rests on the identified mechanism, not on a demonstrated failure, and
the writeup says so. Running the control is what turned "this fixes the flake"
into "this removes the shared state the flake needed" — the first claim would
have been unearned.

### Verification
ctest **465/465** in debug, release, ASan and TSan — TSan run alone. Format clean. SonarQube gate OK.

---

## 2026-08-31 22:57:12 — Session 075 · **mixPoses, and a mutation that mutated nothing**

### The chunk
`rig::mixPoses(base, overlay, bones)`: copy `base`, take `overlay`'s transforms
for the listed bones. This is what layers a facial expression onto a posed body
(`shared/animation.py:449-467`).

Both of the reference's refusals are kept rather than softened:
- differing bone counts -> `FrameCountMismatch`. Mixing poses built for two
  different rigs would otherwise produce a plausible-looking wrong body.
- an index past the end -> `Malformed`. Silently skipping it would drop part of
  the expression.

**No parity fixture, by explicit exclusion.** This is index replacement with no
numerical content — nothing for float32 to round differently, and a captured
`.bin` would only re-assert that a copy copies. Five property tests pin what can
actually break, including a real case: a face expression layered onto the T-pose,
checking all 163 bones took the right source.

### I broke my own mutation rule, and it cost a false negative
The first mutation "passed" — all 5 tests green. That was wrong. `clang-format`
had reflowed

    for (const size_t b : bones) out[b] = overlay[b];

onto two lines, so my one-line search string matched **nothing** and
`str.replace` silently did nothing. I had not asserted the match.

This is *precisely* the failure documented earlier this session, with the rule
already written down: **always `assert s.count(old) == 1` before writing.** A
mutation that does not change the artifact is not a mutation, and its green run
is worthless.

Re-run correctly, the mutation fails **3 of 5 cases, 922 assertions**. The tests
do discriminate.

### Two more stale entries closed
- Pose units are already wired: `makePoseUnits` maps all 60 `framemapping`
  names onto BVH frames; `test_poseunits_parity.cpp:79` pins them in order.
- The parity fixtures said to be missing all exist:
  `skeleton/{rest_global,rest_relative}.bin`,
  `skinning/{skinned,mat_pose,pose_verts}.bin`,
  `poseunits/{blended,blended_reversed}.bin`.

M5's remaining open items are now: SonarQube (owner), `.mhpose` (no asset),
`.mhupb`, BVH export, pose-library UI, and two that belong to M6
(skin normals/tangents, GPU LBS).

### CI
`5eb5cd60` green — the clang-format failure I introduced is cleared. The
repo-wide format check now runs in every sweep.

### Verification
ctest **391/391** in debug, release, ASan and TSan; format clean.

### Still blocked on the owner
**SonarQube credentials.**

---

## 2026-08-31 22:39:00 — Session 074 · **body pose units do not fit our rig; and I broke CI**

### CI failure on `1e09e93d`, and it was mine
`clang-format` failed. Every other job passed (debug, release, ASan, TSan,
benchmark, inventories, licence, charconv).

Cause: in the previous chunk's final sweep I ran `clang-format -i` on
`src/app/main.cpp` **only**, and dropped the repo-wide
`find … | xargs clang-format --dry-run --Werror` that earlier chunks ran. The
test file I had appended to was never checked. It reproduces locally in one
command — I simply did not run it.

**Rule**: the sweep is the repo-wide `--dry-run --Werror` check, never
`-i` on the files I happen to remember touching. CI did its job; my local gate
was the one that regressed.

### The chunk: body pose units
Investigated before building, and the investigation was the deliverable.

`data/poseunits/body-poseunits.json` is 61 poses of bone -> `[w,x,y,z]`
quaternion (format confirmed, no BVH frames). But it targets a **richer,
differently-named skeleton**. Against **both** shipped rigs:

| | |
|---|---|
| fully resolvable | **29 of 61** |
| partially resolvable — silently do less than intended | 24 |
| resolve to **nothing at all** | **8** |

Dead: `TorsoRight`, `UpperLegForwardLeft`, `LowerLegBendLeft1/2`, `FootDownLeft`,
`FootUpLeft`, `Finger1CloseLeft`, `Finger2CloseLeft`.

24 referenced bones are absent from both rigs, and many are a naming
*generation* difference rather than missing joints — `spine1..4` for
`spine01..03`, `neck` for `neck01..03`, `shoulder.L` for `shoulder01.L`,
`upperleg.L` for `upperleg01/02.L` — beside ones we genuinely lack
(`collisionArm*`, `heel.L`, `metatarsal1..5`, `platysma03/06`, `scapula.L`).

**So a consumer needs an explicit bone table**, exactly as the Mixamo retarget
did. Name matching would silently drop a third of the data — a foot pose that
does nothing, with no error.

**An authoring error in the reference asset**: `UpperArmUpLeft1` and
`UpperArmUpLeft2` drive `oris01`/`oris02` — **mouth** bones. Raising the left arm
must not move the lips. Pinned so it is never mistaken for ours.

`tools/audit_poseunits.py` measures it and gates it in CI; both assertions
mutation-tested (rc=1 perturbed, rc=0 restored). **Loader deliberately not
built** — before the bone table exists it would ship poses that do nothing.
Recorded in `memory/project_context.md` §8.0.

### Two more stale entries closed
- Euler helpers **are** wired into BVH: `src/io/BvhReader.cpp:224`
  (`eulerOrderFromString`) and `:336` (`eulerMatrix`).
- `.mhpose`: **no asset exists anywhere in the repo** (`find . -name '*.mhpose'`
  is empty); the format appears only in reference *plugins*. Unverifiable, so
  unbuilt — the same call as the proxy shear forms.

### A measurement trap I nearly fell into again
Checking the audit's exit code through `| tail -3` reported `rc=0` for a run
that had actually failed — `$?` was `tail`'s. zsh has no `PIPESTATUS`. Captured
the code directly instead.

### Verification
ctest **386/386** in debug, release, ASan and TSan; repo-wide clang-format clean.

### Still blocked on the owner
**SonarQube credentials.**

---

## 2026-08-31 22:14:51 — Session 073 · **a silent 19% fidelity loss, now spoken**

### The chunk
"Influence clamping surfaced in the glTF exporter". Before building anything I
measured whether it mattered — the same question that killed the last two
micro-optimisations. This time it did.

`compile()` truncates each vertex to N influences and renormalises. It computed
how much it dropped and **threw that away**. On the shipped rig, compiling to 4
— which is exactly what glTF's `JOINTS_0`/`WEIGHTS_0` allow — clamps:

| | |
|---|---|
| vertices clamped | **3,665 of 19,158 (19.1%)** |
| worst influence count | **12** |

Nearly a fifth of the mesh lost influences with nothing said.
`CompiledWeights` now reports `clampedVertices` and `maxInfluences`, and the app
prints `clamped 3665 of 19158 vertices to 4 influences (rig uses up to 12)`.

The C++ count is cross-checked against an independent count over the raw `.mhw`
in Python, so two implementations agree rather than one recording itself.

### The follow-on question, measured rather than assumed
The reference truncates **nothing** (`shared/skeleton.py:616` iterates every
bone's mapping), so clamping to 4 is a deviation — and the CPU path has no
reason to inherit a GPU/glTF limit. So what does it actually cost?

Under a hard 60° bend of both elbows and both knees: **one vertex of 19,158**
moves more than 10 µm, that one by 2.0 mm. Full 12 influences would cost
0.127 → 0.238 ms.

So: reported, not fixed. Adding API to recover 2 mm on a single vertex is not
worth it, and the report now makes the situation visible instead of silent. The
reopen threshold is recorded.

### Tests
Three properties, each able to fail: the exact counts (3,665 / 12); compiling at
12 clamps **zero** (so the two numbers are not constants); compiling at 2 clamps
strictly more. Plus every weighted vertex still sums to 1.0 after truncation —
without renormalisation the whole body drifts toward the origin when posed.

### Self-review caught my own mess
Moving the `compile()` call orphaned the comment "The file's rotations are in
model space", leaving it attached to the wrong statement. Restored.

### CI
`6f98b4ad` **fully green, all 9 jobs** including ASan and TSan. History is
linear, so this also covers the `--rig` commit whose own run I cancelled.

### Verification
ctest **386/386** in debug, release, ASan and TSan.

### Still blocked on the owner
**SonarQube credentials.**

---

## 2026-08-31 21:56:53 — Session 072 · **the retarget table, and it is not lossy**

### Two stale entries closed first
`memory/todo.md` listed both as open. Both were already implemented — reality
beat memory:
- `matPoseVerts = matPoseGlobal · inv(matRestGlobal)` — `src/rig/Skinning.cpp:27`.
- Joint positions from vertex clouds — `src/rig/Skeleton.cpp:38`, the mean of
  each joint's cloud (`skeleton.py:428-434`), benchmarked at 0.01 ms.

### The chunk: `data/rigs/mixamo_retarget.json`
The mapping existed only as Python source inside a proof tool. It emitted
nothing, so nothing could consume it. Now emitted as data by
`tools/mixamo_mapping.py --emit` — one source of truth, no second copy to drift.

### The item's premise turned out to be wrong
It read "**Lossy by construction**". That was true against MakeHuman's 163. It
is **not** true against the superset: the 16 Mixamo bones `MAPPING` recorded as
`None` — meaning "the superset must add this" — are **exactly** the 16 the
superset added. Checked as a set equality, not by eye. So the table is **TOTAL**:
all 65 Mixamo bones have a counterpart.

| Mixamo | superset |
|---|---|
| `Hips` | `hips` (*not* `root` — the legs and spine meet at root's tail, 0.92 dm away) |
| `Left/RightToeBase` | `ball.L/R` |
| `Left/RightToe_End` | `toe_end.L/R` |
| `HeadTop_End` | `HeadTop_End` |
| `*Hand{Thumb..Pinky}4` | `finger1..5-4.{L,R}` |

The finger correspondence is **not** my guess: `MAPPING` already sends
`LeftHandThumb1..3` to `finger1-1..3.L`, so the tip continues the same chain.

### Not a name-matching heuristic — which the item warned against
Every target must **descend from its Mixamo parent's target** in the 179-bone
hierarchy. **0 violations** across all 64 parented bones. Plus injectivity and
existence-in-rig.

### Both gates mutation-tested, not assumed
- Editing the committed file → `is stale`.
- Reversing the finger tips (thumb onto the pinky chain) → caught by ancestry,
  naming each wrong bone. A pure name match would have accepted it.

This is the lesson from session 070 applied: the superset rig had a staleness
gate and no quality gate. This artifact gets both.

### Deliberately NOT built
No C++ loader. Nothing retargets yet, and an API with no caller is the
speculative kind. The table is data; the loader arrives with its first consumer.

### Verification
`tools/mixamo_mapping.py` and `--check` both exit 0. ctest **384/384** debug and
release (no C++ changed). CI gate added to the `inventories` job.

### PROCESS FAILURE this session — I cancelled my own CI, again
`ci.yml` sets `cancel-in-progress: true`. I pushed `6f98b4ad` while `4d18a6c0`
(`--rig`) was still queued, which **killed its ASan and TSan jobs**. The watch
resolved to `cancelled`, not green.

This is the *same* mistake the rule adopted after commit `7771a0a6` exists to
prevent: **wait for CI as a gate, not a notification.** Knowing the rule was not
enough; I started the next chunk while the previous push was in flight.

Mitigation: history is linear, so `6f98b4ad` contains the `--rig` changes and a
full pass there covers that code. But `--rig` never got its own sanitiser run.

**Concrete rule, not a resolution**: do not `git push` while `gh run list`
shows a run for the previous commit still `queued` or `in_progress`.

### Benchmark note
CI measures `loadWeights` at **39.27 ms** against 16-18 ms locally — CI runners
are simply slower. That confirms the local 15.55 -> 17 drift was machine noise,
not a regression, and also that **CI's figure is not comparable to the local
baseline**. The benchmark job passed and was not among the cancelled ones.

### Still blocked on the owner
**SonarQube credentials.**

---

## 2026-08-31 21:50:41 — Session 071 · **--rig: making the superset rig reachable**

### The chunk
Last session found the 179-bone superset rig could not skin. The reason nothing
had noticed was filed at the end: `src/app/main.cpp` hard-coded
`default.mhskel`, so the application could not load any other rig. Fixed.

`--rig default|mixamo_superset|<path>` now selects the skeleton the app poses
and skins with, taking weights from `<stem>_weights.mhw` beside it. Set once
from the CLI, mirroring the existing `setDataRoot` pattern, so the four
`loadPoseRig` call sites — two of them UI callbacks — need no new parameter.

An unknown rig **fails and lists what is installed**:
`unknown --rig nope; available: default, mixamo_superset`. It exits 1 and writes
no file (both verified), rather than silently falling back to `default` and
posing the wrong skeleton.

### Giving the tests teeth
The app announces which rig it posed with, and the tests assert on that string:
`rig mixamo_superset (179 bones)`. An export-only check would have passed
throughout the entire period the superset was unreachable and unusable — which
is precisely the failure mode being guarded against.

### A bug in my own first version
Reviewing the diff, `--rig ../rigs/default` — a path *without* the `.mhskel`
extension — resolved to a filename with no extension and failed with a
confusing "unknown --rig". Stem and path cases are now reduced to one
extension-less base, so `--rig /x/foo.mhskel`, `--rig /x/foo` and
`--rig default` all behave identically. The fix is also shorter than what it
replaced, and `app_rig_path` covers it.

### Two honest notes
- I tried to demonstrate the path bug empirically with `git stash`, but that
  reverted `--rig` wholesale rather than just the fix, and my grep was
  case-sensitive so Qt's "Unknown option" did not match. The experiment proved
  nothing. Both forms are verified working *now*; the necessity of the fix rests
  on reading the old code path, not on a captured before/after.
- `loadWeights` benchmarks at **16.6-18.0 ms** against 15.55 ms recorded
  earlier, ~8% over. Nothing in this chunk touches `.mhw` parsing and
  `buildRestMatrices` is unchanged at 0.02 ms, so this is most likely machine
  load — four full ctest sweeps and several Blender renders ran before it.
  CI's own benchmark job is the independent reading. **Not claimed as within
  5%.**

### Scope
`--rig` selects the skeleton used to pose and skin. The app's multi-mesh export
path carries no bones by design (`SceneIO.h:116`), so exported bone data is
unaffected.

### Verification
ctest **384/384** in debug, release, ASan and TSan. CI green on `22e43d01`.

### Still blocked on the owner
**SonarQube credentials.**

---

## 2026-08-31 21:26:26 — Session 070 · **the superset rig could not skin at all**

### What I set out to do
Close the one M5 item I had repeatedly reported as "blocked on the owner": the
ball-crease visual check. It was never really blocked — the note itself said
"Blender is available". Keeping the question rather than answering it was the
error.

### What trying to do it found
The check needs a posed superset rig. It could not be posed:
`buildRestMatrices()` **failed** on `mixamo_superset.mhskel`.

13 of the 179 bones are tip markers whose head and tail resolve to the same
joint, and the loop rejected the **whole skeleton** on the first zero-length
bone. So the rig shipped, passed a CI staleness gate, and was **unusable for
skinning** — one bad bone taking the other 178 with it. Nothing noticed because
nothing had ever tried to skin with it.

The 13 are legitimate, not junk to delete — I checked before reaching for the
delete key. Mixamo's own 65 bones include `HeadTop_End`, `Left/RightToe_End`
**and a 4th segment on every finger** (`LeftHandIndex4` and friends). They sit
at their parent's tail and deform nothing (0 weighted vertices on all 13), so
head == tail is simply how "the tip is here" is expressed.

Fix: a tip marker inherits its parent's basis — the convention Blender applies
to leaf bones — keeping its own head as the translation. A length-less root
stays an error.

### The crease, once it could be measured
Rather than squint at a render, compare like with like: same mesh, same LBS,
same 45° bend, our generated crease against a joint MakeHuman itself ships.

| region | faces | min area ratio | inverted normals |
|---|---|---|---|
| `ball.L/R` (generated) | 792 | 0.364 | **7 (0.88%)** |
| `lowerarm01.L/R` (shipped) | 1152 | 0.023 | **22 (1.91%)** |

Half the flip rate and 16x less area collapse than the shipped elbow, and at 25°
(a walking toe-off) **nothing inverts at all**. The residual flips at 45° are
inherent to linear blend skinning, not to these weights. The gate pins the
comparison, not an absolute, so it stays self-calibrating.

### The lesson worth keeping
The superset rig had a CI gate that asked **"is this file current with its
generator?"** and never **"does it work?"**. A staleness gate is not a quality
gate. Both new tests ask the second question.

### Also worth knowing
`src/app/main.cpp:143` hard-codes `default.mhskel`, so the superset rig is
unreachable from the application. That is *why* nothing had exercised it. Filed.

### Blender notes (cost me several attempts)
Renders came back black, then white. The mesh imported fine all along (19,158
verts, Y **is** up — the importer did not rotate it); the fault was my camera.
A `TRACK_TO` constraint did not give a usable view in background mode;
`view3d.camera_to_view_selected()` with an explicit rotation did. Two renders
having identical file sizes was coincidence, not a bug — checked by hash.

Ultimately the numeric measure answered the question better than any render.

### Verification
ctest **381/381** in debug, release, ASan and TSan. CI green on `82ffcccb`.

### Still blocked on the owner
**SonarQube credentials** — genuinely blocked, unlike the crease check was.

---

## 2026-08-31 21:03:04 — Session 069 · **proxy parity at six body shapes, and a fixture that could not fail**

### The chunk
M4's last testable item: eye-proxy fit parity at "several body shapes". There
were two (neutral, mixed). There are now **six**, and **18 comparisons**
(3 proxies x 6 bodies) match the Python oracle within 1e-5.

### Choosing the shapes by what the code reads, not by what looks different
`fitProxy`'s only body-dependent term is the TMatrix scale, and for the eye
proxies that scale is read off **head** vertices:
`x_scale 5399 11998` / `y_scale 791 881` / `z_scale 962 5320`.
So the shapes that matter are the ones that move head proportions:
- `extreme_min` — Age 0.0, an infant: the largest head-to-body ratio the model
  produces;
- `extreme_max` — every macro at 1.0;
- `head_small` / `head_large` — `head-scale-depth|horiz|vert` driven to +/-1,
  straight at the three axes the matrix divides by.

**They earn their place, measured**: neutral + mixed spanned a y-scale of only
0.851..1.034. The six now span **0.417..1.168**. The per-body diagonals are
captured in `tests/golden/proxy/tmatrix_scales.json` precisely so that a body
which exercises nothing new is *visible* rather than silently reassuring.

### The finding: six of the eighteen fixtures cannot fail on scale
Mutation-testing the new fixtures (swap the y and z scale terms in `fitProxy`)
showed the low-poly eye proxy did not notice. The reason is structural: all 96
of its vertices use the **single-index form** — weights (1,0,0) and a **zero
offset** — so `M·d` is zero whatever the TMatrix says.

| comparisons | mutation caught? | worst delta |
|---|---|---|
| 12 high-poly + base | yes, all | 0.00046 .. 0.646 dm |
| 6 low-poly | **none** | 0 |

They are not worthless — they pin the exact-copy path — but that is a different
property from the one they appeared to cover. It is now asserted outright
(`[proxy][exact]`: fitted vertex is **bit-exact** equal to the body vertex, and
the offsets are zero), so the coverage is stated rather than assumed.

Catch2's `INFO` printing only on failure is what surfaced this: the low-poly
rows were simply *absent* from the mutation output.

### Verification
ctest **378/378** in debug, release, ASan and TSan. Fixtures regenerated from
the Python reference (`tools/capture_fixture.py proxy`, 18 entries).

### Still blocked on the owner
**SonarQube credentials** — still the one requested gate that has never run.
Ball-of-foot crease still visually unjudged.

---

## 2026-08-31 20:37:47 — Session 068 · **the ≤50 ms target goal, without the compiled blob**

### The premise was wrong, and checking it changed the work
M3's open item was a compiled target blob (`tools/mhassetc`), justified in this
file by "ASCII parse of all 1,280 targets is 465 ms". That number is real, but
it describes **a path nothing takes**. Targets load lazily — the app says so at
`src/app/main.cpp:687` — and measurement confirmed it:

| case | targets loaded |
|---|---|
| default character | **8** |
| all 291 modifiers driven (worst a character can reach) | **364** (28%) |

Nothing loads 1,280. Building a binary format to fix a 465 ms cost on an
unreachable path would have been machinery for its own sake.

### Where the time actually went
For the worst-case 364-target character, splitting the cost apart:
- read the bytes, **no parsing**: **158.4 ms** (25.1 MB over 364 `open()`s)
- serial parse, warm: 98.1 ms
- **concurrent parse, warm, 10 threads: 20.0 ms**

So it is dominated by *per-file I/O*, not `strtof`. And the files are
independent — which makes concurrency the obvious lever, not a new file format.

### What shipped
`TargetLibrary::prewarm()` loads a set of targets concurrently.
`Human::applyStack` prewarms the cold part of its own stack, so opening a saved
`.mhm` gets it for free. **196 ms -> ~20 ms** end-to-end; 6.09 ms for 364 in the
benchmark. Once cached, the scan allocates nothing (`contains()` first, and
`cold` stays empty).

**The ≤50 ms goal the blob existed to hit is met.** The blob is not built, and
the item records the threshold that should reopen it: the 158 ms `open()` term
is what an `mmap` would really fix, and that term is unmeasured on a genuinely
cold page cache (cannot purge here without sudo).

### ThreadSanitizer, because ASan does not find races
This is the project's first concurrent code and the existing sanitizer gate
could not see the class of bug it introduces. Added `macos-arm64-tsan` +
`MH_ENABLE_TSAN`, and put it in the CI matrix.

**Verified the gate is not theatre** — twice:
- the TSan runtime is actually linked (`otool -L` shows
  `libclang_rt.tsan_osx_dynamic.dylib`);
- writing to the shared cache from the worker threads is reported as a data
  race **42 times**.

The tests were checked the same way: writing results to the wrong slot — the
realistic concurrency bug — fails the byte-identity test, which also proves the
parallel branch is the one under test.

### Verification
ctest **377/377** in debug, release, ASan **and TSan** (373 + 4 new).

### A process note
`json.dumps` rewrote all of `CMakePresets.json` (69 lines churned for a
19-line addition). Reverted and edited textually. Machine-rewriting a
hand-formatted config file is not a free operation.

### Still blocked on the owner
**SonarQube credentials** — the one requested gate that has never been runnable.
Ball-of-foot crease still visually unjudged.

---

## 2026-08-31 20:12:36 — Session 067 · **M2 closed by measurement, not by building**

### The chunk
M2's last two items were micro-optimisations. Both are now closed as
**deliberately not built**, each with the number that justifies it and the
threshold that should reopen it. M2 is complete.

### The measurement that was missing
`RenderMesh::refreshPositions` was benchmarked on the **base** mesh (0.04 ms).
But smoothing is on by default, so the interactive mesh is the **subdivided**
one — 80,252 render verts against 21,833 — and that case had never been
measured. It is the only case the 60 fps target cares about. Now benchmarked:
**0.11 ms**.

Whole slider-drag path on the subdivided mesh:
`rebuildStack` 0.01 + `applyStack` 0.07 + `Subdivider::refresh` 0.48 +
`refreshPositions` 0.11 = **0.67 ms of a 16.7 ms frame**.

### Why dirty-range tracking is not being built
Two independent reasons:
1. **No headroom.** The gather is 0.7% of the frame budget.
2. **The reference's partial path is a pessimisation.** `module3d.py:880`:
   `r_coord[ucoor[vmap]] = coord[vmap][ucoor[vmap]]`. `coord[vmap]` is fancy
   indexing — it materialises the *full* gather, then masks it. The "partial"
   update does everything the full copy does, plus a mask allocation. Porting
   it would be **slower**.

The cost is real and asymmetric: a dirty bitmask threaded through every
mutation site, where one missed mark silently renders stale geometry.

`findFaceGroup` heterogeneous lookup: **zero production callers** (definition,
declaration, one unit test). No measurable gain, and no test could tell the two
spellings apart. Incremental stack application (M3) falls to the same evidence.

### The check I nearly skipped
`refreshPositions` returns early when `matches()` fails — so a wrong benchmark
would have measured *nothing* and reported a fast number. Verified before
trusting it: moving a source vertex 7.5 dm and watching it arrive at the render
vertex proves the gather runs (y 6.814 -> 14.314).

Also caught a noise trap: the first run after linking read **0.12 ms** for the
base case against a 0.04 ms median. Every figure recorded is a median of 5.

### Verification
ctest **373/373** debug, release, ASan. CI green on `c4e6776b`, all 8 jobs.

### Still blocked on the owner
**SonarQube credentials** — `sonar-scanner` 8.1.0 is installed but needs
`SONAR_TOKEN` + `sonar.organization`, or a self-hosted `SONAR_HOST_URL`. This is
the one requested gate that has never been runnable.
Ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 19:49:28 — Session 066 · **refusing beats pretending: proxy shear**

### What the todo asked, and what was actually wrong
The item read "implement `TMatrix` shear forms". Investigating changed the
shape of the job.

The reference parses **nine** shear keys (`shared/proxy.py:476-492`) and, when
shear is present, builds the fit matrix from `matrixFromShear` ->
`affine_matrix_from_points` — a general SVD-based affine solve over point
correspondences, not the diagonal `*_scale` case we implement.

Meanwhile **no shipped asset uses shear**: all four `.mhclo`/`.proxy` files are
scale-only, so a port would be numerically delicate code with nothing to verify
it against.

The real defect was elsewhere and worse: a `.mhclo` containing `shear_x` parsed
**successfully**, the shear was silently dropped, and the proxy fitted with the
wrong transform while the loader reported success. A third-party asset would be
quietly wrong with nothing said.

### What shipped
All nine spellings now return `ProxyErrorKind::Unsupported`, naming the key and
what *is* supported. The `l_`/`r_` variants are covered too — a left/right asset
slipping through would mis-fit exactly as before.

Refusing is worse than supporting and far better than pretending. Implementing
the affine solve stays on the todo **with the citation**, so whoever takes it
knows precisely what it needs.

### Verification
- 9 spellings each refused (one test per spelling, not just the unprefixed set).
- Every shipped proxy still loads; the app still wears eyes.
- ctest **373/373** in debug, release and ASan; format clean.

### Scope note
This chunk is deliberately **smaller than the todo item asked for**. Building an
unverifiable affine solve to tick an item would have been worse than closing the
silent-failure hole and saying plainly what remains.

### Still blocked on the owner
**SonarQube credentials**; the ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 19:25:11 — Session 065 · **the app could not find its own assets once moved**

### What was broken
`MH_DATA_DIR` is a compile-time absolute path into whichever source tree built
the binary. Every one of the app's 13 asset lookups used it directly, so a
copied, installed or bundled binary had **no assets at all** — the directory
does not exist on any other machine. `CMakeLists.txt:27` already called it
"Asset root used by development builds"; nothing acted on that.

This blocks packaging (M11) outright, not merely inconveniently.

### What shipped
`foundation::resolveDataDir(executable, compiledDefault)` — first match wins:

1. `$MH_DATA_DIR` — an explicit override beats every built-in guess.
2. `<exe>/../Resources/data` — inside a macOS `.app`.
3. `<exe>/../share/makehuman/data` — a Unix install prefix.
4. `~/Library/Application Support/MakeHuman/data` — separately installed assets.
5. the compiled default — development builds keep working.

A candidate counts only if it **exists and looks like an asset tree**
(`3dobjs/base.obj`). Accepting a directory with merely the right shape would
trade "no assets" for "no assets, reported later and less clearly" — so an
override pointing nowhere is ignored rather than obeyed, and startup fails with
the path it looked at.

In `main`, a **set-once accessor** rather than a parameter threaded through five
loader signatures: the asset root is a process-wide constant and would never
differ between calls.

### A verification of mine that proved nothing
My first relocation test "passed" — a copied binary exported successfully. It
proved nothing: **the compiled default still exists on this machine**, so the
fallback fired exactly as designed. Correct behaviour, wrong conclusion.

Said so, and tested the override path instead, which *is* isolable here: a
relocated binary loaded a 140 MB tree from a bundle path, and an empty override
fell through rather than being obeyed. The bundle and install branches are
covered by unit tests over synthetic trees; a real `.app` is packaging work.

### Verification
- ctest **371/371** in debug, release and ASan; format clean; licence gate green
  (`DataDir.cpp` lives in Apache-2.0 `foundation` and pulls in nothing AGPL).
- 6 precedence tests, including the two negative cases.

### Still blocked on the owner
**SonarQube credentials**; the ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 19:02:58 — Session 064 · **an ungated Apache module, and five todo entries that lied**

### The real find: `ui` was Apache-2.0 and ungated
The CI licence gate iterated `io foundation render`. The Apache-2.0 modules are
`foundation`, `io`, `render` **and `ui`** — so `ui` was enforced by review only,
which is precisely what the gate exists to replace. `app`, `core` and `rig` are
AGPL, so nothing else was missing.

`ui` happens to be clean today, but "happens to be" is the problem. Extended and
**verified it bites**: an AGPL include added to `src/ui/Theme.cpp` now fails it,
where before it would have passed.

The todo had named this exactly ("extend its module list rather than trusting
review") and it had sat open while I checked `ui` by hand every chunk. My manual
`nm -u` check was doing the gate's job — which is how a gap survives.

### Five todo entries were stale, not open
M3 listed 7 open items; **5 were finished work never ticked**. Each verified
against the code before changing, with a citation now recorded:

| entry | reality |
|---|---|
| target-index tokenisation | `TargetIndex.cpp:9-15`, cited to `targets.py:203`, 4 parity tests |
| `weight = value × Π factors` | `TargetIndex.cpp:125-132`, cited to `humanmodifier.py:644-652` |
| `data/modifiers/*.json` loader | `loadModifiers`, exercised on all three shipped files |
| parity fixtures at ±1 | `modifiers.txt` holds all **291** modifiers, every one checked |
| Apache module gating | done in this chunk |

M3 is now 2 genuinely open items: the compiled target blob, and incremental
stack application.

A todo that overstates what is left is as misleading as one that overstates what
is done — it hides the real remaining work behind noise.

### Verification
- ctest **365/365**; no C++ changed this chunk; all inventory gates green.
- The extended gate demonstrated failing on an injected violation and passing
  once reverted.

### M2 assessment, stated plainly
Its two remaining items are low value and I would rather say so than pad the
loop: dirty-range tracking targets `refreshPositions`, already **0.04 ms**; and
`findFaceGroup`'s string allocation has **no production caller** — tests only.
Optimising either would be speculative.

### Process defect found: pushes were cancelling each other's CI
`ci.yml:11-13` sets `cancel-in-progress: true`. Combined with the loop's fast
cadence, **pushing the next chunk cancels the previous run mid-flight** — commit
`7771a0a6` (masked subdivision) shows `cancelled`, its ASan job never finished.
Seven jobs had passed; ASan was killed by the next push.

The owner's standing requirement is that workflows pass on *every* push, so
"HEAD is green, therefore the tree is fine" is not the standard being asked for,
even though it is true. The cumulative run on `3f81802a` did verify everything.

**Rule adopted: wait for the CI run to finish before pushing the next chunk.**
The watch was already being started after each push; what was missing was
treating its result as a gate rather than a notification.

### Still blocked on the owner
**SonarQube credentials**; the ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 18:57:54 — Session 063 · **a fixture that tested the wrong branch**

### What this turned out to be
`memory/todo.md` filed masked subdivision as a performance item. Checking the
reference showed it is a **correctness gap**.

`guicommon.py:433` passes `staticFaceMask` into `createSubdivisionObject`, and
that function's docstring is explicit: masked faces "are not included as
geometry in this subdivision object (higher performance)". So the application
subdivides **13,378** faces. We subdivided all **18,486**.

**The existing parity fixture never caught this**, because
`tools/capture_fixture.py` captured it with `createSubdivisionObject(mesh, None)`
— it pinned a branch the application does not take. A green fixture is only
evidence about the path it captured.

### What shipped
`Subdivider::build(parent, faceMask)`, plus a new fixture captured from the real
Python reference: **53,512 faces (13,378 x 4)** against the old 73,944. Our
output matches it byte-for-byte — positions and indices — on the first run.

Implemented by **compacting the parent to its visible faces and then running the
existing algorithm unchanged**, which is what the reference does with its
`face_map`/`vtx_map` remaps. Threading a mask through 317 lines of
byte-parity-verified Catmull-Clark would have risked the verified path for no
gain.

**6.97 -> 5.25 ms**, 25% off the app's real path.

### Verification
- New parity fixture, captured from the reference, matches exactly.
- ctest **365/365** in debug, release and ASan; format clean; **0** undefined
  `mh::core` symbols.
- Previous push (RenderMesh radix sort) green on all 8 CI jobs.

### The lesson
Two sessions running, the fixture or test was the weak link rather than the
code: last session nothing pinned render-vertex *order*; this session the subdiv
fixture pinned the wrong *branch*. Both were found by asking what the test
actually proves, not whether it passes.

### Still open in M2
Dirty-range tracking (`refreshPositions` is already 0.04 ms, so low value), and
heterogeneous lookup in `findFaceGroup` (**no production caller** — tests only,
so optimising it would be speculative).

### Still blocked on the owner
**SonarQube credentials**; the ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 18:48:24 — Session 062 · **a 30% faster unweld, and the test that should have existed**

### What shipped
`RenderMesh::build` **2.23 -> 1.56 ms**, 1.5x -> 2.2x against the Python
baseline. It was the weakest ratio in the whole benchmark suite.

Three approaches were **measured**, not reasoned about:

```
indirect comparator over a separate keys array   2.23 ms
std::ranges::sort on (key, index) pairs          2.04 ms
LSD radix sort, 8 bits per pass                  1.56 ms
```

Keeping the key and index adjacent bought 8%; the radix sort another 25%. The
gain is memory behaviour rather than instruction count — six sequential passes
beat ~1.26M cache-missing comparisons. Those figures are in the code so nobody
has to rediscover them to justify the twenty lines.

### The find that mattered more than the speed
Mutation-testing the radix sort — making it skip its top byte — **passed every
existing test**.

The suite checked that render vertices were *valid* (each maps back to a real
vertex and UV, seams split correctly). Nothing checked they were in the *right
order*, even though the code claims to match the reference's `np.unique` and
every downstream buffer — positions, normals, UVs, weights — is indexed by that
order.

Now pinned by `[rendermesh][order]`. The broken sort leaves **4,469 of 21,833**
render vertices out of order; the correct one leaves zero.

**I nearly dismissed the mutation as broken.** That has happened repeatedly this
session, so this time I confirmed the mutated source reached the binary before
drawing a conclusion — and the mutation was fine; the *tests* were inadequate.
"My mutation must be wrong" is a comfortable explanation and was the wrong one
here.

### Verification
- ctest **364/364** in debug, release and ASan; format clean.
- Byte-identical output: 291,388 parity assertions unchanged.
- Previous push (USD materials) green on all 8 CI jobs.

### Still open in M2
Dirty-range tracking, subdivision with a face mask, heterogeneous lookup in
`findFaceGroup`.

### Still blocked on the owner
**SonarQube credentials**; the ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 18:38:11 — Session 061 · **USD materials, and a fix that made the file unopenable**

### What shipped
USD carries materials: `UsdPreviewSurface` under one `Looks` scope, bound per
mesh, with `UsdUVTexture` + `UsdPrimvarReader_float2` for a diffuse map and the
texture copied beside the stage — the same rule the OBJ writer follows for
`map_Kd`. A material-less scene writes **no `Looks` scope at all**, so its output
is byte-for-byte what it was before materials existed.

**Every export format now carries per-mesh materials**: OBJ, glTF, FBX, Collada
and USD.

### The lesson: my tests were blind to the worst mistake
`usdchecker` rejected three things in sequence, and this is the record of them
because the third is the instructive one.

1. **`MissingMaterialBindingAPI`** — a prim that binds a material must *apply*
   the API schema. The binding alone looks right and is not conformant.
2. **`MismatchedPropertyType`** — `inputs:varname` must be `string`, not
   `token`.
3. **My fix for (1) made it worse.** I emitted `prepend apiSchemas` as a
   *property* inside the braces. It is prim **metadata** and belongs in
   parentheses before the body. The stage stopped opening entirely — and
   **every test still passed**, because they all assert on substrings of a file
   nothing was parsing.

A plausible fix took the output from "invalid but loadable" to "will not open",
and only the first-party validator saw it. String assertions over a serialised
format cannot tell you the format is well-formed; they only tell you the bytes
you looked for are present.

Pinned now by a regression test asserting the exact metadata form.

### Verification
- `usdchecker`: **Success** on the dressed, textured stage.
- Blender: `body` (21,833) + `eyes` (1,076), materials bound.
- ctest **363/363** debug and release; ASan separately. format clean.
- Previous push (multi-mesh USD) green on all 8 CI jobs.

### Guards, sixth occurrence
A scripted edit failed its `assert s.count(old) == 1` because clang-format had
aligned `size_t vertices  = 0;` to two spaces. The guard stopped the write and I
inserted by line position instead. Six times now; without it these would be
silent partial edits.

### Still blocked on the owner
- **SonarQube credentials** — never yet runnable.
- The ball-of-foot crease remains visually unjudged.

---

## 2026-08-31 18:29:43 — Session 060 · **multi-mesh USD: every format now exports a dressed character**

### What shipped
`io::UsdSceneEntry` and `writeUsdaScene` — one `Mesh` prim per entry under one
`Xform`, with `extent` per prim as USD expects. A USD stage is already a scene
graph, so unlike OBJ and glTF this needed no index arithmetic at all. The writer
went 165 -> 185 lines, and `writeUsda` is a genuine one-entry wrapper whose
output is **byte-identical** to before.

**This completes multi-mesh export.** OBJ, glTF, FBX, Collada, STL, 3MF and USD
all carry the body plus everything worn. The "exports the body only" warning was
**deleted rather than reworded** — there is nothing left for it to report — with
a comment left where it lived.

### The best-validated format so far
USD was the only one with a *first-party* validator available:

- **Pixar's `usdchecker`: Success** — and crucially I ran it on the
  **pre-existing single-mesh output first**, so a pass afterwards means nothing
  was introduced, not merely that the new code is self-consistent.
- **Blender's USD importer**: `body` (21,833) + `eyes` (1,076), matching what
  FBX and Collada produce.

Checking the baseline before the change is what makes the "after" meaningful.
The glTF work last session found a pre-existing spec violation precisely because
a validator was pointed at code nobody had changed.

### Guards earning their keep
Two scripted edits failed their `assert s.count(old) == 1` this session, both
because the target had been reflowed by clang-format. In each case the guard
stopped a *partial* write rather than letting a half-applied edit through. That
pattern has now caught the same class of failure five times; it is the reason
none of them reached a commit.

### Verification
- ctest **359/359** in debug and release; ASan run separately.
- format clean; **0** undefined `mh::core` symbols across the Apache-2.0 modules.
- Previous push (multi-mesh FBX/Collada/STL/3MF) green on all 8 CI jobs.
- Ran all seven formats dressed: zero omission notes.

### Next
- **USD carries no materials** — it never has, for one mesh or many, so
  `UsdSceneEntry` has no `MaterialDesc` rather than silently ignoring one. Every
  other format now carries them.
- Still blocked on the owner: **SonarQube credentials**.
- Unjudged: whether the ball-of-foot crease *looks* right under a roll.

---

## 2026-08-31 18:21:45 — Session 059 · **multi-mesh FBX, and a bug only a second importer could see**

### What shipped
`io::SceneEntry` and `exportScene(std::span<const SceneEntry>, ...)`. FBX,
Collada, STL and 3MF now carry the body plus everything worn. USD is the last
single-mesh writer and says so.

assimp's `aiScene` holds many meshes natively, so unlike OBJ and glTF this
needed no index arithmetic. Extracting `fillMesh`/`fillMaterial` for both entry
points removed ~40 duplicated lines: **`SceneIO.cpp` is 537 lines, down from
593**, while gaining the feature.

### The bug: assimp said 2 meshes, Blender said 1
My first version hung every mesh on the root node. assimp read the file back and
reported 2 meshes, and the test passed. **Blender read one merged object of
22,909 vertices** — which is exactly 21,833 + 1,076, the two meshes summed.

Cause: assimp's FBX exporter names a mesh after the node that owns it. Sharing
the root, both meshes came back called `body`. The geometry and the materials
survived; the *identity* did not, and Blender merged them because they shared a
node. Collada happened to preserve names, which is the only reason the
difference was visible at all.

**One importer agreeing with the writer proved nothing** — assimp was reading
back its own convention. The disagreement between two independent importers is
what exposed it.

Fixed by giving each mesh its own child node. Both importers now agree on `body`
and `eyes` as separate objects, with matching vertex counts.

### A second-order effect, accepted deliberately
That fix made Collada emit `body_1`: its exporter disambiguates a mesh whose
name matches its node's. Node-per-mesh is still right — FBX matters more for DCC
round-tripping and identity survives either way — so the tests now assert names
by **prefix**, which is what is true rather than what I would prefer.

### A test I deleted rather than fixed
The byte-identity check between the single-mesh and one-entry paths had become
misleading: they are now deliberately different in structure, so comparing bytes
would pin the difference in place rather than test anything. Replaced with a
geometry comparison through both importers.

### Verification
- ctest **357/357** in debug, release and ASan; format clean; **0** undefined
  `mh::core` symbols across the Apache-2.0 modules.
- Previous push (ball weights) green on all 8 CI jobs.
- Verified end to end: the app's dressed `.fbx` and `.dae` both read back as
  `body` + `eyes` in assimp *and* in Blender.

### Recurring, now four times this session
A scripted edit silently failed to match because clang-format had reflowed the
target. The `assert s.count(old) == 1` guard caught it every time it was
present, and the one edit written without it was the one that silently did
nothing. It is not optional.

### Next
- **USD**: `writeUsda` takes one `RenderView` and has no material parameter.
- Still blocked on the owner: **SonarQube credentials**.

---

## 2026-08-31 18:08:47 — Session 058 · **the ball of the foot is weighted, and a gap I had overstated**

### The correction first
Last session I wrote that a foot roll "deforms nothing" without `ball` weights.
**That was wrong.** Rotating `ball.L` already moved **1,042 toe vertices**,
because the five toes are its children and carry their own weights. The real gap
was much narrower: only the *crease at the ball of the foot* failed to deform,
since that skin still belonged to `foot`.

I found it by checking the claim before building on it, which is the only reason
this chunk is scoped correctly rather than solving an imaginary problem.

### What shipped
`data/rigs/mixamo_superset_weights.mhw` — the default weights with the foot's
forward influence divided between `foot` and `ball`. 309 vertices per side.

The rule is a linear ramp between the ball joint and the toe roots: a vertex at
the ball line keeps all its `foot` weight, one at the toe roots gives all of it
to `ball`, split in between.

**Weight is moved, never created**, and the generator asserts it rather than
assuming:

```
vertices: 19158 before, 19158 after
worst per-vertex influence drift: 2.22e-16
```

Mutation-tested: copying weight instead of moving it is caught with "618
vertices changed total influence". The C++ test checks the same property through
the real loader, plus that `ball` received something and `foot` kept its heel.

Spatially coherent too — centroids run heel → ball → toe
(z = 0.348 → 1.141 → 1.495), which a scattered ramp would not produce.

### Verification
- ctest **355/355** in debug, release and ASan.
- Previous push (superset skeleton) green on all 8 CI jobs.
- Staleness gate covers the weights file as well as the skeleton.

### The limit of this, stated plainly
The ramp is a **principled choice, not a parity match** — MakeHuman has no ball
bone to compare against, so there is no reference behaviour to port. The weights
are provably conservative and anatomically ordered, but whether the crease
*looks* right under a roll is a visual judgement nobody has made. Blender is
available for it.

### Still blocked on the owner
- **SonarQube credentials** — the one requested gate that cannot run.
- Mixamo's online auto-rigger behaviour, unverifiable without web access.

### Parked
`git stash@{0}` — WIP multi-mesh FBX/Collada export tests.

---

## 2026-08-31 17:59:56 — Session 057 · **the 179-bone superset exists, and two of my own tests were worthless**

### What shipped
`data/rigs/mixamo_superset.mhskel` — 179 bones, generated by
`tools/build_mixamo_superset.py`. It loads through the **same** `loadSkeleton`
as the default rig, and the 163-bone default still loads unchanged beside it.
CI fails if the committed file drifts from the generator.

A pleasant surprise: **no new joint vertex-groups were required.** Every added
bone reuses a joint that already exists — a fingertip is the tail of the last
phalanx, `hips` sits on `root`'s tail, the ball of the foot on `foot`'s tail —
so the superset introduces no new dependency on the base mesh's helper geometry.

### Two of my own tests were worthless, and only mutation testing showed it
**1. "Every parent precedes its child" can never fail.** `loadSkeleton` reorders
breadth-first (`Skeleton.h:69-71` says so outright), so the property is
guaranteed by the loader regardless of file content. My loop asserted it 179
times and proved nothing. Replaced with assertions that *can* fail and that
matter: `hips` really sits between `root` and the legs, and all five toes really
hang off `ball` rather than `foot` — which is the whole reason `ball` exists.
197 assertions became 34 real ones.

**2. My first mutation run reported "not caught" twice, and both were my error.**
One mutation crashed the generator, so the `.mhskel` was never rewritten and the
test passed against the old good file. The other exercised the vacuous assertion
above. Redone so the generator still succeeds, both are caught.

That is now three times this session that a mutation "passing" meant my
*mutation* was broken rather than my test. The rule that catches it: **a
mutation that does not change the artifact under test is not a mutation** —
check the artifact changed, not just that the command ran.

### Verification
- ctest **354/354** debug and release; ASan run separately.
- Mutation-tested: removing the ball bones, and not reparenting the legs under
  `hips`, both fail the suite.
- The staleness gate bites — tampering with the committed `.mhskel` makes
  `--check` exit 1; regenerating clears it.
- Previous push (geometric oracle) green on all 8 CI jobs.

### Next
- **`ball.L/R` carry no weights yet.** The bones exist and the toes hang off
  them, but no vertices are assigned, so a foot roll deforms nothing. That is
  the next rig chunk.
- Still blocked on the owner: **SonarQube credentials**; and Mixamo's online
  auto-rigger behaviour remains unverified (no web access).

### Parked
`git stash@{0}` — WIP multi-mesh FBX/Collada export tests.

---

## 2026-08-31 17:51:39 — Session 056 · **the geometric oracle, and the check that was blind to its own reason for existing**

### What shipped
`tools/mixamo_mapping.py` gained the geometric check the last review said was
the missing one, plus `docs/rig/mixamo_rest_pose.json` — 65 measured Mixamo rest
positions, committed so CI never needs Blender.

Both errors that previously required a human reviewer are now caught
automatically.

### The obvious oracle is wrong, and measuring showed it
Comparing bone **positions** is the natural idea. It fails: the two rigs sit in
different rest poses, so error accumulates down the arm — 0.13 at the shoulder,
0.40 at the elbow, 0.83 at the wrist, over 1.0 at the fingers — and **42 of 49
perfectly correct mappings "fail"**. A position oracle silently assumes a shared
pose.

**Arc length along a chain is pose-invariant** — bone lengths do not change when
a rig moves. Comparing each bone as a percentage of its chain's total length
gives a measure both rigs agree on, and every mapping lands within a few points.

### It settled the open arm question with a number
`LeftArm` sits at **16.2%** along the clavicle→wrist chain. `shoulder01.L` is at
**16.0%**; `upperarm01.L` at **26.8%**. Remapped to `shoulder01.L` — a 0.2-point
match against a 10.6-point one. `upperarm01` stays in the rig, unmapped, and
still carries the skin weight.

### The bit worth remembering: the new check was blind to its own motivation
I built the arc oracle *because* `Hips -> root` had slipped past. Mutation-testing
it, restoring that exact error passed cleanly.

A chain **root** is at 0% on both sides by definition, so an along-chain measure
can never fault it — and `root` was not even in the chain list, so the check
skipped it outright. The oracle could not see the error it existed to catch.

Chain roots now get a positional check against where the MakeHuman chain
actually starts:

```
Hips maps to root, which sits 0.920 dm (14.2% of the chain) from spine05,
where that chain actually starts
```

**Mutation-testing a new check against the bug that motivated it is not
optional.** Had I only run the suite, I would have shipped an oracle that looked
like protection and was not.

### Verification
- Both original errors rejected: `Arm -> upperarm01` (10.7 points apart) and
  `Hips -> root` (14.2% of the chain away), each with a message naming the
  better candidate.
- Mirror, finger-collision, wrong-ancestor and wrong-side mappings still
  rejected.
- ctest **352/352**; no C++ touched. CI green on the previous push (8/8).
- The CI job was renamed `inventories (task views, Mixamo rig)` — it had been
  called "task-view inventory" while also running a rig check.

### Still blocked on the owner
- **SonarQube**: scanner installed, no credentials (`SONAR_TOKEN` +
  `sonar.organization`, or a self-hosted `SONAR_HOST_URL`).
- Mixamo's **online auto-rigger** behaviour remains unverified (no web access).

### Parked
`git stash@{0}` — WIP multi-mesh FBX/Collada export tests.

---

## 2026-08-31 17:44:29 — Session 055 · **Mixamo: a 179-bone superset, and a review that caught me twice**

### The question
Can the rig carry Mixamo's 65 bones *and* MakeHuman's 163, so the model is easy
to animate from Mixamo without the rig getting worse?

### The answer
Yes, and it grows the rig: **163 + 16 = 179**. Adopting Mixamo's skeleton
instead would lose 59 facial bones, 28 toe bones, 8 metacarpals and every twist
bone. Mixamo leads in exactly one place — a 4th joint per finger — and that
joint is a tip locator, not a deforming bone.

`tools/mixamo_mapping.py` holds the mapping and proves it; CI runs it.

### Measured, not assumed
Blender reports which bones carry animation channels. Across **all seven**
reference clips, identically:

```
bones carrying animation channels: 52 of 65
leaf bones that ARE animated     : 0 of 13
```

52 = the 49 Mixamo bones MakeHuman already has, plus `Hips` and the two
`ToeBase`. **Every bone Mixamo actually drives is either one we already have or
one of the three real bones the superset adds.** That turns "the 13 markers cost
nothing" from a claim into a measurement.

The Mixamo hierarchy itself was re-verified FBX -> Blender -> doc -> tool, with
a *different* importer than the assimp one that produced the table: 65 bones,
zero differing parents.

### Two things the review caught that I had wrong
**1. `Hips -> root` was geometrically wrong by 0.92 dm.** Structurally `root` is
forced — it is the only bone parenting both legs and the spine — and the
ancestry check was satisfied. But the legs and spine meet at root's **tail**:

```
root     head = (0, 0.5639, -0.7609)   tail = (0, 0.7268, 0.1445)
spine05  head = (0, 0.7268,  0.1445)   <- root's tail
pelvis.L head = (0, 0.7268,  0.1445)   <- the same point
```

Binding Hips there pivots the character ~9 cm behind the sacrum on every
rotation and every root-motion translation. The superset adds a `hips` bone at
the junction — the 16th addition, and why this is 179 not 178.

**2. I told the owner the check rejects left-to-right mappings. It did not.**
Ancestry is transitive, so a *whole-side mirror* preserves every parent
relationship and passed. A single swapped bone is caught by its children; a
mirror is not — and a mirror is the version of that bug that happens. The
reviewer passed 6 of 9 deliberately-wrong mappings, including the index finger
driving the pinky (which also silently duplicated targets while coverage still
read 50).

Both now caught, by an injectivity assert and a laterality assert, each
verified against the exact wrong mapping that used to pass.

### And one of my own claims that was false
The doc said "nothing here is hand-counted" directly above a hand-counted region
table with two wrong rows (metacarpals 12, really 8; face 55, really 59) that
summed to 159 rather than 163. Corrected, and the doc now says plainly which six
numbers the tool checks and that the rest is a sketch.

### The lesson
A structural check can be *necessary* and nowhere near *sufficient*, and it is
easy to mistake "my check passes" for "my mapping is right". Ancestry pinned
exactly one bone (`Spine2`); everything else it merely failed to reject. What
actually picks `spine03` over `spine04` is 0.1 cm of distance that nothing
measured. **The next check to add is geometric**, and it would have caught the
`Hips` error without a reviewer.

### Verification
- `tools/mixamo_mapping.py` green; mutation-tested — whole-side mirror,
  finger collision, wrong-ancestor and wrong-side single bones all rejected.
- ctest **352/352**; no C++ touched this chunk.
- CI gained a `Mixamo bone mapping is consistent` step.

### Blocked, needs the owner
- **SonarQube**: scanner installed, no credentials. Needs `SONAR_TOKEN` +
  `sonar.organization`, or a self-hosted `SONAR_HOST_URL`.
- Whether Mixamo's **online auto-rigger** preserves an uploaded skeleton is
  still unverified — web search unavailable, Adobe pages timed out. Does not
  affect the superset, which targets retargeting downloaded clips.

### Parked
`git stash@{0}` holds WIP multi-mesh FBX/Collada export tests (`SceneEntry`
API not yet written), set aside to answer the rig question.

---

## 2026-08-31 16:51:40 — Session 054 · **textures reach the GLB, and the alpha that was being thrown away**

### What shipped
`writeGlbScene` embeds images in the BIN chunk: an `images` entry (bufferView +
mimeType), a `textures` entry, and a `baseColorTexture`/`normalTexture`
reference on the material. A GLB is now self-contained — the eyes export brown
instead of white.

Verified on the real export: the 610 KB PNG is embedded **byte-identical** to
the source, its bufferView carries no `target`, no accessor reaches it, and the
file grew 1.02 -> 1.69 MB, which is the image.

### The finding that mattered: alpha embedded, then discarded
`/code-review` decoded `brown_eye.png` and found **13,282 non-opaque pixels,
minimum alpha 0**. The material is `opacity 1.0` but `transparent True`, and
`foundation::MaterialDesc` had no field for that — so `Material::desc()` dropped
it. glTF's default `alphaMode` is `OPAQUE`, and the spec then says the alpha
channel is *ignored*. Every conformant viewer would have rendered the cut-out
cornea solid.

Before this session there was no texture, so nothing to lose. Embedding the
image is exactly what made the omission bite. `MaterialDesc` now carries
`transparent`, and the shipped eyes export `alphaMode: BLEND`.

### Also fixed from review
- **A textured material on a UV-less mesh** is `MESH_PRIMITIVE_TOO_FEW_TEXCOORDS`
  — a hard validator error, reachable with any proxy `.obj` lacking `vt` lines.
  Refused now.
- **Empty or unreadable textures** produced `byteLength: 0`, which glTF forbids,
  while the writer reported success. A directory reads as empty too. Refused.
- **The mimeType came from the extension.** A PNG named `.jpg` is
  IMAGE_MIME_TYPE_INVALID. Read from the magic bytes now, which also deleted the
  extension sniffing rather than adding to it.
- The 4 GiB container failure reported `TooManyVertices` even when a texture
  caused it.

### Two problems I found by verifying, not by being told
1. **A latent test-helper bug my feature exposed.** `glbJson` scanned for the
   first `{` and the LAST `}` in the whole file. That worked only while the BIN
   chunk held nothing but floats; the moment an embedded PNG put a `0x7D` byte
   in there, `rfind` landed inside the image and returned megabytes of binary,
   which Catch2 then tried to print — a mutation run aborted with SIGABRT
   instead of failing. It reads the chunk by its declared length now. It had
   been quietly weakening every `find(...) == npos` assertion in that file.
2. **The image bufferView index was computed by a second formula.** I re-derived
   each entry's view count (2 + normals + UVs + 3·skin + morphs) instead of
   counting emissions. Correct in all 11 combinations the reviewer tried, but it
   is two statements of one fact, and drift would point images at mesh data.
   Counted as emitted now — nine lines out, two in.

### A recurring failure of mine, third occurrence this session
A scripted `sed`/`replace` silently did not match, and I nearly recorded a
mutation as "not caught" when the mutation had simply never been applied. The
fix is mechanical: **assert the match count before writing** (`assert
s.count(old) == 1`). Used consistently now. This is the same class as the
session-042 lesson and it has now cost time three times.

### Verification
- ctest **352/352 in debug and release**; format clean; **0** undefined
  `mh::core` symbols across the four Apache-2.0 modules.
- Khronos validator (via review) on the real textured export: **0 errors, 0
  warnings**.
- Mutation-tested every new guarantee: dropping the transparency flag, skipping
  the dedup, referencing the image from every material, and declaring an image
  whose bytes are never written — each fails the suite.

### Notes for next session
- **Normal maps will warn** `MESH_PRIMITIVE_GENERATED_TANGENT_SPACE` — no
  `TANGENT` attribute is written though `RenderView::vtang` exists. No shipped
  `.mhmat` sets a normal map, so the path is live but unreached.
- Texture dedup compares paths exactly; `weakly_canonical` when a second
  textured proxy lands.
- **The build tree vanished mid-session** and was rebuilt from scratch. Not
  caused by anything here — git was clean and the disk had 71 GiB free.

---

## 2026-08-30 12:58:01 — Session 053 · **materials reach the exported file, and a test that passed before it should have**

### What shipped
A dressed export now carries a material per mesh. `WornProxy` loads the
`.mhmat` its proxy names; the body loads `data/skins/default.mhmat`, which is
exactly what the reference does (`legacy/python/apps/human.py:89`). OBJ gets two
`newmtl` blocks and two `usemtl` directives; glTF gets two materials indexed per
primitive, roughness derived from shininess (0.96 -> 0.04). `.fbx`, `.dae`,
`.stl` and `.3mf` now pass the body material to `exportScene`, which had always
taken one and never been given it.

### Two mistakes of my own, both caught by checking rather than assuming
**A test that passed before the feature existed.** I wrote "a referenced texture
is copied next to the OBJ" and it went green immediately — because I had put the
source texture in the same directory as the output, so "does it exist beside the
.mtl" found the *source*. A test that passes before the implementation is not a
test. Moved the source into its own directory; it then failed correctly, and
passes for the right reason now.

**A review finding I reported as applied had never landed.** The dead `= {}`
default on `exportMesh` was still there: my scripted replacement had not matched
because clang-format joined the lines, and the edit silently did nothing. This
is the session-042 lesson verbatim — *verify a scripted edit by reading the file
back, never by the command's exit line* — and I repeated it while explicitly
knowing it. Applied and verified by reading back.

### Review findings applied
- **The all-or-nothing fallback was silent.** If a proxy's `.mhmat` fails, the
  body loses the material it had — because both writers refuse a partly-
  materialled scene, and rightly so: OBJ's `usemtl` is sticky, so a
  material-less entry inherits the previous one's appearance. The trade is
  correct; the silence was not. It now prints why.
- **The `.mtl` named a texture nobody copied.** `map_Kd brown_eye.png` with no
  file beside it — a dangling reference *this change introduced*, since before
  it no `.mtl` existed at all. The writer copies now, as the reference does
  (`shared/wavefront.py:278`), and fails loudly if the copy fails rather than
  writing a broken file.
- **Materials were deduped by POINTER.** Two proxies loading the same `.mhmat`
  yield equal descriptions at different addresses, so an ordinary scene would
  have been *refused*. Compared by value now. Not reachable with one proxy
  group; the first pair of clothes sharing a material would have hit it.
- Gave the material refusal its own `InconsistentMaterials` error kind instead
  of borrowing `EmptyMesh`.

### Confirmed correct, not changed
`--skin` not affecting the exported material is **right**: it selects a
litsphere, a viewport matcap with no PBR data. The reference has no such flag —
it blends `skinmat_*.png` from the ethnicity modifiers
(`apps/autoskinblender.py:52-60`). `data/skins/` ships only `default.mhmat`, so
there is nothing else to pick. The confusion is in the flag's name, and renaming
it is user-facing, so it is a todo to ask about rather than a silent change.

### Verification
- ctest **344/344 in debug and release**; format clean; **0** undefined
  `mh::core` symbols across the four Apache-2.0 modules.
- Ran the failure path deliberately (`chmod 000` on the eye `.mhmat`, restored):
  it now reports the drop instead of printing "wrote" and exiting 0.
- Verified the texture is really copied — 610 KB `brown_eye.png` beside the
  output — not merely named.

### Notes for next session
- **glTF carries no textures at all**: no `images`, no `textures`, no
  `baseColorTexture`. The eyes export **white**. Pre-existing, now visible, and
  the most valuable remaining export gap.
- `--skin` may deserve renaming to `--litsphere`; user-facing, so ask.

---

## 2026-08-30 12:35:38 — Session 052 · **multi-mesh glTF, and a spec bug nothing had caught**

### What shipped
`io::writeGlbScene` — several meshes in one GLB, each its own mesh and node with
its own accessor block. glTF addresses buffer views, accessors, meshes, nodes
and materials by index, so every entry's block is offset by everything written
before it. `writeGlb` is now a wrapper over it.

**The safety net that made this refactor sane:** I captured the SHA-256 of a
plain and a rigged T-pose export *before* touching the writer, then required
them to match after. They do, and the 22 existing glTF cases still pass. The BIN
chunk is byte-identical; the only JSON change for the app is the deliberate
rename of the body node from `MakeHuman` to `body`, matching OBJ's `g body`.

At most one entry may carry a skin — a second is refused rather than silently
dropped. Joint nodes follow the mesh nodes, so a joint's node index is
`entries.size() + jointIndex` in all three places it appears (scene roots, node
children, `skins.joints`).

### The bug worth remembering: an accessor bound that bounds nothing
`/code-review` ran the **Khronos glTF validator** and found 14 errors — all
`ACCESSOR_ELEMENT_OUT_OF_MAX_BOUND`, and all **pre-existing**, present in the
byte-identical single-mesh path too. `fmtFloat` prints 7 significant digits;
`FLT_DECIMAL_DIG` is 9. So a declared max of `1.263996` sat *inside* data whose
real maximum was `1.2639964818954468`. A file that loads everywhere and is
formally invalid — which is why nothing caught it for a long time.

**The suggested fix was not enough, and re-validating is what caught that.**
`formatShortest(float)` gives the shortest string that round-trips as a
*binary32*, but a validator parses JSON numbers as doubles and compares against
the data widened to double. `0.84894335` round-trips to the right float and is
still strictly less than that float's double value `0.8489433526992798`, so the
bound was still too small. Widening first — `formatShortest(static_cast<double>(v))`
— is what actually holds. I only found this because I re-ran my own bounds check
after applying the fix instead of trusting that it worked.

Both failures are mutation-tested: reverting to 7 digits fails the new test, and
so does the near-miss float overload.

### Also applied from review
- **The material contract now matches the OBJ writer.** It deduped on the name
  string alone and kept the first descriptor, so two materials named the same
  silently merged — a transparent item losing its `alphaMode` and exporting
  opaque — and a material-less entry could collide with a real material called
  "Skin". Both are refused now, exactly as `ObjWriter` refuses them. The two
  writers must not disagree about what is legal.

### Verification
- ctest **343/343 in debug and release**; format clean; **0** undefined
  `mh::core` symbols across the four Apache-2.0 modules.
- Independent checks: Khronos validator (via review) reported 0 errors and 0
  warnings on the multi-mesh output apart from the bounds issue now fixed; my
  own decoder re-checks every POSITION accessor's declared bounds against the
  decoded float data as doubles — 0 violations.
- assimp reads a skinned-body + unskinned-proxy scene back with bones on mesh 0
  only, `mNumBones` matching, and every bone resolving to a node.
- Mutation-tested: hardcoding the joint base to 1 (right for one mesh, wrong for
  two) fails only the new test — 22 of 23 cases still passed, which is precisely
  why that test had to exist.

### A measurement lesson, and a corrected diagnosis
`load ALL 1280 targets` read 689, 791, 1140 and 778 ms at various points this
session against a ~500 ms baseline. I first blamed CPU contention from
concurrent builds. That is not quite right: measuring again on an **idle**
machine still gave 778 ms on the FIRST run and 502 / 497 ms immediately after.

So the pattern is the first run after heavy build activity, not concurrent load.
The benchmark parses 1,280 target files; a build evicts them from the page
cache, and run one pays the disk. Inferred from the timing shape rather than
proven — I did not purge the cache to confirm — but it predicts the observations
better than contention does, and it means **the first number after a build is
always wrong**. Discard it and take the steady state, which has been 486-511 ms
in every quiet measurement all session.

### Notes for next session
- `.usda`, `.fbx`, `.dae`, `.stl`, `.3mf` are still single-mesh and say so.
- **Proxy materials are the real remaining gap**: both multi-mesh writers accept
  a material per entry, but the app passes `nullptr` for every entry, so body
  and proxies share one default. The per-entry material path is therefore
  exercised only by tests. `WornProxy` needs to load the `.mhmat` beside its
  proxy.

---

## 2026-08-30 11:01:59 — Session 051 · **a dressed character exports dressed (OBJ)**

### What shipped
`io::writeObjScene` — several meshes in one Wavefront OBJ, each as its own named
`g` group. OBJ indices are file-global and 1-based, so each entry's faces are
offset by the vertices, UVs and normals already written. `writeObj` is now a
six-line wrapper over it, so there is one implementation rather than two.

The app's `--export` branch was moved **below** `buildAssetGroups` and the
worn-proxy setup. That is the whole reason export could not see clothes: it
returned before anything knew what the character was wearing.

| | verts | groups |
|---|---|---|
| `--eyes high-poly` | 20,222 | `body`, `eyes` |
| `--eyes none` | 19,158 | `body` |

19,158 + 1,064 is exactly the body plus the eye proxy.

**Behaviour change worth knowing:** an undressed `--export foo.obj` now writes
`g body` where it wrote `g mesh`. One line of a 306k-line file, but it is a
format change for anyone scripting export.

### What I got right for the wrong reason, and what I checked
I flagged the mixed-attribute index arithmetic as the weakest part of the change
and tested it rather than reasoning about it: an entry with UVs but no normals
followed by one with both must write `f 1/1 2/2 3/3 4/4` then
`f 5/5/1 6/6/2 7/7/3 8/8/4` — positions and UVs offset to 5..8, normals starting
at 1 because nothing wrote a normal before. It does. `/code-review` independently
built every mixed combination and walked all meshes with assimp: also correct.
So the part I most distrusted was fine, and the defects were elsewhere.

**A mutation test that lied.** Dropping the vertex offset made the build fail
under `-Werror` (unused variable), so the test binary was stale and reported a
pass. I only noticed because a mutation *should* fail. Redone as `(vBase * 0)`
so it compiles — then it failed correctly. **A mutation that does not build is
not a mutation test**, and the green result it prints is from the previous
binary.

**A benchmark scare that was not one.** `load ALL 1280 targets` read 689 ms
against a 506 ms history — 36% slower, in code this change does not touch. Three
quiet re-runs: 486, 487, 511 ms. It was contention from my own concurrent
builds. Re-measure before believing a regression.

### Review findings applied
- **The omission note fired for unknown extensions**, printing "exports the body
  only" before "unknown export extension" for a file never written. Now gated on
  the formats actually written.
- **A material-less entry silently inherited the previous entry's `usemtl`** —
  OBJ has no "no material" state. Mixing materialled and material-less entries
  is now refused, rather than writing a file whose clothes are textured as skin.
  Refusing beats inventing a placeholder material.
- **Materials were not deduplicated**: two entries sharing one wrote the block
  twice, and two *different* materials sharing a name silently lost one, since
  consumers keep the last block. Deduped by name, and a genuine name clash is an
  error.
- **Two comments in `tests/CMakeLists.txt` had become false** — they still said
  `--export` returns before the asset groups exist. It does not, since this
  change. Corrected in the same commit: a stale comment lies with authority.
- Deleted a dead default argument; replaced a loop shaped like generality it did
  not have with a named `selectedChoice` helper.

### Verification
- ctest **337/337 in debug, release and ASan**; format clean; **0** undefined
  `mh::core` symbols across the four Apache-2.0 modules.
- Mutation-tested every new guarantee: dropping the vertex offset, emitting one
  group for the file, and advancing the normal counter for a mesh with no
  normals each fail the suite.
- assimp reads our output back and reports 2 meshes — independent confirmation
  from a library that knows nothing about the writer.
- A one-entry scene is asserted **byte-identical** to `writeObj`; the reviewer
  also A/B'd 82 option combinations against a binary built from HEAD: zero
  mismatches.

### Notes for next session
- **Only OBJ is multi-mesh.** `.glb`, `.usda`, `.fbx`, `.dae`, `.stl`, `.3mf`
  still export the body alone and now say so on stderr. glTF is the one that
  matters most for DCC round-tripping — `exportScene` takes a single
  `RenderView` and needs one primitive or node per mesh.
- Proxy `delete_verts` are still not applied to the body (a no-op today, all
  shipped proxies declare zero).

---

## 2026-08-30 10:33:36 — Session 050 · **the app wears proxies; the first blocked chooser is unblocked**

### What shipped
`WornProxy` in `src/app/main.cpp` — a proxy's fitting data, its own mesh and its
render buffers. `rebuildInto` re-fits every worn proxy against the **posed,
morphed base mesh** on every rebuild and hands the viewport `body + proxies`
through `setMeshes`, so a proxy follows morphs and pose without special-casing.

**Eyes is the first working proxy chooser** — one of the eight that were blocked
— with `--eyes` beside the existing `--skin` and `--pose`.

Skin now rebuilds rather than calling `setLitsphere`. That was the dead end
recorded last session: the body's material travels with its `MeshInstance`, so
changing it without rebuilding re-uploaded the old one and rendered an unchanged
picture.

### Verified by running the program
The tests alone would not have convinced me, because the interesting failure is
"the picker changed and nothing happened".

| run | result |
|---|---|
| `--eyes high-poly` vs `--eyes none` | 1,329 px differ; luminance 29..212 -> 21..227 |
| `--skin african` vs `caucasian` | 196,787 px differ (4.8%) |
| `--eyes nonsense` | reports the typo, falls back to none, matches "none" exactly |
| `--subdivide --eyes high-poly` | eyes still render; proxies fit base indices, unaffected |

**Coverage is identical with and without eyes** — they sit inside the face
silhouette — which is why the check is luminance range and a pixel diff, not
coverage. Worth remembering for every small proxy that follows.

### Two measurement mistakes, corrected before they became assertions
1. I framed a "head shot" at distance 12 and got zero difference. The camera
   looks at the model origin and the base mesh straddles it (y -8.45..8.50), so
   closing in frames the navel and loses the head. There is no pan yet.
2. My first background test used exact `QColor` equality where the renderer
   needs a tolerance, and reported the eye proxy as covering all 262,144 pixels
   of the frame. The real number is 64.

Both are the same lesson: measure, then assert. An assertion written from an
expectation would have been wrong twice.

### New test: `app_screenshot`
`app_smoke` returns at `--export` **before the asset groups are built**, so it
never reaches the proxy wiring — the same blind spot that let a scripted edit
delete the whole Materials dock in session 042 with 328 tests still green.

`app_screenshot` runs the real window. Its teeth come from
`PASS_REGULAR_EXPRESSION` on the app's own "wearing HighPolyEyes (1064 verts)"
line: without that it would only catch a proxy that fails to *load*, while one
silently dropped from the scene still renders a valid picture and would pass.
Mutation-tested — dropping the proxy fails the test. It skips (not fails) on a
machine with no Metal device, via `SKIP_REGULAR_EXPRESSION` on the app's own
"viewport error".

It genuinely runs under **ASan** (1.73 s, passed), which is the best evidence
the proxy lifetimes are sound.

### Review findings, self-reviewed
The review agent stalled without reporting, so I worked the risk list myself:

- **Lifetime (the one that worried me).** Erasing or replacing a `WornProxy`
  frees geometry the viewport still holds non-owning spans into. Safe *only*
  because erase and rebuild run to completion inside one slot — `update()`
  schedules a repaint rather than performing one, so `setMeshes` replaces the
  list before the event loop can paint. Non-obvious and easy to break, so it is
  now a comment at the erase site.
- **`refitProxy`'s silent early returns are unreachable**: `wearProxy` rejects a
  proxy whose `maxRefIndex` exceeds the body, morphs never change the body's
  vertex count, and a proxy's vertex count is fixed at load. Documented as
  guards rather than expected paths.
- **Not masking proxies with `staticFaceMask` is correct** — the eye `.obj` has
  zero group lines, so there is no helper geometry to hide.
- **Ponytail:** I had duplicated `filesWithExtension` with a `views` pipeline
  because proxies live one directory per asset. Both existing asset directories
  are flat, so making the shared helper recursive is a no-op for them; the
  duplicate and the `<ranges>` include are gone. I kept the `map` keyed by group
  rather than a single `optional` — seven more choosers follow immediately, so
  that is demonstrated need, not speculation.

### Verification
- ctest **330/330 in debug, release and ASan**; format clean; **0** undefined
  `mh::core` symbols in `foundation`, `io`, `render`, `ui`.
- `fitProxy` benchmarks at **0.00 ms**, so re-fitting on every rebuild costs
  nothing measurable — which is what justifies doing it unconditionally.

### Notes for next session — two real gaps, both recorded in todo.md
- **Proxy `delete_verts` are not applied to the body.** `visibleVertexMask` and
  `faceMaskForVisibleVertices` exist and are tested but nothing calls them. A
  no-op today (all four shipped proxies declare zero, verified) and a real bug
  the first time a clothing asset expects the body hidden underneath.
- **Export ignores worn proxies** — a dressed character exports naked. Needs the
  writers to take more than one mesh.

---

## 2026-08-30 08:18:55 — Session 049 · **the renderer draws N meshes, and the bug that found**

### What shipped
`SceneResources` now holds a `Drawable` per mesh — vertex and index buffers, a
litsphere texture and its own `QRhiShaderResourceBindings` — while the pipeline,
sampler, white diffuse stand-in and camera uniform stay shared, because those
belong to the frame rather than to a mesh. `OffscreenRenderer::render` and the
new `ViewportWidget::setMeshes` take a `std::span<const MeshInstance>`; the
single-mesh overload delegates, so all five existing render tests were untouched.

This is the unblocker: eight of the nine blocked task views are proxy choosers
waiting on the viewport drawing more than one mesh.

### The use-after-free this introduced, and how it was caught
Worth recording in full, because the tests I wrote first did **not** catch it.

`upload()` queued each mesh onto the caller's `QRhiResourceUpdateBatch` inside
the per-mesh loop, but could still fail on a later mesh — a missing litsphere,
say. The `return` unwound the vector of already-built `Drawable`s, destroying
buffers and textures the batch still held raw pointers to. A batch never learns
that a resource died. And `ViewportWidget::render` reports an upload failure
**without returning**, then submits that batch — so a two-mesh list whose second
litsphere was missing crashed in `QRhiMetal::enqueueSubresUpload`.

Neither the multi-mesh tests nor 329 ctest cases saw it, because
`OffscreenRenderer` abandons its batch on failure and so cannot reproduce it.
`/code-review` found it by reading the ownership, and proved it with an ASan
repro.

Fixed at the root — build every `Drawable` first, queue nothing until the whole
loop has succeeded — rather than by returning early in `ViewportWidget`, which
would have left the crash live for every other caller.

The regression test drives `QRhi` directly the way the widget does: upload a
good mesh plus one with a missing litsphere, then submit the batch anyway. With
the fix reverted it reproduces the exact SEGV under ASan; with the fix it passes.

### Verification
- ctest **329/329 in debug, release and ASan**; `mh_render_tests` 9 cases.
- **Mutation-tested every new test** rather than trusting a green run: sharing
  one SRB across meshes fails 1 case; drawing only the first mesh fails 2;
  re-queueing inside the loop reproduces the SEGV. A test that has never been
  seen to fail is not evidence.
- `clang-format` clean; licence boundary holds — **0** undefined `mh::core`
  symbols in `foundation`, `io`, `render`, `ui`. This chunk touched two
  Apache-2.0 modules, so that gate mattered here.
- Benchmarks unchanged (6.3x-55.7x over Python); the change touches no core path.

### Also fixed from review
- `OffscreenRenderer`'s failure path abandoned an unreleased batch. The pool is
  64; exhausting it makes `nextResourceUpdateBatch()` return null, which
  `upload` dereferences. One `u->release()`.
- My first draft gave the pipeline its layout SRB via a 1x1 texture created on
  the stack in `create()` and destroyed while the retained SRB still pointed at
  it. I replaced it with the long-lived `diffuseTex` before review came back.
  The review then established it would in fact have been safe — Qt serialises
  only binding/stage/type/arraySize for layout comparison and the Metal backend
  never dereferences the resource — but a dangling pointer that happens to be
  safe today is not a thing to ship, and the replacement was also shorter.
- Corrected a header comment that was simply wrong: `setLitsphere` guards on
  list **size**, not on which setter built the list, so it does rewrite a
  one-element list set by `setMeshes`. Test extended to pin both cases.

### Notes for next session
- Multi-mesh is done in the **renderer**; the app still calls `setMesh` with the
  body alone. Wiring `setMeshes` is the remaining half of the chooser unblock.
- **The Skin picker will appear broken the day that lands**: `applyChoice`
  handles `"Skin"` with `setLitsphere` and returns without rebuilding
  (`src/app/main.cpp:636-639`), which no-ops on a multi-mesh list. Recorded in
  `todo.md` so it is fixed in the same change, not diagnosed as a render bug.
- Hair and eyelashes will need per-mesh diffuse textures and alpha blending:
  a second pipeline and back-to-front ordering, which moves
  `setGraphicsPipeline` into the per-mesh loop.

---

## 2026-08-30 07:31:51 — Session 048 · **the task-view count was wrong three times; now it is derived, not asserted**

### What shipped
`tools/audit_taskviews.py` and `memory/taskviews.md`: a per-view inventory of the
reference's task views, re-derived from `legacy/python` on every CI run.

**51 task views — 44 standalone + 7 built at run time.** `architecture.md` had
carried **50**, uncited, since session 001.

### How the number moved 50 → 48 → 51
This is the part worth reading; the number itself is the least of it.

1. I first audited with a regex, `^class X\(...TaskView\)`, got 43 classes + 7
   dynamic = 50, and took the match with `architecture.md` as confirmation. It
   was not confirmation — two of the 43 (`MeasureTaskView`, `ModifierTaskView`)
   are never standalone views, so the regex was wrong *and* the folklore was
   wrong, in opposite directions, by the same margin. **Two independent wrong
   answers agreeing is not evidence.** Corrected to 48.
2. `/code-review` then found the real defect: that regex requires the base list
   to be a single name, so `class LoadTaskView(gui3d.TaskView, filecache.MetadataCacher)`
   is invisible to it. **Six views were missing, five of them real user tabs** —
   Load, Skin/Material, Pose, Skeleton, Expressions. The entire Pose/Animate
   category was absent from the roadmap and I had not noticed, even though the
   script's own category map declared a `Pose/Animate` bucket that never printed.
3. Rewrote on `ast` with transitive base resolution. 44 standalone + 7 = 51.

The review also corrected two classification calls I had backwards:
`OpenGLTaskView` *is* the Render tab (its label is literally `'Render'`,
`plugins/4_rendering_opengl/__init__.py:53`) and was filed as blocked; and
`ViewerTaskView` is where render output lands
(`plugins/4_rendering_opengl/mh2opengl.py:122-123`), so declining it would have
broken the render feature I was keeping. Both are now `todo`.

### A second review round, which found three more real errors
Re-reviewed adversarially after the rewrite. The 44 was confirmed by an
*independent* method — resolving every `.addTask()` call site to the class it
registers, ignoring inheritance entirely — with zero symmetric difference. But
three of my judgements were still wrong:

1. **`ExportTaskView` was bucketed "covered" against a File menu action that
   does not exist.** `grep 'file.export' src/` finds nothing; only Open, Save
   and Save As are wired (`src/ui/MainWindow.cpp:138-143`). The writers exist,
   the UI entry point does not. A roadmap that labels missing work as done is
   worse than one that omits it. Moved to `todo`.
2. **`AnimationLibrary` was blocked on multi-mesh; it is not.** Its only gates
   are a skeleton and an active animation
   (`plugins/3_libraries_animation.py:150,157`) and all of `rig/Skeleton.h`,
   `rig/Skinning.h`, `io/BvhReader.h` exist. Moved to `todo`.
3. **`SceneLibraryTaskView` is blocked on a lighting model, not mesh count** — a
   `Scene` is `lights = []` plus an `Environment` (`shared/scene.py:190-192`).
   Still blocked, but no longer miscredited to multi-mesh.

Two structural holes in the tool itself, both fixed:

- `dynamic_view_names()` hardcoded three slider filenames. A fourth
  `loadModifierTaskViews` call site adds views but **no `addTask` line** — the
  loop is shared — so every check would have passed on a wrong answer. The
  filenames are now derived from the call sites.
- The bucket-sum check could never fire: the sum is
  `len(standalone) + len(dynamic)` by construction. It read like a guard and
  guarded nothing. Deleted.

Also corrected: my `except SyntaxError: # a few py2 files remain` was a false
claim — all 196 reference files parse. Removed, so a genuine parse failure now
raises instead of silently skipping a file that `registered_tabs()` still reads.

### The lesson, which is not about task views
Three wrong numbers in one session, all from **pattern-matching text instead of
parsing structure**. A regex over source is a guess that looks like a
measurement. `ast` is in the standard library and was always the cheaper rung.
Where a count must be defensible, derive it — and give it a second, independent
derivation that must agree: the auditor now cross-checks the AST result against
the uncommented `addTask(` call sites, and that cross-check is what fires first
when a view goes missing (verified by making the AST deliberately miss one).

### Verification
- `python3 tools/audit_taskviews.py` → rc=0, `51 (44 standalone + 7 built at run time)`.
- Failure gates exercised and confirmed to fail closed: count drift,
  unclassified view, and the addTask cross-check (which fires first when a view
  goes missing — verified by making the AST deliberately miss one).
- Proved the AST auditor sees a mixin-based view that the old regex returns
  `[]` for (scratchpad fixture, no repo edit).
- ctest **329/329 green in debug, release and ASan**. No C++ changed this
  chunk, so the benchmark is unmoved by construction (`git diff --name-only`
  lists no `.cpp/.h/.mm`).
- Licence boundary re-checked: `nm -u` on foundation/io/render/ui → **0**
  undefined `mh::core` symbols. No forbidden deps.

### Roadmap effect
Buckets are now 7 done · 2 covered by the File menu · **17 todo** · 9 blocked ·
16 declined. Eight of the nine blocked views are proxy choosers waiting on the
same thing: **the viewport draws exactly one mesh.** Multi-mesh rendering clears
eight of nine in one change — it is unambiguously the next thing to build.

### Notes for next session
- The CI gate lives in its own `taskviews` job now, not in `licence` — an
  inventory check has nothing to do with licensing and confused the reviewer.
- I did not add a ctest for it: CI already gates it, and a ctest entry would
  make `ctest` depend on a system `python3`.
- Next: **multi-mesh rendering** (M4), the unblocker above.

---

## 2026-08-31 00:18:44 — Session 047 · **presets derived from the registry, and two tests that could not fail**

### What shipped
`WorkspacePreset` now names **categories**, not dock object names, and its `categories`
field is a `std::optional<QStringList>`: `nullopt` means every registered category, an
empty list means none. So the first preset shows a category registered later **by
construction** — last session that was only guarded by a test noticing afterwards.

`std::optional` rather than the `bool showsEveryCategory` + list I first wrote: that pair
had an invalid combination (true plus a non-empty list) and needed six lines of doc to
explain. The optional has no invalid state and reads as one line at the call site.

### The failure class was relocated, not removed — until the review said so
Presets stopped naming dock objects and started naming categories, but `workspacePresets()`
still hardcodes category strings that `main.cpp` independently hardcodes. Nothing tied the
two. Probed: with only "Modelling" registered, `applyWorkspacePreset("Materials")`
**returned true**, hid every dock, and put "Workspace: Materials" in the status bar — then
`saveWorkspace()` persisted the empty layout on quit, with Reset Workspace the only way
back. Exactly the failure this change set out to eliminate, moved from dock names to
category names.

It returns false now when a preset names categories and none resolve to a live dock, and
`--workspace` in a script fails instead of silently producing a blank window.

### Two tests of mine that could not fail
1. **The full-preset test.** On a window that was never `show()`n, every dock already
   reports `isHidden() == false`, so `CHECK_FALSE(dock->isHidden())` was satisfied by the
   default state rather than by the preset. Proven: stubbing out the `setVisible` loop
   left it green. It now hides first and checks the dock comes back — and with the loop
   stubbed, **two** cases fail where one did before.
2. My own mutation earlier in the session (reverting Modelling to a literal preset) did
   catch the intended bug, but a weaker mutation slipped through it. Both are covered now.

### memory/todo.md was false as of the diff
The entry read "a test now fails if that happens, but the presets should be derived
instead" — while this diff *deleted* that test and implemented the derivation. `CLAUDE.md`
requires memory updated in the same commit; a stale line there would have sent the next
session hunting a guardrail that no longer exists. Corrected.

### Also
`MainWindow::Impl` stored a whole `TaskRegistry` when `categories()` was the only thing
ever read from it. Now a `QStringList`.

### Verified this session
- 329/329 in debug, release and ASan.
- Both preset assertions mutation-tested: stubbing `setVisible` fails two cases, and
  reverting Modelling to a literal preset fails five assertions.
- All four presets re-checked in the running app by viewport area.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- Port the 50 task views (`architecture.md` §I.8) — the registry was the prerequisite.
- `reduceMotion()` returning true is still unexercised; VoiceOver on a real device.

---

## 2026-08-30 22:41:07 — Session 046 · **the task registry, and a slug I added for a problem that did not exist**

### What shipped
`ui::TaskRegistry` closes the M8 item. The reference derives a plugin's category *and* its
position from its file name — `loadPlugins` does
`sorted(pluginsToLoad, key=lambda plugin: plugin[0])` (`core/mhmain.py:562`) — so inserting
a view between two others means renaming files. The registry declares both, and
`MainWindow` builds one dock per registered category instead of hardcoding two.

### I did it again
Before the reviews returned I "pre-emptively" replaced the dock name with a slug that folds
punctuation to `-`, on the reasoning that spaces break a QSS ID selector. The review
measured it:

    escaped   #dock\.arms\ and\ legs  -> matches
    unescaped #dock.modelling          -> no match

The mandatory `dock.` prefix's own period already forces escaping, and once escaping, a
space works fine. **No QSS in the repo selects these names at all** — `grep -rn "#dock"`
returns zero. The slug bought nothing and introduced a real hazard: two categories folding
to one object name, which `saveState` keys on. Reverted.

My design.md citation was wrong too: it says "Arms **&** Legs", and that is a *task* inside
Modelling, not a category — `dockObjectName` is only ever called on categories.

That is the second session running where I added complexity for an unverified problem
(last time: word wrap at 200% text). The rule I keep relearning: **measure before fixing,
not after.**

### The use-after-free the refactor introduced
`setPanel(category, widget)` is stringly typed where the old `setModellingWidget` /
`setMaterialsWidget` were compile-checked names. `installInDock` deletes the widget when
the dock lookup fails, so `setPanel("Modeling", panel)` — one L, **the reference's own
spelling, cited in the comment three lines above the call** — freed the panel while
`main.cpp` went on connecting signals to it and calling `setValue` on every slider drag.
Proven by probe. Now `[[nodiscard]] bool`, and ownership does **not** pass on failure: a
leak is a far better failure than a use-after-free.

### The regression the review proved
Docks became data-driven; `workspacePresets()` is still a hardcoded list of two dock names.
Register a third category and **every** preset hides it — and once the layout is saved on
quit it never returns except via Reset Workspace. Measured across all four presets. A test
now fails if a registered category is not shown by at least one preset; deriving the
presets is recorded as the real fix.

### Cut before shipping
A first draft of `TaskDescriptor` carried `order` and `icon`, plus `tasks(category)` and
`taskCount()`. Grep: **zero production readers** — the app *writes* icon names and ranks
that nothing consumes, and the sort existed only to satisfy its own test. Same mistake as
the JSON workspace layer in session 041, caught earlier this time. The registry is now a
category list with a case-insensitive duplicate guard, which is the part the filename
scheme genuinely could not do.

### Verified this session
- 329/329 in debug, release and ASan; UI binary 444 assertions / 67 cases.
- The app and all four workspace presets re-checked by screenshot after the refactor.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- Derive the workspace presets from the registry.
- Port the 50 task views (`architecture.md` §I.8) — the registry is the prerequisite.

---

## 2026-08-30 21:03:18 — Session 045 · **undo for pose and skin, and a merge policy chosen rather than assumed**

### What shipped
- **`ui::ChoiceChangeCommand`** — skin and pose changes go through the undo stack, so ⌘Z
  means the same thing whichever panel was last touched.
- **`AssetPanel::setChoice` restored.** It was cut in session 041 for having no production
  caller; undo is that caller. A good illustration that "no caller" is a reason to remove
  something, not a permanent verdict.

### A behaviour choice, made with a measurement
My first version documented that choices "deliberately do **not** merge — picking two
skins in a row is two decisions, not a drag." The review showed that is true for mouse
clicks and false for the keyboard: arrow-keying a **closed** combo emits one change per
keystroke, so Down/Up/Down leaves the picker where it started while costing three undo
steps and **three full skeleton-and-weights reloads**.

I checked whether `QComboBox::activated` (user-initiated only) would separate the cases.
Measured: three Down presses give `currentIndexChanged=2 activated=2` — identical, so
switching signals fixes nothing.

So consecutive choices in a group now merge, and the header says plainly that this is a
choice with a trade-off rather than a forced consequence: trying several skins to compare
them is one decision, and the caller ends the run when a different kind of edit happens.

### The failure path I flagged, confirmed and fixed
If `loadPoseRig` failed, three things had already happened — the picker had moved,
`applyStack` had reset the mesh to its morph base leaving it unposed while `rig` still
held the old pose, and the map had the new id — and then it returned before rebuilding.
Picker, viewport and undo history all disagreed, and the stack held an entry whose `redo()`
did nothing.

It cannot be fixed by reordering: `applyStack` **must** precede `loadPoseRig`, because
fitting the skeleton to an already-posed mesh is the 33 cm bug from session 038. So the
pose is now probed *before* the command is pushed; on failure the picker goes back and
nothing is recorded. The in-command path also rebuilds on failure, so the surfaces agree
even if a file disappears between the probe and the redo.

### Also
- `apply_` was guarded in one command and not the other. Made consistent — both unguarded,
  because a null callback is a programming error and an undo that silently does nothing is
  harder to diagnose than one that throws.
- Two dead lines: an `if (before == id)` guard that cannot be true (QComboBox only emits on
  a real change) and a map write that `push()`'s synchronous `redo()` already does. The
  second mattered: two writers, and the redundant one is the one that goes stale.
- All the new undo state is declared **above** `window`, matching the invariant this file
  states — verified by the reviewer against the full capture list.

### Verified this session
- 329/329 in debug, release and ASan.
- Merge semantics tested in all four combinations: same group same run, same group new run,
  different group same run, and the undo target after a merge.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- Workspace presets and Save As still bypass the undo stack.
- `reduceMotion()` returning true is still unexercised; VoiceOver on a real device.

---

## 2026-08-30 19:26:40 — Session 044 · **200% text and reduce motion, and a fix for a problem I never demonstrated**

### What shipped
- **`theme::reduceMotion()`** in `src/ui/Motion.mm`, the project's only Objective-C++ file,
  reading AppKit's `accessibilityDisplayShouldReduceMotion`. `AnimatedDocks` is dropped
  when it is set.
- Word wrap on slider captions, kept for a **measured** reason (below).
- AppKit recorded in `LICENSING.md`.

### There is no Qt API for reduce motion
Checked before writing Objective-C++: `QStyleHints` has no motion hint;
`QAccessibilityHints` (Qt 6.10) carries exactly one property, `contrastPreference`;
`QPlatformTheme::ThemeHint` has only the Windows `Animate*UiEffect` family.
And **`QSettings` cannot read another application's preference domain** — measured:
`com.apple.dock` reports **0 keys** through QSettings while `defaults read` lists dozens.
My first plan was to read the plist through QSettings; it would have silently always
returned false.

### I fixed a problem I never demonstrated
I added word wrap "because at 200% the caption is wider than the dock and forces the
viewport off screen". Then I measured: the longest shipped caption wants **263 px** in a
**380 px** dock. Nothing clipped. The panel's own minimum is a constant **151 px** in all
four configurations, because the `QScrollArea` ignores its widget's minimum.

Worse, I had claimed a mutation test proved the fix. Re-reading my own output: all four
failures were `CHECK(caption->wordWrap())` — a restatement of the line I had just written.
The layout assertions passed with and without the change.

Word wrap **is** worth keeping, for a different measured reason: it drops the caption's
minimum from 263 px to 72, so a user dragging the dock narrower keeps a usable panel
instead of one label holding it open. Benefit band ~253-380 px, confirmed by a scrollbar
probe. The test now asserts *that* property and flips when the change is removed.

### Two more tests of mine that could not fail
- `CHECK(animated == !reduced)` reduces to `CHECK(animated == true)` on any machine with
  the setting off — i.e. every CI runner — so it passed even against the previous
  unconditional code. `dockOptionsFor(bool)` is now a pure function and both branches are
  tested machine-independently.
- `CHECK(panel.minimumSizeHint().width() <= 380)` is a constant 151 in every
  configuration. Replaced with the scroll area's page, which actually moves.

### Also
The 200% test built a bare panel while the app always applies the stylesheet, and
QLabel's wrapped `sizeHint` differs between the two. It applies the stylesheet now, so it
measures the regime the program runs in.

### Deliberate, with the reason recorded in the code
`reduceMotion()` is read **once** at construction. Honouring it live needs an
`NSNotificationCenter` observer, a callback across the language boundary and observer
lifetime management — for one flag on one widget, for a setting essentially never changed
mid-session. `Motion.mm` says so, so the next reader does not "fix" it.

### Verified this session
- 329/329 in debug, release and ASan.
- Both surviving assertions mutation-tested: each fails when its change is removed.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- **`reduceMotion()` returning true has never been exercised** — off on this machine and
  CI. Needs one manual toggle in System Settings before shipping.
- VoiceOver on a real device; the readout double-announcement still needs a custom
  `QAccessibleInterface`.

---

## 2026-08-30 17:48:55 — Session 043 · **accessibility, and a focus ring that ringed everything**

### What shipped
- Accessible names on every control, set explicitly as `design.md` §9 requires, with a
  **sweep test** that fails when a control added later arrives without one (14 controls
  covered out of 95 descendant widgets).
- **Arrow-key orbiting in the viewport.** I had given it `StrongFocus` with no
  `keyPressEvent`, so a keyboard user could tab into a control that did nothing — worse
  than not focusing it. Arrows orbit, `+`/`-` dolly, `Home` resets, Shift coarsens, and
  the same clamps the mouse obeys are reapplied.
- Focus rings for buttons, combos, sliders and scroll areas.

### The finding that mattered
`QSlider:focus::handle:horizontal` looks right and is not. **Qt drops the `:focus` when it
precedes a sub-element**, so the border painted on every handle *at rest* and focus changed
nothing. That is a missing accessibility feature and a visual regression at once — all 291
handles permanently ringed, and a keyboard user tabbing onto a slider with no idea which
one they are on while arrow keys silently edit a morph.

The reviewer found it by **rendering the widget twice**, with and without
`State_HasFocus`, and comparing pixels — not by trusting that Qt logged no parse error.
Qt logs nothing for a selector that parses and does nothing. Measured table: as written,
focused == unfocused *and* the unfocused one had the ring; `QSlider::handle:horizontal:focus`
is entirely dead; only `QSlider:focus` responds.

### My test certified the bug
`CHECK(css.contains("QSlider:focus"))` passes for the broken rule, because it is a
**substring** of it. It would also have passed for `QSlider:focus-nonsense`. Replaced with
the paint-and-compare assertion, and **mutation-tested both ways**: it fails with the old
selector, passes with the fix.

### Two claims of mine that were simply false
- `readout->setAccessibleName(QString{})` does not hide the label. Qt falls back to
  `QLabel::text()`, so the value is still announced twice — the exact thing the comment
  said it prevented. Both lines were also no-ops on a default-constructed QLabel. Deleted,
  and the double-announcement recorded as a known gap needing a custom
  `QAccessibleInterface`.
- The test justified `dock->setAccessibleName` by saying the custom title bar shadowed
  Qt's own name lookup. It does not — Qt reads `windowTitle` either way. The line is kept
  as pinning; the false rationale is gone.

### Also fixed
- `setAccessibleDescription` repeated the name, so a reader said "Oval … Oval, in head
  shape". The section is folded into the name now.
- `QDockWidget:focus` could never fire (every dock is `Qt::NoFocus`), and
  `QTabBar::tab:focus` painted exactly what `::tab:selected` already paints, since
  selection follows focus. Both dropped.
- Three exclusion clauses in the sweep collapsed to one: everything this project creates
  has an object name, and nothing Qt creates for us does.

### Verified this session
- 329/329 in debug, release and ASan.
- The focus assertion mutation-tested in both directions.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- 200% text scaling, reduce-motion, and VoiceOver on a real device are still open.
- Undo covers modifiers only; pose, skin and workspace changes are not undoable.

---

## 2026-08-30 16:12:33 — Session 042 · **undo/redo, and an edit that deleted working code while the suite stayed green**

### What shipped
- **`ui::ValueChangeCommand` + `QUndoStack`**, Edit menu on the platform ⌘Z/⇧⌘Z. The
  command holds a key, two floats and a callback and knows nothing about modifiers, which
  is what lets undo live in Apache-2.0 `mh_ui`.
- Drag merging via `mergeId`, `ModifierPanel::editingFinished` to close the group, and
  `resetInProgress` to bracket a Reset into one macro.
- **`app_smoke` ctest** — see below.

### The mistake worth the whole entry
A scripted edit silently deleted ~35 lines: the entire Materials-dock wiring, including
`setLitsphere`. **All 328 tests still passed.** Only running the binary caught it —
`--screenshot` reported `viewport error: texture missing`, and `git stash` confirmed the
same command worked before my change.

That is the third session running where a `str.replace` did something other than what I
believed, and the first where it destroyed working code rather than failing to apply.
The suite could not help because **nothing tested `main.cpp`'s wiring at all**. Added
`app_smoke`: a ctest that runs the real binary through `--load --set --pose --export`.
It is deterministic on a build box because `--export` needs no window — and it explicitly
does *not* cover the window path, which is what actually broke. Recorded as a known gap.

### Review findings fixed
- **The undo stack survived File > Open.** Drag Gender to 1.0, open another character,
  press ⌘Z, and the *previous* document's 0.5 was written into the new one and saved.
  The stack is cleared on load now.
- **`editingFinished` fired before the `valueChanged` it was meant to close.**
  `QAbstractSlider::actionTriggered` is emitted *before* the value lands, measured as
  `finished,changed`. So a keyboard nudge followed by a drag became one undo step — the
  mechanism failed on the one path it was added for, since `sliderReleased` already
  covered drags. `actionTriggered` now only records intent; the emit happens after
  `valueChanged`.
- **Reset pushed one command per slider and never closed the group** — up to 291 presses
  of ⌘Z to undo one click, and the next drag merged into the last reset command.
- **The test could not catch either**: it emitted the signals by hand, so it only proved
  the `connect`s existed. It drives `triggerAction` now and asserts the *order*.
- Ponytail proved the `applying` re-entrancy guard was dead code — `ModifierPanel::setValue`
  wraps the slider in a `QSignalBlocker`, so the callback cannot come back round. Deleted
  rather than made RAII; the existing "setValue moves the slider without emitting" test
  pins the invariant.
- Undo state moved above `window`, matching the invariant this file states twice.

### Known and accepted
A drag out and back leaves a `from_ == to_` command, so Edit > Undo can be enabled and do
nothing visible. Qt cannot un-push a command, so removing it needs care; recorded rather
than papered over.

### Verified this session
- 329/329 in debug, release and ASan.
- The app re-verified by hand after the restore: plain, `--load`, both skins
  (mean luminance 152.3 / 125.6) and `--workspace Export`.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- Undo covers modifiers only; pose, skin and workspace changes are not undoable.
- M8 remainder: task registry, accessibility pass, symbolic shortcut persistence.

---

## 2026-08-30 14:36:12 — Session 041 · **workspaces, and a path traversal I nearly shipped**

### What shipped
- **Nested and tabbed docking** — `setDockNestingEnabled` + `AllowNestedDocks |
  AllowTabbedDocks | GroupedDragging`. Qt draws the drop indicators; without nesting a
  dock can only sit in one of the four areas, so there was nothing to snap into.
- **Workspaces** (`design.md` §6.4) — four presets on ⌘1-⌘4, a Saved Layouts submenu,
  Save Workspace As…, Reset. Named layouts are JSON with a schema version plus base64
  `saveState`/`saveGeometry` blobs.
- `--workspace <name>` so a preset is verifiable in a screenshot.

### The security bug
`saveWorkspaceAs` pasted the name straight into a path. Proven by the reviewer:
`saveWorkspaceAs("../escaped")` returned **true** and wrote one level above the
workspaces directory — with `QIODevice::Truncate`. I was in the middle of adding the
free-text Save dialog that feeds it, so `../../../Desktop/notes` would have replaced that
file with JSON. `isValidWorkspaceName` now rejects separators, `.`/`..`, `:` and NUL, and
both the save and load paths check it.

### Two more the review proved rather than asserted
- **`f.write(json) == json.size()` reports success before any byte reaches the disk.**
  Demonstrated with a probe: a 4096-byte `write()` returned 4096 while the file on disk
  was still 0 bytes; it only landed on `close()`, whose error is discarded. Now
  `QSaveFile` + `commit()`.
- **`state.isEmpty() || restoreState(...)` reported success for a workspace that restored
  nothing** — the exact failure my own comment said the base64 check existed to prevent.
  `workspaceFromJson` now refuses an empty `state` (`saveState()` never produces one).

### A test that could not fail, proven by mutation
The reviewer deleted `restoreState(d_->defaultState)` from `applyWorkspacePreset`, rebuilt,
and the suite stayed green: my "must not accumulate" test only asserted `isHidden()`, which
the visibility loop sets either way. It now moves a dock first and asserts the **position**
comes back, which only the state restore does.

### ASan caught a bug in my own test — twice
`QByteArray("\x00\x01\xFE\xFF binary state", 20)` on a 17-byte literal: a two-byte
global overread, in the test I wrote to check binary blobs survive base64.

**The first fix silently did not apply.** The heredoc turned `\\x00` into a real NUL
byte in the Python pattern, so `str.replace` matched nothing — and I reported it fixed
because the filtered `[workspace]` run happened to come back green. The full ASan suite
caught it again. Lesson already in the log from session 039, now with a second instance:
**verify a scripted edit by reading the file back, not by the command's exit line.**

### Deliberate disagreements with the ponytail review
It recommended deleting the whole JSON layer (~200 lines) as having no production caller,
which was true when it looked. The better answer was to *finish the documented feature*:
Save Workspace As… and the Saved Layouts submenu now call it. It also wanted `--workspace`
cut as a duplicate of ⌘1-⌘4; kept for the same reason as `--skin` — it is the only headless
way to exercise a preset, which is how the viewport measurements exist.

### Also fixed
- `workspaceDirectory()` created the directory as a side effect of a const query, so
  merely opening the menu made one for a user who had never saved a workspace.
- `--workspace Export` was persisted to QSettings on quit, so the next plain launch came
  up with no panels at all.
- A dead `QTemporaryDir` in the test, and `setTestModeEnabled(false)` that a failing
  `REQUIRE` would unwind past, leaving every later test in test mode. Now RAII.

### Verified this session
- 328/328 in debug, release and ASan.
- Viewport area measured per preset: 2,340,644 / 2,779,708 / 3,957,760 px.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.

### Next
- M8 remainder: task registry, undo/redo, accessibility pass, symbolic shortcut
  persistence.
- Proxies still have no picker; the viewport draws one mesh.

---

## 2026-08-30 12:55:41 — Session 040 · **honouring the .mhm fields we only stored**

### What shipped
`camera` and `subdivide` were parsed, carried and written for four milestones without
ever being acted on. Both now work.

- **`mhmCameraFrom` / `orbitFromMhmCamera`** — the format stores a *magnification*
  (`lib/camera.py:454-457`, 0.25..15, default 1.0); this renderer orbits at a distance.
  Mapped by `zoom = 45 / distance`, anchored so the default view writes the default zoom.
  Documented as a convention, not a measurement: the projections differ, so a file opens
  in MakeHuman 1.x at a sensible zoom rather than identical framing.
- **Subdivision** — `--subdivide` and the `.mhm` flag both drive Catmull-Clark.
  19,158 → **75,008** verts, 18,486 → **73,944** faces, matching the documented counts.
  `build()` runs once (6.9 ms) and `refresh()` (0.5 ms) handles every later change.
- **`ViewportWidget::kMinDistance` / `kMaxDistance` / `kMaxPitchDegrees`** — the
  navigation clamps are public now, because a camera restored from a file has to obey the
  same limits the mouse does.

### The defect I introduced and the review caught
Routing every save through the live view meant the **headless** `--save` path — which has
no window — rewrote the camera through float. Verified: `-13.399999999999999` came back
as `-13.399999618530273`, on a framing nobody had touched. `documentFor` now takes
`std::optional<OrbitView>`; absent means leave the camera line exactly as loaded.

The existing precision test did not catch this because it drives `loadMhm`/`saveMhm`
directly and no longer covered the program's actual write path — and `reference_save.mhm`
holds round numbers, so the byte test stayed green.

### Two non-finite holes
- `parseFloat` rejects `nan`/`inf`, but **`1e300` is a perfectly finite double** and a
  legal camera rotation. Narrowed to float it became `inf`, and `saveMhm`'s `".0"` fixup
  wrote **`inf.0`** — unreadable by this loader *and* by MakeHuman 1.x
  (`float('inf.0')` raises). Now range-checked on the way in.
- The divide-by-zero guard tested `zoom > 0`, but `45.0 / 1e-38` overflows float long
  before it divides by zero, giving an infinite orbit distance and an empty viewport with
  nothing reported. My own test certified a guard that did not hold — it checked `0.0`
  and `-2.0` only.

### Also fixed
- A restored camera bypassed the viewport's clamps. MakeHuman's max `zoomFactor` of 15
  maps to distance 3 — **inside the head** — and this is reachable from a genuine
  reference file, no corruption needed.
- `setFaceMask`'s result was checked at one call site and dropped at the other.
- `displayMesh()` was called twice in one expression. The reviewer traced it as safe today
  and explained exactly why (`staticFaceMask` returns by value; `topologyVersion_` is
  bumped in one place that never runs post-startup) — but it ran `refresh()` twice for
  nothing and was one `std::optional` reassignment from a use-after-free no test would
  catch. Bound once.
- A test that could not tell pitch from yaw: the round trip is self-inverse under a
  consistent swap. Now asserts the slots asymmetrically.

### Verified this session
- 328/328 in debug, release and ASan; the app ASan-clean subdividing a loaded character.
- `--load reference_save.mhm --save out` still **byte-identical**.
- A 17-digit camera survives a headless save unchanged.
- All 7 licence gates; `mh_ui` → 0 `mh::core`, and `mh_core` → 0 `mh::render` symbols.
- Subdivider 29.5x / 56.6x the Python baseline.

### Next
- Camera *pan* has no equivalent here; the loaded translation is carried forward.
- Snapping with drop indicators, nested/tabbed docking (M8 remainder).
- Proxies have a loader and 4 shipped assets but no picker, and the viewport draws one
  mesh, so showing them needs multi-mesh rendering.

---

## 2026-08-30 11:18:07 — Session 039 · **.mhm save, and a Save that threw the character away**

### What shipped
- **`core::saveMhm` / `mhmFromHuman`** — the `.mhm` writer. There was only a reader.
- **File menu** with Open / Save / Save As on the platform shortcuts, plus `--load` and
  `--save` for scripted use.
- **`Human::resetToDefaults()`** — what the reference does before applying a load.
- **`foundation::parseFloat` / `formatShortest` at double precision.**

### Byte parity, this time with a fixture I can vouch for
`tests/golden/mhm/reference_save.mhm` is the reference's own `Human.save()` output,
captured by `capture_fixture.py mhm_save`, carrying a uuid, mixed-case duplicate tags, a
camera and two plugin lines — none of which the older fixtures had.

**The two older fixtures were committed without a generator**, so the claim in the test
that they came from the reference was unsupported. Corrected: they stay as round-trip
corpus, and the new one is the evidence.

### The bug that mattered
`writeTo` and `--save` built the document with `mhmFromHuman`, which carries only
version, name and modifiers. **uuid, tags, camera, subdivide and every plugin line the
loader had carefully preserved were dropped on every save.** Open a rigged, clothed
character, press Cmd+S, reopen: naked, unrigged, unnamed. Now `documentFor` refreshes the
modifiers on the loaded file instead, and `--load X --save Y` reproduces X byte for byte.

And `applyMhm` never reset, so loading a second character blended it with the first: a
modifier the new file does not mention kept the old value, in the mesh *and* in the
slider. The reference resets at `human.py:1486`. Fixed, and the panel now syncs from
`human` rather than from the file's own lines, which is what makes the omitted sliders
come back.

### A review finding that was wrong, and how I knew
The review reported that the reference writes `subdivide` with **no** trailing newline
(`human.py:1640`), making my writer wrong and my fixtures impossible. Running the
reference's save proved the opposite: the file **does** end with `0x0a`, because
`f = SaveWriter(f)` at `:1633` wraps the handle and `SaveWriter.write` appends a newline
when the text lacks one (`:1619-1623`). My writer was right. Worth recording because the
finding was specific, cited a real line, and was still wrong — running it was the only
way to tell.

### Other review findings fixed
- **tags**: the reference lower-cases, truncates to 25 and de-duplicates on load
  (`human.py:131`, reached at `:1524-1526`). `Zulu;alpha;MIKE;alpha` must come back as
  `alpha;mike;zulu`; ours kept case and order.
- **camera was float**: the reference writes Python doubles with `'%s'`, up to 17
  significant digits, so `-13.399999999999999` came back as `-13.4`. Now double, with
  `parseFloat`/`formatShortest` overloads in foundation.
- `--load` never seeded the panel, so the sliders lied — the exact failure the comment
  20 lines above it warns about.
- `documentPath` and the File-menu lambdas were declared **after** `window` while their
  connections are owned by it. Hoisted, same as `rebuildInto` last session.
- Ponytail: `--save` now always exits as its help says; one version constant instead of
  two literals; the status-bar echo of the path dropped (the title already shows it); the
  duplicate window-level `addAction` dropped (Qt routes menu-bar shortcuts).

### Corrections made this session
- Four scripted edits aborted on their own assertions after clang-format reflowed the
  target text; twice I reported an edit as applied when the script had died before
  writing. Caught by grepping for the change rather than trusting the exit line.

### Verified this session
- 323/323 in debug, release and ASan; the app ASan-clean loading and saving.
- `--load reference_save.mhm --save out.mhm` → **byte-identical**.
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.
- Benchmarks unchanged.

### Next
- A new character writes no `camera` line: the viewport camera is not plumbed into the
  document. The reference always writes one.
- Snapping with drop indicators, nested/tabbed docking (M8 remainder).
- Proxies (clothes, hair, eyes) have a loader and 4 shipped assets but no picker.

---

## 2026-08-30 09:41:22 — Session 038 · **skin and pose pickers, and a 33 cm pose bug**

### What shipped
- **`ui::AssetPanel`** — labelled pickers, one per asset group, fed plain
  `foundation::AssetGroup`s. The Materials dock is real.
- **App** — `--skin` alongside `--pose`; both drive the same state the pickers do, so
  the panel and the viewport cannot start out disagreeing. Groups are *scanned* from
  `data/litspheres` and `data/poses`, so a dropped-in asset appears with no code change.

### The bug the review caught, with a number
Switching pose → pose fitted the rig to the **already-posed** mesh. `loadPoseRig` calls
`updateJoints(mesh.coord())`, and `poseInPlace` leaves the mesh posed after every
rebuild — so the second pose was conjugated into the first pose's rest frame. The
reviewer probed it: **3.33 dm (33 cm) maximum, 0.83 dm mean, across all 19,158
vertices.** Gross visible corruption, not drift, and it produced a complete, smooth,
plausible body — which is why nothing looked wrong.

Fix: reset to the morph base (`applyStack`) *before* loading the new rig. Pinned by a
test that fits the same pose twice — once from rest, once from the posed result — and
asserts they differ by more than 1 dm while the rest-fitted one matches the reference
fixture to 1e-3.

### Other review findings fixed
- **`rebuild` was declared after `window`**, so it died before the panels whose
  connections referenced it — and the comment above it asserted the opposite ordering
  was load-bearing. Nothing emits during teardown today, so it was latent, but the
  invariant was already false. Now `rebuildInto(window)`, declared above the window and
  taking it as an argument.
- **`std::error_code` written and never read.** A missing `data/litspheres` left the
  group empty, `setLitsphere` never called, and `SceneResources::upload` failing
  *every frame forever* because the viewport deliberately retries. Now reported, and a
  missing litsphere directory is a clean exit rather than a spinning blank window.
- **`--skin` typo silently rendered African**, because the fallback took index 0 while
  the help documented caucasian. Now falls back to the documented default and says so.
- **Pose alias desync**: `loadPoseRig` accepts `tpose` and `t-pose`, but only the first
  was matched when selecting the picker entry, so `--pose t-pose` opened a T-posed model
  over a picker reading "A-pose (rest)" — and clicking that entry was a no-op because
  the index was already 0. Aliases now compare on the stem with hyphens stripped.
- Ponytail: `AssetPanel` lost its `QHash` side-index (QObject already indexes children by
  name), `groupCount()`, `setChoice()` and a stray `<QList>`; both panels' bold headings
  moved from `QFont` calls into one stylesheet rule.

### Deliberate disagreement
Ponytail wanted `--skin` cut as "a second way to say one thing". Kept: unlike `--pose`
it does not change the exported mesh, so `--screenshot` is the **only** headless way to
exercise the litsphere path — which is how the three skins were shown to render at
152.3 / 125.6 / 148.4 mean luminance.

### Corrections made this session
- Claimed `--set` error paths exited non-zero after reading `$?` through a pipe again;
  the real check needed `${PIPESTATUS[0]}`.
- A `str.replace` on the stylesheet asserted against a double-escaped string that never
  matched, so the edit silently did nothing while I reported it as applied. Caught by
  grepping the file afterwards.

### Verified this session
- 316/316 in debug, release and ASan; UI binary 226 assertions / 34 cases.
- The GUI itself ASan-clean (`--skin african --pose t-pose --screenshot`).
- All 7 licence gates; clang-format clean; `mh_ui` → 0 `mh::core` symbols.
- Benchmarks unchanged: `applyStack` 0.07 ms, `skinPositions` 0.13 ms.

### Next
- Snapping with drop indicators, and nested/tabbed docking (M8 remainder).
- Proxies (clothes, hair, eyes) have a loader and 4 shipped assets but no picker.
- `.mhm` load/save is not wired to the UI, so a character cannot be saved from the app.

---

## 2026-08-29 17:04:38 — Session 037 · **291 sliders, and a tab order I had wrong**

### What shipped
- **`core::loadSliderLayout` / `loadStandardLayout`** — ports `modifiers/*_sliders.json`,
  the reference's own tab registry: **7 task views, 50 sections, 291 sliders**. Full parity
  on order, labels, ranges, defaults and camera hints (`tests/golden/slider_layout/`,
  1,912 assertions).
- **`foundation::SliderSpec`** — the licence bridge, same idea as `RenderView`.
  `mh_ui` drives 291 modifiers and still has **0 `mh::core` symbols**.
- **`ui::ModifierPanel`** — tabbed, sectioned, searchable, with a Reset control.
- **App** — the Modelling dock is real, and `--set <modifier>=<value>` (repeatable) makes
  render and export scriptable.

### Two ordering traps
1. **`applyStack` resets the mesh to its morph base**, so posing has to come *after*
   morphing or the pose is silently discarded on every slider move. The rig is also
   re-fitted each time (`updateJoints`), because a morphed body has moved its joints.
2. **Tab order is not file order.** I shipped file order (Face first). The reference sorts
   by `sortOrder`, assigning a view that gives none the lowest free non-negative integer
   (`gui3d.py:300-317`), ties keeping load order. Correct order is
   **Macro modelling, Body shapes, Gender, Face, Torso, Arms and Legs, Measure**.
   The ponytail review flagged `sortOrder` as a dead field; the right answer was to *use*
   it, not cut it. Verified by cropping the tab strip out of a screenshot and reading it.

### Verified end to end, in Blender
| Export | Height | Width |
|---|---|---|
| default | 16.94 dm (169 cm) | 10.54 |
| `Gender=1.0` | 17.67 dm (177 cm) | 11.35 |
| `Age=0.0` (1 year old) | **6.33 dm (63 cm)** | 3.88 |
| `Gender=1.0` + T-pose | 17.67 (height unchanged) | **18.43** |

The last row is the point: morph and pose compose. Height matches the un-posed male
exactly while the span goes 11.35 -> 18.43.

### Review findings fixed
Code review (7) and ponytail (13). The ones that mattered:
- **Use-after-free in `setModellingWidget`**: `delete dock->widget()` then `setWidget(widget)`
  frees and reparents the *same* pointer when called twice. ASan-confirmed SEGV. Guarded.
- **`--set macrodetails/Gender=nan` produced an all-NaN mesh and exited 0.**
  `QString::toFloat` accepts "nan" and `std::clamp` passes NaN straight through, because
  both comparisons are false. `std::isfinite` closes it. A trust boundary, so not a
  lazy-skip.
- **`--set` never reached the panel**, so the mesh was morphed while the sliders showed
  defaults and the first nudge snapped the model back. The same hole would have swallowed
  `.mhm` loading.
- The header promised `filter()` hid emptied **tabs**; it only hid sections. Implemented
  rather than downgrading the comment.
- `ModifierPanel` lost its pImpl, its precomputed lowercase `haystack`, its `search`
  member and the O(sections x rows) rescan (one counting pass now).
- Hand-rolled `split()` -> `std::views::split`; `optString()` -> `json::value()`;
  `saveName` deleted (it appears in **zero** shipped files).
- One vacuous test assertion replaced: `sliderCount()` cannot change, so it proved nothing
  about the unknown-id path.

### Corrections made this session
- Two `str.replace` edits silently failed to match after clang-format reflowed the code,
  leaving `d_->` references behind. Caught by the compiler; the lesson is that every
  scripted edit needs its assertion, which is why the failing ones aborted before writing.
- I fixed the tab ordering in the tests and **not** in `main.cpp`, so the tests passed
  while the app still showed file order. Caught by looking at the screenshot rather than
  trusting the green suite.
- Read `$?` after a pipe again and reported a failing `--set` as exit 0.

### Verified this session
- 315/315 in debug, release and ASan; UI binary 217 assertions / 31 cases.
- The app itself ASan-clean with modifiers + pose + export.
- All 7 licence gates; clang-format clean; `mh_ui` -> 0 `mh::core` symbols.
- Benchmarks unchanged (`Human::applyStack` 0.07 ms, `rebuildStack` 0.01 ms).

### Next
- Materials dock still says "not yet implemented".
- Nothing in the window selects a pose; `--pose` remains CLI only.
- `resetAll()` emits 291 changes, each driving a full stack rebuild and re-skin. Fine at
  0.07 ms each today, but it is 291 of them.

---

## 2026-08-29 16:02:11 — Session 036 · **the dark theme, and a stylesheet rule that painted over itself**

### What shipped
- **`ui::theme`** — the `design.md` §3 token table as 21 named `QColor`s, a WCAG 2.1
  contrast function, a generated stylesheet, `QFontDatabase` registration of the bundled
  42dot Sans, and Lucide SVGs recoloured at load.
- **`PanelTitleBar`** — the six-dot panel menu (`design.md` §6.3). Float, Dock
  Left/Right/Top/Bottom, Tab with…, Reset This Panel, Close. Qt has no hook for extra
  title-bar buttons, so the whole bar is replaced.
- 14 new test cases; the UI binary is now 180 assertions across 23 cases.

### The bug the reviewer caught by sampling pixels
`QWidget { background: bgBase; }` matched **every child of every dock**, so it painted
the window's base colour opaquely over the `QDockWidget { background: bgPanel }` rule
below it. The panel-vs-base separation the entire design system rests on never rendered,
and nothing failed: the app looked plausible and the contrast tests measured text against
a surface that was never painted.

The fix needed two parts, and the second is a Qt trap worth remembering:
1. Scope the background rule to `QMainWindow, QDialog`, keep `QWidget { color: … }`
   (colour inherits without painting).
2. **Qt does not honour a stylesheet `background` on a plain `QWidget` subclass** unless
   `WA_StyledBackground` is set. The rule matches, parses, and paints nothing.
   `PanelTitleBar` needed the attribute.

Verified by sampling the app's own screenshot, which is how the reviewer found it:

| Surface | Sampled | Token |
|---|---|---|
| dock title bar | `#2a2a2e` | `--bg-panel` |
| dock body | `#2a2a2e` | `--bg-panel` |
| status bar | `#212124` | `--bg-base` |
| viewport | `#1a1a1c` | `--bg-viewport` |

The viewport had its own drift: `ViewportWidget` hard-coded `#19191b` while the token
says `#1a1a1c`. Now reads the token.

### design.md was wrong and has been corrected
The recorded contrast ratios were **13.1 / 6.2 / 3.4**. Measured: **12.12 / 6.05 / 3.17**.
Every conclusion survives (AAA / AA / large-text-only) but the figures did not.
`tests/ui/test_theme.cpp` now computes them from the tokens, so the table and the code
cannot drift again.

### Review findings fixed
Code review (8) and ponytail (14). The ones that mattered beyond the stylesheet:
- `buildMenu` documented the menu as caller-owned while giving it a QObject parent —
  two owners, and only declaration order kept the suite from double-freeing. ASan proved
  it. Now returns `std::unique_ptr<QMenu>` with no parent.
- A "Tab with…" entry captured its target dock with the **wrong context object**, so
  deleting that dock left the action live and handed a freed pointer to
  `tabifyDockWidget`. Qt 6.11 happens to only pointer-compare, so it silently no-ops —
  relying on an undocumented internal. Context is now the target itself.
- `icon()` carried a comment claiming device-pixel-ratio rendering that the code did not
  do; a 16 px title-bar icon was stretched to 32 device pixels. Now actually rasterised
  at the DPR.
- `g_iconDir` had no default and is read during widget construction, so a `MainWindow`
  built before `setIconDir` got two invisible title-bar buttons. Catch2 randomises order,
  so which tests saw icons was luck. Defaults to the shipped directory now.
- The stylesheet test could not catch the bug it looked like it guarded: an unsubstituted
  `%12` carries no `#`, so the regex sailed past while Qt rejected the whole sheet.
  Now also asserts no `%` survives.

### Structural fix: CI can no longer skip the renderer silently
Last session found CI had been green on code it never compiled. Added `MH_REQUIRE_RENDER`,
set in the CI matrix: a missing Qt module is now a `FATAL_ERROR` instead of a status
message nobody reads. **Verified by configuring with `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`**
— rc=1 with the flag, rc=0 without it, so the no-Qt fallback job still configures.

### Corrections made this session
- A regex meant to swap `hex(c)` for `c.name()` mangled the `.arg()` chain into unbalanced
  parentheses. Caught by reading the result, not by the compiler — the file was rewritten
  rather than patched.
- Claimed the `#panel\.titlebar { background }` rule was part of the fix. Its edit
  failed its own assertion and never applied; `WA_StyledBackground` alone was the fix.
  Reported after re-sampling, not before.

### Deliberate disagreements with the ponytail review
- **Kept all 21 palette tokens** though 5 have no code consumer yet. The `Palette` is the
  machine-readable copy of a documented spec, not a speculative abstraction, and the
  tests validate the whole table. Wired `accentHover`, `accentPress`, `textDisabled` and
  `bgViewport` into real rules while here.
- **Kept `setIconDir` and the `fontDir` parameter.** A `.app` bundle resolves resources
  relative to the executable, not the source tree; `MH_RESOURCE_DIR` is a dev-time
  default, not the shipping answer.

### Verified this session
- 309/309 in debug, release and ASan; UI binary 180 assertions / 23 cases.
- All 7 licence gates `rc=0`; clang-format clean; boundary holds (`mh_ui` → 0 `mh::core`).
- Benchmarks unchanged against the Python baseline.
- Qt version corrected to 6.11.1 in `LICENSING.md`, and **Qt Svg** recorded as a newly
  linked module.

### Next
- Task-view registry and the modelling sliders — the docks still say "not yet implemented".
- Nothing in the window selects a pose; `--pose` is still CLI only.
- `Duplicate` in the panel menu needs a panel factory that does not exist.

---

## 2026-08-29 15:12:04 — Session 035 · **a window, and a T-pose that is actually a T-pose**

### What shipped
- **`mh_ui` (Apache-2.0)** — `ViewportWidget` (QRhiWidget on Metal, 4x MSAA, orbit on
  left-drag with pitch clamped +/-89, multiplicative wheel dolly clamped 5-300) and
  `MainWindow` (two dockable panels, workspace save/restore/reset via QSettings).
- **`makehuman` (AGPL)** — the app. `--pose`, `--export`, `--screenshot`, `--shaders`.
  The AGPL side loads the mesh; the UI only ever sees a `foundation::RenderView`, which
  is what keeps `mh_ui` and `mh_render` permissive. Verified: `nm -u libmh_ui.a` finds
  **0** `mh::core` symbols.
- **`rig::loadBodyPose`** and **`rig::poseToBoneLocal`** — whole-body poses from a
  single-frame BVH.

### The bug the oracle caught
`loadBodyPose` alone produced a pose that loaded cleanly, moved every vertex, and was
**wrong**: the shipped T-pose came out with the arms only part-way up (body X span
**10.67 dm** instead of 16.0). It looked like a stylistic difference, not a defect.

Cause: a BVH stores rotations in the file's global axes, and `computeSkinningMatrices`
wants them in each bone's rest frame. The reference conjugates them in
`shared/skeleton.py:566-593`:

    matPose = inv(matRestGlobal) * pose * matRestGlobal

We fed the raw matrices straight in. `poseToBoneLocal` now does the conversion, and it
is documented as the trap it is. Only tests called `computeSkinningMatrices` before, so
nothing else was affected — the bug existed only because nothing had ever loaded a real
pose file.

**Three-way agreement after the fix** (all from this session, all run):
| | A-pose (rest) X span | T-pose X span |
|---|---|---|
| C++ (body only) | 9.9254 dm | 15.8967 dm |
| Python reference (all verts) | 9.9464 dm | 15.9893 dm |
| Blender, `tpose.obj` | 9.9464 dm | 15.9892 dm |

Vertex-for-vertex parity against the reference's own posed mesh: worst error
**< 1e-3 dm** against a 6.21 dm maximum displacement.

### CI was green on code it never compiled
The matrix installed only `ninja assimp`, so `MH_HAVE_RENDER` was **FALSE** on CI and the
whole `render/ui/app` subtree was silently skipped. Every "CI green" claim since the
renderer landed covered `core`/`io`/`rig` only. Fixed: `brew install ... qt` plus
`CMAKE_PREFIX_PATH`. Also added the missing `macos-arm64-release` **test** preset — the
workflow had been running `mh_tests` directly, which skips the render and ui suites.
Render tests now `SKIP` loudly on a machine with no Metal device rather than failing.

### Corrections made this session
- Asserted a T-pose would be **shorter** than an A-pose. Measured: marginally **taller**
  (16.71 vs 16.66 dm) — standing height is head-to-foot either way. Assertion replaced
  with the measured relationship.
- Wrote a test using `benchmark.bvh` as "a multi-frame animation". It is **single-frame**.
  Now uses the 60-frame face pose-unit BVH.
- While removing `Camera::view()` I retyped its translation with a `-8.0F` Y offset that
  was never there. Caught before building; restored to `(0, 0, -distance)`.
- Guessed at `Mesh`/`FaceGroup` APIs from memory instead of reading them; eight compile
  errors. `FaceGroup` has only `name` and `idx` — face-to-group mapping is
  `staticFaceMask()`.
- Twice read `$?` after a pipe and got `head`'s status, once reporting a failing
  format gate as clean.

### Review findings fixed
Code review (11) and ponytail (14). The ones that mattered:
- `initialize()` tore down and rebuilt every GPU resource on **every resize** — two disk
  reads, a PNG decode and a pipeline compile per drag event. Now compares the device and
  render pass it was built against.
- `--screenshot` computed "nothing drew" and **exited 0**. Now exits 3, and checks the
  viewport error *before* printing success rather than after.
- A renderer failure showed a black rectangle and a status bar reading "Ready". The
  viewport now emits `statusChanged` and the window shows it.
- `needsUpload` was cleared even when the upload failed, so a transient failure was never
  retried.
- The one render test without the device guard was the one that misfires without a device.
- `resetWorkspace` hand-rebuilt the layout; it now restores a `saveState()` snapshot taken
  in the constructor, which also covers docks added later.

### Verified this session
- 309/309 tests in **debug, release and ASan**.
- All 7 licence gates `rc=0` (run with `/usr/bin/grep`, not ugrep).
- clang-format clean across every tracked source.
- Benchmarks 6.6x-56.5x faster than the Python baseline; no regression.
- 14 exports written (7 formats x 2 poses); Blender re-imported obj/glb/fbx for both.

### Next
- Dark theme from `design.md` tokens, Lucide icons, 42dot Sans, the six-dot panel menu.
- `poseToBoneLocal` is not yet reachable from the UI — `--pose` is CLI only.
- Known and measured, not fixed: `OffscreenRenderer::render` rebuilds the pipeline per
  call because each call makes its own render target. Costs ~2 ms in a non-hot path.

---

## 2026-08-29 — Session 034 · **it renders**

**Ended:** 2026-08-29 17:28:50 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

The first thing this port puts on screen. Qt RHI on **Metal**, the ported
litsphere shader, the base mesh drawn to an image. CI green.

**Offscreen first, deliberately.** A widget cannot be checked in a test; an
offscreen render produces pixels that can be *measured*. `mh_render_probe`
reports coverage and luminance because a **blank frame is the failure that reads
as success in a log**. Measured: 7.9% coverage, luminance 37..212, mean 156.1 —
and the image is a recognisable human body.

### `base.obj` is 138 parts helper geometry to 1 part body

138 of its 139 face groups are `joint-*` markers and `helper-*` fitting cages;
only `body` is visible human. Drawing it raw gives a figure in a **solid skirt
with a box over its face** — plausible enough to read as a renderer bug rather
than geometry never meant to be shown.

`Mesh::staticFaceMask()` applies the reference's own rule
(`apps/human.py:274-289`), leaving 13,378 of 18,486 faces. **This is what the
M2 face-masking work was for.**

### Two of my own assumptions were wrong

Both checked rather than left standing:

1. I wrote that winding is inconsistent after fan triangulation and disabled
   culling on that basis. **It is consistent** — culling on and off give
   byte-identical statistics. Culling is now on (free halving of fragment work)
   and the comment states what was measured.
2. Dark patches on a forearm and the feet looked like bad normals. The normals
   are clean — all 19,158 unit, none zero or non-finite. **The cause was mine**:
   I had bound the litsphere to *both* the litsphere and diffuse slots, so
   "diffuse" was the matcap sampled by the MESH's UVs. A 1×1 white stand-in
   fixed it; mean luminance 64.5 → 156.1.

### The renderer is optional in the build

`find_package(Qt6 QUIET)`, so the CI runner (no Qt) builds and tests everything
else. Verified with `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`: renderer disabled,
301/301.

### The licence gate earned its keep again

`mh_render` is Apache-2.0 and links only `mh::foundation` and Qt — but
`mh_render_probe` links `mh::core`, and having that target in
`src/render/CMakeLists.txt` made the boundary gate fire on that file. **The gate
was right**: an Apache module's build file should not mention the AGPL one at
all. The probe moved to `tools/`, and the gate now covers `render`.

302/302 across all four builds, including 5 render tests that measure pixels.

**Next:** the interactive viewport — swapchain, MSAA, and the documented
navigation bindings, so the same pipeline drives a window.

---

## 2026-08-29 — Session 033 · format documentation

**Ended:** 2026-08-29 17:07:49 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

Eight documents in `docs/formats/`. The seven native formats have **no
specification anywhere** — the Python source was the only definition — so this
is how that knowledge stops being locked inside `legacy/python/`, which is
scheduled for deletion.

### The one worth keeping if only one could be

`docs/formats/README.md` — UV origin, up axis, matrix order and units for every
format we touch, with **what each disagreement actually cost**:

- glTF's UV origin is **top-left**, so V is flipped. Everything else is
  bottom-left.
- USD's is bottom-left, so V must **not** be flipped — having just written the
  glTF flip, carrying it over is the natural mistake.
- glTF matrices are **column-major**; ours are row-major.
- BVH **does not record its up axis at all**. Both shipped pose files are Z-up;
  assuming Y-up yields a plausible skeleton lying on its side.
- **Blender's world is Z-up**, so height arrives as its Z — reading Y reports the
  body's depth and looks like a 4× unit error that is not there.

Every one produces a plausible wrong answer rather than an error, and three of
them cost real time before being understood.

### `.mhpose` is documented as `bvh.md`

There are **no `.mhpose` files in `data/` at all** — the shipped pose data is
BVH. Documenting a format nothing uses would have been fiction.

### Facts checked, not recalled

A script verified all **14 Python citations** resolve to files that exist and to
line numbers within them, and all **17 repo paths** exist.

It caught a real error: `mhmat.md` claimed to show "a real example" while
quoting the tag as `MakeHuman(tm)` when the shipped file says `MakeHuman™`. A
document that shows a real example must show the real text.

301/301 in debug and release. CI 7/7.

**Next:** M7 has USDZ (a zip — packaging, not geometry) and UsdSkel left, but
both are lower value than starting **M8, the application shell** — the whole
point of the port is a working desktop app, and nothing yet renders.

---

## 2026-08-29 — Session 032 · unit correctness, the reference's known defect

**Ended:** 2026-08-29 16:55:34 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

`fbx_binary.py:736` hardcodes `scale_factor = 10.0` with the correct
`10.0/config.scale` commented out **one line above**, so every non-decimetre FBX
export in the reference is off by the configured scale. A character that is 1.7 m
in one exporter and 17 m in another is the commonest interchange failure there
is, and it stays invisible until someone opens the file in a tool that respects
units.

Every writer is now checked at **all four units** (dm, m, cm, inch), with the
height measured back **out of the file** rather than taken from the writer's
return value:

| format | measured from |
|---|---|
| OBJ | its own `v` lines |
| glTF | the POSITION min/max |
| USD | `extent` |
| FBX | re-importing it |

Plus the property a user actually depends on: **all writers agree at the same
unit**, so the same character is the same size in two tools.

### Checked the test was not self-cancelling

If assimp normalised units on FBX import, the round trip would cancel and the
test would measure nothing. It does not: dm returns **16.9455**, cm returns
**169.455** — ratio exactly 10.0. Worth doing before trusting any round-trip
test.

### A real inconsistency, found while writing the test

`UsdWriteOptions` took a bare `float scale` while every other writer takes a
`Unit` — USD was the one exporter whose scale could not be set the same way as
the rest, which is exactly the "one UnitSystem consumed by every writer" M7
exists to establish. Now takes `Unit`, and `metersPerUnit` is **derived** from
it rather than set independently: two knobs for one physical fact is how a file
ends up claiming metres while holding centimetres.

### Memory reconciled against reality

Three todo entries were stale — skin-weight clamping (session 022) and glTF
blendshapes (session 029) were both done but still listed open. Session-start
rule applied: state beats memory.

301/301 across all four builds. 7/7 agree with Blender. CI 7/7.

**Next:** `docs/formats/*.md`. The per-format conventions have now cost real time
three times (glTF's flipped V, USD's un-flipped V, BVH's guessed up axis) and
they are scattered across code comments.

---

## 2026-08-29 — Session 031 · USD export, written not linked

**Ended:** 2026-08-29 16:41:55 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

**Checked availability before choosing.** assimp has **no USD support at all** —
neither import nor export — verified against the linked build's own format list
rather than assumed from docs. OpenUSD is the alternative but is a very large
build for a mesh export, and carries Pixar's modified Apache-2.0 with a
trademark clause.

`.usda` is documented text, so it is written directly: no new dependency, and it
stays on the permissive side. I learned the exact syntax by **exporting a cube
from Blender and reading it** — the "learn from their implementation, write our
own" the owner asked for.

Blender confirms: 21,833 verts / 36,972 tris / 1 UV layer / **169.5 cm** —
identical to glTF and FBX. **7/7 exports now agree across four formats.**

### Three conventions that differ from glTF

1. **USD's UV origin is bottom-left**, same as OBJ and MakeHuman. glTF's is
   top-left and flips V; doing that here would mirror every texture vertically.
   A test asserts 0.25 stays 0.25 rather than becoming 0.75.
2. **USD records its up axis** in the header — a consumer never has to guess it
   from geometry the way a BVH reader must.
3. **`subdivisionScheme = "none"`** written explicitly: without it a consumer may
   treat the mesh as a subdivision cage and render a smoothed, shrunken body.

`interpolation = "vertex"` rather than `"faceVarying"` because RenderView is
already unwelded — one normal and one UV per point, no per-corner variation left.

### A test bug, found by the test failing

The marker `"int[] faceVertexCounts = ["` **itself contains a `]`**, so searching
for the closing bracket from the marker's *start* finds that one four characters
in and yields an empty array — it counted zero 3s out of 36,972. Both array
lookups now search from *after* the marker. Worth remembering when parsing any
format whose type syntax includes brackets.

296/296 across all four builds. CI 7/7.

**Next:** M7 leftovers — USDZ is a zip of the `.usda` (packaging, not geometry),
and UsdSkel would add the rig. glTF and FBX already carry the rig, so the
per-format docs (`docs/formats/*.md`) may be the more valuable next step.

---

## 2026-08-29 — Session 030 · FBX rig and blendshapes

**Ended:** 2026-08-29 16:29:16 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

**Checked before promising.** A probe scene with two bones and one `aiAnimMesh`
round-tripped through assimp's FBX writer, and Blender read back the armature,
vertex groups and shape key. Only then was this built. A writer that silently
dropped skins would have been worse than not offering the feature.

Blender confirms the real export: **163 bones, 21,833/21,833 skinned, 3 shape
keys** — and the moved-vertex counts (2200 / 5865 / 294) match `morphed.glb`
**exactly**. Two independent formats agreeing is stronger than either matching
an expectation I wrote.

139 vertex groups rather than 163 is *correct*: only 139 bones carry weight in
the `.mhw`, and both assimp and Blender drop the empty ones.

### Two non-obvious things, one failed export each

1. **Every `aiBone` needs a NODE of the same name.** The bone array is not a
   skeleton — the hierarchy lives in the nodes, and `aiBone` references it by
   name. Without them the writer fails outright:
   `Failed to find node for bone: root`. That is how it was found.
2. **`aiAnimMesh` holds ABSOLUTE positions, not deltas.** Sending deltas gives a
   shape key that collapses the model toward the origin when enabled — reads as
   corrupt geometry, not a units mistake.

Node transforms and bone offset matrices both derive from the same scaled global
array (as in the glTF writer), so the rig cannot drift from the mesh when the
export unit changes.

291/291 across all four builds. 6/6 agree with Blender. CI 7/7.

**Next:** M7's remaining interchange work — USD/USDZ export, or the per-format
docs. USD is the bigger gap for a modern DCC pipeline.

---

## 2026-08-29 — Session 029 · blendshapes, with no oracle to lean on

**Ended:** 2026-08-29 16:17:19 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

The reference's blendshape export is **dead in every format**
(`project_context.md` §8), so there is nothing to compare against. Blender is the
**primary** check here, not a cross-check — which is what makes wiring it in last
session load-bearing rather than merely nice.

### Verified by moved-vertex counts, reconciled to the source

| target | non-zero rows | Blender moved | seam copies |
|---|---|---|---|
| head-oval | 2,143 | 2,200 | +57 |
| head-trans-backward | 5,498 | 5,865 | +367 |
| nose-base-up | 294 of **305 rows** | 294 | +0 |

**The nose number looked wrong** — fewer moved vertices than the file has rows,
which UV seams cannot cause since they only ever duplicate. Cause: **11 of its
305 rows are literally (0,0,0)**, no-ops the reference stores happily. A test
pins that count, because it is exactly the kind of discrepancy that otherwise
gets explained away rather than explained.

### Three details that fail quietly

1. **Deltas take the unit scale but NOT the ground offset.** A delta is a
   displacement, not a point; adding the offset would translate the whole body
   once per active target.
2. **Morph POSITION accessors need min/max** like any other. Omitting it is the
   commonest way a hand-written morph export fails validation while still
   loading in some engines.
3. **`extras.targetNames`** is an extras convention, not core glTF — but every
   DCC reads it. Without it the keys import as "Key 1", "Key 2" and are useless.

### Sparse vs dense

Targets are sparse by nature (~2k of 19,158 touched). glTF *can* express that
with a sparse accessor but support is patchy, so dense is written. That makes
the caller choose which targets to export: all 1,280 densely would be **~335 MB**.
Recorded as a future optimisation rather than left as a surprise.

The validator now reports how many vertices each shape key actually **moves** —
a key that exists but displaces nothing is the failure a name-only check misses.

288/288 across all four builds. 5/5 agree with Blender. CI 7/7.

**Next:** FBX blendshapes — but check first whether assimp's FBX writer carries
morphs at all, rather than promising it.

---

## 2026-08-29 — Session 028 · rigged glTF, checked by Blender

**Ended:** 2026-08-29 16:04:29 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

The 4-influence compile from session 022 finally has a consumer. A rigged GLB
carries `JOINTS_0`, `WEIGHTS_0`, `inverseBindMatrices` and one node per bone.

**Blender confirms it independently: 163 bones, 1 armature, all 21,833 vertices
skinned.** 4/4 exports agree. That is a check neither our code nor the Python
reference can make about itself.

### Three things that fail quietly, all now pinned

1. **glTF matrices are COLUMN-major**; ours are row-major, so they transpose on
   the way out. Writing them unchanged gives a file that loads, poses, and is
   wrong in a way that looks like bad weights.
2. **Joints must scale with the mesh.** The writer derives node transforms *and*
   inverse binds from one scaled array rather than trusting the caller, so they
   cannot drift. A test asserts both scale by exactly 10× between metres and
   decimetres. A rig 10× the mesh reads as a rigging bug, not a unit bug.
3. **Weights are per MESH vertex (19,158); glTF's are per RENDER vertex
   (21,833).** They are vertex attributes. Skipping the `vmap` expansion leaves
   everything past the first UV seam weighted to the wrong bone.

### Blender's own helper geometry

Its glTF importer creates an Icosphere as the custom bone shape for all 163
bones and parks it in a collection named `glTF_not_exported`. Counting it made a
**correct** export look like it had 42 stray vertices and a second mesh. The
validator now skips anything Blender marks not-for-export, and reports vertex
groups and skinned vertices — an armature with no weights looks fine in a static
screenshot and deforms nothing.

### Two of my own mistakes, caught by review rather than CI

- A test compared a value **to itself** (`joints[i] != joints[i]`), so it could
  never fail. Now compares against the compiled source weights.
- I asserted two exports at different scales would be the **same file size**.
  The numbers are text, so "0.5" and "5" differ in length. Replaced with the
  property actually worth checking.

Also: `SkinView::valid()` hardcoded glTF's influence count of 4 into a
format-neutral type. Structure is the type's business; the count is the
format's.

283/283 across all four builds. CI 7/7.

**Next:** blendshape (morph target) export — dead in the reference everywhere,
so there is no oracle and Blender becomes the primary check.

---

## 2026-08-29 — Session 027 · Blender as a third implementation

**Ended:** 2026-08-29 15:47:01 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

Owner pointed out Blender headless is available. It is worth more than it first
looks, because **every existing check shares lineage with what it checks**:
parity tests compare us against the Python reference we were ported *from*, and
the glTF/FBX tests read back through assimp, which also *wrote* the FBX. A
convention both sides get wrong the same way passes all of it.

Blender 5.2 has never seen this codebase. **3/3 exports agree.**

| export | vertices | triangles | tallest extent |
|---|---|---|---|
| `base.obj` | 19,158 welded | 36,972 | 16.9455 (dm) |
| `base.glb` | 21,833 unwelded | 36,972 | 1.69455 (m) |
| `base.fbx` | 21,833 unwelded | 36,972 | 1.69455 (m via cm) |

All three are the same **169.5 cm** body under three different unit
conventions. That is exactly the check that would catch the reference's
documented FBX 10× unit error — now confirmed by something with no stake in our
conventions.

### I got it wrong first, and it looked like a real bug

**Blender is Z-UP** and rotates every Y-up file on import, so the model's height
arrives as Blender's **Z**. My first script read the Y component, reported 4.36,
and made the exporter look broken. It was measuring the body's *depth*.

Recorded in the script docstring and `memory/test.md`, because it reads as a 4×
unit error rather than a reader mistake.

### Two of my own shell bugs, same root

`$?` after a pipe reads the **last** command's status, not the script's. It made
a failing gate look like it exited 0, twice. Exit codes are now verified without
a pipe. The summary line also printed negative totals (`len(seen) - failures`).

Both found by *proving the gate fires* — which is now paying for itself.

### On the licence-inventory job

Owner asked; checked rather than assumed. **18 of the last 20 runs green, last
6 consecutive green.** Two failures, both mine, both fixed in the next commit:
a gate matching a comment, and a stray zero-byte file. Both were real problems
the gate correctly caught, not flakes.

279/279 across all four builds. CI 7/7.

**Next:** body pose units (`body-poseunits.json` — bone → quaternion directly,
not BVH frames), then `.mhupb` expression files. Also worth doing now that
Blender is wired in: export a **rigged** mesh and have Blender confirm the
armature, bone count and weights — the current check is geometry only.

---

## 2026-08-29 — Session 026 · pose units, and a blend that must stay asymmetric

**Ended:** 2026-08-29 15:26:24 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

Parity on all **60 units × 163 bones (9,780 transforms)** and on a five-unit
weighted blend, in both orders. CI green.

### The blend is order-dependent, and that is the point

It composes by quaternion multiplication — `quat = q_i * quat`, left-multiplied
— and quaternion multiplication does not commute, so **the input order changes
the resulting face**. Replicated, not corrected: expression assets are authored
against it.

Measured rather than argued: reversing the five-unit blend moves the result by
**0.034**.

The test asserts **both halves** — that the reversed blend matches the
reference's own reversed result, *and* that the two orders differ by more than
0.03. Without the second, an implementation that symmetrised the blend would
pass the first by accident. Worth reusing: when replicating a quirk, assert the
quirk is still present, not just that the output matches.

Weights are **not** normalised — the blend is additive.

### Joint sets do not match

The BVH has 212 joints, the rig 163. Mapping walks the **rig's** bones and takes
each one's identically-named BVH joint, identity where absent. Walking the BVH
instead would silently reorder every bone, which is why all 9,780 transforms are
compared rather than a sample.

### Reference edit (allowed, and recorded)

`transformations.py` used `numpy.array(x, copy=False)` in 18 places. **numpy 2.0
changed `copy=False` to mean "never copy" and to RAISE when a copy is needed**,
so every quaternion conversion failed on numpy 2.5 and the fixture could not be
captured at all. Replaced with `numpy.asarray` — the documented replacement,
behaviour-identical on numpy 1.x.

Hard rule 2 permits editing `legacy/` only to keep the oracle runnable. This is
that case, and it is now in `project_context.md` §8.0.1 so the diff is never
mistaken for a port decision.

### Habits from last session, applied

Both process fixes held: gates re-run with `/usr/bin/grep` **after** the last
experiment, and `git status` read before committing. No strays this time.

279/279 across all four builds.

**Next:** `.mhupb` expression files — the consumer of this blend — and the body
pose units, which use a different format (bone → quaternion directly).

---

## 2026-08-29 — Session 025 · BVH import, and two owner directives

**Ended:** 2026-08-29 15:13:49 · **Agent:** Claude Opus 5 (1M context) · **Branch:** master

### New standing directives from the owner

1. **Where a licence's terms do not suit us, design our own implementation
   learning from theirs** rather than translating. Applied immediately: BVH is a
   published open format, so the reader is written from the format spec and
   lives in the **Apache-2.0** `mh_io` module instead of being trapped under
   AGPL as a port of `bvh.py` would have been.
2. **Research modern approaches, published work and libraries.** Two findings
   recorded below; both change the roadmap.

### BVH import

There are **no `.mhpose` files in `data/` at all** — the shipped pose data is
BVH. Parity on both: `tpose.bvh` (222 joints) and `face-poseunits.bvh`
(212 joints × 60 frames). **12,942 joint-frames**, worst delta < 1e-5.

The subtle part is the up axis, which BVH does not record. Both files measure as
**Z-up**, so offsets become `(x, z, -y)`, position channels swap with a sign
flip, and the rotation order remaps `szyx` → `syzx`. Getting it wrong does not
fail — it yields a complete, plausible skeleton **lying on its side**.

Improvement kept: the reference names all 49 End Sites `"End effector"`, so they
collide; ours derives `<parent>_end`.

### Research

- **DQS** removes LBS's candy-wrapper collapse but is not a drop-in — it adds
  joint bulging, and Disney shipped an enhanced variant on *Frozen* to make it
  production-viable. Queued as a **display** option for M6; LBS stays the parity
  path because glTF and every DCC expect it.
- **SMPL / SMPL-X is licence-blocked for us.** The full parametric model is
  research-only; the CC-BY "Body" subset deliberately excludes the shape
  blendshapes that make it a generator. Forbidden for the same reason as the FBX
  SDK — it blocks the commercial-derivative use we promise. `LICENSING.md` §5.2.
  Our 1,280 CC0 targets are the M10 asset base.

### Two failures worth keeping

**A gate matched prose for the fourth time.** The BSD-notice gate flagged
`SceneIO.h`, which is Apache-2.0 and merely *says* "assimp (BSD-3-Clause)". The
sharper rule now in `instruction.md`: **where a gate is about a field, parse the
field** — never substring-search the file for its value.

**I committed a stray empty file.** Proving that gate fires, I redirected into
`src/foundation/Transform.h`, a path that did not exist — creating a zero-byte
file. CI caught it; I did not, because I had run the full gate set *before* the
experiment and never after, and did not read `git status`. Also: local `grep` is
**ugrep**, not what the runner has — mirrored gates now use `/usr/bin/grep`.

272/272 across all four builds. CI green.

**Next:** wire `face-poseunits.json` (60 named units) onto the BVH frames, then
the order-dependent slerp blend.

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
