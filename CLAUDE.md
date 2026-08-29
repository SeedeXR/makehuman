# CLAUDE.md

Instructions for Claude Code working in this repository. **Read `memory/` first.**

---

## What this project is

A port of **MakeHuman** — a parametric 3D human generator — from Python/PyQt5/legacy
OpenGL to **native C++23 / Qt6 / Metal (via Qt RHI)** for macOS, with modern DCC
interchange (FBX, glTF/GLB, USD — **import and export**), a dockable dark UI, and
MetaHuman-class character tooling. The end state ships **no Python**.

## Start every session here

**Run `memory/session_start.md` in full before doing anything else.** It is
mandatory, not advisory. In short:

1. Read `memory/agent_profile.md` — how you behave.
2. Read `memory/project_context.md` — vision, **licensing**, measured baseline,
   known-broken reference behaviour.
3. Read `memory/handover_session.md` — most recent entry, in full.
4. Read `memory/todo.md` — what is next.
5. Reconcile memory against live state (`git status`, `git log`, does it build?).
   **Reality wins over memory.**

## The memory system

| File | Contents |
|---|---|
| `memory/agent_profile.md` | Operating personality, zero-hallucination rules, multi-agent standards, token discipline |
| `memory/project_context.md` | Vision, objectives, **licensing constraints**, measured baseline, known reference defects |
| `memory/architecture.md` | The system as-is (Python, cited) and to-be (C++ design) |
| `memory/mindmap.md` | How everything connects; who breaks when you change what |
| `memory/todo.md` | Roadmap M0–M11, open questions |
| `memory/handover_session.md` | Session-by-session log, timestamped |
| `memory/session_start.md` | Mandatory startup procedure |
| `memory/instruction.md` | Coding standards, layout, workflows, build, CI |
| `memory/philosophy.md` | Engineering principles that settle arguments |
| `memory/test.md` | Testing policy, parity discipline, Definition of Done |
| `memory/design.md` | UI/UX design system, tokens, docking, components |
| `memory/docs.md` | Documentation standards |

**Update `memory/` in the same commit as the code it describes.** A stale memory
file lies with authority.

## Repository layout

```
src/{core,rig,io,render,ui,app}   C++ implementation
include/makehuman/                public headers
data/                             CC0 assets (136 MB, 1,787 files)
legacy-python/                    the Python reference — ORACLE ONLY, read-only
tests/{unit,integration,regression,golden,smoke}
benchmarks/                       baseline_python.json + C++ benchmarks
memory/                           project memory (above)
graphify-out/                     knowledge graph, 5,392 nodes / 9,319 edges
```

## Hard rules

1. **Zero hallucination.** Every claim about code, an API, or state must be
   observed this session and cited `file:line`. Never quote an API from memory
   when it can be checked. "I don't know yet — checking" beats a plausible guess.
2. **Never edit `legacy-python/`** except to keep the reference runnable.
3. **Never port a known-broken behaviour.** `memory/project_context.md` §8 lists
   verified defects (three tangent bugs, the FBX 10× unit error, dead morph
   export, and more). Parity tests must exclude them explicitly, with a comment.
4. **Licence boundary is real.** AGPL-3.0 modules (`core`, `rig`, `asset`, `app`)
   may depend on Apache-2.0 modules (`foundation`, `rhi`, `io`, `render`, `ui`) —
   **never the reverse**. An Apache-2.0 module may never be written by translating
   an AGPL file. See `memory/project_context.md` §4.
5. **Never use Epic MetaHuman assets, SDK, or exported data.** MetaHuman-class
   *capability* is the goal; MetaHuman *content* is forbidden.
6. **No new dependency without recording it in `LICENSING.md`.** Forbidden
   outright: Autodesk FBX SDK, GPL-2.0-only, anything non-commercial-restricted.
7. **Tests gate everything.** A ported subsystem with no parity fixture is not
   done. Never weaken or delete a test to go green. Never claim a test passes
   without running it.
8. **Never `git push --force` to `master`.**

## Build and test

```bash
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug -j
ctest --preset macos-arm64-debug --output-on-failure
```

Reference oracle (Python):
```bash
cd legacy-python && ../.venv-mh/bin/python makehuman.py
./.venv-mh/bin/python benchmarks/baseline_python_core.py    # from repo root
```

## Before you commit

- [ ] Builds clean, `-Wall -Wextra -Wpedantic -Werror`
- [ ] `ctest` green, including new tests
- [ ] Parity test present (or an explicit, commented exclusion)
- [ ] Benchmarks within 5% of baseline
- [ ] `/code-review` findings addressed
- [ ] `/ponytail-review` findings addressed
- [ ] `memory/todo.md` and `memory/handover_session.md` updated
- [ ] New dependencies in `LICENSING.md`

## Useful context

- Base mesh: **19,158 verts / 18,486 quad faces / 21,334 UVs**. Subdivided: 75,008 / 73,944.
- **1,280** `.target` morphs; **163** bones; **172** face groups; **50** task views.
- Internal units: **decimetres**, **Y-up**, model faces **+Z**, right-handed.
- Matrices: row-major storage, **column vectors** (`v' = M·v`).
- Quaternions: **`[w,x,y,z]`** — Eigen's `.coeffs()` is `[x,y,z,w]`. Classic trap.
- Ask the knowledge graph before broad searches: `graphify query "<question>"`.
