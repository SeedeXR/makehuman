#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check memory/taskviews.md against the reference it describes.

    python3 tools/audit_taskviews.py

Exits non-zero if the counts recorded in the plan no longer match the reference,
or if a task view is missing from the classification. Run in CI.

Counting task views is harder than it looks; `memory/taskviews.md` records why
the answer was wrong twice and what the three exclusions below are for. The
short version: an earlier `^class X(...TaskView)` regex matched only a single
base class, so six views mixing in `MetadataCacher` were invisible to it. Hence
`ast` with transitive base resolution -- a mixin added tomorrow cannot hide a
view from it.

The counts live in the markdown as `<!-- audit:key=N -->` markers so this checks
the file a human reads, not a copy of the numbers kept elsewhere.
"""

import ast
import json
import pathlib
import re
import sys
import warnings
from collections import Counter

# The reference still carries py2-era escapes; their warnings are not our news.
warnings.simplefilter("ignore", SyntaxWarning)

REPO = pathlib.Path(__file__).resolve().parent.parent
REFERENCE = REPO / "legacy" / "python"
PLAN_FILE = REPO / "memory" / "taskviews.md"

# Constructed but never registered -- see the module docstring.
NOT_REGISTERED = {"TextureProjectionView"}

# Where each view stands. Held here rather than tallied by hand in markdown: the
# first draft of the plan claimed "19 declined" over a list naming 16.
#
# blocked  = the engine cannot do it yet, not that it is hard.
# declined = Python tooling, or dev-only tabs, with no meaning in a build
#            shipping no Python. A decision to revisit, not an omission.
BUCKETS = {
    # "covered" means the capability reaches the user, just not as a TAB. The
    # reference makes each of these a tab; this port is dockable, so they arrive
    # as File/Edit/Settings menu actions or as groups in the Assets panel.
    # EVIDENCE below names the literal that proves each one, and the gate fails
    # if the claim and src/ ever disagree again -- in either direction.
    "LoadTaskView": "covered", "SaveTaskView": "covered",

    # Menu actions. Export was listed `todo` under the comment "the writers
    # exist but nothing in the UI reaches them" long after `file.export` shipped
    # with a handler, five formats and three tests.
    "ExportTaskView": "covered", "OpenGLTaskView": "covered",
    "RandomTaskView": "covered", "SettingsTaskView": "covered",
    "ShortcutsTaskView": "covered",

    # Groups in the Assets panel rather than tabs of their own.
    "MaterialTaskView": "covered", "PoseLibraryTaskView": "covered",
    "SkeletonLibrary": "covered",

    # The six proxy choosers that shipped as Assets-panel groups. They sat in
    # `blocked` under "the viewport draws exactly one mesh" long after that was
    # fixed -- five of the six shipped in the sessions of 2026-09-11 and -12.
    # A user's own .target morphs, via --custom-targets. A directory of files
    # rather than a tab, which is the same "reaches the user, not as a TAB"
    # shape as the rest of this bucket.
    "CustomTargetsTaskView": "covered",

    # Both of these sat in `todo` while shipped, and their stated reasons for
    # being unchecked were simply false. The mouse task is inside
    # Settings > Shortcuts... by design -- `ShortcutsDialog` merges the
    # reference's two settings tasks so a user need not know whether "how do I
    # pan" is a key question or a mouse one -- and the viewer is
    # `mh::ui::ImageViewer`, which every render has been shown in since M8.
    "MouseActionsTaskView": "covered", "ViewerTaskView": "covered",

    # The mixer is sixty sliders, a Save and a Load. --pose-unit sets any of
    # the sixty (--facs names only 48 of them), --list-pose-units prints the
    # names, --save-expression writes the result and --expression reads it back.
    "ExpressionMixerTaskView": "covered",

    "TeethTaskView": "covered", "TongueTaskView": "covered",
    "HairTaskView": "covered", "ClothesTaskView": "covered",
    "EyelashesTaskView": "covered", "EyesTaskView": "covered",

    # Still blocked, and NOT on rendering. Eyebrows have no helper cage in the
    # base mesh, and ProxyTaskView would choose between alternate BODY
    # topologies -- measured, the only proxymesh-shaped assets shipped are
    # data/3dobjs/base.mhclo (basemesh alpha_7, the OLD topology's map) and
    # a7_converter.proxy (the alpha_7 to hm08 converter), neither of them
    # wearable. Both are blocked on CONTENT. A scene is lights plus environment
    # (shared/scene.py:190-192), so SceneLibraryTaskView needs a lighting model.
    "EyebrowsTaskView": "blocked", "ProxyTaskView": "blocked",
    "SceneLibraryTaskView": "blocked",

    # AnimationLibrary SHIPPED 2026-09-16: an Animation combo beside Pose, the
    # two clearing each other because they fill one .bvh slot, plus
    # `--animation` for the command line. Its EVIDENCE literal is below.
    # ExpressionTaskView chooses .mhpose files and this port ships NONE
    # (measured: zero under data/), so its expressions arrive as --facs units.
    "AnimationLibrary": "covered",
    # blocked -> COVERED, same day, because the owner took that decision: the
    # content is now AUTHORED rather than ported. `legacy/python/data/
    # expressions/` still does not exist in the reference and MakeHuman2's
    # demos still name pose units this rig lacks -- so nothing was portable --
    # but `tools/make_expressions.py` composes six expressions from published
    # FACS Action Units through this application's own `--facs`, and an
    # Expression chooser lists them beside Pose and Animation.
    "ExpressionTaskView": "covered",
    # SHIPPED 2026-09-17 as a Material dock: 29 rows built from
    # `editableProperties`, the exact inverse of `setMaterialProperty`, plus
    # `--set-material` and `--save-material`. `covered`, not `done`: the
    # reference edits the material of ANY selected object and this edits the
    # skin's.
    "MaterialEditorTaskView": "covered",
    # SHIPPED 2026-09-17 as Help > About / Credits plus `--about`/`--credits`.
    # `covered`, not `done`: the reference also offers Website, FAQ and Forum
    # buttons, and those URLs belong to the upstream community rather than to a
    # port -- an owner decision, not an omission to fix quietly.
    "HelpTaskView": "covered",
    # SHIPPED 2026-09-17. `covered` rather than `done` on purpose: the reference
    # task also lets a user DRAG and SCALE the image, and that half is not
    # built. What is built is the half the tab exists for -- a reference
    # photograph in the VIEWPORT, bound to an axis view and hidden from every
    # other one, with an opacity.
    "BackgroundChooser": "covered",

    "ShellTaskView": "declined", "ScriptingView": "declined",
    "ScriptingExecuteTab": "declined", "SocketTaskView": "declined",
    "PluginsTaskView": "declined", "UserPluginsTaskView": "declined",
    "LoggingTaskView": "declined", "ProfilingTaskView": "declined",
    "DataTaskView": "declined", "ExampleTaskView": "declined",
    "TargetsTaskView": "declined", "SaveTargetsTaskView": "declined",
    "SceneEditorTaskView": "declined", "AssetDownloadTaskView": "declined",
    "MassProduceTaskView": "declined",
    "SkeletonDebugLibrary": "declined",
}


def _collect_class(node, bases) -> None:
    """Merge, never overwrite: 14 class names are reused across the reference,
    and last-parsed-wins could erase a view's bases."""
    bases.setdefault(node.name, []).extend(
        b.id if isinstance(b, ast.Name) else b.attr
        for b in node.bases if isinstance(b, (ast.Name, ast.Attribute)))


