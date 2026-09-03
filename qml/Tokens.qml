// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The token vocabulary, spelled once.
//
// ThemeTokens loads the set and answers by string; this is where those strings
// live. A component writes `Tokens.readout`, never `Theme.colour("readout")`,
// so a mistyped token is a QML error naming the line rather than a black
// rectangle nobody can trace. It is also the list of what a variant may change:
// if a name is not here, no component can be using it.
//
// **Scaling is here, and only here.** Every metric below is the reference value
// from the token file multiplied by `scale`, so a component that lays out in
// token units is resolution-independent without knowing it. F-041 requires the
// window to resize continuously rather than snapping to fixed multiples, and
// AV-005 is what happens when that is got wrong — the founding defect of the
// project is bitmap chrome resampled to a size it was not drawn for.
//
// Sizes are in device-independent units throughout. Qt multiplies by the device
// pixel ratio on its own, so 1.5x and 2x cost nothing here; what matters is that
// nothing below is ever expressed in pixels. A hairline in pixels is F-040's
// named failure and AV-005 in miniature: at 2x it is either two pixels or a
// blur, and at 1.5x it is always a blur.

pragma Singleton

import QtQuick

QtObject {
    id: tokens

    // Set by the window. 1.0 draws the panel at the reference size the token
    // file was authored against; everything scales from there continuously,
    // never in steps.
    property real scale: 1.0

    readonly property real referenceWidth: Theme.metric("reference-width")

    // Given a window width, the scale that fills it. Clamped at both ends: a
    // panel scaled below the minimum has controls too small to hit, and one
    // scaled past the maximum is a legibility problem rather than a rendering
    // one. The clamp bounds are in the token file so a variant with a denser
    // layout can move them.
    function scaleFor(width) {
        return Math.max(Theme.metric("scale-min"),
                        Math.min(Theme.metric("scale-max"), width / referenceWidth))
    }

    // ---- palette ----------------------------------------------------------
    // SPEC.md §Design tokens is authoritative for these; tests/tokens_test
    // asserts the file still agrees with it.
    readonly property color shell: Theme.colour("shell")
    readonly property color shellRecess: Theme.colour("shell-recess")
    readonly property color shellEdge: Theme.colour("shell-edge")
    readonly property color displayBg: Theme.colour("display-bg")
    readonly property color readout: Theme.colour("readout")
    readonly property color readoutDim: Theme.colour("readout-dim")
    readonly property color readoutFloor: Theme.colour("readout-floor")
    readonly property color ink: Theme.colour("ink")

    // ---- geometry ---------------------------------------------------------
    readonly property real radiusPanel: Theme.metric("radius-panel") * scale
    readonly property real radiusSection: Theme.metric("radius-section") * scale
    readonly property real radiusControl: Theme.metric("radius-control") * scale
    readonly property real radiusSlot: Theme.metric("radius-slot") * scale

    // A division line, in device-independent units. Deliberately sub-unit: this
    // is the line the chassis is scored with, not a border, and at 2x it becomes
    // one crisp pixel rather than two.
    readonly property real hairline: Theme.metric("hairline") * scale

    readonly property real bevel: Theme.metric("bevel") * scale

    // Factors for Qt.lighter and Qt.darker, not fractions. A moulded face is
    // lit from above, so its top is its own colour lightened by one of these
    // and its bottom the same colour darkened by the other; pressing a control
    // exchanges them. Both are greater than 1 because that is what those two
    // functions take — a value below 1 passed to Qt.lighter darkens, silently.
    readonly property real bevelLight: Theme.metric("bevel-light")
    readonly property real bevelShadow: Theme.metric("bevel-shadow")

    // How far a moulded control moves when pressed, and how long it takes.
    // F-040 requires visible travel: a control that changes colour but does not
    // move reads as a picture of a button.
    readonly property real travel: Theme.metric("travel") * scale
    readonly property int travelMs: Theme.metric("travel-ms")

    readonly property real gapPanel: Theme.metric("gap-panel") * scale
    readonly property real gapSection: Theme.metric("gap-section") * scale
    readonly property real gapControl: Theme.metric("gap-control") * scale
    readonly property real padSection: Theme.metric("pad-section") * scale
    readonly property real padRow: Theme.metric("pad-row") * scale

    readonly property real controlHeight: Theme.metric("control-height") * scale
    readonly property real slotWidth: Theme.metric("slot-width") * scale
    readonly property real thumbWidth: Theme.metric("thumb-width") * scale
    readonly property real thumbHeight: Theme.metric("thumb-height") * scale
    readonly property real faderTravel: Theme.metric("fader-travel") * scale
    readonly property real detent: Theme.metric("detent") * scale

    // ---- type -------------------------------------------------------------
    // Four faces, one per role, fixed for the project by D-012. SPEC.md §Typography
    // makes the division between them a hard rule and not a preference: values
    // the instrument reports are lit in a readout face, and text naming a control
    // is printed on the chassis in the legend face. A lit legend implies a state
    // it does not have, and a printed value looks inert.
    //
    // readoutNumeric renders digits and separators only. A seven-segment alphabet
    // cannot tell 5 from S or 0 from O, so it must never be given text — that is
    // what readoutText is for, and why readoutText is a dot-matrix face with a
    // wide script coverage rather than a second segmented one.
    readonly property string readoutNumeric: Theme.face("readout-numeric")
    readonly property string readoutText: Theme.face("readout-text")
    readonly property string readoutSegment: Theme.face("readout-segment")
    readonly property string legend: Theme.face("legend")

    readonly property real sizeReadoutLarge: Theme.size("size-readout-large") * scale
    readonly property real sizeReadout: Theme.size("size-readout") * scale
    readonly property real sizeLegend: Theme.size("size-legend") * scale
    readonly property real sizeLegendSmall: Theme.size("size-legend-small") * scale
}
