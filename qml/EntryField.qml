// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A lit field that can be typed into.
//
// Text the user is entering is a value the instrument is holding, not a legend
// naming a control, so it is lit in a well like every other value. What names
// it — `filter` — is printed on the chassis beside it, in the ordinary way.
//
// `TextInput` rather than Controls' `TextField`: the primitive is the editing
// behaviour, which is what is wanted, and the wrapper is a background and a
// placeholder drawn from a style, which is what is not. The placeholder is
// re-made below at the ghost brightness, so an empty field reads as an unlit
// one rather than as a field with grey words in it.

import QtQuick

Rectangle {
    id: field

    property alias text: input.text
    property string placeholder: ""

    color: Tokens.displayBg
    radius: Tokens.radiusSlot
    implicitHeight: Tokens.controlHeight
    implicitWidth: Tokens.controlHeight * 4

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Tokens.gapControl
        anchors.rightMargin: Tokens.gapControl
        verticalAlignment: Text.AlignVCenter
        clip: true

        color: Tokens.readout
        selectionColor: Tokens.readoutFloor
        selectedTextColor: Tokens.readout
        selectByMouse: true

        font.family: Tokens.readoutText
        font.pixelSize: Tokens.sizeReadout

        // The caret is part of the lamp, not part of the desktop.
        cursorDelegate: Rectangle {
            width: Tokens.hairline * 2
            color: Tokens.readout
        }

        // Unlit rather than grey. The prompt sits at the ghost brightness the
        // rest of the panel uses for a segment that is off, so an empty field
        // looks like a field waiting to be lit.
        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: input.text === "" && !input.activeFocus
            text: field.placeholder
            color: Tokens.readoutFloor
            font: input.font
        }
    }
}