def _collect_call(node, constructed, dynamic_only, sliders) -> None:
    # `X(...)` and `pkg.X(...)` construct; `X.__init__(...)` does not -- its
    # callee name is `__init__`.
    if isinstance(node.func, ast.Name):
        constructed.add(node.func.id)
    elif isinstance(node.func, ast.Attribute):
        constructed.add(node.func.attr)

    if (getattr(node.func, "attr", None) == "loadModifierTaskViews"
            or getattr(node.func, "id", None) == "loadModifierTaskViews"):
        # first arg is getpath.getSysDataPath('modifiers/x_sliders.json')
        sliders.update(a.value for a in ast.walk(node.args[0])
                       if isinstance(a, ast.Constant)
                       and str(a.value).endswith(".json"))

    for kw in node.keywords:
        if kw.arg == "taskviewClass" and isinstance(kw.value, ast.Name):
            dynamic_only.add(kw.value.id)


def parse_reference():
    """(class -> base names, constructed names, taskviewClass= names, slider files).

    A hardcoded list of slider files would undercount silently: a fourth
    `loadModifierTaskViews` call site adds views but no `addTask` line -- the
    loop at `guimodifier.py:232` is shared -- so every other check here would
    still pass. Derived from the call sites instead.
    """
    bases, constructed, dynamic_only, sliders = {}, set(), set(), set()
    for path in sorted(REFERENCE.rglob("*.py")):
        # Every reference file parses today. Deliberately not guarded: skipping
        # one here while registered_tabs() still reads it would break the
        # cross-check in the confusing direction.
        tree = ast.parse(path.read_text(errors="replace"))
        for node in ast.walk(tree):
            if isinstance(node, ast.ClassDef):
                _collect_class(node, bases)
            elif isinstance(node, ast.Call):
                _collect_call(node, constructed, dynamic_only, sliders)
    return bases, constructed, dynamic_only, sliders


