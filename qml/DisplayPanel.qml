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

    color: Tokens.displayBg
    radius: Tokens.radiusSection

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

    implicitHeight: Math.max(titleReadout.implicitHeight, timeReadout.implicitHeight)
                    + Tokens.padSection * 2

    // The time is laid out first and the title takes what is left. A title is
    // elidable and a clock is not: giving the title the remainder means a long
    // title shortens, whereas sharing the width evenly would eventually clip a
    // digit off the time, and a clock missing a digit is wrong rather than
    // abbreviated.
    Readout {
        id: timeReadout
        anchors.right: parent.right
        anchors.rightMargin: Tokens.padSection
        anchors.verticalCenter: parent.verticalCenter
        width: implicitWidth
        height: implicitHeight

        face: Tokens.readoutNumeric
        size: Tokens.sizeReadoutLarge
        colour: Tokens.readout
        ghostColour: Tokens.readoutFloor
        alignment: Text.AlignRight

        // Elapsed, against a ghost built from the *duration* — the field is
        // then exactly as wide as this recording can make it and no wider, and
        // a track that crosses an hour does not grow a cell mid-play.
        text: display.clock(display.position)
        ghost: display.ghostFor(display.clock(display.duration))
    }

    Readout {
        id: titleReadout
        anchors.left: parent.left
        anchors.leftMargin: Tokens.padSection
        anchors.right: timeReadout.left
        anchors.rightMargin: Tokens.gapControl
        anchors.verticalCenter: parent.verticalCenter
        height: implicitHeight

        face: Tokens.readoutText
        size: Tokens.sizeReadoutLarge
        colour: Tokens.readout
        ghost: ""   // no all-lit glyph in the dot-matrix face; see above
        text: display.title
    }
}
