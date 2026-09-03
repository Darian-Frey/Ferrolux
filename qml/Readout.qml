// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A lit readout: two text layers, one face, one position.
//
// SPEC.md §Typography — "Physical LED and VFD readouts show their inactive
// segments faintly rather than not at all, and omitting this is the most common
// tell of a simulated readout." A seven-segment `1` on real hardware sits in a
// visible ghost of the `8` it could have been, and the eye reads the whole cell
// whether or not it is looking for it. Draw only the lit segments and the
// display reads as text in a segmented font, which is exactly what it is.
//
// So: a ghost layer of the all-segments-lit string in `readout-floor`, with the
// live string in `readout` over it, at identical face, size and position. One
// extra text node per readout and nothing else — no shader, no offscreen pass.
//
// The ghost is also why the field does not move. Sized to the ghost rather than
// to the live string, a clock counting 9:59 to 10:00 keeps its box instead of
// growing a digit, because the ghost was always as wide as the field can get.
//
// **Which face is not this component's choice.** SPEC.md makes the division a
// hard rule: `type-readout-numeric` renders digits and separators only, because
// a seven-segment alphabet cannot distinguish 5 from S, 6 from b, or 0 from O.
// Text goes to `type-readout-text`, which is a dot-matrix face for that reason.
// Passing a title to a numeric readout is the mistake the rule exists to
// prevent, so `ghost` has no default: a caller must say what the field's full
// extent is, and saying it for a title is awkward enough to be a prompt.

import QtQuick

Item {
    id: readout

    // The ground the readout is lit against. SPEC.md's palette calls
    // `display-bg` the readout background, and that is not decoration: the
    // whole lit-over-unlit arrangement below depends on the ground being
    // darker than both layers.
    //
    // Put a readout on the chassis instead and the relationship inverts. The
    // ghost is a dark brown, which against a light shell has *more* contrast
    // than the amber value over it — so the segments that are off read louder
    // than the number that is on, and the field becomes hard to read precisely
    // because it is drawn correctly. A readout standing on the chassis
    // therefore carries its own window, as the numerals on a deck do.
    //
    // Transparent by default, for the readouts already inside a well: painting
    // the same colour again there would only cover whatever the well is doing
    // underneath, such as the backlight on a selected playlist row.
    property color ground: "transparent"
    property real inset: 0

    // What the instrument reports.
    property string text: ""

    // The all-segments-lit string for this field: "88:88" for a time, "888" for
    // a three-digit gain. Where a face has no all-lit glyph, leave it empty and
    // the ghost layer is omitted rather than approximated — SPEC.md prefers no
    // ghost to a wrong one.
    property string ghost: ""

    property string face: Tokens.readoutNumeric
    property real size: Tokens.sizeReadout
    property color colour: Tokens.readout
    property color ghostColour: Tokens.readoutFloor

    // Left by default. A right-aligned readout keeps its digits still as the
    // value grows, which is what a time field wants and a title does not.
    property int alignment: Text.AlignLeft

    implicitWidth: Math.max(ghostLayer.implicitWidth, liveLayer.implicitWidth) + inset * 2
    implicitHeight: Math.max(ghostLayer.implicitHeight, liveLayer.implicitHeight) + inset * 2

    Rectangle {
        anchors.fill: parent
        color: readout.ground
        radius: Tokens.radiusSlot
        visible: readout.ground.a > 0
    }

    Text {
        id: ghostLayer
        anchors.fill: parent
        anchors.margins: readout.inset
        visible: readout.ghost !== ""
        text: readout.ghost
        color: readout.ghostColour
        horizontalAlignment: readout.alignment
        verticalAlignment: Text.AlignVCenter
        font.family: readout.face
        font.pixelSize: readout.size
    }

    Text {
        id: liveLayer
        anchors.fill: parent
        anchors.margins: readout.inset
        text: readout.text
        color: readout.colour
        horizontalAlignment: readout.alignment
        verticalAlignment: Text.AlignVCenter
        font.family: readout.face
        font.pixelSize: readout.size

        // Elide belongs to the live layer alone. The ghost describes the field's
        // extent and must never shrink to fit its contents, or the unlit cells
        // would come and go with the value.
        elide: Text.ElideRight
    }
}