def standalone_views():
    """Task views that are their own registered tab."""
    bases, constructed, dynamic_only, _ = parse_reference()

    def derives_from_taskview(name, seen=frozenset()):
        if name in seen:
            return False
        return any(base == "TaskView" or derives_from_taskview(base, seen | {name})
                   for base in bases.get(name, ()))

    return sorted(name for name in bases
                  if derives_from_taskview(name)
                  and name in constructed          # not an abstract base
                  and name not in dynamic_only     # counted as its dynamic view
                  and name not in NOT_REGISTERED)


def registered_tabs():
    """Uncommented `addTask(` call sites -- an independent count of the same thing.

    A smoke alarm, not an invariant. It equals `len(standalone) + 1` only
    because exactly one call site is a loop (`apps/gui/guimodifier.py:232`) and
    every other registers one distinct class -- including the conditional one at
    `plugins/3_libraries_skeleton/__init__.py:67`, which registers exactly one.
    Factoring the eight near-identical proxy-chooser `load()` bodies into a
    shared helper would break it on a pure refactor; if that happens, fix the
    expectation rather than the refactor.
    """
    return sum(1 for path in REFERENCE.rglob("*.py")
               for line in path.read_text(errors="replace").splitlines()
               if "addTask(" in line
               and not line.lstrip().startswith("#")
               and "def addTask(" not in line)


def dynamic_view_names():
    """The views loadModifierTaskViews builds -- one per top-level slider key."""
    *_, sliders = parse_reference()
    return [key
            for relative in sorted(sliders)
            if (path := REPO / "data" / relative).exists()
            for key in json.loads(path.read_text())]


def recorded_totals():
    text = PLAN_FILE.read_text()
    return {key: int(m.group(1))
            for key in ("explicit", "dynamic", "total")
            if (m := re.search(rf"<!-- audit:{key}=(\d+) -->", text))}


BUCKET_NAMES = ("done", "covered", "todo", "blocked", "declined")


def recorded_buckets():
    """The bucket table in the plan file, as written.

    Only the five known bucket names are read, so a table added elsewhere in
    the file cannot quietly join the comparison.
    """
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^\|\s*(\w+)\s*\|\s*(\d+)\s*\|",
                                 PLAN_FILE.read_text(), re.MULTILINE)
            if m.group(1) in BUCKET_NAMES}


