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
    # Done differently: the reference makes these tabs, we make them File menu
    # actions (src/ui/MainWindow.cpp:138-143). Export is NOT here -- the writers
    # exist but nothing in the UI reaches them, so it is a todo, not a gap we
    # closed differently.
    "LoadTaskView": "covered", "SaveTaskView": "covered",

    # All eight proxy choosers, blocked on one thing: the viewport draws exactly
    # one mesh. SceneLibraryTaskView is blocked on something else -- a scene is
    # lights plus environment (shared/scene.py:190-192), so it needs a lighting
    # model, not multi-mesh.
    "ClothesTaskView": "blocked", "EyesTaskView": "blocked",
    "EyebrowsTaskView": "blocked", "EyelashesTaskView": "blocked",
    "HairTaskView": "blocked", "TeethTaskView": "blocked",
    "TongueTaskView": "blocked", "ProxyTaskView": "blocked",
    "SceneLibraryTaskView": "blocked",

    # AnimationLibrary gates only on a skeleton and an active animation
    # (3_libraries_animation.py:150,157) -- rig/ and io/BvhReader.h have both.
    "AnimationLibrary": "todo", "ExportTaskView": "todo",
    "MaterialTaskView": "todo", "PoseLibraryTaskView": "todo",
    "SkeletonLibrary": "todo", "ExpressionTaskView": "todo",
    "OpenGLTaskView": "todo", "ViewerTaskView": "todo",
    "BackgroundChooser": "todo", "CustomTargetsTaskView": "todo",
    "RandomTaskView": "todo", "MaterialEditorTaskView": "todo",
    "ExpressionMixerTaskView": "todo", "SettingsTaskView": "todo",
    "MouseActionsTaskView": "todo", "ShortcutsTaskView": "todo",
    "HelpTaskView": "todo",

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

    unclassified = [name for name in standalone if name not in BUCKETS]
    if unclassified:
        print("unclassified task views (add them to BUCKETS): "
              + ", ".join(unclassified), file=sys.stderr)
        return 1

    counts = Counter(BUCKETS[name] for name in standalone)
    counts["done"] = len(dynamic)

    print(f"task views: {totals['total']} "
          f"({totals['explicit']} standalone + {totals['dynamic']} built at run time); "
          f"buckets {dict(counts)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
