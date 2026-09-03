// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// One of the five transport marks, drawn by qml/shaders/transport.frag.
//
// A named wrapper rather than a raw ShaderEffect at each call site, so that a
// button asks for `TransportGlyph { mark: TransportGlyph.Play }` and no part of
// the panel has to know that the mark is a shader or which index it is.

import QtQuick

ShaderEffect {
    id: glyph

    // Matches the branch order in the shader. Kept as an enumeration so a
    // caller never writes the number.
    enum Mark { Play, Pause, Stop, Previous, Next, RollUp, RollDown }

    property int mark: TransportGlyph.Play
    property color ink: Tokens.ink

    // Uniforms. Named to match the shader's block, which is how ShaderEffect
    // binds them.
    property real glyph: mark
    property real aspect: width / Math.max(1, height)
    property color inkColour: ink

    fragmentShader: "qrc:/qt/qml/Ferrolux/qml/shaders/transport.frag.qsb"
}