def recorded_section_counts():
    """The N in each `### <bucket> (N)` heading in the plan file.

    The bucket TABLE was audited and the sections under it were not, so only
    the sections could rot -- and both had: `### todo (8)` went on naming
    `ViewerTaskView`, `MouseActionsTaskView` and `ExpressionMixerTaskView`
    after all three moved to `covered`, and `### covered (17)` stayed behind
    when the bucket reached 20. A reader picking the next view to port reads a
    section, not the table.

    Only the heading number is compared. The sections are prose with names
    threaded through several paragraphs -- and they legitimately MENTION views
    from other buckets while explaining a move -- so matching names here would
    fail on correct text. The count is the part that is unambiguous, and it
    caught both real drifts.
    """
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^### (\w+) \((\d+)\)\s*$",
                                 PLAN_FILE.read_text(), re.MULTILINE)
            if m.group(1) in BUCKET_NAMES}


# What proves a view REACHES THE USER in this port.
#
# The counts above audit the reference. Nothing audited the other half of the
# claim -- whether OUR bucket for a view is still true -- and that is the half
# that rotted. `ExportTaskView` sat in `todo` under the comment "the writers
# exist but nothing in the UI reaches them" long after `file.export` shipped
# with a handler, five formats and three tests. Six proxy choosers sat in
# `blocked` under "the viewport draws exactly one mesh" after that was fixed
# and five slots had shipped.
#
# So each entry names a literal that appears in `src/` if and only if the
# capability is wired. The rule runs BOTH ways, because the file got it wrong
# in both directions:
#
#   covered/done  ->  the evidence MUST be present
#   blocked, and the not-yet-ported bucket  ->  it must be ABSENT
#
# Every non-declined view must appear here or in `ABSENT` below. Being unlisted
# used to be allowed, which meant a view could ship and go on being unchecked
# because nobody had written it down -- exactly the decorative check this exists
# to prevent.
EVIDENCE = {
    "LoadTaskView": '"file.open"',
    "SaveTaskView": '"file.save"',
    "ExportTaskView": '"file.export"',
    "OpenGLTaskView": '"file.render"',
    "RandomTaskView": '"edit.randomise"',
    "ShortcutsTaskView": '"settings.shortcuts"',
    "SettingsTaskView": '"settings.units"',
    "MaterialTaskView": '"Skin material"',
    "PoseLibraryTaskView": '"Pose"',
    # SHIPPED 2026-09-16. Kept from the old ABSENT rationale because it is the
    # reason the chooser took two attempts: the first produced four state bugs
    # -- a frame index leaking into later pose loads, the two combos
    # contradicting each other, a Skeleton change dropping the animation, and a
    # failed load leaving a lying combo plus a no-op undo entry. All four were
    # live again in the second attempt until they were looked for by name.
    "AnimationLibrary": '"Animation"',
    "SkeletonLibrary": '"Skeleton"',
    # The MENU ENTRY. A literal matching the licence TEXT would be satisfied by
    # a string in a header comment; this appears only when the window builds the
    # action.
    "HelpTaskView": '"help.about"',
    # The CHOOSER, not the `--expression` flag: the flag shipped long before
    # the view did, so a literal matching it would have reported this covered
    # while the window offered nothing.
    "ExpressionTaskView": '"Expression"',
    # The MENU ENTRY, not the flag. `--background` existed for a year as a
    # `--render` backdrop while the modelling aid the reference task actually is
    # did not, so a literal matching the flag would have reported this covered
    # throughout. This one appears only when the window can ask for it.
    "BackgroundChooser": '"view.background.set"',
    # NO BRACES on the five helper-cage slots. These match a `kProxySlots`
    # row, and the row GREW a third field when teeth became default-on --
    # `{"teeth", "Teeth"}` stopped matching `{"teeth", "Teeth", "teeth"}` and
    # all five views were reported uncovered at once, by a change that had
    # taken nothing away. The key/group pair is what identifies the slot;
    # pinning the closing brace pinned the struct's arity as well, which is not
    # what this evidence is about.
    "TeethTaskView": '"teeth", "Teeth"',
    "TongueTaskView": '"tongue", "Tongue"',
    "HairTaskView": '"hair", "Hair"',
    "ClothesTaskView": '"clothes", "Clothes"',
    "EyelashesTaskView": '"eyelashes", "Eyelashes"',
    "EyesTaskView": '"Eye colour"',
    "CustomTargetsTaskView": '"custom-targets"',
    "MouseActionsTaskView": '"mouse.orbit"',
    "ViewerTaskView": 'ImageViewer(&window)',
    "ExpressionMixerTaskView": '"pose-unit"',
    # The CONNECT, not the panel. A literal naming the class would be satisfied
    # by `MaterialPanel.cpp` existing, and this chunk's own lesson is that the
    # panel can exist, build, pass its tests and still not reach the user -- it
    # first arrived as a one-row sliver, and before that an edit had nowhere to
    # go. This appears only where an edited row is applied to the character.
    "MaterialEditorTaskView": '&mh::ui::MaterialPanel::edited',
}

