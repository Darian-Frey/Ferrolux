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
    title: qsTr("Ferrolux RS-1 — Phase 5 harness")

    // The chassis. Warm off-white, and the surface every moulded control is
    // lit against — the bevel gradients are only legible as mouldings because
    // the face they sit on is lighter than the panel around them.
    color: Tokens.shell

    // ---- compact mode (F-042) ---------------------------------------------
    // Winamp's window shade: the panel folds away to the strip that reports
    // and controls what is playing, and everything that is browsed or adjusted
    // goes with it. Nothing here touches the engine or the playlist, so
    // playback state is preserved by construction rather than by being saved
    // and restored — the sections are hidden, not unloaded.
    property bool compact: false

    // The height to come back to, kept up to date rather than snapshotted when
    // the panel folds. A window the user has resized should come back the size
    // they left it, and a snapshot taken at the toggle is only right if nothing
    // has happened since.
    property int expandedHeight: 780
    onHeightChanged: if (!compact) expandedHeight = height

    // What the strip comes to once the folded sections have stopped counting.
    readonly property int stripHeight: Math.ceil(panel.implicitHeight + Tokens.gapPanel * 2)

    // The strip is *constrained* to its content rather than assigned a height
    // once. Assigning was the first attempt and it produced a strip with a
    // third of a window of empty air beneath it: the height was read before the
    // layout had been polished with the new visibilities, so it was the height
    // of a panel that had already stopped existing. A constraint re-evaluates
    // as the layout settles, and it has the second effect of stopping the
    // window being dragged taller while folded — a shade that can be stretched
    // is a window with a lot of chassis in it.
    minimumHeight: compact ? stripHeight : 0
    maximumHeight: compact ? stripHeight : 16777215

    onCompactChanged: {
        // F-042 requires the position to survive the toggle. Changing only the
        // height ought to leave it alone and does not always: a manager is free
        // to move a window it is already resizing, and some recentre one that
        // shrinks. Putting it back costs nothing and does not depend on which
        // manager is running.
        const keptX = x
        const keptY = y

        Qt.callLater(function () {
            if (!window.compact)
                window.height = window.expandedHeight
            window.x = keptX
            window.y = keptY
        })
    }

    // The panel's scale, set once for everything drawn in the window. F-041
    // requires continuous resizing rather than snapping to fixed multiples, so
    // this is a plain ratio and not a step function; Tokens clamps the ends.
    onWidthChanged: Tokens.scale = Tokens.scaleFor(width)
    Component.onCompleted: Tokens.scale = Tokens.scaleFor(width)

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
        id: panel
        anchors.fill: parent
        anchors.margins: Tokens.gapPanel
        spacing: Tokens.gapSection

        // ---- now playing -----------------------------------------------
        // Everything on it is a value the instrument reports, so all of it is
        // lit. This and the two rows below it are what compact mode keeps.
        DisplayPanel {
            Layout.fillWidth: true
            // The tags, not the file name. TagLib has already read them for the
            // playlist row directly beneath this, and the display showing
            // `01 - Carousel.mp3` under a list showing `Blink-182 — Carousel`
            // was the same fact in its least useful form.
            title: Playlist.currentRow < 0
                   ? qsTr("no disc")
                   : (Playlist.currentArtist !== ""
                      ? Playlist.currentArtist + " — " + Playlist.currentTitle
                      : Playlist.currentTitle)
            album: Playlist.currentAlbum
            format: Engine.streamFormat
            position: Engine.position
            duration: Engine.duration
            status: window.stateNames[Engine.state].toLowerCase()
            counter: Playlist.count > 0
                     ? qsTr("%1 of %2").arg(Playlist.currentRow + 1).arg(Playlist.count)
                     : ""
            error: Engine.errorText
        }

        // ---- transport ---------------------------------------------------
        // Position, volume and the transport keys on one raised plate, the
        // way a deck mounts its transport as a module rather than setting
        // each control into the chassis on its own. It is the same treatment
        // the equaliser drawer already had; giving it to the controls that
        // are used most is what stops the panel reading as a list of parts.
        //
        // Compact mode keeps this plate and folds everything else, so the
        // strip is a module rather than a few controls left behind.
        PanelSection {
            recessed: false
            title: qsTr("transport")
            Layout.fillWidth: true
            Layout.preferredHeight: transportPlate.implicitHeight + Tokens.padSection * 2

            ColumnLayout {
                id: transportPlate
                anchors.fill: parent
                anchors.margins: Tokens.padSection
                spacing: Tokens.gapControl

            // Position and volume share a row, and volume lives here rather than
            // down in the mixing section because it is the control reached for
            // most often while listening — and because compact mode keeps this row
            // and folds that section away. One control that is present in both
            // modes beats two instances bound to the same property, which is the
            // arrangement that drifts.
            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.gapControl

            Slot {
                id: positionBar
                Layout.fillWidth: true
                Layout.preferredHeight: Tokens.controlHeight * 0.6
                enabled: Engine.seekable
                from: 0
                to: Engine.duration > 0 ? Engine.duration : 1

                // The position binding is suspended while the lever is held.
                // Binding value directly to Engine.position means the per-frame
                // poll re-asserts it sixty times a second, so a drag is undone
                // as fast as it is made and the lever appears to snap back.
                // See BUG-009.
                Binding on value {
                    when: !positionBar.held
                    value: Engine.position
                    restoreMode: Binding.RestoreNone
                }

                // Dragging moves the lever; the seek happens on release. Seeking
                // continuously would ask the pipeline to flush and refill on every
                // frame of the drag, which stutters the audio for the whole of it.
                onMoved: function(to) { positionBar.value = to }
                onReleased: Engine.seek(positionBar.value)
            }

                // The value is lit because it is a value; the unit beside it is
                // printed because it names what the value is in. The seven-segment
                // face has no per-cent sign — digits and separators and nothing
                // else — so the sign is silkscreened, which is what a deck does.
                Legend { text: qsTr("vol") }
                Slot {
                    Layout.preferredWidth: Tokens.controlHeight * 3
                    Layout.preferredHeight: Tokens.controlHeight
                    from: 0; to: 1
                    ticks: 5
                    value: Engine.volume
                    onMoved: function(to) { Engine.volume = to }
                }
                Readout {
                    Layout.preferredWidth: Tokens.sizeLegend * 3
                    face: Tokens.readoutNumeric
                    size: Tokens.sizeLegend
                    ghost: "888"
                    alignment: Text.AlignRight
                    text: String(Math.round(Engine.volume * 100))
                    ground: Tokens.displayBg
                    inset: Tokens.hairline * 4
                }
                Legend { text: qsTr("%") }
            }

            // ---- transport ---------------------------------------------------
            // Moulded controls with drawn marks. Play latches while the pipeline is
            // playing, which is the lamp behind the button rather than the finger
            // on it: `activated` and `pressed` are different states and look it.
            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.gapControl

                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Tokens.controlHeight
                    onClicked: Engine.previous()
                    TransportGlyph { anchors.fill: parent; mark: TransportGlyph.Previous }
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Tokens.controlHeight
                    activated: window.stateNames[Engine.state] === "Playing"
                    onClicked: Engine.play()
                    TransportGlyph {
                        anchors.fill: parent
                        mark: TransportGlyph.Play
                        // On an amber face the mark is printed in the darkest of the
                        // readout ambers rather than in `ink`: black on amber reads
                        // as a hole punched in the lamp, where the deeper amber
                        // reads as ink on a lit surface.
                        ink: parent.activated ? Tokens.readoutFloor : Tokens.ink
                    }
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Tokens.controlHeight
                    activated: window.stateNames[Engine.state] === "Paused"
                    onClicked: Engine.pause()
                    TransportGlyph {
                        anchors.fill: parent
                        mark: TransportGlyph.Pause
                        ink: parent.activated ? Tokens.readoutFloor : Tokens.ink
                    }
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Tokens.controlHeight
                    onClicked: Engine.stop()
                    TransportGlyph { anchors.fill: parent; mark: TransportGlyph.Stop }
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Tokens.controlHeight
                    onClicked: Playlist.advance()
                    TransportGlyph { anchors.fill: parent; mark: TransportGlyph.Next }
                }

                // The shade. Narrower than the transport controls and not stretched
                // with them: it is not one of the transport, and a control that
                // looked like one would be reached for by mistake.
                PanelButton {
                    Layout.preferredWidth: Tokens.controlHeight
                    Layout.preferredHeight: Tokens.controlHeight
                    onClicked: window.compact = !window.compact
                    TransportGlyph {
                        anchors.fill: parent
                        mark: window.compact ? TransportGlyph.RollDown : TransportGlyph.RollUp
                    }
                }
            }

            }
        }

        // ---- playlist ---------------------------------------------------
        RowLayout {
            visible: !window.compact
            Layout.fillWidth: true
            spacing: 6

            Legend { text: qsTr("filter") }
            EntryField {
                Layout.fillWidth: true
                Layout.preferredHeight: Tokens.controlHeight
                placeholder: qsTr("all entries")
                onTextChanged: PlaylistView.filterText = text
            }
            Readout {
                // How many of the list the filter leaves, which is a value and
                // so is lit. Shown only while a filter is in force: an unlit
                // window sitting there permanently would be a lamp that never
                // comes on.
                visible: PlaylistView.filterText !== ""
                Layout.preferredHeight: Tokens.controlHeight
                face: Tokens.readoutNumeric
                size: Tokens.sizeLegend
                ground: Tokens.displayBg
                inset: Tokens.gapControl
                alignment: Text.AlignRight
                text: qsTr("%1:%2").arg(PlaylistView.count).arg(Playlist.count)
            }
            PanelButton { text: qsTr("add files"); onClicked: addFilesDialog.open() }
            PanelButton { text: qsTr("add folder"); onClicked: addFolderDialog.open() }
            PanelButton {
                text: qsTr("remove")
                enabled: window.selection.length > 0
                onClicked: { Playlist.removeRows(window.selection); window.selection = [] }
            }
            PanelButton {
                text: qsTr("clear")
                enabled: Playlist.count > 0
                onClicked: { Playlist.clear(); window.selection = [] }
            }
            PanelButton {
                text: qsTr("undo")
                enabled: Playlist.canUndo
                onClicked: Playlist.undo()
            }
            PanelButton {
                id: sortButton
                text: qsTr("sort")
                enabled: Playlist.count > 1
                onClicked: sortMenu.open()

                // A list of commands rather than of states, so `current` is
                // left at -1 and nothing in it is lit brighter than the rest:
                // there is no "the sort order in effect" to report, only
                // orderings that can be applied. Indices match
                // PlaylistModel::SortKey, with the last entry reusing the first
                // key in the other direction.
                PanelMenu {
                    id: sortMenu
                    options: [qsTr("title"), qsTr("artist"), qsTr("album"),
                              qsTr("duration"), qsTr("path"), qsTr("file date"),
                              qsTr("title, reversed")]
                    onChosen: function(index) {
                        if (index === 6)
                            Playlist.sortBy(0, Qt.DescendingOrder)
                        else
                            Playlist.sortBy(index, Qt.AscendingOrder)
                    }
                }
            }
            PanelButton { text: qsTr("open"); onClicked: openPlaylistDialog.open() }
            PanelButton {
                text: qsTr("save")
                enabled: Playlist.count > 0
                onClicked: savePlaylistDialog.open()
            }
        }

        // The playlist is a lit surface, not a chassis one: every row on it is
        // a value the instrument reports. So it is a well, and its rows are in
        // the readout face at two brightnesses — one lamp colour, the current
        // entry brighter — rather than in two different hues. A single-colour
        // display is what the palette describes and what a deck actually has.
        PanelSection {
            visible: !window.compact
            title: qsTr("programme")
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Tokens.padRow
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
                    height: Tokens.sizeReadout * 2

                    readonly property int sourceRow: PlaylistView.toSourceRow(index)
                    readonly property bool selected: window.selection.indexOf(sourceRow) >= 0

                    // A selected row is *inverted*: the lamp fills the cell and
                    // the text is cut out of it in the colour of the unlit
                    // display. This is what a panel does to say "this one" —
                    // the whole segment lights and the glyph becomes the hole
                    // in it — and it is unmistakable in a way that a change of
                    // brightness is not. A backlight in the dimmest amber was
                    // the previous answer and it was too quiet to find.
                    //
                    // Never a highlight colour from the system theme: that
                    // would be the one thing on the panel that changed with the
                    // desktop.
                    color: selected ? Tokens.readout : "transparent"
                    radius: Tokens.radiusSlot

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
                        anchors.leftMargin: Tokens.gapControl
                        anchors.rightMargin: Tokens.gapControl
                        spacing: Tokens.gapControl

                        // The playing entry is marked, not merely coloured: on
                        // a lit display, brightness alone is hard to pick out
                        // of a long list at a glance, and the mark survives
                        // being looked at from an angle.
                        TransportGlyph {
                            Layout.preferredWidth: Tokens.sizeReadout
                            Layout.preferredHeight: Tokens.sizeReadout
                            mark: TransportGlyph.Play
                            ink: row.selected ? Tokens.displayBg : Tokens.readout
                            visible: isCurrent
                        }
                        Item {
                            Layout.preferredWidth: Tokens.sizeReadout
                            visible: !isCurrent
                        }

                        Readout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            face: Tokens.readoutText
                            size: Tokens.sizeReadout
                            // Every row is lit at full brightness, and what is
                            // playing is said by the mark beside it rather than
                            // by the others being dimmed.
                            //
                            // Dimming them was the first design and it was
                            // wrong: `readout-dim` is 3.8:1 against the well,
                            // under the 4.5:1 that normal text needs, and this
                            // is a dot-matrix face, which is harder to read
                            // than a plain one at the same contrast. It made
                            // the bulk of the playlist the least legible thing
                            // on the panel to distinguish one row that already
                            // had a mark of its own. See BUG-020.
                            //
                            // A missing or unreadable file is still dimmed, and
                            // that one is deliberate: it is not a row to be
                            // read so much as one to be noticed as unavailable,
                            // and there is no red on this display to say so
                            // with. 3 == MetadataState::Missing, 2 == Failed.
                            colour: row.selected
                                        ? Tokens.displayBg
                                        : (metadataState === 3 || metadataState === 2)
                                              ? Tokens.readoutFloor
                                              : Tokens.readout
                            text: artist !== "" ? artist + " — " + title : title
                        }
                        Readout {
                            Layout.fillHeight: true
                            face: Tokens.readoutNumeric
                            size: Tokens.sizeReadout
                            colour: row.selected ? Tokens.displayBg : Tokens.readout
                            alignment: Text.AlignRight
                            width: implicitWidth
                            text: window.formatTime(duration)
                        }
                    }
                }
            }
        }
        // ---- meters --------------------------------------------------------
        // Defined in MeterDisplay.qml so that tests/frame_bench measures the
        // same shaders this window shows. See AV-002.
        MeterDisplay {
            visible: !window.compact
            title: qsTr("level")
            Layout.fillWidth: true
            Layout.preferredHeight: 92
        }

        // ---- equaliser ---------------------------------------------------
        RowLayout {
            visible: !window.compact
            Layout.fillWidth: true
            spacing: 8

            PanelButton {
                text: qsTr("equaliser")
                // Latched while the drawer is open, so the control reports the
                // state of the drawer rather than only being the way to it.
                activated: eqPanel.open
                onClicked: eqPanel.open = !eqPanel.open
            }

            // Bypass is a setting the machine is left in, not an action taken,
            // so it is a switch for the same reason shuffle and repeat are.
            ColumnLayout {
                spacing: 0
                Legend { text: qsTr("eq"); font.pixelSize: Tokens.sizeLegendSmall }
                SlideSwitch {
                    Layout.preferredHeight: Tokens.controlHeight
                    positions: [qsTr("out"), qsTr("in")]
                    current: Equaliser.enabled ? 1 : 0
                    onThrown: function(position) { Equaliser.enabled = position === 1 }
                }
            }

            // The preset in effect is a value the instrument reports, so it is
            // lit in a window rather than printed on a button. Clicking the
            // window opens the list — the field is both the report and the way
            // to change it, which is what a combo box is when it is drawn as
            // hardware.
            Readout {
                id: presetField
                Layout.preferredWidth: Tokens.controlHeight * 4
                Layout.preferredHeight: Tokens.controlHeight
                face: Tokens.readoutText
                size: Tokens.sizeReadout
                ground: Tokens.displayBg
                inset: Tokens.gapControl

                // Dimmed while the equaliser is out of circuit: the preset is
                // still what would be applied, but it is not what is being
                // heard, and a readout at full brightness would claim it was.
                colour: Equaliser.enabled ? Tokens.readout : Tokens.readoutDim
                text: Equaliser.preset === "custom" ? qsTr("edited") : Equaliser.preset

                TapHandler { onTapped: presetMenu.open() }

                PanelMenu {
                    id: presetMenu
                    options: Equaliser.availablePresets()
                    current: options.indexOf(Equaliser.preset)
                    onChosen: function(index) { Equaliser.applyPreset(options[index]) }

                    Connections {
                        target: Equaliser
                        function onUserPresetsChanged() {
                            presetMenu.options = Equaliser.availablePresets()
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }
            PanelButton { text: qsTr("save"); onClicked: savePresetDialog.open() }
            PanelButton { text: qsTr("import .eqf"); onClicked: eqfFileDialog.open() }
            PanelButton { text: qsTr("reset"); onClicked: Equaliser.reset() }
        }

        // The equaliser is a raised surface: it is touched, not watched. The
        // gain readouts on it are lit because they report values, and the band
        // centres beneath are printed because they name controls — the same
        // division as everywhere else, and the reason the two are components
        // rather than differently configured Text items.
        PanelSection {
            id: eqPanel

            // Whether the drawer is pulled out, which is a different question
            // from whether it can be seen. Compact mode hides it without
            // closing it, so folding the panel away and opening it again
            // returns the drawer to how it was left.
            property bool open: false
            visible: open && !window.compact
            recessed: false
            Layout.fillWidth: true
            Layout.preferredHeight: eqRow.implicitHeight + Tokens.padSection * 2

            // Dimmed, not disabled: the curve stays readable and editable while
            // the equaliser is bypassed, but it is visibly not in circuit.
            opacity: Equaliser.enabled ? 1.0 : 0.45

            // A gain in a seven-segment face. DSEG7 Classic has a minus and no
            // plus — SPEC.md says the numeric face renders digits and separators
            // only, and this is what that means in practice — so a boost shows
            // unsigned, as the readout on a deck does. A '+' here would fall
            // back to a substituted system font in the middle of a lit field,
            // which is the failure the role rule exists to prevent.
            function gain(decibels) {
                return String(Math.round(decibels))
            }

            RowLayout {
                id: eqRow
                anchors.fill: parent
                anchors.margins: Tokens.padSection
                spacing: Tokens.gapControl

                // Preamp sits apart from the bands: it is a different quantity,
                // applied ahead of the filters rather than being one of them.
                ColumnLayout {
                    spacing: Tokens.padRow
                    Readout {
                        Layout.alignment: Qt.AlignHCenter
                        face: Tokens.readoutNumeric
                        size: Tokens.sizeLegend

                        // Three cells: a sign and two digits, which is what
                        // -12 to 12 needs. The ghost is the whole field lit, so
                        // the live value must be *right*-aligned into it — a
                        // centred single digit would sit between two unlit
                        // cells rather than in one of them, which no segmented
                        // display does.
                        ghost: "-88"
                        alignment: Text.AlignRight
                        text: eqPanel.gain(Equaliser.preamp)

                        // Its own window, because it stands on the chassis. A
                        // readout needs a ground darker than both of its layers
                        // or the unlit segments out-contrast the lit ones.
                        ground: Tokens.displayBg
                        inset: Tokens.hairline * 4
                    }
                    Slot {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredHeight: Tokens.faderTravel
                        Layout.preferredWidth: Tokens.thumbWidth
                        vertical: true
                        from: -12; to: 12
                        origin: 0
                        detent: 0
                        ticks: 5
                        value: Equaliser.preamp
                        onMoved: function(to) { Equaliser.preamp = to }
                    }
                    Legend { Layout.alignment: Qt.AlignHCenter; text: qsTr("pre") }
                }

                // A scored line, not a Controls separator: one hairline in the
                // bevel colour is what divides two areas of a moulded chassis.
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: Tokens.hairline
                    color: Tokens.shellEdge
                }

                Repeater {
                    model: 10
                    delegate: ColumnLayout {
                        required property int index
                        spacing: Tokens.padRow

                        Readout {
                            Layout.alignment: Qt.AlignHCenter
                            face: Tokens.readoutNumeric
                            size: Tokens.sizeLegend
                            ghost: "-88"
                            alignment: Text.AlignRight
                            text: eqPanel.gain(Equaliser.bands[index])
                            ground: Tokens.displayBg
                            inset: Tokens.hairline * 4
                        }
                        Slot {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredHeight: Tokens.faderTravel
                            Layout.preferredWidth: Tokens.thumbWidth
                            vertical: true
                            from: -12; to: 12
                            // Flat is the origin and the detent both: the lit
                            // run reports a departure from flat rather than an
                            // amount of something, and flat is the position the
                            // control is returned to and cannot be found by eye.
                            origin: 0
                            detent: 0
                            ticks: 5
                            value: Equaliser.bands[index]
                            onMoved: function(to) { Equaliser.setBand(index, to) }
                        }
                        Legend {
                            Layout.alignment: Qt.AlignHCenter
                            text: Equaliser.bandLabels()[index]
                        }
                    }
                }
            }
        }

        // ---- play order and output --------------------------------------
        // Two slim plates rather than one. The controls at either end of this
        // row do different jobs — shuffle and repeat decide what plays next,
        // balance decides what comes out — and a single plate spanning both
        // would be mostly empty in the middle, which is a worse use of a
        // surface than leaving the chassis showing between two modules.
        RowLayout {
            visible: !window.compact
            Layout.fillWidth: true
            spacing: Tokens.gapSection

            PanelSection {
                recessed: false
                title: qsTr("play order")
                Layout.preferredWidth: orderRow.implicitWidth + Tokens.padSection * 2
                Layout.preferredHeight: orderRow.implicitHeight + Tokens.padSection * 2

                RowLayout {
                    id: orderRow
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl

                // Settings, not actions, so they are switches and not buttons —
                // F-040 names the cycling buttons that used to be here as exactly
                // what it excludes. The lever reports the state; the marks beneath
                // it are printed on the chassis and never light up.
                ColumnLayout {
                    spacing: 0
                    Legend { text: qsTr("shuffle"); font.pixelSize: Tokens.sizeLegendSmall }
                    SlideSwitch {
                        Layout.preferredHeight: Tokens.controlHeight
                        positions: [qsTr("off"), qsTr("on")]
                        current: Playlist.shuffle ? 1 : 0
                        onThrown: function(position) { Playlist.shuffle = position === 1 }
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Legend { text: qsTr("repeat"); font.pixelSize: Tokens.sizeLegendSmall }
                    SlideSwitch {
                        Layout.preferredHeight: Tokens.controlHeight
                        // Three detents rather than a boolean with a special case:
                        // a three-position slide switch is an ordinary object.
                        positions: [qsTr("off"), qsTr("all"), qsTr("one")]
                        current: Playlist.repeat
                        onThrown: function(position) { Playlist.repeat = position }
                    }
                }
                }
            }

            Item { Layout.fillWidth: true }

            PanelSection {
                recessed: false
                title: qsTr("output")
                Layout.preferredWidth: outputRow.implicitWidth + Tokens.padSection * 2
                Layout.preferredHeight: outputRow.implicitHeight + Tokens.padSection * 2

                RowLayout {
                    id: outputRow
                    anchors.fill: parent
                    anchors.margins: Tokens.padSection
                    spacing: Tokens.gapControl


                // Balance is set once and left, so it stays in the mixing section
                // with the play-order switches. Volume is not, and has moved up to
                // the transport row. Both are continuous controls that are set
                // rather than watched, so both carry a printed scale.
                Legend { text: qsTr("bal") }
                Slot {
                    Layout.preferredWidth: Tokens.controlHeight * 3
                    Layout.preferredHeight: Tokens.controlHeight
                    from: -1; to: 1

                    // Centre is the origin as well as the detent: the lit run
                    // reports a departure from centre, so a control set to the
                    // middle shows no run at all, which is exactly what "centred"
                    // should look like.
                    origin: 0
                    detent: 0
                    detentRange: 0.04
                    ticks: 5
                    value: Engine.balance
                    onMoved: function(to) { Engine.balance = to }
                }
                Readout {
                    Layout.preferredWidth: Tokens.sizeLegend * 4
                    face: Tokens.readoutNumeric
                    size: Tokens.sizeLegend
                    alignment: Text.AlignRight
                    // Nothing lit when centred. A seven-segment field cannot spell
                    // "centre" — it cannot tell 5 from S — and the absence of a
                    // reading against an unlit run is the clearer statement anyway.
                    ghost: "88"
                    text: Math.abs(Engine.balance) < 0.005
                          ? ""
                          : String(Math.round(Math.abs(Engine.balance) * 100))
                    ground: Tokens.displayBg
                    inset: Tokens.hairline * 4
                }
                Legend {
                    Layout.preferredWidth: Tokens.sizeLegend
                    text: Math.abs(Engine.balance) < 0.005 ? ""
                          : (Engine.balance < 0 ? qsTr("L") : qsTr("R"))
                }
                }
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
