# Testing and Verification Policy

**Version:** 1.0 · **Created:** 2026-08-29

> **Nothing is done until its tests pass.** Not "written", not "compiles",
> not "seems to work". Tests pass, or it is not done.

---

## Third-party validation: Blender

Every other check here shares lineage with what it checks. Parity tests compare
us against the Python reference we were ported *from*; the glTF and FBX tests
read back through assimp, which also **wrote** the FBX. A convention that both
sides get wrong the same way passes all of it.

Blender is a third implementation that has never seen this codebase.

```bash
cmake --build --preset macos-arm64-release --target mh_export_fixture
tools/run_blender_validation.sh          # exits non-zero on any disagreement
```

It round-trips the base mesh through every exporter and checks vertex count,
triangle count, UV layers and physical size. Current result: **3/3 agree** —
19,158 welded vertices in OBJ, 21,833 unwelded in glTF/FBX, 36,972 triangles
throughout, and a **169.5 cm** body in all three despite three different unit
conventions (decimetres, metres, centimetres). That last one is the check that
would catch the reference's documented FBX 10x unit error.

**Blender is Z-up.** Every importer rotates a Y-up file on the way in, so the
model's height arrives as Blender's **Z**. Reading the Y component reports the
body's *depth* and looks like a 4x unit error that is not there — I made exactly
that mistake the first time this ran, and briefly believed the exporter was
broken. Compare `tallest_extent`.

Not in CI: the runner has no Blender and installing it per-run costs more than
the check is worth at this cadence. Run it after touching an exporter.

## 1. Why this policy is strict

This is a **port**. The failure mode of a port is not a crash — it is silently
producing *slightly different* output than the original, and nobody noticing until
a user's saved character loads wrong. Ordinary unit tests do not catch that.
**Parity testing against the Python reference does**, and it is the backbone of
this project's verification.

The second failure mode is hallucinated completeness: claiming a subsystem is
ported when only the easy path was implemented. Golden fixtures make that
impossible to fake.

## 2. Test taxonomy

| Kind | Location | Answers | When required |
|---|---|---|---|
| **Unit** | `tests/unit/` | Does this function do what it says? | Every non-trivial function |
| **Integration** | `tests/integration/` | Do these subsystems agree at the seam? | Every cross-module interface |
| **Regression** | `tests/regression/` | Did a fixed bug come back? | **Every** bug fix |
| **Golden / Parity** | `tests/golden/` | Does C++ match the Python oracle? | **Every** ported subsystem |
| **Smoke** | `tests/smoke/` | Does the app start and render? | Every release build |
| **E2E** | `tests/integration/e2e_*` | Does a real user workflow complete? | Every user-facing feature |
| **Performance** | `benchmarks/` | Did it get slower? | Every hot-path change |

"Non-trivial" = has a branch, a loop, a parser, numerical work, memory ownership,
or a file-format path. A one-line accessor needs no test; YAGNI applies to tests too.

## 3. Parity testing — the core discipline

### 3.1 Capture

`tools/capture_fixture.py` drives the Python reference headlessly and writes
deterministic fixtures to `tests/golden/<subsystem>/`:

```
tests/golden/targets/
    base_mesh.bin              19158 float3 vertices, the unmodified base
    stack_default.json         the modifier stack at default values
    stack_default.bin          resulting vertex positions
    stack_age0.9_male.json     a non-default stack
    stack_age0.9_male.bin      its resulting vertex positions
    MANIFEST.json              source commit, python/numpy versions, timestamp
```

`MANIFEST.json` records the exact reference version so a fixture is never
ambiguous about what it captured.

### 3.2 Compare

```cpp
TEST_CASE("modifier stack matches python reference", "[parity][core]") {
    auto human    = mh::core::Human::fromStack(loadJson("stack_age0.9_male.json"));
    auto expected = loadVec3Blob("stack_age0.9_male.bin");

    REQUIRE(human.mesh().coord.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        INFO("vertex " << i);
        REQUIRE_THAT(human.mesh().coord[i].x, WithinAbs(expected[i].x, kPosTol));
        REQUIRE_THAT(human.mesh().coord[i].y, WithinAbs(expected[i].y, kPosTol));
        REQUIRE_THAT(human.mesh().coord[i].z, WithinAbs(expected[i].z, kPosTol));
    }
}
```

### 3.3 Tolerances — stated, justified, never widened to pass

| Quantity | Tolerance | Justification |
|---|---|---|
| Vertex position | `1e-5` mesh units | float32 accumulation over a stack of sparse adds |
| Vertex position, from a compiled target | `1e-3` | the reference quantises to `int16 * 1e-3` (`legacy/python/core/algos3d.py:221`) |
| Normals | `1e-4` per component | normalisation after an area-weighted sum |
| Bone matrices | `1e-5` per element | `inv()` round-trip |
| Skinned position | `1e-4` | matrix blend before transform |
| Barycentric proxy fit | `1e-5` | 3-term weighted sum + a 3×3 transform |

**Widening a tolerance to make a test pass is forbidden.** If the difference is
real, either the port is wrong or the reference is (see §3.4) — investigate,
do not paper over.

### 3.4 Known-broken behaviour is EXCLUDED, not matched

`project_context.md` §8 lists verified defects in the reference. Parity tests
must **not** assert equality for those. Instead:

```cpp
// The reference computes tangents incorrectly:
//   legacy/python/core/module3d.py:411  t2 = w3[:,1] = w1[:,1]  (chained assignment)
//   legacy/python/core/module3d.py:429  np.sum(...) with no axis= collapses to a scalar
// We compute them correctly, so tangents are deliberately NOT parity-tested.
// Instead we assert the mathematical property the reference fails to satisfy.
TEST_CASE("tangents are orthonormal to normals", "[core][tangent]") {
    ...
    REQUIRE_THAT(dot(tangent.head<3>(), normal), WithinAbs(0.0f, 1e-5f));
}
```

