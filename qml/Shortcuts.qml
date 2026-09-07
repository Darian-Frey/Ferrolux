// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Every keyboard shortcut, declared once. F-043.
//
// The table below is both the binding and the reference. `KeyGuide.qml` prints
// this same list, so the two cannot disagree — and a shortcut reference that
// disagrees with the shortcuts is worse than none, because it is believed. The
// alternative shape, a set of `Shortcut` objects beside a hand-written list of
// what they do, is one edit away from lying at all times.
//
// Modifiers rather than bare letters, and deliberately. The panel has a text
// field in it (the preset dialog), and a `Shortcut` at window scope fires while
// a field has focus — so a bare `S` for stop is a key that cannot be typed into
// a preset name. `enabled` guards the whole set against that anyway; the choice
// of sequences means the guard is a second line rather than the only one.
//
// The bare arrows are missing from this table on purpose: they belong to
// whatever has focus, which is the playlist or a fader. A global binding on
// them would take the keyboard away from every control that needs it.

import QtQuick
import QtQml

Item {
    id: root

    // Supplied rather than derived, so the enum mapping lives in one place —
    // `Main.qml` already has `stateNames` and this does not need a second copy.
    property bool playing: false

    // Window-scale actions belong to the window; this only asks.
    signal foldRequested()
    signal settingsRequested()
    signal guideRequested()
    signal addFilesRequested()
    signal savePlaylistRequested()

    // A shortcut fires even while a text field has focus, which would make the
    // preset dialog unusable. Duck-typed rather than named: anything with a
    // `selectedText` is taking typed characters, whatever it is called, and a
    // guard written against one dialog's id stops working the day a second one
    // appears.
    readonly property bool typing: {
        const item = Window.window ? Window.window.activeFocusItem : null
        return item !== null && item !== undefined
               && item.selectedText !== undefined
    }

    readonly property var groups: [
        {
            title: qsTr("transport"),
            keys: [
                { seq: "Space",       legend: qsTr("play or pause"),
                  act: function () { root.playing ? Engine.pause() : Engine.play() } },
                { seq: "Ctrl+.",      legend: qsTr("stop"),
                  act: function () { Engine.stop() } },
                { seq: "Ctrl+Right",  legend: qsTr("next track"),
                  act: function () { Playlist.advance() } },
                { seq: "Ctrl+Left",   legend: qsTr("previous track"),
                  act: function () { Engine.previous() } },
                { seq: "Shift+Right", legend: qsTr("forward 5 seconds"),
                  act: function () { Engine.seek(Engine.position + 5000000000) } },
                { seq: "Shift+Left",  legend: qsTr("back 5 seconds"),
                  act: function () { Engine.seek(Math.max(0, Engine.position - 5000000000)) } },
            ]
        },
        {
            title: qsTr("sound"),
            keys: [
                { seq: "Ctrl+Up",   legend: qsTr("volume up"),
                  act: function () { Engine.volume = Math.min(1, Engine.volume + 0.05) } },
                { seq: "Ctrl+Down", legend: qsTr("volume down"),
                  act: function () { Engine.volume = Math.max(0, Engine.volume - 0.05) } },
                { seq: "Ctrl+E",    legend: qsTr("equaliser in or out"),
                  act: function () { Equaliser.enabled = !Equaliser.enabled } },
                { seq: "Ctrl+0",    legend: qsTr("flatten the equaliser"),
                  act: function () { Equaliser.applyPreset("flat") } },
            ]
        },
        {
            title: qsTr("programme"),
            keys: [
                { seq: "Ctrl+O",       legend: qsTr("add files"),
                  act: function () { root.addFilesRequested() } },
                { seq: "Ctrl+S",       legend: qsTr("save the playlist"),
                  act: function () { root.savePlaylistRequested() } },
                { seq: "Ctrl+Z",       legend: qsTr("undo the last change"),
                  act: function () { Playlist.undo() } },
                { seq: "Ctrl+H",       legend: qsTr("shuffle on or off"),
                  act: function () { Playlist.shuffle = !Playlist.shuffle } },
            ]
        },
        {
            title: qsTr("panel"),
            keys: [
                { seq: "Ctrl+K",     legend: qsTr("fold the panel"),
                  act: function () { root.foldRequested() } },
                { seq: "Ctrl+,",     legend: qsTr("settings"),
                  act: function () { root.settingsRequested() } },
                { seq: "F1",         legend: qsTr("this list"),
                  act: function () { root.guideRequested() } },
                { seq: "Ctrl+Q",     legend: qsTr("quit"),
                  act: function () { Qt.quit() } },
            ]
        },
        {
            // Not bindings — these belong to whichever control has the keyboard,
            // and are printed because a reference that lists only the global
            // ones tells the user the arrows do nothing.
            title: qsTr("where the focus is"),
            reference: true,
            keys: [
                { seq: "Tab",           legend: qsTr("move between the sliders") },
                { seq: "← → ↑ ↓",       legend: qsTr("adjust the slider, or move in the list") },
                { seq: "Page Up/Down",  legend: qsTr("adjust in larger steps") },
                { seq: "Home / End",    legend: qsTr("either end of the slider or the list") },
                { seq: "Return",        legend: qsTr("play the selected entry") },
                { seq: "Delete",        legend: qsTr("remove the selected entries") },
            ]
        },
    ]

    // One `Shortcut` per bound entry, from the same table the guide prints.
    // `Instantiator` rather than `Repeater` because a `Shortcut` is not an Item
    // and a Repeater will not have it.
    Instantiator {
        model: {
            let flat = []
            for (const group of root.groups) {
                if (group.reference)
                    continue
                for (const entry of group.keys)
                    flat.push(entry)
            }
            return flat
        }
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.seq
            enabled: !root.typing
            onActivated: modelData.act()
        }
    }
}
