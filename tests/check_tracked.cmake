# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Every file named here must be TRACKED BY GIT, not merely present on disk.
#
# THE FAILURE THIS CATCHES, which .gitignore has now recorded four times: a
# blanket extension rule (`*.png`, `*.mhpose`, `*.mhskel`) silently swallows a
# new file, `git add -A` says nothing, `git status` shows the containing
# directory as untracked either way, and every local build keeps working
# because the file is sitting right there. It fails only on a fresh checkout --
# which means CI, after the push, on a run that was otherwise green.
#
# `packaging/dmg-background.png` did exactly that: committed alongside its
# .DS_Store, ignored by `*.png`, and the `dmg` CI job died on "No such file or
# directory" while the same target had just succeeded here.
foreach(f IN LISTS FILES)
    execute_process(COMMAND ${GIT} -C "${ROOT}" ls-files --error-unmatch "${f}"
                    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR
                "${f} is not tracked by git. It exists here and will not exist on a fresh "
                "clone, so CI will fail where this build passes. If a .gitignore rule ate "
                "it, add the exception rather than force-adding the file.")
    endif()
endforeach()
list(LENGTH FILES n)
message(STATUS "all ${n} named files are tracked")
