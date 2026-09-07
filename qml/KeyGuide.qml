// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The keyboard, printed. F-043's second clause.
//
// Every line comes from `Shortcuts.qml`'s own table — the one the bindings are
// made from — so this cannot describe a key that does nothing or miss one that
// does something. That is the whole reason the table exists in that shape: a
// reference maintained separately from the thing it describes is one edit away
// from lying, and it is believed for months afterwards.
//
// Printed rather than lit, mostly. A key name is a legend by SPEC.md's rule —
// it names a control and never changes — so it is silkscreened in `ink`. The
// sequences themselves are the exception and are lit, because they are what the
// reader is looking for and a page of uniform grey is a page nobody scans.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: guide

    // Supplied by the panel, so there is one table and not a copy of it.
    property var groups: []

    title: qsTr("Ferrolux — keys")
    color: Tokens.shell
    width: 460
    height: 620
    minimumWidth: 380
    minimumHeight: 320

    // No `onClosing` handler, deliberately — closing a QML Window already hides
    // it and keeps its state, and refusing the close is what stopped the whole
    // application quitting once. See BUG-021.

    ScrollView {
        anchors.fill: parent
        anchors.margins: Tokens.gapPanel
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Tokens.gapSection

            Repeater {
                model: guide.groups

                delegate: PanelSection {
                    id: section
                    required property var modelData
                    required property int index

                    recessed: false
                    title: modelData.title
                    Layout.fillWidth: true

                    // A section prints its name above itself, and the first in a
                    // column has nothing above it to print into.
                    Layout.topMargin: index === 0
                                      ? Tokens.sizeLegendSmall + Tokens.hairline * 4 : 0
                    Layout.preferredHeight: rows.implicitHeight + Tokens.padSection * 2

                    ColumnLayout {
                        id: rows
                        anchors.fill: parent
                        anchors.margins: Tokens.padSection
                        spacing: Tokens.gapControl

                        Repeater {
                            model: section.modelData.keys

                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Tokens.gapControl

                                // The sequence is lit and in the readout face:
                                // it is the thing being looked up, and a page of
                                // uniform printing is a page nobody scans.
                                Readout {
                                    Layout.preferredWidth: Tokens.controlHeight * 4.4
                                    Layout.preferredHeight: Tokens.controlHeight
                                    face: Tokens.readoutText
                                    size: Tokens.sizeReadout
                                    ground: Tokens.displayBg
                                    inset: Tokens.hairline * 4
                                    alignment: Text.AlignHCenter
                                    text: modelData.seq
                                }

                                Legend {
                                    text: modelData.legend
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.gapControl
                Item { Layout.fillWidth: true }
                PanelButton {
                    Layout.preferredHeight: Tokens.controlHeight
                    text: qsTr("close")
                    onClicked: guide.visible = false
                }
            }
        }
    }
}
