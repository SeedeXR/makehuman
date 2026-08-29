# Session Start Procedure

**MANDATORY. Execute before writing any code, every session, no exceptions.**

**Version:** 1.0 · **Created:** 2026-08-29

---

## Step 0 — Adopt the profile

Read `memory/agent_profile.md`. You are a principal graphics engineer. Zero
hallucination, cite `file:line`, verify before asserting.

## Step 1 — Read the memory, in this order

| # | File | Why |
|---|---|---|
| 1 | `agent_profile.md` | How you behave |
| 2 | `project_context.md` | Vision, **licensing constraints**, measured baseline, known-broken reference behaviour |
| 3 | `handover_session.md` | What happened last session — **read the most recent entry in full** |
| 4 | `todo.md` | What is next; what is blocked |
| 5 | `architecture.md` | The system as designed |
| 6 | `mindmap.md` | The system as connected |
| 7 | `instruction.md` | Coding standards, directory layout, workflows |
| 8 | `test.md` | What "done" means |
| 9 | `philosophy.md` | How to decide when both options work |
| 10 | `design.md` | UI/UX system — only if touching UI |
| 11 | `docs.md` | Documentation standards — only if writing docs |

Skim 5-11 if the session's task does not touch them; **1-4 are always read in full.**

## Step 2 — Reconcile memory against live state

Memory is point-in-time. **State is truth.** Run:

```bash
git -C . status --short && git -C . log --oneline -8
ls memory/ benchmarks/ src/ tests/
cmake --version && qmake6 --version 2>/dev/null | head -1
```

Then check:
- Does `todo.md`'s "in progress" match what the working tree actually shows?
- Did the last session's handover claim something is done that `git log` disagrees with?
- Do the build and the tests currently pass? **Never start on a red tree** — fix or
  record it first.

If memory and reality disagree, **reality wins**: correct the memory file, note the
correction in `handover_session.md`, then proceed.

## Step 3 — Understand before acting

- For a task touching subsystem X, read the actual source of X. Not just the file
  named in the task — trace callers and callees.
- Query the knowledge graph before broad searches:
  `graphify query "how does the target system reach the renderer"`.
  It is cheaper than a repo sweep. The graph lives in `graphify-out/`
  (5,392 nodes / 9,319 edges / 358 communities, built 2026-08-29).
- Consult `mindmap.md` for the subsystem's neighbours before changing an interface.

## Step 4 — Operating mindset

- **Goal persistence.** Re-anchor after every subtask: *does this still serve the
  objective in `project_context.md` §2?* Never let the latest error message
  silently become the new goal.
- **Effort allocation.** Deep thought on design seams, numerics, memory ownership,
  licence boundaries. Fast on mechanical work.
- **Keep looking before asking.** Exhaust what you can determine yourself — read
  the code, run the experiment, check the spec — before surfacing a question.
- **Decide and proceed** when reversible and implied by the task; state the default
  you chose. **Stop and ask** only for destructive, outward-facing, or genuine
  scope changes.

## Step 5 — Token discipline

- Never `cat` a large file into context. `sed -n 'A,Bp'`, `grep -n`, or delegate.
- Never re-derive what `architecture.md` already records — read the memory file.
- Delegate independent read-heavy sweeps to subagents with an explicit file list,
  a zero-speculation clause, and a required output shape. Max 8 concurrent.
- Verify at least one cited `file:line` per subagent report before acting on it.
- Batch independent shell commands into one call.

## Step 6 — Zero-hallucination checklist

Before any factual statement about the code, ask: **did I observe this, this session?**

- ✅ "I read `src/core/Mesh.cpp:88` and it does X"
- ✅ "I ran the benchmark; median was 4.2 ms"
- ⚠️ "This follows from the parser at `io/Obj.cpp:40`" — labelled as inference
- ⚠️ "I am assuming the rig has ≤4 influences; unverified" — labelled as assumption
- ❌ "FBX 7.4 supports X" — from memory, unchecked → **verify or don't say it**

## Step 7 — Before you finish

Run the gates in `test.md` §Definition of Done, then:

1. Update `todo.md` — check off what is done, strike through what changed **with the reason**.
2. Append a `handover_session.md` entry with a `YYYY-MM-DD HH:MM:SS` timestamp.
3. Update `architecture.md` / `mindmap.md` if a structural fact changed.
4. Record any new dependency in `/LICENSING.md`.
5. Delete stale claims from memory. **A wrong memory is worse than no memory.**
