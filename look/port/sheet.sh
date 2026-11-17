#!/bin/sh
# Builds firmware/lib/wisp natively, renders Wisp's contact sheets, runs the checks and times a frame.
#   sh look/port/sheet.sh        (node look/port/gen_tables.mjs first, once, or when the model changes)
# Output in look/port/scratch/.
set -e
cd "$(dirname "$0")"
LIB=../../firmware/lib/wisp
mkdir -p scratch/frames

# the library: warnings are errors, and no float may silently become a double
c++ -O2 -std=c++11 -Wall -Wextra -Wdouble-promotion -Werror -c $LIB/wisp.cpp -o scratch/wisp.o
c++ -O2 -std=c++11 -Wall -Wextra -I$LIB wisp_png.cpp scratch/wisp.o -o scratch/wisp_png
# the chip has no SIMD: time a build the compiler may not vectorise
c++ -O2 -std=c++11 -fno-vectorize -fno-slp-vectorize -Wall -Wextra -c $LIB/wisp.cpp -o scratch/wisp_novec.o
c++ -O2 -std=c++11 -Wall -Wextra -I$LIB wisp_png.cpp scratch/wisp_novec.o -o scratch/wisp_png_novec

echo "-- double-precision maths called by the library (should be none):"
nm scratch/wisp.o | grep -E ' U _(sin|cos|tan|exp|log|pow|sqrt|floor|ceil|fmod|atan2|hypot)$' || echo "   none"
echo "-- sections of the library object:"
size -m scratch/wisp.o | grep -E 'Section|__bss|__data|__const|__text|__common' || true

./scratch/wisp_png sheet scratch/wisp-sheet.png scratch/frames
./scratch/wisp_png still scratch/wisp-sheet-still.png scratch/frames
./scratch/wisp_png extra scratch/frames
./scratch/wisp_png check || echo "SOME CHECKS FAILED"
./scratch/wisp_png_novec time
