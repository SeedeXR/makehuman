# Writes the backdrop test fixture: a 2:1 chequered image, as a PPM.
#
# GENERATED rather than committed, and the ASPECT is the whole point: 2:1 into a
# portrait viewport means cover-fit must crop the width, so a broken cover rule
# shows as a distorted or off-centre pattern instead of being bypassed by an
# image that happened to fit.
#
# PPM because CMake can write text but cannot encode PNG, and Qt reads PPM
# natively -- so this needs no imaging tool and no committed binary. It is
# deliberately TINY (32x16): the test asks whether the backdrop is drawn and
# where, not what it looks like up close, and CMake's string handling is
# quadratic enough that a full-size image would cost seconds per run.
set(_rows "")
foreach(_y RANGE 0 15)
    set(_line "")
    foreach(_x RANGE 0 31)
        math(EXPR _cell "(${_x} / 4 + ${_y} / 4) % 2")
        if(_cell EQUAL 0)
            string(APPEND _line "230 180 60 ")
        else()
            string(APPEND _line "20 40 90 ")
        endif()
    endforeach()
    string(APPEND _rows "${_line}\n")
endforeach()
file(WRITE "${OUT}" "P3\n32 16\n255\n${_rows}")
