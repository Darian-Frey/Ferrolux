// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Throwaway Phase 1/2 harness. Deliberately plain Qt Quick Controls with no
// styling at all: the cassette futurism panel is Phase 5 (F-040), and dressing
// this window up would only produce something that has to be thrown away twice.
//
// The ListView is bound to PlaylistView, the filter proxy, while every command
// is addressed to Playlist, the model. Selection and playback are therefore
// always expressed in source rows — the model owns play order and knows nothing
// about what happens to be visible.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 720
    height: 780
    visible: true
    title: qsTr("Ferrolux RS-1 — Phase 3 harness")

    // Invariant 4: the single position poll for the whole application.
    FrameAnimation {
        running: true
        onTriggered: Engine.poll()
    }

    readonly property var stateNames: ["Stopped", "Loading", "Playing", "Paused", "Error"]
    readonly property var repeatNames: ["off", "all", "one"]

    // Selected *source* rows, not proxy rows.
    property var selection: []
    property int anchorRow: -1

    function formatTime(nanoseconds) {
        if (nanoseconds < 0)
            return "--:--"
        const total = Math.floor(nanoseconds / 1000000000)
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function selectRow(sourceRow, modifiers) {
        if (modifiers & Qt.ControlModifier) {
            const at = selection.indexOf(sourceRow)
            const next = selection.slice()
            if (at >= 0)
                next.splice(at, 1)
            else
                next.push(sourceRow)
            selection = next
        } else if ((modifiers & Qt.ShiftModifier) && anchorRow >= 0) {
            const lo = Math.min(anchorRow, sourceRow)
            const hi = Math.max(anchorRow, sourceRow)
            const range = []
            for (let r = lo; r <= hi; ++r)
                range.push(r)
            selection = range
            return
        } else {
            selection = [sourceRow]
        }
        anchorRow = sourceRow
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ---- now playing -----------------------------------------------
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            text: Engine.source == "" ? qsTr("Drop files or a folder here")
                                      : Engine.source.toString().split("/").pop()
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: window.stateNames[Engine.state] }
            Label {
                text: qsTr("· %1 of %2").arg(Playlist.currentRow + 1).arg(Playlist.count)
                visible: Playlist.count > 0
            }
            Item { Layout.fillWidth: true }
            Label {
                color: "#a00"
                visible: Engine.errorText !== ""
                text: Engine.errorText
                elide: Text.ElideRight
                Layout.maximumWidth: 300
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
            Button { text: qsTr("Next");  Layout.fillWidth: true; onClicked: Playlist.advance() }
        }

        // ---- playlist ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Filter…")
                onTextChanged: PlaylistView.filterText = text
            }
            Label {
                text: qsTr("%1/%2").arg(PlaylistView.count).arg(Playlist.count)
                visible: PlaylistView.filterText !== ""
            }
            Button {
                text: qsTr("Remove")
                enabled: window.selection.length > 0
                onClicked: { Playlist.removeRows(window.selection); window.selection = [] }
            }
            Button {
                text: qsTr("Clear")
                enabled: Playlist.count > 0
                onClicked: { Playlist.clear(); window.selection = [] }
            }
            Button {
                text: qsTr("Undo")
                enabled: Playlist.canUndo
                onClicked: Playlist.undo()
            }
            Button {
                text: qsTr("Sort ▾")
                enabled: Playlist.count > 1
                onClicked: sortMenu.open()

                Menu {
                    id: sortMenu
                    y: parent.height
                    // Values match PlaylistModel::SortKey.
                    MenuItem { text: qsTr("Title");    onTriggered: Playlist.sortBy(0, Qt.AscendingOrder) }
                    MenuItem { text: qsTr("Artist");   onTriggered: Playlist.sortBy(1, Qt.AscendingOrder) }
                    MenuItem { text: qsTr("Album");    onTriggered: Playlist.sortBy(2, Qt.AscendingOrder) }
                    MenuItem { text: qsTr("Duration"); onTriggered: Playlist.sortBy(3, Qt.AscendingOrder) }
                    MenuItem { text: qsTr("Path");     onTriggered: Playlist.sortBy(4, Qt.AscendingOrder) }
                    MenuItem { text: qsTr("File date");onTriggered: Playlist.sortBy(5, Qt.AscendingOrder) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Reverse by title"); onTriggered: Playlist.sortBy(0, Qt.DescendingOrder) }
                }
            }
            Button { text: qsTr("Open…"); onClicked: pathDialog.prompt(false) }
            Button { text: qsTr("Save…"); enabled: Playlist.count > 0; onClicked: pathDialog.prompt(true) }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 1

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: PlaylistView
                // Virtualised by default, which is what keeps a 20,000-entry
                // playlist scrolling at frame rate. See AV-008.
                cacheBuffer: 200
                ScrollBar.vertical: ScrollBar {}

                // Row index the dragged selection would land before, or -1.
                property int dropIndicator: -1

                Rectangle {
                    parent: list.contentItem
                    visible: list.dropIndicator >= 0
                    y: list.dropIndicator * 24
                    width: list.width
                    height: 2
                    color: palette.highlight
                    z: 2
                }

                // A plain Rectangle rather than an ItemDelegate: selection needs
                // the keyboard modifiers from the click, and ItemDelegate's
                // onClicked does not carry them.
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string title
                    required property string artist
                    required property var duration
                    required property bool isCurrent
                    required property int metadataState

                    width: ListView.view.width
                    height: 24

                    readonly property int sourceRow: PlaylistView.toSourceRow(index)
                    readonly property bool selected: window.selection.indexOf(sourceRow) >= 0
                    color: selected ? palette.highlight : "transparent"

                    // Drag reorder (F-011). Disabled while a filter is active:
                    // dropping between two visible rows is ambiguous when rows
                    // are hidden between them, and guessing would silently move
                    // the entry somewhere the user cannot see.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        property int pressedY: 0
                        property bool dragging: false

                        onClicked: function(mouse) { window.selectRow(row.sourceRow, mouse.modifiers) }
                        onDoubleClicked: Playlist.setCurrentRow(row.sourceRow)

                        onPressed: function(mouse) {
                            pressedY = mouse.y
                            dragging = false
                            // Dragging a row that is not part of the current
                            // selection starts a fresh single-row drag, rather
                            // than silently carrying an unrelated selection.
                            if (window.selection.indexOf(row.sourceRow) < 0)
                                window.selectRow(row.sourceRow, 0)
                        }

                        onPositionChanged: function(mouse) {
                            if (PlaylistView.filterText !== "" || !pressed)
                                return
                            if (!dragging && Math.abs(mouse.y - pressedY) < row.height / 2)
                                return
                            dragging = true

                            const inList = mapToItem(list.contentItem, mouse.x, mouse.y)
                            list.dropIndicator = Math.max(0, Math.min(Playlist.count,
                                                          Math.round(inList.y / row.height)))
                        }

                        onReleased: {
                            // Committed on release, not continuously: a
                            // multi-row move is an arbitrary permutation and so
                            // resets the model, which would fight a live drag.
                            if (dragging && list.dropIndicator >= 0) {
                                const landed = Playlist.moveSelection(window.selection,
                                                                      list.dropIndicator)
                                if (landed >= 0) {
                                    const block = []
                                    for (let i = 0; i < window.selection.length; ++i)
                                        block.push(landed + i)
                                    window.selection = block
                                }
                            }
                            dragging = false
                            list.dropIndicator = -1
                        }

                        onCanceled: { dragging = false; list.dropIndicator = -1 }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8

                        Label {
                            text: isCurrent ? "▶" : ""
                            Layout.preferredWidth: 12
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.bold: isCurrent
                            // 3 == MetadataState::Missing, 2 == Failed
                            color: metadataState === 3 ? "#a00"
                                 : metadataState === 2 ? "#a60"
                                 : (row.selected ? palette.highlightedText : palette.text)
                            text: artist !== "" ? artist + " — " + title : title
                        }
                        Label {
                            text: window.formatTime(duration)
                            opacity: 0.7
                        }
                    }
                }
            }
        }

        // ---- equaliser ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: (eqPanel.visible ? qsTr("Equaliser \u25B4") : qsTr("Equaliser \u25BE"))
                onClicked: eqPanel.visible = !eqPanel.visible
            }
            CheckBox {
                text: qsTr("On")
                checked: Equaliser.enabled
                onToggled: Equaliser.enabled = checked
            }
            ComboBox {
                id: presetBox
                Layout.preferredWidth: 150
                model: Equaliser.availablePresets()
                onActivated: Equaliser.applyPreset(currentText)
                Connections {
                    target: Equaliser
                    function onUserPresetsChanged() { presetBox.model = Equaliser.availablePresets() }
                    function onPresetChanged() {
                        const at = presetBox.model.indexOf(Equaliser.preset)
                        if (at >= 0)
                            presetBox.currentIndex = at
                    }
                }
            }
            Label {
                text: Equaliser.preset === "custom" ? qsTr("(edited)") : ""
                opacity: 0.6
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Save…");   onClicked: savePresetDialog.open() }
            Button { text: qsTr("Import .eqf…"); onClicked: eqfDialog.open() }
            Button { text: qsTr("Reset");   onClicked: Equaliser.reset() }
        }

        Frame {
            id: eqPanel
            visible: false
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 4

                // Preamp sits apart from the bands: it is a different quantity,
                // applied ahead of the filters rather than being one of them.
                ColumnLayout {
                    spacing: 2
                    Label { Layout.alignment: Qt.AlignHCenter; text: Math.round(Equaliser.preamp) }
                    Slider {
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        Layout.preferredHeight: 90
                        from: -12; to: 12
                        value: Equaliser.preamp
                        onMoved: Equaliser.preamp = value
                    }
                    Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("pre"); font.bold: true }
                }

                ToolSeparator {}

                Repeater {
                    model: 10
                    delegate: ColumnLayout {
                        required property int index
                        spacing: 2
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: Math.round(Equaliser.bands[index])
                        }
                        Slider {
                            Layout.alignment: Qt.AlignHCenter
                            orientation: Qt.Vertical
                            Layout.preferredHeight: 90
                            from: -12; to: 12
                            value: Equaliser.bands[index]
                            onMoved: Equaliser.setBand(index, value)
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: Equaliser.bandLabels()[index]
                        }
                    }
                }
            }
        }

        // ---- play order and mixing --------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: qsTr("Shuffle: %1").arg(Playlist.shuffle ? qsTr("on") : qsTr("off"))
                onClicked: Playlist.shuffle = !Playlist.shuffle
            }
            Button {
                text: qsTr("Repeat: %1").arg(window.repeatNames[Playlist.repeat])
                onClicked: Playlist.repeat = (Playlist.repeat + 1) % 3
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Vol") }
            Slider {
                Layout.preferredWidth: 110
                from: 0; to: 1
                value: Engine.volume
                onMoved: Engine.volume = value
            }
            Label { text: qsTr("Bal") }
            Slider {
                Layout.preferredWidth: 110
                from: -1; to: 1
                value: Engine.balance
                onMoved: Engine.balance = value
            }
        }
    }

    // A typed path rather than a native file chooser. QtQuick.Dialogs is a
    // separate package that is not a build dependency of anything else here,
    // and the Phase 5 panel will want native dialogs on its own terms — adding
    // the dependency for a harness that gets deleted would be the wrong trade.
    Dialog {
        id: pathDialog
        anchors.centerIn: parent
        width: 460
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        property bool saving: false
        title: saving ? qsTr("Save playlist as") : qsTr("Open playlist")

        function prompt(save) {
            saving = save
            pathField.text = save ? "playlist.m3u8" : ""
            open()
            pathField.forceActiveFocus()
        }

        onAccepted: {
            const url = pathField.text.startsWith("/")
                      ? "file://" + pathField.text
                      : pathField.text
            if (saving)
                Playlist.saveTo(url)
            else
                Playlist.loadFrom(url)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            Label {
                text: qsTr("Path to an .m3u, .m3u8 or .pls file")
                font.pixelSize: 11
                opacity: 0.7
            }
            TextField {
                id: pathField
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: pathDialog.accept()
            }
        }
    }

    Connections {
        target: Playlist
        function onIoError(message) { errorBanner.show(message) }
    }

    Label {
        id: errorBanner
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        padding: 8
        visible: false
        color: "white"
        background: Rectangle { color: "#a00"; radius: 4 }
        function show(message) { text = message; visible = true; hideTimer.restart() }
        Timer { id: hideTimer; interval: 4000; onTriggered: errorBanner.visible = false }
    }

    Dialog {
        id: savePresetDialog
        anchors.centerIn: parent
        width: 320
        modal: true
        title: qsTr("Save preset")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { presetName.text = ""; presetName.forceActiveFocus() }
        onAccepted: {
            if (!Equaliser.saveUserPreset(presetName.text))
                errorBanner.show(qsTr("A preset needs a name without a slash in it."))
        }
        TextField {
            id: presetName
            anchors.fill: parent
            placeholderText: qsTr("Preset name")
            onAccepted: savePresetDialog.accept()
        }
    }

    Dialog {
        id: eqfDialog
        anchors.centerIn: parent
        width: 460
        modal: true
        title: qsTr("Import Winamp preset")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: eqfPath.forceActiveFocus()
        onAccepted: Equaliser.importEqf(eqfPath.text)
        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            Label {
                text: qsTr("Path to a .eqf file")
                font.pixelSize: 11
                opacity: 0.7
            }
            TextField {
                id: eqfPath
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: eqfDialog.accept()
            }
        }
    }

    Connections {
        target: Equaliser
        function onImportFailed(message) { errorBanner.show(message) }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls)
                Playlist.addUrls(drop.urls)
        }
    }
}
