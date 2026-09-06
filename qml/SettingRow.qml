// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// One adjustable value: a printed name, a slot, and a lit reading.
//
// The settings window is a dozen of these, so the arrangement is stated once.
// It is also where the panel's rules stop being a matter of remembering them:
// the name is a legend and the value is a readout, and a caller cannot get that
// the wrong way round without going out of its way.
//
// The reading is right-aligned into a ghost of the field's full width, so the
// digits do not shuffle sideways as a slider moves — which is the whole reason
// a segmented field has fixed cells.

import QtQuick
import QtQuick.Layouts

RowLayout {
    id: row

    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 1

    // Decimal places in the reading. Zero for a count — ranks, segments — where
    // a fractional value would be a reading of something that cannot exist.
    property int places: 2

    // Snapped before it is reported, so a control for a count cannot hand out
    // 8.6 ranks and leave the shader to round it somewhere else.
    property bool integral: places === 0

    signal moved(real to)

    spacing: Tokens.gapControl

    Legend {
        text: row.label
        Layout.preferredWidth: Tokens.controlHeight * 3.2
    }

    Slot {
        Layout.fillWidth: true
        Layout.preferredHeight: Tokens.controlHeight
        from: row.from
        to: row.to
        ticks: 5
        value: row.value
        onMoved: function (to) { row.moved(row.integral ? Math.round(to) : to) }
    }

    Readout {
        Layout.preferredWidth: Tokens.sizeLegend * 5
        Layout.preferredHeight: Tokens.controlHeight
        face: Tokens.readoutNumeric
        size: Tokens.sizeLegend
        alignment: Text.AlignRight
        ground: Tokens.displayBg
        inset: Tokens.hairline * 4

        // The all-lit form of this field, so the value sits in fixed cells. A
        // range that reaches below zero needs a cell for the sign as well, or
        // the reading is one character wider than the ghost it is meant to sit
        // inside and the field it belongs to stops being a field.
        readonly property string sign: row.from < 0 ? "-" : ""
        ghost: sign + (row.places === 0 ? "88" : "8." + "8".repeat(row.places))
        text: row.value.toFixed(row.places)
    }
}
