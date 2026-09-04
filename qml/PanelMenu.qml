// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// A list of choices, shown as a lit well over the chassis.
//
// The one place this phase uses a Qt Quick Controls type without replacing it
// outright, and the reason is worth stating rather than glossing. What a popup
// has to get right is not appearance but behaviour: closing when something else
// is clicked, closing on Escape, staying inside the window, and taking focus
// away from what is beneath it. Reimplementing that on a MouseArea produces a
// menu that mostly works, and the ways it fails are exactly the ways nobody
// tests. `Popup` is a container primitive rather than a styled widget, and both
// of the parts a style would supply — the background and the contents — are
// replaced below, so nothing of the desktop's appearance reaches the panel.
//
// The rows are lit, at the two brightnesses the playlist uses: the current
// choice bright, the rest dim. That is consistent rather than decorative — a
// menu of presets reports which one is in effect, and a lit list is how this
// panel reports anything.

import QtQuick
import QtQuick.Controls

Popup {
    id: menu

    property var options: []

    // The option currently in effect, or -1 where the menu is a list of
    // commands rather than of states. A command list has nothing to report, so
    // nothing in it is lit brighter than the rest.
    property int current: -1

    signal chosen(int index)

    // Which side of its control the list unrolls from. Below where there is
    // room, above where there is not — the equaliser and play-order controls
    // sit at the bottom of the window, and a menu that always opened downward
    // would put half its options past the edge of it. Popup does not do this on
    // its own; only Menu does, and Menu brings the style with it.
    //
    // Decided when the menu opens rather than in a binding, because
    // `mapToItem` is a function call and not a tracked expression: as a
    // binding it evaluates once, while the control is still at the origin and
    // before any layout has run, and then never again. It reported room below
    // in every case, which is the same answer as not having the check at all
    // and is why this looked as though it worked.
    onAboutToShow: {
        if (!parent)
            return
        const top = parent.mapToItem(null, 0, 0).y
        const roomBelow = parent.Window.height - (top + parent.height)
        y = implicitHeight > roomBelow ? -implicitHeight : parent.height
    }

    padding: Tokens.hairline * 2
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: PanelSection {
        recessed: true
    }

    // The width is measured from the options rather than derived from the
    // layout, and that is the second attempt. Sizing a row from the column and
    // the column from its rows is circular: the first version resolved to
    // something narrow and elided every entry to `loud…`, and the second
    // resolved to nothing at all and drew no menu. Measuring the strings has no
    // cycle in it, and the answer does not depend on when it is asked.
    TextMetrics {
        id: metrics
        font.family: Tokens.readoutText
        font.pixelSize: Tokens.sizeReadout
    }

    contentWidth: {
        let widest = 0
        for (let i = 0; i < options.length; ++i) {
            metrics.text = options[i]
            widest = Math.max(widest, metrics.width)
        }
        // A unit of slack past the measurement. TextMetrics and the
        // distance-field renderer that actually draws the row do not agree to
        // the last fraction of a pixel, and a field sized to exactly its
        // content elides on the rounding — which is how the longest option
        // ends up as the only one with an ellipsis.
        return Math.ceil(widest) + Tokens.gapControl * 3
    }

    contentItem: Column {
        spacing: 0

        Repeater {
            model: menu.options

            delegate: Rectangle {
                required property int index
                required property string modelData

                width: menu.contentWidth
                height: Tokens.sizeReadout * 1.8
                // The option in effect is *backlit*, not merely brighter.
                // Brightness alone gave it 1.7 times the contrast of the rest,
                // which is not enough to pick out at a glance and left every
                // other option below the contrast normal text needs. A lit
                // ground says which one is current and lets them all stay
                // readable. See BUG-020.
                color: hover.hovered || index === menu.current
                       ? Tokens.readoutFloor : "transparent"
                radius: Tokens.radiusSlot

                Readout {
                    id: entry
                    anchors.fill: parent
                    anchors.leftMargin: Tokens.gapControl
                    anchors.rightMargin: Tokens.gapControl
                    face: Tokens.readoutText
                    size: Tokens.sizeReadout
                    colour: Tokens.readout
                    text: modelData
                }

                HoverHandler { id: hover }

                TapHandler {
                    onTapped: {
                        menu.chosen(index)
                        menu.close()
                    }
                }
            }
        }
    }
}
