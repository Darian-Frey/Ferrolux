// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// The meter display: five modes over one texture and one meter source, cycled
// by clicking (F-033). Each mode is a fragment shader reading the same N×1
// texture; switching hides one and shows another, which is why it cannot drop
// a frame or interrupt anything.
//
// Its own file rather than part of Main.qml because tests/frame_bench renders
// it offscreen at 3840x2160 to answer AV-002's resolution clause. A benchmark
// over a *copy* of the shaders would measure whatever the copy had drifted
// into, and would go on reporting a pass after the real display had changed.

import QtQuick
import Ferrolux

// A well like every other, rather than a rectangle that happens to be the same
// colour as one. It carried literal hex and a literal radius from before the
// token set existed — which meant a theme could change every surface on the
// panel except the one the meters sit in.
PanelSection {
    id: meterPanel
    recessed: true

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
        // A bound, not a look: it lets the shader dismiss an empty
        // pixel without sampling the texture. See BUG-016.
        property real ceiling: Meters.ceiling
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
    // Text rather than Controls' Label: this file is the only part of the
    // harness Phase 5 keeps, and a plain item is one less thing to unpick when
    // the panel replaces the surrounding window. It also lets frame_bench
    // render the display without instantiating a Controls style.
    Text {
        id: modeLabel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        color: Tokens.readoutDim
        font.family: Tokens.readoutText
        font.pixelSize: Tokens.sizeLegendSmall
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
