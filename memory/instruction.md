# Implementation Guide

**Version:** 1.0 · **Created:** 2026-08-29

Coding standards, repository conventions, workflows. Binding for all contributors.

---

## CI gates: a grep will match the document that forbids the thing

Three separate gates in `.github/workflows/ci.yml` have failed on the **comment
explaining the rule** rather than on a violation of it:

| Gate | Matched | Should have matched |
|---|---|---|
| Forbidden dependencies | prose about the Autodesk SDK in `SceneIO.h` | `#include <fbxsdk`, `-lfbxsdk`, a CMake link |
| Apache/AGPL boundary | `src/io/CMakeLists.txt` saying "must never depend on one" | an actual `mh::core` in `target_link_libraries` |
| Legacy tree | `src/rig/CMakeLists.txt` recording which `legacy/` file it ports | an actual `include_directories(legacy/...)` |

The third happened in the same session the second was written up — recording the
pattern was not enough to avoid it.

| BSD notice | `SceneIO.h` saying "assimp (BSD-3-Clause)" | the file's own SPDX field being BSD |

The fourth happened *after* this note existed, because the rule as written
("match the construct") still allowed a 3-line substring search.

**The sharper rule:** where a gate is about a *field*, PARSE the field and
compare it. Never substring-search the file for the field's value. Grepping a
file for `BSD-3-Clause` finds every file that mentions BSD; extracting
`SPDX-License-Identifier:` and comparing finds the files that ARE BSD.

**The general rule:** a gate that greps for a forbidden string must match the
*construct*, not the word.

### Re-run the gates AFTER proving them

Proving a gate fires means deliberately breaking something. Do that in a temp
directory, or re-run the full gate set afterwards — not just the one gate.

A redirect in a "prove it fires" step (`grep -v ... > src/foundation/Transform.h`,
against a path that did not exist) silently CREATED an empty file. The full gate
run had already happened; CI caught the stray file, not me. Two habits fix it:

- `git status` before every commit, and read it — an unexpected `A` is the tell.
- Never redirect into a path inside the repo during an experiment.

### Use `/usr/bin/grep` when mirroring a CI gate locally

`grep` on this machine resolves to **ugrep**, which is not what the runner has.
A gate that passes locally through ugrep can still fail in CI. Mirror gates with
the absolute path.

- strip comments first (`sed 's/#.*//'`), or
- anchor on syntax (`^\s*#\s*include\s*<charconv>`, `target_link_libraries.*mh::core`).

And prove it both ways before committing: introduce a real violation and watch
it fire, then revert and watch it go quiet. A gate that has only ever been seen
passing is not known to work.

## 1. Language and toolchain

| Item | Choice | Verified on this machine |
|---|---|---|
| Language | **C++23** (`-std=c++23`) | Apple clang 21.0.0 — verified `std::expected` available |
| Build | **CMake ≥ 3.28** + **Ninja** | CMake 4.3.2, Ninja 1.13.2 |
| UI | **Qt 6.11** (Widgets, Gui, Core, ShaderTools) | qt 6.11.0 via Homebrew |
| Renderer | **Qt RHI** → Metal on macOS | Xcode 26.6, SDK 26.5 |
| Math | **Eigen 3/5** (MPL-2.0) | eigen 5.0.1 |
| Threading | **oneTBB** (Apache-2.0) | tbb 2023.1.0 |
| Import | **assimp** (BSD-3) | assimp 6.0.4_1 |
| Tests | **Catch2 v3** (BSL-1.0) via FetchContent | to add |
| Target OS | macOS 26+, arm64 first | macOS 26.6.2 arm64 |

Do not add a dependency without recording it in `/LICENSING.md` (§7).

## 2. Repository layout

See `architecture.md` §II.2 for the full tree. The rules:

- **`src/<module>/`** — implementation. One module = one CMake target = one
  namespace. Modules: `core`, `rig`, `io`, `render`, `ui`, `app`.
- **`include/makehuman/<module>/`** — public headers only. If a header is not
  used outside its module it belongs next to the `.cpp` in `src/`.
- **`legacy/python/`** — the reference oracle. **Read-only.** The only permitted
  edits are those that keep it runnable as a reference.
- **`data/`** — CC0 assets, shared by both implementations.
  `legacy/python/data` is a symlink to it.
