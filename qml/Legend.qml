// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Silkscreened text: the name of a control, printed on the chassis.
//
// The other half of SPEC.md's lit-versus-printed rule, and the reason it is a
// component rather than a `Text` with the right properties. A legend names a
// control and never changes; a readout reports a value and does. Giving each a
// component means the choice is made once, at the point where a thing is
// identified as one or the other, rather than every time something is drawn.
//
// `ink` on the shell, in the legend face, always. A legend rendered in the
// readout palette implies a state it does not have — a printed word that looks
// lit reads as a lamp that is on.

import QtQuick

Text {
    color: Tokens.ink
    font.family: Tokens.legend
    font.pixelSize: Tokens.sizeLegend
    // Distance-field rendering, which is the default and is kept deliberately.
    // Native rendering hints glyphs onto the pixel grid and is crisper at 1x,
    // but it does not survive a fractional device pixel ratio or a scaled
    // parent — and F-041 requires exactly those. AV-005 is about chrome that is
    // correct only at the size it was drawn for; hinted text is that in
    // miniature.
    verticalAlignment: Text.AlignVCenter
}