Every exclusion carries a comment naming the defect and citing `file:line`.

## 4. Format round-trip testing

For every importer/exporter pair:

1. **Structural round-trip**: import → export → import; assert equality of vertex
   count, topology, UVs, skin weights (sorted, normalised), morph target count,
   material channels.
2. **Numerical round-trip**: positions within `1e-5` after unit conversion.
3. **Unit correctness**: export at each of dm/m/cm/inch, re-import, assert real
   scale. *This test exists specifically because the reference gets it wrong —
   `cfg.scale *= 10` (`legacy/python/plugins/9_export_fbx/__init__.py:112`) with a
   hardcoded `scale_factor = 10.0` (`fbx_binary.py:736`) makes every non-decimetre
   FBX export a 10× error.*
4. **Malformed input**: truncated file, bad magic, out-of-range indices, negative
   counts, huge counts. Must return an error, never crash, never read OOB.
5. **Cross-DCC**: document which DCC and version was verified. Never claim
   compatibility that was not tested.

## 5. Resource budgets and profiling

Every benchmark run records CPU time, peak RSS, and (where applicable) GPU memory
and frame time. Recorded to `benchmarks/results/<date>-<commit>.json`.

### Budgets

| Resource | Budget | Rationale |
|---|---|---|
| Peak RSS, default character | **≤ 400 MB** | The reference keeps both seed and subdivided meshes resident plus an unbounded target cache (`legacy/python/core/algos3d.py:64`) |
| Peak RSS, subdivided + 5 proxies | ≤ 900 MB | |
| Compiled target blob, resident | ≤ 120 MB, mmapped | 1,280 targets; mmap means the OS can evict |
| GPU memory, default scene | ≤ 256 MB | |
| Frame time, subdivided viewport | ≤ 16.6 ms (60 fps) | Reference cannot do this: subdiv `calcNormals` alone is 20.57 ms |
| Slider drag → pixels | ≤ 8 ms | |
| Cold start → interactive | ≤ 1.5 s | |

Budgets are **asserted in CI**, not merely reported. Exceeding one fails the build.

### Regression gate

A benchmark that regresses **> 5%** against the recorded baseline fails CI. To
land a deliberate regression, update the baseline in the same commit **with a
justification in the commit message**.

The Python baseline (`benchmarks/baseline_python.json`, measured 2026-08-29) is
the floor the port must beat, not a target to match.

## 6. Determinism

Character generation, export, and baking must be **bit-deterministic** given the
same inputs and seed. Tests assert this by running twice and comparing hashes.
Non-determinism from thread scheduling in the job pool is a bug — reductions must
use a fixed-order combine, not whatever order tasks finish in.

## 7. What is NOT allowed

1. Deleting or `[[maybe_unused]]`-ing a failing test to go green.
2. Widening a tolerance to make a test pass.
3. `REQUIRE(true)` placeholders, or tests that assert nothing.
4. Claiming a test passes without having run it and read the output.
5. Committing on a red tree.
6. A ported subsystem with no parity fixture.
7. A bug fix with no regression test that fails on the old code.

## 8. Definition of Done

A change is done when **all** of these are true and were **observed this session**:

- [ ] Compiles clean, `-Wall -Wextra -Wpedantic -Werror`, debug and release
- [ ] `ctest` green, including the new tests
- [ ] Green under ASan + UBSan
- [ ] Parity test present and passing (ported subsystems), or an explicit,
      commented exclusion citing the reference defect
- [ ] Benchmarks within 5% of baseline, or the baseline updated with justification
- [ ] Resource budgets respected
- [ ] `/code-review` findings addressed
- [ ] `/ponytail-review` findings addressed
- [ ] `memory/` updated in the same commit
- [ ] New dependencies recorded in `/LICENSING.md`

Report the outcome honestly: failing tests are reported **with output**, skipped
steps are named as skipped, and "done" is said only when the list above is
genuinely complete.

---

## SonarQube (added 2026-09-01)

Self-hosted **SonarQube 26.8.0 Community**, `m2m-sonarqube` container, shared
with the mesh2motion project.

```bash
docker start m2m-sonarqube
set -a; . ./.sonar-token; set +a      # SONAR_HOST_URL + SONAR_TOKEN, gitignored
sonar-scanner                          # reads sonar-project.properties
```

`./.sonar-token` is gitignored and 0600. Its token is a
**PROJECT_ANALYSIS_TOKEN scoped to `makehuman`** — it can analyse this project
and nothing else.

**Community Edition has no C or C++ analyser.** Verified:
`GET /api/languages/list` lists 26 languages, none of them `c` or `cpp`, and the
project measures report `ncloc_language_distribution = py=1956` for a repo of
~60k lines of C++23. **A green Sonar gate says nothing about the C++.** It
covers the Python in `tools/` and `benchmarks/`, the workflow YAML, and the
`secrets` analyser across every indexed file.

**The C++ gates are unchanged and remain the real ones**: `-Wall -Wextra
-Wpedantic -Werror -Wconversion -Wsign-conversion`, `ctest` in
debug/release/ASan/TSan, the golden parity fixtures, and Blender / usdchecker as
third-party validators.

The `MakeHuman` quality gate deliberately carries **no coverage condition**: the
449 tests are C++ and invisible to this server, there are no Python tests, so
`new_coverage` would read 0% forever. Removing a condition that can only ever
fail is not the same as weakening a test — Python coverage genuinely is 0, and
that is stated in `todo.md` rather than hidden.
