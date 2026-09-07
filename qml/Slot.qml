// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A continuous control: a slot cut into the chassis, with a lit run and a lever.
//
// One component for both orientations, because a fader is a slot stood on end
// and nothing else about it differs. The alternative was two files sharing the
// value arithmetic, the detent and the BUG-009 rule by copy, which is three
// things to keep in step for the sake of an axis.
//
// F-040 requires continuous controls to carry a legible scale, and the lit run
// *is* the scale at its coarsest: light against unlit is readable across a room,
// which is the property a position indicator actually needs. `ticks` prints a
// finer one on the chassis for the controls that are set rather than watched.
//
// **The value is the caller's, and this never writes to it.** That is BUG-009
// rather than fastidiousness. The position is re-asserted by a per-frame poll
// sixty times a second, so a control that wrote its own value while being
// dragged would have the write undone before the next frame, and the lever
// would appear to spring back to where it started. A drag emits `moved` and
// leaves the binding alone; `held` tells the caller to suspend that binding
// while the lever is under the finger.
//
// The lever is moulded and lit like every other control face, because it is the
// same piece of plastic. It does not sink — a lever that travelled into the
// chassis when grabbed would fight the movement along it that it exists to
// report.

import QtQuick

Item {
    id: slot

    property bool vertical: false

    property real from: 0
    property real to: 1
    property real value: 0

    // Where the lit run starts. A level runs from its minimum, but a gain runs
    // from the centre — a band at +6 dB should light upward from zero and one
    // at −6 dB downward from it, because what the control reports is a
    // departure from flat rather than a quantity of something.
    property real origin: from

    // A value the lever settles onto when released near it, and `NaN` for a
    // control that has none. F-040 requires one at centre for balance, and the
    // reason generalises: centre is the position such a control returns to most
    // and the one position it cannot be set to by eye. A control that can be
    // left imperceptibly off-centre with no way to see it is the defect — the
    // user's aim is not the problem.
    property real detent: NaN
    property real detentRange: Math.abs(to - from) * 0.02

    // Marks printed on the chassis beside the slot. Legends by SPEC.md's rule:
    // they describe the scale and never change, so they are `ink` and unlit.
    property int ticks: 0

    // What one press of an arrow key moves the value by, and what one press of
    // Page Up or Page Down moves it by. F-043 wants the equaliser reachable
    // from the keyboard, and a fader is not reachable if the only way to set it
    // is to aim at a lever a few pixels wide. Defaults are a twentieth and a
    // fifth of the range, which suit a level; a gain in decibels overrides
    // `step` to 1 so that arrowing lands on the whole numbers the readout
    // prints, rather than a fifth of a decibel short of them every time.
    property real step: Math.abs(to - from) / 20
    property real pageStep: Math.abs(to - from) / 5

    readonly property alias held: drag.pressed

    // Reachable by Tab. Only the continuous controls take focus this way: the
    // buttons are all on shortcuts, and a Tab order that stops at twenty things
    // is a Tab order nobody uses.
    activeFocusOnTab: enabled

    // Emitted continuously during a drag and once on a click, always clamped
    // into range and settled onto the detent when near it. What to do with it
    // is the caller's: seeking on release is a different policy from setting a
    // volume at once, and that policy does not belong in the control.
    signal moved(real to)
    signal released()

    implicitWidth: vertical ? Tokens.thumbWidth : Tokens.controlHeight * 4
    implicitHeight: vertical ? Tokens.faderTravel : Tokens.controlHeight

    readonly property real span: Math.max(1e-9, to - from)
    readonly property real length: vertical ? height : width
    readonly property real travelLength: Math.max(1, length - Tokens.thumbHeight * 2)

    function fractionOf(v) { return Math.max(0, Math.min(1, (v - from) / span)) }
    readonly property real fraction: fractionOf(value)
    readonly property real originFraction: fractionOf(origin)

    // Arrow keys move by `step`, Page Up and Page Down by `pageStep`, Home and
    // End to the ends. Both axes are accepted whichever way the slot runs: a
    // vertical fader answers to Up and Down, and to Left and Right as well,
    // because which of them a person reaches for is not worth being strict
    // about. Emitted through `moved` like every other route, so the caller's
    // policy — seek on release, set at once — is applied to a keypress exactly
    // as it is to a drag.
    Keys.onPressed: function (event) {
        let delta = 0
        switch (event.key) {
        case Qt.Key_Right: case Qt.Key_Up:     delta = slot.step; break
        case Qt.Key_Left:  case Qt.Key_Down:   delta = -slot.step; break
        case Qt.Key_PageUp:                    delta = slot.pageStep; break
        case Qt.Key_PageDown:                  delta = -slot.pageStep; break
        case Qt.Key_Home:
            slot.moved(slot.from); slot.released(); event.accepted = true; return
        case Qt.Key_End:
            slot.moved(slot.to); slot.released(); event.accepted = true; return
        default:
            return
        }

        const next = Math.max(Math.min(slot.from, slot.to),
                              Math.min(Math.max(slot.from, slot.to), slot.value + delta))
        slot.moved(next)

        // `released` too, because a control whose caller only acts on release —
        // the position bar does, per BUG-009 — would otherwise take the key and
        // do nothing with it.
        slot.released()
        event.accepted = true
    }

    // The slot itself, cut into the chassis and therefore dark.
    Rectangle {
        id: track
        anchors.centerIn: parent
        width: slot.vertical ? Tokens.slotWidth : parent.width
        height: slot.vertical ? parent.height : Tokens.slotWidth
        radius: Tokens.radiusSlot
        color: Tokens.displayBg

        // The lit run, between the origin and the value. Drawn from whichever
        // of the two is lower, so a gain below flat lights downward without the
        // caller having to say so.
        Rectangle {
            readonly property real a: Math.min(slot.fraction, slot.originFraction)
            readonly property real b: Math.max(slot.fraction, slot.originFraction)

            x: slot.vertical ? 0 : a * track.width
            y: slot.vertical ? (1 - b) * track.height : 0
            width: slot.vertical ? track.width : (b - a) * track.width
            height: slot.vertical ? (b - a) * track.height : track.height
            radius: parent.radius
            color: slot.enabled ? Tokens.readout : Tokens.readoutFloor
        }
    }

    // The printed scale. Evenly spaced rather than at named values: this is the
    // ruler beside the slot, not a set of labels, and a label would have to be
    // a legend in its own right.
    Repeater {
        model: slot.ticks
        delegate: Rectangle {
            required property int index
            readonly property real at: slot.ticks > 1 ? index / (slot.ticks - 1) : 0.5
            color: Tokens.ink
            opacity: 0.5

            width: slot.vertical ? Tokens.thumbHeight : Tokens.hairline * 2
            height: slot.vertical ? Tokens.hairline * 2 : Tokens.thumbHeight

            x: slot.vertical
               ? slot.width / 2 + Tokens.slotWidth
               : Tokens.thumbWidth / 2 + at * (slot.width - Tokens.thumbWidth)
            y: slot.vertical
               ? Tokens.thumbHeight + (1 - at) * (slot.height - Tokens.thumbHeight * 2)
               : slot.height / 2 + Tokens.slotWidth
        }
    }

    Rectangle {
        id: lever
        x: slot.vertical ? (slot.width - width) / 2
                         : slot.fraction * (slot.width - Tokens.thumbWidth)
        y: slot.vertical ? (1 - slot.fraction) * slot.travelLength + Tokens.thumbHeight / 2
                         : (slot.height - height) / 2
        width: slot.vertical ? Tokens.thumbWidth : Tokens.thumbWidth
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

    // Which control has the keyboard, drawn rather than borrowed. Everything
    // else in the panel is, and a focus ring supplied by a desktop style would
    // be the one part of the chassis that came from somewhere else.
    Rectangle {
        anchors.fill: track
        anchors.margins: -Tokens.hairline * 2
        radius: track.radius + Tokens.hairline * 2
        color: "transparent"
        border.width: Tokens.hairline * 2
        border.color: Tokens.readout
        opacity: slot.activeFocus ? 0.7 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Tokens.travelMs } }
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        enabled: slot.enabled

        function report(x, y) {
            // The lever's centre follows the cursor, so whatever point was
            // grabbed stays under it. Measured from the item's edge instead,
            // the lever would jump by half its length on the first press.
            const along = slot.vertical
                        ? 1 - (y - Tokens.thumbHeight) / slot.travelLength
                        : (x - Tokens.thumbWidth / 2) / (slot.width - Tokens.thumbWidth)
            let next = slot.from + Math.max(0, Math.min(1, along)) * slot.span

            if (!isNaN(slot.detent) && Math.abs(next - slot.detent) < slot.detentRange)
                next = slot.detent

            slot.moved(next)
        }

        // Taking the keyboard as well as the value, so that working a control
        // with the mouse and then nudging it with an arrow key is one gesture
        // rather than two unrelated ones.
        onPressed: function(mouse) {
            slot.forceActiveFocus(Qt.MouseFocusReason)
            report(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) { if (pressed) report(mouse.x, mouse.y) }
        onReleased: slot.released()
    }
}
