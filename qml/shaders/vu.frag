// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// One VU meter: face, scale, needle and peak lamp. Instantiated once per
// channel (F-032).
//
// Everything here is geometry evaluated per fragment — arcs, tick marks and the
// needle are distance fields, not drawn paths — which is what lets the same
// shader be correct at any size and any device pixel ratio without a single
// fixed pixel dimension (F-041, AV-005). It is also why the needle and the face
// share one antialiasing model, which F-032 asks for: both are resolved by
// smoothstep over one fragment's worth of the same distance function.
//
// The ballistics are not here. Deflection arrives already integrated, because
// D-005 puts the physical modelling on the CPU where it can be tested against
// IEC 60268-17 without rendering anything. This shader is told where the needle
// is; it never decides.

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float deflection;     // 1.0 is 0 VU; may exceed 1.0 on peaks
    float peakIndicator;  // separate faster indicator, 0..1
    float aspect;         // width / height, so the arc stays circular
    vec4 faceColour;
    vec4 inkColour;       // scale marks and legends, printed on the face
    vec4 needleColour;
    vec4 overColour;      // scale above 0 VU, and the peak lamp when lit
};

const float kPi = 3.14159265;

// Pivot sits below the visible face so the arc sweeps across the upper part,
// which is what gives a VU meter its shallow, wide travel.
const vec2 kPivot = vec2(0.5, 1.28);
const float kRadius = 0.96;

// Deflection 0 rests left of centre; 1.0 (0 VU) sits right of it, leaving room
// for the overshoot a correct ballistic produces.
const float kAngleAtRest = -0.86;   // radians from vertical
const float kAngleAtZeroVu = 0.34;

float angleFor(float value)
{
    return mix(kAngleAtRest, kAngleAtZeroVu, clamp(value, 0.0, 1.4));
}

// Distance from a point to a line segment, used as the needle's own field.
float segmentDistance(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main()
{
    // Work in a space where one unit is one unit in both axes, or the arc
    // becomes an ellipse the moment the meter is not square.
    vec2 uv = qt_TexCoord0;
    vec2 p = vec2((uv.x - 0.5) * aspect + 0.5, uv.y);
    vec2 pivot = vec2((kPivot.x - 0.5) * aspect + 0.5, kPivot.y);

    vec2 fromPivot = p - pivot;
    float radius = length(fromPivot);
    float angle = atan(fromPivot.x, -fromPivot.y); // 0 straight up

    float aa = fwidth(radius) * 1.5;
    vec4 colour = faceColour;

    // The scale arc.
    float arcRadius = kRadius * 0.86;
    float onArc = 1.0 - smoothstep(0.004, 0.004 + aa, abs(radius - arcRadius));
    float withinSweep = step(kAngleAtRest - 0.04, angle) * step(angle, angleFor(1.4) + 0.03);
    // Above 0 VU the arc changes colour, so a needle in that region reads as
    // over reference rather than as a meter that has broken against its stop.
    float over = step(angleFor(1.0), angle);
    colour = mix(colour, mix(inkColour, overColour, over), onArc * withinSweep * 0.85);

    // Tick marks, spaced along the sweep rather than at fixed pixel offsets.
    float sweep = (angle - kAngleAtRest) / (kAngleAtZeroVu - kAngleAtRest);
    if (withinSweep > 0.5 && sweep > -0.02) {
        float ticks = fract(sweep * 8.0);
        float tickWidth = fwidth(sweep * 8.0) * 1.2;
        float onTick = 1.0 - smoothstep(tickWidth, tickWidth * 2.2, min(ticks, 1.0 - ticks));
        float tickBand = (1.0 - smoothstep(arcRadius - 0.075, arcRadius - 0.070, radius))
                       * step(arcRadius - 0.075, radius)
                       * (1.0 - smoothstep(arcRadius, arcRadius + aa, radius));
        colour = mix(colour, mix(inkColour, overColour, over), onTick * tickBand * 0.85);
    }

    // The needle. A tapered segment from just above the pivot to the arc.
    float needleAngle = angleFor(deflection);
    vec2 tip = pivot + vec2(sin(needleAngle), -cos(needleAngle)) * kRadius;
    vec2 tail = pivot + vec2(sin(needleAngle), -cos(needleAngle)) * 0.10;
    float toNeedle = segmentDistance(p, tail, tip);

    // Tapers from root to tip, which is what a balanced pointer looks like.
    float along = clamp(dot(p - tail, tip - tail) / dot(tip - tail, tip - tail), 0.0, 1.0);
    float halfWidth = mix(0.011, 0.0022, along);
    float onNeedle = 1.0 - smoothstep(halfWidth, halfWidth + aa, toNeedle);
    colour = mix(colour, needleColour, onNeedle);

    // Hub.
    float onHub = 1.0 - smoothstep(0.05, 0.05 + aa, radius);
    colour = mix(colour, needleColour, onHub);

    // Peak lamp, upper right, driven by the faster indicator rather than by the
    // needle — it exists to show what the ballistic cannot follow.
    vec2 lampCentre = vec2((0.90 - 0.5) * aspect + 0.5, 0.16);
    float lampRadius = 0.045;
    float onLamp = 1.0 - smoothstep(lampRadius, lampRadius + aa, length(p - lampCentre));
    // Lights from about -4 dBFS on the -60 dB peak scale. A peak lamp means
    // close to clipping; one that lights on ordinary programme says nothing.
    float lit = smoothstep(0.93, 0.98, peakIndicator);
    colour = mix(colour, mix(faceColour * 1.25, overColour, lit), onLamp);

    fragColor = colour * qt_Opacity;
}
