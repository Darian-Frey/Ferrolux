// Throwaway Phase 1 harness — five transport buttons and a position bar, per
// ROADMAP.md Phase 1. This is deliberately plain Qt Quick Controls with no
// styling at all: the cassette futurism panel is Phase 5 (F-040), and dressing
// this window up would only produce something that has to be thrown away twice.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 560
    height: 320
    visible: true
    title: qsTr("Ferrolux RS-1 — Phase 1 harness")

    // Invariant 4: the single position poll for the whole application. Nothing
    // else may query the pipeline for position or duration.
    FrameAnimation {
        running: true
        onTriggered: Engine.poll()
    }

    function formatTime(nanoseconds) {
        if (nanoseconds < 0)
            return "--:--"
        const total = Math.floor(nanoseconds / 1000000000)
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    readonly property var stateNames: ["Stopped", "Loading", "Playing", "Paused", "Error"]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            text: Engine.source == "" ? qsTr("Drop an audio file here, or pass one on the command line")
                                      : Engine.source.toString().split("/").pop()
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: window.stateNames[Engine.state] }
            Item { Layout.fillWidth: true }
            Label {
                color: "#a00"
                visible: Engine.errorText !== ""
                text: Engine.errorText
                elide: Text.ElideRight
                Layout.maximumWidth: 320
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label { text: window.formatTime(Engine.position) }

            Slider {
                id: positionBar
                Layout.fillWidth: true
                enabled: Engine.seekable
                from: 0
                to: Engine.duration > 0 ? Engine.duration : 1

                // F-003: seek on release only. Seeking on every value change
                // while dragging is what produces audible scrub artefacts.
                property bool scrubbing: false
                value: scrubbing ? value : Engine.position
                onPressedChanged: {
                    if (pressed) {
                        scrubbing = true
                    } else {
                        scrubbing = false
                        Engine.seek(value)
                    }
                }
            }

            Label { text: window.formatTime(Engine.duration) }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button { text: qsTr("Prev");  Layout.fillWidth: true; onClicked: Engine.previous() }
            Button { text: qsTr("Play");  Layout.fillWidth: true; onClicked: Engine.play() }
            Button { text: qsTr("Pause"); Layout.fillWidth: true; onClicked: Engine.pause() }
            Button { text: qsTr("Stop");  Layout.fillWidth: true; onClicked: Engine.stop() }
            Button { text: qsTr("Next");  Layout.fillWidth: true; onClicked: Engine.stop() }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8

            Label { text: qsTr("Volume") }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 1
                value: Engine.volume
                onMoved: Engine.volume = value
            }
            Label { text: Math.round(Engine.volume * 100) + "%" }

            Label { text: qsTr("Balance") }
            Slider {
                Layout.fillWidth: true
                from: -1; to: 1
                value: Engine.balance
                onMoved: Engine.balance = value
            }
            Label {
                text: Math.abs(Engine.balance) < 0.005 ? qsTr("centre")
                    : (Engine.balance < 0 ? qsTr("L") : qsTr("R")) + " " + Math.round(Math.abs(Engine.balance) * 100)
            }
        }

        Item { Layout.fillHeight: true }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls) {
                Engine.setSource(drop.urls[0])
                Engine.play()
            }
        }
    }
}
