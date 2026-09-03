// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A moulded control surface: the momentary buttons of the transport.
//
// F-040 asks for "chunky moulded controls with visible travel state", and the
// operative word is *travel*. A control that only changes colour when pressed
// reads as a picture of a button; one that moves reads as a button. So pressing
// this drops its face into the well and inverts the lighting, and the two
// together are what the eye takes as depression — either alone is a flicker.
//
// The moulding is a gradient and an edge, not a bitmap and not a blur. A piece
// of light-coloured plastic under diffuse light from above is brighter at the
// top of its curve and darker at the bottom, with a fine dark line where the
// face meets the chassis. That is three vector primitives, exact at any scale,
// and it costs almost nothing — which matters, because there will be a lot of
// these and AV-002 is measured over the whole window rather than the meters.
//
// The face is always one `travel` shorter than the control, and moves within
// that space rather than outside it. A button that grew when pressed would push
// its neighbours about, and the strip would breathe every time it was used.
//
// Nothing here is a Controls Button. Qt Quick Controls draws its background
// from a style, and a style is the thing this phase exists to replace;
// inheriting one and overriding its parts would split the panel's appearance
// between the token set and whatever the style does when nobody is looking.

import QtQuick

Item {
    id: control

    property alias pressed: tap.pressed

    // A latching appearance for the control that is currently doing something —
    // Play, while playing. Distinct from `pressed`, which is momentary: this is
    // the lamp behind the button rather than the finger on it.
    property bool activated: false

    // What the button is marked with: a drawn glyph, or a legend. Held on the
    // face so it travels with it — a mark that stayed still while its button
    // moved would separate the two.
    default property alias mark: holder.data

    signal clicked()

    implicitWidth: Tokens.controlHeight * 2
    implicitHeight: Tokens.controlHeight

    // 0 at rest, 1 fully depressed. A real number rather than the boolean, so
    // the travel can be animated: F-040 asks for tactile timing, and instant
    // travel reads as a redraw rather than as a press.
    property real depress: pressed ? 1.0 : 0.0
    Behavior on depress { NumberAnimation { duration: Tokens.travelMs } }

    Rectangle {
        id: face
        x: 0
        y: control.depress * Tokens.travel
        width: control.width
        height: control.height - Tokens.travel

        radius: Tokens.radiusControl
        border.width: Tokens.hairline
        border.color: control.activated ? Tokens.readoutFloor : Tokens.shellEdge
        opacity: control.enabled ? 1.0 : 0.45

        // Light from above. Pressed, the same surface is lit from below, which
        // is what a face sunk into a shadowed well actually does — and is why
        // inverting the gradient reads as depth rather than as a colour change.
        // An activated button is amber and lit the same way, so it stays the
        // same moulding rather than becoming a differently drawn control.
        readonly property color base: control.activated ? Tokens.readout : Tokens.shellRecess

        readonly property color lit: Qt.lighter(base, Tokens.bevelLight)
        readonly property color shadowed: Qt.darker(base, Tokens.bevelShadow)

        // The lighting follows `depress`, the same quantity as the travel, and
        // not the mouse. Two states for one thing is how a face ends up moved
        // but still lit from above — which was the first version of this, and
        // it read as the button sliding rather than sinking. Anything that
        // depresses the control now inverts its lighting by construction.
        //
        // Interpolated rather than switched, so the turn-over happens across
        // the same few tens of milliseconds as the movement. A face that jumped
        // between two lightings while gliding between two positions would come
        // apart at exactly the moment it is being looked at.
        function blend(a, b, t) {
            return Qt.rgba(a.r + (b.r - a.r) * t,
                           a.g + (b.g - a.g) * t,
                           a.b + (b.b - a.b) * t, 1.0)
        }

        gradient: Gradient {
            GradientStop { position: 0.0; color: face.blend(face.lit, face.shadowed, control.depress) }
            GradientStop { position: 1.0; color: face.blend(face.shadowed, face.lit, control.depress) }
        }

        // The fillet: a fine bright line just inside the top edge, where the
        // moulded curve turns over and catches the light. It is the detail that
        // separates a rounded rectangle from a moulding, and the first thing to
        // go when the button is pressed and that edge falls into shadow.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Tokens.radiusControl
            anchors.rightMargin: Tokens.radiusControl
            anchors.topMargin: Tokens.hairline * 2
            height: Tokens.hairline
            color: Qt.lighter(face.base, Tokens.bevelLight * 1.08)
            opacity: 1.0 - control.depress
            visible: control.enabled
        }

        Item {
            id: holder
            anchors.fill: parent
        }
    }

    MouseArea {
        id: tap
        anchors.fill: parent
        onClicked: control.clicked()
    }
}