- **`tests/{unit,integration,regression,golden,smoke}/`** — mirrors `src/` layout.
- **`memory/`** — this folder. Updated in the same commit as the code it describes.

**Namespace = directory.** `src/io/GltfWriter.cpp` → `mh::io::GltfWriter`.

## 3. Naming and style

```cpp
namespace mh::core {

class TargetLibrary {                    // PascalCase types
public:
    // camelCase functions, camelCase locals and params
    [[nodiscard]] const Target* find(std::string_view path) const;

    static constexpr uint32_t kMaxInfluences = 4;   // kPascalCase constants

private:
    std::vector<Target>  targets_;       // trailing underscore on members
    std::unordered_map<std::string, uint32_t> index_;
};

} // namespace mh::core
```

- Files: `PascalCase.h` / `PascalCase.cpp`, matching the primary type.
- 4 spaces, no tabs. 100-column soft limit.
- `#pragma once`. No include guards.
- Include order: own header, C++ stdlib, third-party, project. Blank line between groups.
- `east const` is not used; write `const T&`.
- `auto` only where the type is obvious from the RHS or unspeakable.
- Braces on the same line for functions and control flow.
- `.clang-format` and `.clang-tidy` are authoritative; CI enforces them.

## 4. C++ rules that are not negotiable

1. **No raw owning pointers.** `unique_ptr` for exclusive, values by default.
   `shared_ptr` requires a comment justifying shared ownership.
2. **No `new`/`delete`.** `make_unique`, containers, RAII.
3. **`[[nodiscard]]`** on anything returning a value the caller must not ignore.
4. **`std::expected<T, Error>`** for recoverable failures (file parsing, import).
   Exceptions only for programmer errors and unrecoverable state.
5. **No exceptions in the render loop or job tasks.** Hot paths are noexcept.
6. **`std::span` / `std::string_view`** for non-owning parameters. Never
   `const std::vector<T>&` for a read-only range.
7. **Rule of zero.** If you write a destructor, justify it in a comment.
8. **No virtual functions in per-vertex or per-frame code.** Interfaces live at
   subsystem seams only.
9. **`constexpr`/`consteval`** wherever the value is compile-time known.
10. **Warnings are errors**: `-Wall -Wextra -Wpedantic -Werror`.
11. **No `using namespace` in a header.** Ever.
12. **Sanitize**: an ASan+UBSan preset exists and CI runs the test suite under it.

## 5. Error handling

```cpp
enum class IoError { NotFound, PermissionDenied, MalformedHeader, UnsupportedVersion, Truncated };

struct ParseError {
    IoError     kind;
    std::string file;
    uint32_t    line = 0;
    std::string detail;      // what was expected vs. what was found
};

[[nodiscard]] std::expected<Mesh, ParseError> loadObj(const std::filesystem::path&);
```

- **Validate at the trust boundary.** Every file parser checks bounds, counts,
  and index ranges before indexing. A malformed asset must not read out of bounds.
- **Never catch-and-continue on data integrity.** The reference does this
  (`legacy/python/plugins/9_export_ogre/mh2ogre.py:191` bare `except: pass`) and
  the result is a corrupt file that looks fine. We fail loudly.
- Errors carry file, line/offset, expected, and found.
- `assert` is for invariants that indicate a bug in our code. It is not input validation.

## 6. Process flows

### 6.1 Porting a subsystem (the core workflow)

```
1. READ      the Python reference in full; trace callers and callees.
2. RECORD    the observed behaviour in architecture.md with file:line citations.
3. FIXTURE   capture golden data from the Python reference:
                 tools/capture_fixture.py <subsystem> -> tests/golden/<subsystem>/
4. DESIGN    the C++ interface. Climb the ladder (philosophy.md §2-3).
5. IMPLEMENT the minimum that satisfies the fixture.
6. PARITY    tests/regression/<subsystem>_parity.cpp asserts C++ == Python
             within a stated tolerance, EXCLUDING known-broken behaviour
             (project_context.md §8).
7. BENCH     add a benchmark; compare against benchmarks/baseline_python.json.
8. REVIEW    /code-review then /ponytail-review. Fix findings.
9. DOCUMENT  update architecture.md, mindmap.md, todo.md, handover_session.md.
10. COMMIT   one subsystem per commit, message per §8.
```

