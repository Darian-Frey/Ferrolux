// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Spectrum bars, and the mirrored variant, from the MeterTexture.
//
// The frequency mapping is already logarithmic before it arrives: MeterSource
// buckets 512 linearly spaced analysis bins into 24 or 48 logarithmically
// spaced display bands, so sampling evenly across the texture is a logarithmic
// sweep. That bucketing is on the CPU because it needs the maximum of a bin
// range, which a texture fetch cannot express; what remains here is per-fragment
// work, which is what D-004 wanted the GPU for.
//
// Bar edges are resolved with smoothstep over one fragment's worth of the
// height coordinate rather than a hard comparison. That is the sub-pixel
// antialiasing the texture path was chosen to make free, and it is why the bars
// stay clean at any device pixel ratio (F-041, AV-005).

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float bandCount;
    float gap;           // fraction of a band's width left blank, 0..1
    float capThickness;  // peak-hold cap height, normalised
    float mirrored;      // 0 upright, 1 reflected about the centre line
    vec4 barColour;
    vec4 barColourLow;   // colour at the bottom of a bar, blended upward
    vec4 capColour;
};

layout(binding = 1) uniform sampler2D source;

// SPEC.md §Meters: magnitude is packed across two 8-bit channels, high byte in
// red. Recombining is linear in both channels, so bilinear filtering between
// texels still yields the correct intermediate value rather than tearing at
// byte boundaries — which is what makes this packing safe to filter.
float magnitudeAt(float u)
{
    vec4 texel = texture(source, vec2(u, 0.5));
    return dot(texel.rg, vec2(65280.0, 255.0)) / 65535.0;
}

float peakAt(float u)
{
    return texture(source, vec2(u, 0.5)).b;
}

void main()
{
    vec2 uv = qt_TexCoord0;

    // Height from the baseline. Mirrored mode grows both ways from the centre,
    // so the same bar logic serves both without a second shader.
    float height = mirrored > 0.5 ? abs(uv.y - 0.5) * 2.0 : 1.0 - uv.y;

    float position = uv.x * bandCount;
    float band = floor(position);
    float within = fract(position);

    // Sample the band's own texel centre, so a bar is one flat value rather
    // than a smear of its neighbours.
    float u = (band + 0.5) / bandCount;
    float magnitude = magnitudeAt(u);
    float peak = peakAt(u);

    // One fragment's worth of each coordinate, for the antialiased edges.
    float heightAA = fwidth(height);
    float widthAA = fwidth(within);

    // Vertical extent of the bar.
    float bar = 1.0 - smoothstep(magnitude - heightAA, magnitude + heightAA, height);

    // Horizontal extent, leaving a gap between neighbours.
    float halfGap = gap * 0.5;
    float edge = min(smoothstep(halfGap - widthAA, halfGap + widthAA, within),
                     smoothstep(halfGap - widthAA, halfGap + widthAA, 1.0 - within));
    bar *= edge;

    // The peak-hold cap, a thin line floating above the bar. A cap resting at
    // the floor is not lit: on a real meter nothing marks silence, and drawing
    // it leaves a row of marks along the baseline that read as signal.
    float capDistance = abs(height - peak);
    float capLit = smoothstep(0.0, capThickness, peak);
    float cap = (1.0 - smoothstep(capThickness * 0.5 - heightAA,
                                  capThickness * 0.5 + heightAA, capDistance)) * edge * capLit;

    // Bars are graded from bottom to top, which is what gives a drawn bar the
    // depth of a lit one rather than the flatness of a filled rectangle.
    vec4 body = mix(barColourLow, barColour, clamp(height / max(magnitude, 0.001), 0.0, 1.0));

    vec4 colour = body * bar;
    colour = mix(colour, capColour, cap * capColour.a);

    fragColor = colour * qt_Opacity;
}
