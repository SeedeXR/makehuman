# Engineering Philosophy

**Version:** 1.0 · **Created:** 2026-08-29

The principles that decide arguments. When two approaches both work, these pick.

---

## 1. Correctness is not negotiable; everything else is a trade

A fast wrong answer is worthless. A beautiful wrong abstraction is worse — it is
wrong *and* expensive to remove. Order of precedence when they conflict:

**Correctness → Robustness → Performance → Maintainability → Elegance**

Elegance is last on purpose. Clever is what someone decodes at 3am.

---

## 2. The code that is never written has no bugs

Every feature, abstraction, dependency, and configuration knob is a liability
carried forever. Before writing, ask *does this need to exist?* Speculative
generality — an interface with one implementation, a factory for one product, a
plugin system for a thing that will never have a second plugin — is not
"future-proofing", it is prepaying for a future that usually never arrives, with
interest.

**Deletion is a feature.** The port is an opportunity to remove: the software
rasterizer fallback, `safeRun`, the RenderMan exporter (dead), the warp modifier
(unreferenced, buggy), the dual ASCII/binary FBX code paths. Roughly 4,000 lines
of the Python tree are dead or broken. None of it gets ported.

---

## 3. Data-oriented, not object-oriented, in the hot path

A human is 19,158 vertices; subdivided, 75,008. The renderer touches all of them
60 times a second. That is not a place for virtual dispatch, `shared_ptr`
chasing, or an `Object` per vertex.

- **Struct-of-arrays** for anything iterated in bulk. The Python reference already
  does this (`coord`, `vnorm`, `fvert` as parallel arrays) — keep it.
- **Contiguous memory, predictable strides.** `std::vector`, not `std::list`,
  not `std::map` keyed by string in a loop.
- **Batch, don't iterate.** The reference's worst hotspots are all Python loops
  over vertices (`_update_faces`, `core/module3d.py:697-770`). In C++ they become
  flat loops the compiler vectorises — but only if the data layout permits.
- **Virtual dispatch at the subsystem seam, never inside it.** An `Importer`
  interface is fine; a virtual `Vertex::transform()` is not.

## 4. Measure, then optimise. Never the reverse.

Every performance claim needs a number from `benchmarks/`. "This should be
faster" is a hypothesis, not a result. We have a measured Python baseline
(`project_context.md` §6) precisely so that improvement is provable rather than
asserted.

Corollary: **do not micro-optimise cold paths.** File parsing that runs once at
startup does not need SIMD. The frame loop does.

## 5. The GPU does graphics work; the CPU orchestrates

The reference does linear-blend skinning on the CPU in NumPy and recomputes all
normals every pose change (`legacy/python/shared/animation.py:1086-1094`, whose
own comment says "this is way too slow"). That entire class of work belongs on
the GPU:

- Skinning → vertex shader with a matrix palette in a UBO/SSBO.
- Morph target accumulation → compute shader or transform feedback over sparse
  target buffers.
- Normals → recomputed on GPU, or skipped entirely where the shader can derive them.

The CPU's job is to decide *what* to draw and to keep buffers coherent, not to
transform vertices.

## 6. Zero-copy where the data already lives in the right shape

Assets are read once and used for the session. Prefer `mmap` over parse-into-heap
for the compiled target blob. Prefer uploading a buffer slice over rebuilding it.
The reference re-feeds every vertex array to the driver on every frame
(`legacy/python/lib/glmodule.py:479`); persistent buffers plus dirty-range updates
are the single largest win available.

## 7. Explicit over implicit; boring over clever

- No macro metaprogramming where a function will do.
- No template wizardry that makes a compile error 400 lines long.
- No inheritance more than two deep.
- Name things what they are. `applyAllTargets` is a good name. `_doIt2` is not.
- If a reviewer has to ask "what does this do", the code failed, not the reviewer.

## 8. Fail loudly, early, and with context

The reference swallows errors: bare `except: pass` in the Ogre exporter
(`legacy/python/plugins/9_export_ogre/mh2ogre.py:191`), in the FBX template
builder (`fbx_utils_bin.py:593-595`), in `Bone.update`
(`legacy/python/shared/skeleton.py:908-912`). The result is a partially-written
file indistinguishable from a good one.

Our rules:
- Validate at the trust boundary — file parsing, plugin input, user paths.
- An error carries *what* failed, *where* (file/line/offset), and *what was expected*.
- Never catch-and-continue on a data-integrity failure. A corrupt asset is a
  hard error with a clear message, not a silent half-load.
- `assert` for invariants that indicate a programming bug; exceptions/`expected`
  for conditions the user can cause.

## 9. Ownership is designed, not discovered

- Prefer values and `unique_ptr`. `shared_ptr` means "I could not decide who owns
  this" — it needs a justification in review.
- No raw owning pointers. Raw pointers are non-owning observers only.
- No reference cycles; the reference's weak-ref `Object3D.object` property
  (`legacy/python/core/module3d.py:459-464`) exists because Python let a cycle
  happen. Design the graph so it cannot.
- RAII for every GPU resource, file handle, and lock.

## 10. Backwards compatibility is a promise, not a preference

Users have `.mhm` files, custom `.target` files, community `.mhclo` assets. Those
must keep loading. Format readers are permissive in what they accept and strict
in what they write. Round-trip fidelity is a tested property, not an aspiration.

## 11. The reference implementation is the oracle, not the model

The Python code tells us **what the software does** — that is authoritative and
must be matched. It does **not** tell us how the C++ should be structured, and
where it is provably wrong (`project_context.md` §8) it must not be matched.

Port the *behaviour*, not the *implementation*.

## 12. Native means native

No Electron, no embedded browser, no Python runtime, no scripting-language
bridge in the shipped product. Qt6 widgets, C++, and a Metal-backed renderer.
Startup should feel instant; the binary should be one bundle the user drags to
Applications.

## 13. Open source is an obligation, not a badge

The licence (`project_context.md` §4) is a design input. Every dependency is
checked before it is added. Every borrowed line is attributed. The clean-room
boundary between AGPL and Apache-2.0 modules is maintained deliberately, because
downstream users' commercial freedom depends on it being real rather than claimed.

## 14. Documentation is part of the change, not a follow-up

A subsystem without a design note is a subsystem only its author can maintain.
`memory/` is updated in the same commit as the code that invalidates it. A stale
memory file is worse than no memory file — it lies with authority.
