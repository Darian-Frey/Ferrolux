// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The token vocabulary, spelled once.
//
// ThemeTokens loads the set and answers by string; this is where those strings
// live. A component writes `Tokens.readout`, never `readColour("readout")`,
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

    // ---- reading a token --------------------------------------------------
    // Through the maps rather than through ThemeTokens' lookup methods, and
    // that is the whole of what makes a theme switchable while the panel is
    // running.
    //
    // A binding tracks *property reads*. `readColour("shell")` is a method
    // call: it returns the right answer once and never runs again, so
    // exchanging the set would repaint nothing. `Theme.palette[...]` reads a
    // property, so every binding below depends on the palette and all of them
    // re-evaluate together when it changes. The same trap as `mapToItem` in
    // PanelMenu, and it fails the same way — silently, and looking like it
    // works until the moment it has to.
    //
    // A missing token returns magenta rather than nothing. Nothing resolves to
    // black, and black chrome on a black readout is not a visible failure;
    // magenta is not a colour this panel contains, so it can only be a fault.
    function readColour(name) {
        const value = Theme.palette[name]
        if (value === undefined) {
            console.warn("no colour token:", name, "in set", Theme.name)
            return "#FF00FF"
        }
        return value
    }

    function readMetric(name) {
        const value = Theme.metrics[name]
        if (value === undefined) {
            console.warn("no metric token:", name, "in set", Theme.name)
            return 0
        }
        return value
    }

    function readType(name) {
        const value = Theme.type[name]
        if (value === undefined) {
            console.warn("no type token:", name, "in set", Theme.name)
            return ""
        }
        return value
    }

    // Set by the window. 1.0 draws the panel at the reference size the token
    // file was authored against; everything scales from there continuously,
    // never in steps.
    property real scale: 1.0

    readonly property real referenceWidth: readMetric("reference-width")

    // Given a window width, the scale that fills it. Clamped at both ends: a
    // panel scaled below the minimum has controls too small to hit, and one
    // scaled past the maximum is a legibility problem rather than a rendering
    // one. The clamp bounds are in the token file so a variant with a denser
    // layout can move them.
    function scaleFor(width) {
        return Math.max(readMetric("scale-min"),
                        Math.min(readMetric("scale-max"), width / referenceWidth))
    }

    // ---- palette ----------------------------------------------------------
    // SPEC.md §Design tokens is authoritative for these; tests/tokens_test
    // asserts the file still agrees with it.
    readonly property color shell: readColour("shell")
    readonly property color shellRecess: readColour("shell-recess")
    readonly property color shellEdge: readColour("shell-edge")
    readonly property color displayBg: readColour("display-bg")
    readonly property color readout: readColour("readout")
    readonly property color readoutDim: readColour("readout-dim")
    readonly property color readoutFloor: readColour("readout-floor")
    readonly property color ink: readColour("ink")

    // ---- geometry ---------------------------------------------------------
    readonly property real radiusPanel: readMetric("radius-panel") * scale
    readonly property real radiusSection: readMetric("radius-section") * scale
    readonly property real radiusControl: readMetric("radius-control") * scale
    readonly property real radiusSlot: readMetric("radius-slot") * scale

    // A division line, in device-independent units. Deliberately sub-unit: this
    // is the line the chassis is scored with, not a border, and at 2x it becomes
    // one crisp pixel rather than two.
    readonly property real hairline: readMetric("hairline") * scale

    readonly property real bevel: readMetric("bevel") * scale

    // Factors for Qt.lighter and Qt.darker, not fractions. A moulded face is
    // lit from above, so its top is its own colour lightened by one of these
    // and its bottom the same colour darkened by the other; pressing a control
    // exchanges them. Both are greater than 1 because that is what those two
    // functions take — a value below 1 passed to Qt.lighter darkens, silently.
    readonly property real bevelLight: readMetric("bevel-light")
    readonly property real bevelShadow: readMetric("bevel-shadow")

    // How far a moulded control moves when pressed, and how long it takes.
    // F-040 requires visible travel: a control that changes colour but does not
    // move reads as a picture of a button.
    readonly property real travel: readMetric("travel") * scale
    readonly property int travelMs: readMetric("travel-ms")

    // How deep a well is sunk, as distinct from `bevel`, which is how the lip
    // of a raised face is lit. A recess has a wall; a bevel is a highlight on
    // an edge. Naming them apart because they are a letter apart.
    readonly property real recess: readMetric("recess") * scale

    readonly property real gapPanel: readMetric("gap-panel") * scale
    readonly property real gapSection: readMetric("gap-section") * scale
    readonly property real gapControl: readMetric("gap-control") * scale
    readonly property real padSection: readMetric("pad-section") * scale
    readonly property real padRow: readMetric("pad-row") * scale

    readonly property real controlHeight: readMetric("control-height") * scale
    readonly property real slotWidth: readMetric("slot-width") * scale
    readonly property real thumbWidth: readMetric("thumb-width") * scale
    readonly property real thumbHeight: readMetric("thumb-height") * scale
    readonly property real faderTravel: readMetric("fader-travel") * scale
    readonly property real detent: readMetric("detent") * scale

    // ---- lamp tiers -------------------------------------------------------
    // The colours a lit display uses beyond the three the palette names, all
    // derived from `readout` rather than written down.
    //
    // They were literal hexadecimal in the meter shaders until now (IMP-006),
    // which meant a set that changed the lamp would have changed the whole
    // panel except the meters — F-044's VFD-green readout would have arrived
    // with amber caps and an amber flame.
    //
    // Derived rather than added to the palette, because that is what they are.
    // Measured against the literals they replace, every one is the same lamp at
    // a different hue and lightness: the cap is +3° and a fifth lighter, the
    // flame's near rank is 11° redder, the ladder's over-segments 23° redder
    // still. A physical display has one phosphor and shows it hotter or cooler,
    // and that is exactly this arithmetic — so a green lamp gives a green cap
    // and a green-shifted warning without anyone choosing them.
    //
    // The offsets were fitted to the literals and reproduce them to within
    // 2/255 on the worst channel, which is below what the eye resolves and
    // below what the display's own dithering does. The flame's two in
    // particular were tuned by eye by the author over several passes; the point
    // of fitting rather than guessing was to keep that work.
    function lampShift(hueDegrees, saturationFactor, lightnessLift) {
        return Qt.hsla((readout.hslHue + hueDegrees / 360 + 1) % 1,
                       Math.max(0, Math.min(1, readout.hslSaturation * saturationFactor)),
                       Math.max(0, Math.min(1, readout.hslLightness + lightnessLift)),
                       1)
    }

    // The hot tier: a peak cap, a VU needle. Brighter than the lamp and very
    // slightly further round the hue, which is what a segment driven harder
    // actually does.
    readonly property color readoutPeak: lampShift(3, 1.00, 0.21)

    // The flame's near and far ranks. The near one is redder and barely
    // lighter; the far one is the hot tier a shade further still.
    readonly property color flameFront: lampShift(-11, 0.91, 0.05)
    readonly property color flameBack: lampShift(4.5, 1.16, 0.23)

    // Over reference. Redder again — the direction a warning goes on a lamp
    // that has only one colour to say it with.
    readonly property color segmentOver: lampShift(-22.6, 0.92, 0.02)

    // An unlit segment: the well with a little of the lamp bled into it, which
    // is what an LED that is off but sitting behind the same window looks like.
    // Not black — a segment that vanishes when unlit reads as a gap in the
    // display rather than as a segment.
    readonly property color segmentOff: Qt.tint(displayBg,
                                                Qt.rgba(readout.r, readout.g, readout.b, 0.075))

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
    readonly property string readoutNumeric: readType("readout-numeric")
    readonly property string readoutText: readType("readout-text")
    readonly property string readoutSegment: readType("readout-segment")
    readonly property string legend: readType("legend")

    readonly property real sizeReadoutLarge: readType("size-readout-large") * scale
    readonly property real sizeReadout: readType("size-readout") * scale
    readonly property real sizeLegend: readType("size-legend") * scale
    readonly property real sizeLegendSmall: readType("size-legend-small") * scale
}