SRC = REPO / "src"


def shipped(literal) -> bool:
    """Whether @p literal appears anywhere under src/."""
    return any(literal in path.read_text(encoding="utf-8", errors="ignore")
               for path in SRC.rglob("*")
               if path.is_file() and path.suffix in {".cpp", ".h"})


# Views that have NOT shipped -- each with the literal that would be in `src/`
# if it had, and why it has not. The reason is not the check: the LITERAL is.
#
# The first version of this map held free text only, and two of its eleven
# reasons were false. `MouseActionsTaskView` said "no UI surface yet" while
# `ShortcutsDialog` had been rebinding `mouse.orbit` and `mouse.pan` since it
# was written (its own header says it merges the reference's mouse and shortcut
# tasks on purpose), and `ViewerTaskView` said "deliberately not built" while
# `mh::ui::ImageViewer` had been showing every render since M8. A sentence
# nobody runs is a claim nobody checks -- which is the hole this gate exists to
# close, one level up from where it was closed last time.
#
# So an entry here asserts the same kind of fact an EVIDENCE entry does, in the
# opposite direction, and the moment the view ships the gate says so.
ABSENT = {
    # NO BRACES here either -- same reason as EVIDENCE above, and the inverse
    # failure. `absence_mismatches` fires when the literal IS in src/, so a
    # brace-pinned string would keep reporting these views absent after they
    # ship, because the row they will be added to now reads
    # `{"eyebrows", "Eyebrows", "none"}`. Fixing EVIDENCE alone would have left
    # the trap armed in the direction that is harder to notice: a view that
    # shipped and is still reported missing.
    "EyebrowsTaskView": ('"eyebrows", "Eyebrows"',
                         "blocked on content: no helper cage in the base mesh"),
    "ProxyTaskView": ('"proxy", "Proxy"',
                      "blocked on content: no wearable alternate body topology"),
    "SceneLibraryTaskView": ('"Scene lighting"', "blocked on a lighting model"),
}


def unchecked_views(standalone):
    """Non-declined views that claim neither evidence nor a stated absence."""
    return [name for name in standalone
            if BUCKETS.get(name) != "declined"
            and name not in EVIDENCE and name not in ABSENT]


def absence_mismatches():
    """Views claimed absent that are contradicted -- by src/, or by their own
    bucket.

    The second half closes a hole the first half would otherwise leave: a view
    listed here but bucketed `covered` is checked by neither rule, because
    `evidence_mismatches` only walks `EVIDENCE`. It would then claim to reach
    the user on no evidence whatsoever.
    """
    wrong = []
    for name, (literal, reason) in sorted(ABSENT.items()):
        if shipped(literal):
            wrong.append(f"{name}: claimed absent ({reason}) but {literal} IS in src/")
        elif BUCKETS.get(name) in {"covered", "done"}:
            wrong.append(f"{name}: bucket '{BUCKETS[name]}' but it is listed as "
                         f"absent -- a covered view needs an EVIDENCE literal")
    return wrong


