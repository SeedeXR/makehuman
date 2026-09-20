# Writes a .mhm carrying `background` lines this port does NOT own, so a
# round-trip gate can prove they survive being saved back out.
#
# Generated rather than committed: the point is the exact bytes, and a
# committed fixture is a second thing to keep in step with the test reading it.
#
# `left` and `top` ARE sides this port owns, and the round-trip run places
# neither -- they are here because `recordBackgrounds` replaces the whole owned
# set, so a save that knew only about `front` silently deleted them. Measured.
#
# `other` is the reference's SEVENTH side -- the three-quarter view this port
# deliberately refuses -- and its filename contains a SPACE, which is what
# makes tail-parsing necessary rather than merely tidy.
file(WRITE "${OUT}"
"version v1.2.0\n\
background other some photo.png 1.5 0.2 0.3 1.5\n\
background enabled True\n\
background front stale.png 1 0 0 1\n\
background left leftphoto.png 1 0.1 0.2 1.5\n\
background top topphoto.png 1 0.3 0.4 2\n\
modifier macrodetails/Gender 0.500000\n")
