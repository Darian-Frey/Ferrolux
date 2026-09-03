// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A division of the chassis: either a lit well sunk into it, or a raised
// surface standing on it.
//
// The two are the same component because they are the same moulding seen from
// opposite sides, and saying so once is what keeps them consistent. A well is
// darker at the top, where the chassis overhangs it and casts a shadow, and
// catches light along its bottom lip. A raised surface is the reverse. Getting
// that backwards is the single most common way a drawn panel stops reading as
// an object, and it is one boolean here rather than a decision taken again at
// every section.
//
// F-040 requires hairline gaps between sections in device-independent units,
// and AV-005 explains why: a hairline given in pixels is the founding defect of
// the project in miniature — at 2x it is either two pixels or a blur. The edge
// below is `hairline` from the token set, which is sub-unit for that reason.

import QtQuick

Rectangle {
    id: section

    // A well holds lit things: the display, the playlist. A raised surface
    // holds controls that are touched.
    property bool recessed: true

    color: recessed ? Tokens.displayBg : Tokens.shellRecess
    radius: Tokens.radiusSection
    border.width: Tokens.hairline
    border.color: Tokens.shellEdge

    // The lip. One hairline of light along the bottom of a well, or along the
    // top of a raised surface — the edge that faces the light, in either case.
    // Drawn inside the border rather than over it, so the two do not fight for
    // the same physical pixel at fractional scales.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: section.recessed ? undefined : parent.top
        anchors.bottom: section.recessed ? parent.bottom : undefined
        anchors.leftMargin: Tokens.radiusSection
        anchors.rightMargin: Tokens.radiusSection
        anchors.topMargin: Tokens.hairline * 2
        anchors.bottomMargin: Tokens.hairline * 2
        height: Tokens.hairline
        color: section.recessed ? Qt.lighter(Tokens.displayBg, Tokens.bevelLight)
                                : Qt.lighter(Tokens.shellRecess, Tokens.bevelLight)
    }
}
