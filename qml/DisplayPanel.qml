// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The lit display: what the instrument is playing, and where it has got to.
//
// The first piece of the real panel rather than of the harness. Everything here
// is a value the instrument reports, so all of it is lit in a readout face on
// `display-bg` — nothing on this block names a control, and so nothing on it is
// printed in `ink`. SPEC.md §Typography makes that division a hard rule and
// F-040 restates it: a lit legend implies a state it does not have, and a
// printed value looks inert.
//
// Two faces, because the rule divides them by content and not by taste. The
// time is `type-readout-numeric`, a seven-segment face, which renders digits
// and separators and must never be given text — a seven-segment alphabet cannot
// distinguish 5 from S, 6 from b, or 0 from O. The title is arbitrary Unicode
// from a tag, so it goes to `type-readout-text`, a dot-matrix face chosen for
// covering Latin, Cyrillic, Greek, Armenian, Hebrew, Arabic and Korean: a
// non-Latin title degrades to a wider glyph set from the same face rather than
// to a substituted system font in the middle of a lit readout.
//
// The time carries an unlit ghost and the title does not, which is a finding
// rather than an inconsistency. SPEC.md left the dot-matrix ghost codepoint
// provisional pending this phase; the bundled Handjet instance has no full-cell
// glyph — no U+2588, U+25A0, U+2593 or U+25CF among its 1,339 — and SPEC.md is
// explicit that a face without an all-lit glyph gets no ghost rather than an
// approximated one. A row of some other dense character would be a different
// string in the same face, not the same string unlit.

import QtQuick