**Step 3 is not optional.** A port without a captured fixture is a rewrite.

### 6.2 Adding a file format

```
1. Obtain the published specification. Record the URL in LICENSING.md.
2. Implement against mh::io::Scene (architecture.md §II.5), never against
   mh::core::Human directly.
3. Register in the format registry with extension, magic bytes, capabilities.
4. Round-trip test: import -> export -> import, assert structural equality.
5. Cross-DCC test: export a fixture, note in docs/ which DCCs were verified
   and at what version. Never claim compatibility you did not test.
```

**Never** implement a format by translating GPL-licensed code into an
Apache-2.0 module (`project_context.md` §4.2).

### 6.3 Fixing a bug

```
1. Reproduce. Write the failing test FIRST, in tests/regression/.
2. Find the root cause: grep every caller of the function you intend to change.
3. Fix in the shared path all callers route through, not at the call site.
4. Confirm the regression test now passes and the old code fails it.
```

## 7. Dependency policy

Before adding anything:

1. Can the C++ standard library do it? → use that.
2. Can Qt6 do it? → use that.
3. Is it already vendored? → use that.
4. Is it fifty lines? → write it.
5. Otherwise: check the licence against `project_context.md` §4.4, then add via
   **CMake FetchContent with a pinned tag or commit SHA** (never a moving branch),
   and record it in `/LICENSING.md`.

Forbidden regardless of usefulness: the Autodesk FBX SDK, anything GPL-2.0-**only**,
anything with a non-commercial or field-of-use restriction, anything whose licence
cannot be positively identified.

## 8. Git workflow

- Branch from `master`: `feat/<area>-<slug>`, `fix/<area>-<slug>`, `port/<subsystem>`.
- **Never `git push --force` to `master`.**
- One logical change per commit. A commit must build and pass tests on its own.
- Commit message:

```
<area>: <imperative summary under 72 chars>

Why this change is needed. What was verified, and how.
Benchmarks if performance-relevant. file:line citations for
reference behaviour that was matched.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
```

`<area>` ∈ `core, rig, io, render, ui, app, build, tests, docs, memory, data`.

## 9. Build

```bash
cmake --preset macos-arm64-debug          # or -release, -asan
cmake --build --preset macos-arm64-debug -j
ctest --preset macos-arm64-debug --output-on-failure
```

Presets live in `CMakePresets.json`. Options:

| Option | Default | Meaning |
|---|---|---|
| `MH_BUILD_TESTS` | ON | Catch2 test targets |
| `MH_BUILD_BENCHMARKS` | ON | benchmark targets |
| `MH_ENABLE_ASAN` | OFF | AddressSanitizer + UBSan |
| `MH_USE_ASSIMP` | ON | assimp-backed import |
| `MH_DATA_DIR` | `${CMAKE_SOURCE_DIR}/data` | asset root for dev builds |

## 10. CI expectations

Every PR must pass, in order:

1. `clang-format --dry-run --Werror`
2. `clang-tidy` on changed files
3. Configure + build, `-Werror`, debug and release
4. `ctest` — unit, integration, regression, golden
5. `ctest` under ASan+UBSan
6. Benchmarks; fail on >5% regression vs. the recorded baseline
7. Licence scan — every dependency present in `LICENSING.md`
8. Smoke: launch the app headless, render one frame, exit 0

## 11. Documentation duties

Every commit that changes structure updates the relevant `memory/` file in the
**same commit**. See `docs.md` for standards. Specifically:

- New/changed subsystem → `architecture.md` + `mindmap.md`
- Task started/finished/changed → `todo.md`
- Any session → `handover_session.md` entry with `YYYY-MM-DD HH:MM:SS`
- New dependency or asset → `/LICENSING.md`
- New UI surface → `design.md`

## 12. macOS packaging

- Bundle: `MakeHuman.app` with `Info.plist`, `Resources/data`, `Resources/fonts`.
- Qt deployment via `macdeployqt` + a CMake install rule.
- Hardened runtime, codesign, notarize, staple. Scripts in `packaging/macos/`.
- Universal binary (`arm64;x86_64`) is a later milestone, not the first release.
- **Ship the LGPL relinking notice** for Qt and the full `LICENSING.md` inside the
  bundle. AGPL §13 and LGPL §4 obligations are met by including the source offer.
