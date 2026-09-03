// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A physical slide switch, with as many detents as it has positions.
//
// F-040 is specific about this and about why. Shuffle and repeat are *not*
// momentary controls, so they must not be momentary buttons: their state is
// something the machine is set to, not something being done to it, and a
// control that has to be read by its caption is a control whose position tells
// you nothing. The criterion says so outright — state readable from the switch
// position alone, without a text label — and names the cycling buttons the
// Phase 2 harness used as exactly what it excludes.
//
// So the lever sits at the position it is set to, and the positions are marked
// on the chassis beneath it. Those marks are legends by SPEC.md's rule: they
// name a setting and never change, so they are printed in `ink`, and the one
// under the lever is no more lit than the others. What reports the state is the
// lever, which is the point.
//
// Two positions or three cost the same here. Repeat has three — off, all, one —
// and a three-detent slide switch is an ordinary object; making it a boolean
// with a special case would be the awkward version.
//
// Clicking a position selects it, rather than advancing to the next. A slide
// switch is thrown to where it is wanted, and a three-position switch that
// could only be advanced would need two throws to go back one.

import QtQuick

Item {
    id: switchControl

    // Printed under each detent, in order. Their number is the number of
    // positions; there is no separate count to disagree with them.
    property var positions: ["off", "on"]
    property int current: 0

    signal thrown(int position)

    readonly property int count: Math.max(1, positions.length)
    readonly property real detentWidth: track.width / count

    implicitWidth: Tokens.thumbWidth * count + Tokens.gapControl * 2
    implicitHeight: Tokens.controlHeight

    // The way cut into the chassis for the lever to run in. A well, so the
    // shadow is at the top — see PanelSection for why that direction matters.
    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height - legends.height - Tokens.hairline * 2
        radius: Tokens.radiusControl
        color: Tokens.displayBg
        border.width: Tokens.hairline
        border.color: Tokens.shellEdge

        // The lever. Moulded and lit from above like every other control face,
        // because it is the same material; it slides rather than sinks, so it
        // has no travel.
        Rectangle {
            id: lever
            x: switchControl.current * switchControl.detentWidth + Tokens.hairline
            y: Tokens.hairline
            width: switchControl.detentWidth - Tokens.hairline * 2
            height: parent.height - Tokens.hairline * 2
            radius: Tokens.radiusControl
            border.width: Tokens.hairline
            border.color: Tokens.shellEdge

            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(Tokens.shellRecess, Tokens.bevelLight) }
                GradientStop { position: 1.0; color: Qt.darker(Tokens.shellRecess, Tokens.bevelShadow) }
            }

            // Thrown, not faded. A switch arrives at its detent; the movement is
            // quick and it stops dead rather than easing, which is what makes it
            // read as a mechanism instead of an animation.
            Behavior on x { NumberAnimation { duration: Tokens.travelMs * 2 } }
        }
    }

    // The marks on the chassis. Printed, never lit — the position under the
    // lever is not highlighted, because a lit mark would be a second and
    // redundant report of a state the lever already gives, and the two could
    // disagree.
    Row {
        id: legends
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Repeater {
            model: switchControl.positions
            delegate: Legend {
                required property string modelData
                width: switchControl.detentWidth
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Tokens.sizeLegendSmall
                text: modelData
            }
        }
    }

    MouseArea {
        anchors.fill: track
        onClicked: function(mouse) {
            const at = Math.floor(mouse.x / switchControl.detentWidth)
            switchControl.thrown(Math.max(0, Math.min(switchControl.count - 1, at)))
        }
    }
}
