// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Settings, in a window of their own.
//
// They began as a drawer in the panel, which was right while there were two of
// them. A dozen controls is a different thing: the drawer pushed the playlist
// down every time it opened, and adjusting a display meant covering the display
// being adjusted. In its own window both problems go away — the panel keeps its
// layout, and the meters stay visible and running while their proportions are
// dragged, which is the only way to judge what a change does.
//
// It is drawn in the finish it is setting, and that is deliberate rather than
// incidental: choose a chassis and this window changes with it, which is a
// preview that costs nothing to provide and cannot disagree with the result.
//
// Closing it hides it rather than destroying it, so reopening returns to the
// same section and the same scroll position. The application does not quit with
// it — `quitOnLastWindowClosed` would otherwise make closing the settings the
// same gesture as closing the player, which nobody means.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: settings

    title: qsTr("Ferrolux — settings")
    color: Tokens.shell
    width: 520
    height: 780
    minimumWidth: 460
    minimumHeight: 420

    // Owned by the player, which holds the state and persists it. This window
    // asks for a change and does not make one, so there is one place the
    // setting lives and one place it is written from.
    property bool inverted: false
    signal invertRequested(bool wanted)

    // The panel's scale follows the *player's* width, not this window's. These
    // controls are the same size as the ones they are setting, which is what
    // makes the two read as one instrument in two frames.

    ScrollView {
        anchors.fill: parent
        anchors.margins: Tokens.gapPanel

        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Tokens.gapSection

            // ---- finish ------------------------------------------------
            PanelSection {
                recessed: false
                title: qsTr("finish")
                Layout.fillWidth: true

                // A section prints its name *above* itself, and the first one
                // in a column has nothing above it — its legend lands outside
                // the layout and is clipped away. The margin is the room the
                // gap between sections gives every other one.
                Layout.topMargin: Tokens.sizeLegendSmall + Tokens.hairline * 4
                Layout.preferredHeight: finishColumn.implicitHeight + Tokens.padSection * 2

                ColumnLayout {
                    id: finishColumn
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl

                    RowLayout {
                        spacing: Tokens.gapControl
                        Repeater {
                            model: Theme.available()
                            delegate: PanelButton {
                                required property string modelData
                                text: modelData
                                activated: Theme.name === modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: Tokens.controlHeight
                                onClicked: Theme.loadNamed(modelData)
                            }
                        }
                    }

                    RowLayout {
                        spacing: Tokens.gapControl
                        Legend {
                            text: qsTr("display")
                            Layout.preferredWidth: Tokens.controlHeight * 3.2
                        }
                        SlideSwitch {
                            Layout.preferredHeight: Tokens.controlHeight
                            positions: [qsTr("lit"), qsTr("inverse")]
                            current: settings.inverted ? 1 : 0
                            onThrown: function (position) { settings.invertRequested(position === 1) }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ---- levels ------------------------------------------------
            PanelSection {
                recessed: false
                title: qsTr("levels")
                Layout.fillWidth: true
                Layout.preferredHeight: levelColumn.implicitHeight + Tokens.padSection * 2

                ColumnLayout {
                    id: levelColumn
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl

                    // What counts as 0 VU. SPEC.md §Meters puts it at −9 dBFS
                    // rather than the −18 broadcast figure, because a modern
                    // master sits far above the latter and pins every meter to
                    // full — which is what BUG-014 was reported as.
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("0 VU at")
                        from: -30; to: 0; places: 1
                        value: Meters.referenceLevel
                        onMoved: function (to) { Meters.referenceLevel = to }
                    }

                    RowLayout {
                        spacing: Tokens.gapControl
                        Legend {
                            text: qsTr("bands")
                            Layout.preferredWidth: Tokens.controlHeight * 3.2
                        }
                        SlideSwitch {
                            Layout.preferredHeight: Tokens.controlHeight
                            positions: ["24", "48"]
                            current: Meters.bandCount === 48 ? 1 : 0
                            onThrown: function (position) {
                                Meters.bandCount = position === 1 ? 48 : 24
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // How fast a cap falls once it has finished holding.
                    // F-031 asks for this to be configurable and it was a
                    // constant until 2026-09-07 — the clause had gone unmet
                    // without anybody noticing, because the caps plainly
                    // worked. It belongs to the meter rather than to the
                    // shader, so it is `Meters` and not `Visuals`.
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("cap fall"); from: 2; to: 60; places: 1
                        value: Meters.peakFall
                        onMoved: function (to) { Meters.peakFall = to }
                    }
                }
            }

            // ---- bars --------------------------------------------------
            PanelSection {
                recessed: false
                title: qsTr("bars")
                Layout.fillWidth: true
                Layout.preferredHeight: barColumn.implicitHeight + Tokens.padSection * 2

                ColumnLayout {
                    id: barColumn
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl

                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("gap"); from: 0; to: 0.6
                        value: Visuals.spectrumGap
                        onMoved: function (to) { Visuals.spectrumGap = to }
                    }
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("cap"); from: 0; to: 0.15; places: 3
                        value: Visuals.spectrumCap
                        onMoved: function (to) { Visuals.spectrumCap = to }
                    }
                }
            }

            // ---- flame -------------------------------------------------
            PanelSection {
                recessed: false
                title: qsTr("flame")
                Layout.fillWidth: true
                Layout.preferredHeight: flameColumn.implicitHeight + Tokens.padSection * 2

                ColumnLayout {
                    id: flameColumn
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl

                    // Ranks is the one with a cost attached: each is up to five
                    // texture taps per pixel, and BUG-016 is what happened when
                    // nine of them went unmeasured at 2160p. Sixteen is the
                    // shader's own ceiling, above which it stops drawing them.
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("ranks"); from: 1; to: 16; places: 0
                        value: Visuals.flameRanks
                        onMoved: function (to) { Visuals.flameRanks = to }
                    }
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("front"); from: 0.05; to: 2
                        value: Visuals.flameFront
                        onMoved: function (to) { Visuals.flameFront = to }
                    }
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("back"); from: 0.05; to: 2
                        value: Visuals.flameBack
                        onMoved: function (to) { Visuals.flameBack = to }
                    }
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("parallax"); from: 0; to: 8
                        value: Visuals.flameParallax
                        onMoved: function (to) { Visuals.flameParallax = to }
                    }
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("softness"); from: 0; to: 3
                        value: Visuals.flameSoftness
                        onMoved: function (to) { Visuals.flameSoftness = to }
                    }
                }
            }

            // ---- ladder ------------------------------------------------
            PanelSection {
                recessed: false
                title: qsTr("ladder")
                Layout.fillWidth: true
                Layout.preferredHeight: ladderColumn.implicitHeight + Tokens.padSection * 2

                ColumnLayout {
                    id: ladderColumn
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl

                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("segments"); from: 4; to: 64; places: 0
                        value: Visuals.ladderSegments
                        onMoved: function (to) { Visuals.ladderSegments = to }
                    }
                    SettingRow {
                        Layout.fillWidth: true
                        label: qsTr("over from"); from: 0.2; to: 0.98
                        value: Visuals.ladderOver
                        onMoved: function (to) { Visuals.ladderOver = to }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.gapControl
                Item { Layout.fillWidth: true }

                // A dozen sliders needs a way out of whatever it has been
                // dragged into. Only the display proportions revert: the finish
                // and the reference level are choices rather than shapes, and
                // undoing them here would be a surprise.
                PanelButton {
                    Layout.preferredHeight: Tokens.controlHeight
                    text: qsTr("reset displays")
                    onClicked: Visuals.reset()
                }
                PanelButton {
                    Layout.preferredHeight: Tokens.controlHeight
                    text: qsTr("close")
                    onClicked: settings.visible = false
                }
            }
        }
    }

}
