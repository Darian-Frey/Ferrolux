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
import QtQuick.Dialogs
import Ferrolux

ApplicationWindow {
    id: window
    width: 720
    height: 780
    visible: true
    title: qsTr("Ferrolux RS-1 — Phase 3 harness")

    // Invariant 4: the single position poll for the whole application. It also
    // drives the meters, releasing queued analysis frames as the pipeline's
    // clock reaches them and advancing the ballistics by the frame's own
    // elapsed time.
    FrameAnimation {
        id: frame
        running: true
        onTriggered: {
            Engine.poll()
            Meters.releaseUpTo(Engine.runningTime())
            Meters.advance(frameTime * 1000)
        }
    }

    // Explicit rather than relying on quitOnLastWindowClosed. Not strictly
    // needed — that default was working — but it states the intent without
    // depending on how many windows happen to exist. The hang this was first
    // written for turned out to be a teardown deadlock in main(); see BUG-012.
    onClosing: Qt.quit()

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

                // The position binding is suspended while the handle is held.
                // Binding value directly to Engine.position means the per-frame
                // poll re-asserts it sixty times a second, so a drag is undone
                // as fast as it is made and the handle appears to snap back.
                // See BUG-009.
                Binding on value {
                    when: !positionBar.pressed
                    value: Engine.position
                    restoreMode: Binding.RestoreNone
                }

                onPressedChanged: {
                    if (!pressed)
                        Engine.seek(value)
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
            Button { text: qsTr("Add files…"); onClicked: addFilesDialog.open() }
            Button { text: qsTr("Add folder…"); onClicked: addFolderDialog.open() }
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
            Button { text: qsTr("Open…"); onClicked: openPlaylistDialog.open() }
            Button { text: qsTr("Save…"); enabled: Playlist.count > 0; onClicked: savePlaylistDialog.open() }
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

        // ---- meters --------------------------------------------------------
        // Four display modes over one texture and one meter source, cycled by
        // clicking the display (F-033). Each mode is a fragment shader reading
        // the same N×1 texture; switching hides one and shows another, which is
        // why it cannot drop a frame or interrupt anything.
        Rectangle {
            id: meterPanel
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            color: "#2C2C2A"
            radius: 3

            // Band values, resident on the GPU. Draws nothing itself — it
            // exists to be sampled, and builds its texture during scene graph
            // synchronisation rather than when painted.
            MeterTexture {
                id: meterTexture
                source: Meters
                visible: false
                width: 1
                height: 1
            }

            // Spectrum bars, upright or reflected about the centre line. One
            // shader serves both: the mirrored form is the same bar logic over
            // a folded height coordinate.
            ShaderEffect {
                anchors.fill: parent
                anchors.margins: 4
                visible: Meters.mode === "spectrum" || Meters.mode === "spectrum-mirror"

                property variant source: meterTexture
                property real bandCount: Meters.bandCount
                property real gap: 0.18
                property real capThickness: 0.035
                property real mirrored: Meters.mode === "spectrum-mirror" ? 1.0 : 0.0
                property color barColour: "#EF9F27"
                property color barColourLow: "#BA7517"
                property color capColour: "#F6D08A"

                fragmentShader: "qrc:/qt/qml/Ferrolux/qml/shaders/spectrum.frag.qsb"
            }

            // The same bands as the bar display, read as a continuous curve and
            // shaded in contour steps. Sampling between band centres is what
            // the texture's linear filtering was for.
            ShaderEffect {
                anchors.fill: parent
                anchors.margins: 4
                visible: Meters.mode === "flame"

                property variant source: meterTexture
                property real bandCount: Meters.bandCount
                property real ranks: 9
                property real frontHeight: 0.52
                property real backHeight: 1.15
                property real parallax: 2.2
                property real softness: 0.9
                // Six-digit hex, deliberately. Qt reads an eight-digit colour as
                // #AARRGGBB — alpha *first* — not the #RRGGBBAA that most tools
                // emit, so "#e98b48ff" is not a light orange with full alpha but
                // a 91%-opaque purple. Leaving the alpha off avoids the question.
                property color frontColour: "#E98B48"
                property color backColour: "#FFD98A"

                fragmentShader: "qrc:/qt/qml/Ferrolux/qml/shaders/flame.frag.qsb"
            }

            // Stereo VU: two needles, one shader instantiated per channel.
            Row {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 6
                visible: Meters.mode === "vu"

                Repeater {
                    model: 2
                    delegate: ShaderEffect {
                        required property int index
                        width: (meterPanel.width - 8 - 6) / 2
                        height: meterPanel.height - 8

                        property real deflection: Meters.vu[index]
                        property real peakIndicator: Meters.peakIndicators[index]
                        property real aspect: width / Math.max(1, height)
                        property color faceColour: "#2C2C2A"
                        property color inkColour: "#BA7517"
                        property color needleColour: "#F6D08A"
                        property color overColour: "#EF9F27"

                        fragmentShader: "qrc:/qt/qml/Ferrolux/qml/shaders/vu.frag.qsb"
                    }
                }
            }

            // Segmented peak ladder. The lit run follows the ballistic level and
            // the marked segment follows the faster peak indicator, which is the
            // pairing a hardware ladder shows.
            ShaderEffect {
                anchors.fill: parent
                anchors.margins: 4
                visible: Meters.mode === "ladder"

                // The lit run follows the ballistic level and the marked
                // segment is its held maximum, which is the arrangement a
                // hardware ladder shows. Driven by sample peak instead it would
                // sit near full on any modern master — correct, and useless.
                //
                // 0 VU lands at 80% of the ladder, so the hot segments above it
                // mean "over reference" rather than merely "loud".
                property real levelLeft: Math.min(1, Meters.vu[0] / 1.25)
                property real levelRight: Math.min(1, Meters.vu[1] / 1.25)
                property real peakLeft: Math.min(1, Meters.channelPeaks[0] / 1.25)
                property real peakRight: Math.min(1, Meters.channelPeaks[1] / 1.25)
                property real segments: 28
                property real overFrom: 0.8
                property color offColour: "#3A342C"
                property color onColour: "#EF9F27"
                property color overOnColour: "#E8613A"
                property color capColour: "#F6D08A"

                fragmentShader: "qrc:/qt/qml/Ferrolux/qml/shaders/ladder.frag.qsb"
            }

            // Mode name, shown briefly after a change so a click has an answer.
            Label {
                id: modeLabel
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                color: "#BA7517"
                font.pixelSize: 10
                text: Meters.mode
                opacity: 0
                Connections {
                    target: Meters
                    function onModeChanged() { modeLabel.opacity = 1; modeFade.restart() }
                }
                Timer {
                    id: modeFade
                    interval: 1400
                    onTriggered: modeLabel.opacity = 0
                }
                Behavior on opacity { NumberAnimation { duration: 250 } }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: Meters.cycleMode()
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
                // An equaliser that is off still shows its curve and still
                // lets it be edited, which is correct — but silently doing
                // nothing is not. Say so.
                text: !Equaliser.enabled ? qsTr("(not active)")
                      : Equaliser.preset === "custom" ? qsTr("(edited)") : ""
                opacity: 0.6
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Save…");   onClicked: savePresetDialog.open() }
            Button { text: qsTr("Import .eqf…"); onClicked: eqfFileDialog.open() }
            Button { text: qsTr("Reset");   onClicked: Equaliser.reset() }
        }

        Frame {
            id: eqPanel
            visible: false
            Layout.fillWidth: true
            // Dimmed, not disabled: the curve stays readable and editable while
            // the equaliser is bypassed, but it is visibly not in circuit.
            opacity: Equaliser.enabled ? 1.0 : 0.45

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
                Layout.preferredWidth: 100
                from: 0; to: 1
                value: Engine.volume
                onMoved: Engine.volume = value
            }
            Label {
                Layout.preferredWidth: 34
                horizontalAlignment: Text.AlignRight
                text: Math.round(Engine.volume * 100) + "%"
            }

            Label { text: qsTr("Bal") }
            Slider {
                id: balanceSlider
                Layout.preferredWidth: 100
                from: -1; to: 1
                value: Engine.balance

                // Snap to exact centre within a few percent. Every hardware
                // balance control has a detent there for the same reason:
                // centre is the position returned to most often, and the one
                // position a continuous control cannot be set to by eye.
                onMoved: Engine.balance = Math.abs(value) < 0.04 ? 0.0 : value

                // Centre mark, so the detent has something to agree with.
                Rectangle {
                    z: -1
                    width: 1
                    height: 6
                    color: palette.mid
                    x: balanceSlider.leftPadding + balanceSlider.availableWidth / 2
                    y: balanceSlider.topPadding + balanceSlider.availableHeight / 2 + 6
                }
            }
            Label {
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
                text: Math.abs(Engine.balance) < 0.005
                      ? qsTr("centre")
                      : (Engine.balance < 0 ? qsTr("L ") : qsTr("R "))
                        + Math.round(Math.abs(Engine.balance) * 100)
            }
        }
    }

    // Native choosers, from QtQuick.Dialogs. Multi-select for files because
    // F-010 asks for individual files, whole folders and a drag-and-drop
    // selection; all three funnel into Playlist.addPaths, which expands
    // directories and filters by suffix.
    FileDialog {
        id: addFilesDialog
        title: qsTr("Add files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Audio files (*.flac *.mp3 *.ogg *.oga *.opus *.m4a *.aac *.wav *.aiff *.aif *.wv *.mpc)"),
                      qsTr("All files (*)")]
        onAccepted: Playlist.addPaths(selectedFiles)
    }

    FolderDialog {
        id: addFolderDialog
        title: qsTr("Add folder")
        onAccepted: Playlist.addPaths([selectedFolder])
    }

    FileDialog {
        id: openPlaylistDialog
        title: qsTr("Open playlist")
        nameFilters: [qsTr("Playlists (*.m3u *.m3u8 *.pls)"), qsTr("All files (*)")]
        onAccepted: Playlist.loadFrom(selectedFile)
    }

    FileDialog {
        id: savePlaylistDialog
        title: qsTr("Save playlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "m3u8"
        nameFilters: [qsTr("Extended M3U (*.m3u8 *.m3u)"), qsTr("PLS (*.pls)")]
        onAccepted: Playlist.saveTo(selectedFile)
    }

    FileDialog {
        id: eqfFileDialog
        title: qsTr("Import Winamp equaliser preset")
        nameFilters: [qsTr("Winamp presets (*.eqf)"), qsTr("All files (*)")]
        onAccepted: Equaliser.importEqf(selectedFile)
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

    Connections {
        target: Equaliser
        function onImportFailed(message) { errorBanner.show(message) }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls)
                Playlist.addPaths(drop.urls)
        }
    }
}