def evidence_mismatches():
    """Views whose bucket disagrees with whether their evidence is in src/."""
    wrong = []
    for name, literal in sorted(EVIDENCE.items()):
        bucket = BUCKETS.get(name)
        if bucket is None:
            wrong.append(f"{name}: has evidence but no bucket")
            continue
        found = shipped(literal)
        if bucket in {"covered", "done"} and not found:
            wrong.append(f"{name}: bucket '{bucket}' but {literal} is not in src/")
        elif bucket in {"todo", "blocked"} and found:
            wrong.append(f"{name}: bucket '{bucket}' but {literal} IS in src/ -- it shipped")
    return wrong


def main():
    standalone = standalone_views()
    dynamic = dynamic_view_names()
    totals = {"explicit": len(standalone), "dynamic": len(dynamic),
              "total": len(standalone) + len(dynamic)}

    tabs = registered_tabs()
    if tabs != len(standalone) + 1:
        print(f"{len(standalone)} standalone views but {tabs} addTask call sites "
              f"(expected {len(standalone) + 1}: one is the dynamic-view loop)",
              file=sys.stderr)
        return 1

    recorded = recorded_totals()
    if recorded != totals:
        print(f"task-view inventory drifted: live {totals}, recorded {recorded}",
              file=sys.stderr)
        return 1

    mismatched = evidence_mismatches()
    if mismatched:
        print("task-view buckets disagree with src/:", file=sys.stderr)
        for line in mismatched:
            print(f"  {line}", file=sys.stderr)
        return 1

    unclassified = [name for name in standalone if name not in BUCKETS]
    if unclassified:
        print("unclassified task views (add them to BUCKETS): "
              + ", ".join(unclassified), file=sys.stderr)
        return 1

    unchecked = unchecked_views(standalone)
    if unchecked:
        print("task views with neither evidence nor a stated absence "
              "(add to EVIDENCE or ABSENT): " + ", ".join(sorted(unchecked)),
              file=sys.stderr)
        return 1

    shipped_after_all = absence_mismatches()
    if shipped_after_all:
        print("task views whose claimed absence is contradicted:", file=sys.stderr)
        for line in shipped_after_all:
            print(f"  {line}", file=sys.stderr)
        return 1

    # Seeded with every bucket name, because a Counter drops the ones that
    # reach zero -- and `todo` reached zero on 2026-09-17. Without the seed the
    # only way to pass would be to DELETE the `todo` row and its section, which
    # is the one moment the count is most worth stating, and it would leave the
    # bucket unaudited if a later view moved back into it.
    counts = Counter(dict.fromkeys(BUCKET_NAMES, 0))
    counts.update(BUCKETS[name] for name in standalone)
    counts["done"] = len(dynamic)

    # The plan file states these numbers in a table nobody was reading. Two
    # views moved bucket in this very chunk; without this the table would have
    # gone on saying 17 and 8.
    if recorded_buckets() != dict(counts):
        print(f"bucket table drifted: live {dict(counts)}, "
              f"recorded {recorded_buckets()}", file=sys.stderr)
        return 1

    # ...and the heading over each section, which is what a reader picking the
    # next view actually reads. Audited for the same reason the table is.
    if recorded_section_counts() != dict(counts):
        print(f"section headings drifted: live {dict(counts)}, "
              f"headings {recorded_section_counts()}", file=sys.stderr)
        return 1

    print(f"task views: {totals['total']} "
          f"({totals['explicit']} standalone + {totals['dynamic']} built at run time); "
          f"buckets {dict(counts)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
