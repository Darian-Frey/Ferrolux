// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The settings drawer: what the panel looks like, rather than what it plays.
//
// Built to be added to. Everything about a setting is stated once — its legend,
// its control, its effect — so a new one is a block here and nothing else, and
// the panel does not acquire a preferences dialogue that has to be maintained
// alongside it.
//
// It is a plate like the equaliser drawer, not a popup. A popup is for a choice
// made and dismissed; these are settings the panel is left in, and a drawer that
// opens in place says that. It also means the settings are drawn in the theme
// they are setting — pick a chassis and the drawer changes with everything else,
// which is the clearest possible preview and costs nothing to provide.

import QtQuick
import QtQuick.Layouts

PanelSection {
    id: settings

    recessed: false
    title: qsTr("settings")

    implicitHeight: layout.implicitHeight + Tokens.padSection * 2

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Tokens.padSection
        spacing: Tokens.gapControl

        RowLayout {
            spacing: Tokens.gapControl

            Legend { text: qsTr("finish") }

            // One button per set, latched to show which is in effect. A list of
            // four is shorter than the menu that would hold it, and a control
            // that shows every option at once is a better answer than one that
            // hides three of them behind the fourth.
            Repeater {
                model: Theme.available()

                delegate: PanelButton {
                    required property string modelData
                    text: modelData
                    activated: Theme.name === modelData
                    Layout.preferredHeight: Tokens.controlHeight
                    onClicked: Theme.loadNamed(modelData)
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 0
                Legend {
                    text: qsTr("display")
                    font.pixelSize: Tokens.sizeLegendSmall
                }
                SlideSwitch {
                    Layout.preferredHeight: Tokens.controlHeight
                    positions: [qsTr("lit"), qsTr("inverse")]
                    current: window.displayInverted ? 1 : 0
                    onThrown: function(position) { window.displayInverted = position === 1 }
                }
            }
        }
    }
}
