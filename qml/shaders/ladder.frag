// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Segmented peak ladder, one row per channel.
//
// SPEC.md §Meters is explicit that there is no interpolation between segments.
// A segment is lit or it is not — that discreteness is the whole character of
// an LED ladder, and smoothing it into a continuous bar would produce the
// spectrum display with fewer bands. The only antialiasing here is on the
// *edges* of each segment, never on its brightness.

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float levelLeft;
    float levelRight;
    float peakLeft;
    float peakRight;
    float segments;
    float overFrom;     // fraction of the ladder above which segments read hot
    vec4 offColour;     // an unlit segment is visible, not absent
    vec4 onColour;
    vec4 overOnColour;
    vec4 capColour;
} ;

void main()
{
    vec2 uv = qt_TexCoord0;

    // Two rows with a gap between them.
    float row = step(0.5, uv.y);
    float withinRow = uv.y < 0.5 ? uv.y / 0.5 : (uv.y - 0.5) / 0.5;
    float level = row < 0.5 ? levelLeft : levelRight;
    float peak = row < 0.5 ? peakLeft : peakRight;

    // Vertical gap between the two ladders, and a margin top and bottom.
    float rowAA = fwidth(withinRow);
    float inRow = smoothstep(0.18 - rowAA, 0.18 + rowAA, withinRow)
                * (1.0 - smoothstep(0.82 - rowAA, 0.82 + rowAA, withinRow));

    float position = uv.x * segments;
    float index = floor(position);
    float within = fract(position);

    // Segment edges, antialiased. The gap is a fixed fraction of a segment, so
    // it scales with the ladder rather than being a pixel count.
    float gap = 0.22;
    float xAA = fwidth(within);
    float inSegment = smoothstep(gap * 0.5 - xAA, gap * 0.5 + xAA, within)
                    * (1.0 - smoothstep(1.0 - gap * 0.5 - xAA, 1.0 - gap * 0.5 + xAA, within));

    // Lit or not — a hard comparison on the segment's own threshold, with no
    // partial brightness for a segment the level has only half reached.
    float threshold = index / segments;
    float lit = step(threshold, level - 1.0 / segments * 0.5);
    float hot = step(overFrom, threshold);

    vec4 colour = offColour;
    colour = mix(colour, mix(onColour, overOnColour, hot), lit);

    // The peak-hold segment, held above the lit run.
    float peakIndex = floor(peak * segments);
    float isPeakSegment = step(abs(index - peakIndex), 0.5) * step(1.0 / segments, peak);
    colour = mix(colour, capColour, isPeakSegment);

    fragColor = colour * inSegment * inRow * qt_Opacity;
}
