#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# Fetches the four bundled faces (D-012, SPEC.md §Typography) and produces the
# Handjet static instance the panel uses.
#
# The faces are redistributed in resources/fonts/ under the SIL Open Font
# License 1.1, with each family's licence text beside it. This script exists so
# that what is committed there has a stated provenance and can be regenerated,
# rather than being binaries of unclear origin that happen to be in the tree.
#
# Handjet is variable, and Ferrolux ships a static instance rather than the
# variable file. Setting an axis at runtime needs QFont::setVariableAxis, which
# arrived in Qt 6.7 — later than Ubuntu 24.04 and the Mint 22 series can give us
# — and on an older Qt the variable file silently renders its *default* element,
# which is a square (ELSH 2.0), not the circle the panel specifies (ELSH 8.0).
# Instancing removes the requirement rather than working around it. See BUG-001.
#
# Consequence, recorded in SPEC.md: changing the dot-matrix character means
# re-running this, not editing a token. It is the one place F-044's token-swap
# property does not hold.
#
#   usage: tools/make-fonts.sh
#          needs curl, unzip, and python3 with fonttools (a venv is made if
#          fonttools is not importable).

set -e
cd "$(dirname "$0")/.."
OUT=resources/fonts
mkdir -p "$OUT"

GF=https://raw.githubusercontent.com/google/fonts/main/ofl
DSEG_VERSION=v0.46
DSEG_ZIP=https://github.com/keshikan/DSEG/releases/download/${DSEG_VERSION}/fonts-DSEG_v046.zip

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "DSEG7 and DSEG14 Classic (keshikan, ${DSEG_VERSION})"
curl -sSL --max-time 120 -o "$WORK/dseg.zip" "$DSEG_ZIP"
unzip -j -o "$WORK/dseg.zip" \
    "fonts-DSEG_v046/DSEG7-Classic/DSEG7Classic-Regular.ttf" \
    "fonts-DSEG_v046/DSEG14-Classic/DSEG14Classic-Regular.ttf" -d "$OUT" >/dev/null
unzip -p "$WORK/dseg.zip" "fonts-DSEG_v046/DSEG-LICENSE.txt" > "$OUT/OFL-DSEG.txt"

echo "IBM Plex Sans Condensed (IBM, via Google Fonts)"
# Regular only. SPEC.md names one face per type role, not a weight range, and
# the other thirteen weights and every italic go unreferenced by the panel —
# shipping a face nothing asks for is package weight for nothing. It is also
# the one that behaves: the other weights each declare their own family name
# ("IBM Plex Sans Condensed Medium") rather than a style within one family, so
# asking for the family and a weight would not reach them anyway.
curl -sSL --max-time 120 -o "$OUT/IBMPlexSansCondensed-Regular.ttf" \
    "$GF/ibmplexsanscondensed/IBMPlexSansCondensed-Regular.ttf"
curl -sSL --max-time 120 -o "$OUT/OFL-IBMPlexSansCondensed.txt" "$GF/ibmplexsanscondensed/OFL.txt"

echo "Handjet (Rosetta Type, via Google Fonts) — instancing"
curl -sSL --max-time 120 -o "$WORK/Handjet-variable.ttf" \
    "$GF/handjet/Handjet%5BELGR%2CELSH%2Cwght%5D.ttf"
curl -sSL --max-time 120 -o "$OUT/OFL-Handjet.txt" "$GF/handjet/OFL.txt"

PY=python3
if ! $PY -c "import fontTools" 2>/dev/null; then
    echo "  fonttools not importable; building a virtualenv"
    $PY -m venv "$WORK/venv"
    "$WORK/venv/bin/pip" install -q fonttools
    PY="$WORK/venv/bin/python"
fi

"$PY" - "$WORK/Handjet-variable.ttf" "$OUT/Handjet-Panel.ttf" <<'PYEOF'
import sys
from fontTools import ttLib
from fontTools.varLib import instancer

# SPEC.md §Typography. ELSH 8.0 is a circular element and 2.0 — the file's own
# default, and so what an un-instanced file would render — is a square. ELGR 1.0
# is one element per grid cell.
#
# wght sets element size, and therefore the gap between neighbouring dots: a
# physical dot-matrix cell shows separated dots, so the value is chosen for a
# visible gap rather than for stroke weight. SPEC.md carried 500 as provisional
# pending this phase, and 500 does not meet its own stated intent — at 500 the
# elements touch and the face renders as continuous strokes at every size the
# panel uses. 300 separates them while staying legible. Below 300 the dots
# separate further but the readout goes faint. See BUG-017.
AXES = {"ELSH": 8.0, "ELGR": 1.0, "wght": 300.0}

font = ttLib.TTFont(sys.argv[1])
instancer.instantiateVariableFont(font, AXES, inplace=True, updateFontNames=True)
font.save(sys.argv[2])

# updateFontNames derives a family name from the pinned axis values, which is
# how the instance ends up as "Handjet Light Circle Single" rather than as
# plain "Handjet". That is the name the panel asks for, and it is deliberate:
# the file is not Handjet as published, and naming it as though it were would
# misreport what has been shipped.
check = ttLib.TTFont(sys.argv[2])
assert "fvar" not in check, "instancing left the variable axes in place"
family = next(str(r) for r in check["name"].names if r.nameID == 1 and r.platformID == 3)
print(f"  instanced as: {family}")
PYEOF

echo
echo "bundled in $OUT:"
ls -1 "$OUT"
echo
echo "All four families are SIL Open Font License 1.1. Handjet declares no"
echo "Reserved Font Name; DSEG reserves \"DSEG\" and is redistributed unmodified."