Rectangle {
    id: display

    property string title: ""
    property real position: 0
    property real duration: 0

    color: display.ground
    radius: Tokens.radiusSection
    border.width: Tokens.hairline
    border.color: Tokens.shellEdge

    // The wall of the recess: a well has a depth, and the shadow the near wall
    // casts across the top of it is what shows that. See PanelSection, which
    // does the same thing for every other well on the panel.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Tokens.radiusSection
        anchors.rightMargin: Tokens.radiusSection
        anchors.topMargin: Tokens.hairline
        height: Tokens.recess
        radius: Tokens.radiusSlot
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.darker(display.ground, Tokens.bevelShadow * 1.3) }
            GradientStop { position: 1.0; color: display.ground }
        }
    }

    // Nanoseconds to m:ss, or h:mm:ss once there is an hour to show. The field
    // widens by a whole cell when it does, which is why the ghost is built from
    // the same string rather than fixed at "88:88" — a two-hour recording must
    // not be drawn over a ghost that stops at minutes.
    function clock(nanoseconds) {
        const total = Math.max(0, Math.floor(nanoseconds / 1000000000))
        const seconds = total % 60
        const minutes = Math.floor(total / 60) % 60
        const hours = Math.floor(total / 3600)
        const pad = n => (n < 10 ? "0" : "") + n
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds)
    }

    // The all-segments-lit form of a given time: every digit becomes 8 and the
    // separators stay lit, per SPEC.md §Typography. Derived from the live string
    // so the two are always the same shape.
    function ghostFor(text) {
        return text.replace(/[0-9]/g, "8")
    }

    // What the transport is doing, and where in the list it is. Both are values
    // the instrument reports, so both are lit and both belong here rather than
    // on the chassis beside it — which is where they were, in the system font,
    // because the harness put them there before there was a display to put them
    // in.
    property string status: ""
    property string counter: ""
    property string error: ""

    // The album, and what the stream actually is. Both are annotation beside
    // the title rather than things a reader works through, which is what
    // `readout-dim` is for — see SPEC.md §Design tokens and BUG-020.
    property string album: ""
    property string format: ""

    // Lit-on-dark, or dark-on-lit. Inverted, the well *is* the lamp and the
    // text is the part that is not lit — which is how a filled indicator cell
    // works, and is a different instrument rather than the same one recoloured.
    //
    // The secondary tier cannot simply be `readout-dim` here: that is a lamp
    // colour and this ground is already the lamp. It is the primary ink at
    // reduced opacity instead, which stays a fixed distance from the ground
    // whatever the ground is, and so survives a change of theme without a
    // second value having to be chosen for every set.
    property bool inverted: false

    readonly property color ground: inverted ? Tokens.readout : Tokens.displayBg
    readonly property color lit: inverted ? Tokens.displayBg : Tokens.readout
    readonly property color annotation: inverted ? Tokens.displayBg : Tokens.readoutDim
    readonly property real annotationFade: inverted ? 0.72 : 1.0

    // Unlit segments still sit between the ground and the live value, so the
    // ghost darkens the lamp rather than dimming a dark well.
    readonly property color ghostInk: inverted ? Qt.darker(Tokens.readout, 1.22)
                                               : Tokens.readoutFloor

    implicitHeight: titleRow.implicitHeight + statusRow.implicitHeight
                    + Tokens.padSection * 2 + Tokens.padRow

    Item {
        id: titleRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tokens.padSection
        implicitHeight: Math.max(titleReadout.implicitHeight, timeReadout.implicitHeight)
        height: implicitHeight

        // The time is laid out first and the title takes what is left. A title
        // is elidable and a clock is not: giving the title the remainder means
        // a long title shortens, whereas sharing the width evenly would
        // eventually clip a digit off the time, and a clock missing a digit is
        // wrong rather than abbreviated.
        Readout {
            id: timeReadout
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: implicitWidth
            height: implicitHeight

            face: Tokens.readoutNumeric
            size: Tokens.sizeReadoutLarge
            colour: display.lit
            ghostColour: display.ghostInk
            alignment: Text.AlignRight

            // Elapsed, against a ghost built from the *duration* — the field is
            // then exactly as wide as this recording can make it and no wider,
            // and a track that crosses an hour does not grow a cell mid-play.
            text: display.clock(display.position)
            ghost: display.ghostFor(display.clock(display.duration))
        }

        Readout {
            id: titleReadout
            anchors.left: parent.left
            anchors.right: timeReadout.left
            anchors.rightMargin: Tokens.gapControl
            anchors.verticalCenter: parent.verticalCenter
            height: implicitHeight

            face: Tokens.readoutText
            size: Tokens.sizeReadoutLarge
            colour: display.lit
            ghost: ""   // no all-lit glyph in the dot-matrix face; see above
            text: display.title
        }
    }

    // The second line, dimmer than the first. One lamp at two brightnesses:
    // what is playing is the headline and what the transport is doing is the
    // annotation, and a display that lit both equally would make the reader
    // decide which mattered.
    Item {
        id: statusRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleRow.bottom
        anchors.topMargin: Tokens.padRow
        anchors.leftMargin: Tokens.padSection
        anchors.rightMargin: Tokens.padSection
        implicitHeight: albumReadout.implicitHeight
        height: implicitHeight

        // Album on the left, what the stream is in the middle, and where we are
        // in the list on the right. Two thirds of this line used to be empty:
        // the display is the largest surface on the panel and it was reporting
        // three short fields.
        Readout {
            id: albumReadout
            anchors.left: parent.left
            anchors.right: formatReadout.left
            anchors.rightMargin: Tokens.gapControl
            height: implicitHeight
            face: Tokens.readoutText
            size: Tokens.sizeReadout

            // An error takes this line rather than getting one of its own, and
            // takes it at full brightness: it is the more urgent report, and
            // there is no red on this display to say so with, so brightness is
            // the only emphasis a single-colour lamp has.
            colour: display.error !== "" ? display.lit : display.annotation
            opacity: display.error !== "" ? 1.0 : display.annotationFade
            text: display.error !== "" ? display.error : display.album
        }

        Readout {
            id: formatReadout
            anchors.horizontalCenter: parent.horizontalCenter
            width: implicitWidth
            height: implicitHeight
            face: Tokens.readoutText
            size: Tokens.sizeReadout
            colour: display.annotation
            opacity: display.annotationFade
            alignment: Text.AlignHCenter

            // Hidden until the pipeline has something to say. An empty field
            // in the middle of the display is a gap; a field that appears when
            // a track starts is the machine reporting what it found.
            visible: display.error === "" && text !== ""
            text: display.format
        }

        Readout {
            id: counterReadout
            anchors.right: parent.right
            width: implicitWidth
            height: implicitHeight
            face: Tokens.readoutText
            size: Tokens.sizeReadout
            colour: display.annotation
            opacity: display.annotationFade
            alignment: Text.AlignRight
            text: display.status !== "" && display.counter !== ""
                  ? display.status + "  ·  " + display.counter
                  : display.status + display.counter
        }
    }
}
