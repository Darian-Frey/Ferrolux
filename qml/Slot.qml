// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A continuous control: a slot cut into the chassis, with a lit run and a lever.
//
// F-040 requires continuous controls to carry a legible scale, and this carries
// the plainest one there is — the lit part of the slot is the value. A run of
// light against an unlit remainder is readable from across a room, which is the
// property a deck's position indicator actually needs.
//
// **The value is the caller's, and this never writes to it.** That is not
// fastidiousness; it is BUG-009. The position bar is bound to a value that a
// per-frame poll re-asserts sixty times a second, so a control that wrote its
// own value while being dragged would have that write undone before the next
// frame, and the lever would appear to spring back to where it started. So a
// drag emits `moved` and leaves the binding alone, and `held` tells the caller
// to suspend the binding for as long as the lever is under the finger.
//
// The lever is moulded like the buttons and lit the same way, because it is the
// same piece of plastic. It does not travel — a lever that sank when grabbed
// would fight the horizontal movement it exists to report.

import QtQuick

Item {
    id: slot

    property real from: 0
    property real to: 1
    property real value: 0

    // True while the lever is under the finger. The caller suspends its binding
    // on this, which is the whole of the BUG-009 fix.
    readonly property alias held: drag.pressed

    // Emitted continuously during a drag and once on a click, always with a
    // value clamped into range. The caller decides what to do with it — seeking
    // on release is a different policy from setting a volume immediately, and
    // that policy does not belong in the control.
    signal moved(real to)

    // Emitted when the lever is let go, for a caller that acts on release.
    signal released()

    implicitWidth: Tokens.controlHeight * 4
    implicitHeight: Tokens.controlHeight

    readonly property real span: Math.max(1e-9, to - from)
    readonly property real fraction: Math.max(0, Math.min(1, (value - from) / span))
    readonly property real travelWidth: Math.max(0, width - Tokens.thumbWidth)

    // The slot itself: cut into the chassis, so it is dark at the top where the
    // material overhangs it.
    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: Tokens.slotWidth
        radius: Tokens.radiusSlot
        color: Tokens.displayBg

        // The lit run. Amber up to the value, nothing after it.
        Rectangle {
            width: track.width * slot.fraction
            height: parent.height
            radius: parent.radius
            color: slot.enabled ? Tokens.readout : Tokens.readoutFloor
        }
    }

    Rectangle {
        id: lever
        x: slot.fraction * slot.travelWidth
        anchors.verticalCenter: parent.verticalCenter
        width: Tokens.thumbWidth
        height: Tokens.thumbHeight * 2
        radius: Tokens.radiusSlot
        border.width: Tokens.hairline
        border.color: Tokens.shellEdge
        opacity: slot.enabled ? 1.0 : 0.45

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Tokens.shellRecess, Tokens.bevelLight) }
            GradientStop { position: 1.0; color: Qt.darker(Tokens.shellRecess, Tokens.bevelShadow) }
        }
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        enabled: slot.enabled

        function report(x) {
            // The lever's centre follows the cursor, so the point grabbed stays
            // under it. Measuring from the item's left edge instead would make
            // the lever jump by half its width on the first press.
            const at = Math.max(0, Math.min(1, (x - Tokens.thumbWidth / 2) / slot.travelWidth))
            slot.moved(slot.from + at * slot.span)
        }

        onPressed: function(mouse) { report(mouse.x) }
        onPositionChanged: function(mouse) { if (pressed) report(mouse.x) }
        onReleased: slot.released()
    }
}
