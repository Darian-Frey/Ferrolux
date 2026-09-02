// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Spectrum rendered as flame (F-035).
//
// The same band data as the bar display, read as a continuous curve rather than
// as discrete bars. This is the one mode that spends what D-004 bought: the
// texture is sampled *between* band centres and linear filtering interpolates
// for free, so the silhouette is smooth at any width without the CPU
// resampling anything. The bar display deliberately samples only at centres —
// bars want their own flat value — which is why both modes read one texture.
//
// Depth comes from ranks, not from a gradient. The same silhouette is drawn
// several times: the rearmost tallest and palest, the frontmost shortest and
// darkest, each shifted slightly sideways. Drawing back to front means the near
// ranks occlude the far ones, which is what reads as depth — a single shape
// with a vertical gradient reads as a shape with a gradient on it.

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float bandCount;
    float ranks;        // how many receding copies
    float frontHeight;  // height of the nearest rank, as a fraction
    float backHeight;   // height of the furthest
    float parallax;     // sideways drift from front to back, in band widths
    float softness;     // horizontal blur, in band widths
    vec4 frontColour;   // nearest: darker
    vec4 backColour;    // furthest: lighter
};

layout(binding = 1) uniform sampler2D source;

const int kMaxRanks = 16;

float magnitudeAt(float u)
{
    vec4 texel = texture(source, vec2(clamp(u, 0.0, 1.0), 0.5));
    return dot(texel.rg, vec2(65280.0, 255.0)) / 65535.0;
}

// A few taps either side, so neighbouring bands bleed into one another the way
// adjacent tongues of flame do, and the silhouette loses the sawtooth a bare
// interpolation between two texel centres would leave.
float smoothedAt(float u)
{
    float step = softness / bandCount;
    float sum = magnitudeAt(u) * 0.4;
    sum += (magnitudeAt(u - step) + magnitudeAt(u + step)) * 0.22;
    sum += (magnitudeAt(u - step * 2.0) + magnitudeAt(u + step * 2.0)) * 0.08;
    return sum;
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float height = 1.0 - uv.y;

    int count = int(clamp(ranks, 1.0, float(kMaxRanks)));
    vec4 colour = vec4(0.0);

    // Back to front. Each rank overwrites where it covers, so the near ones
    // occlude the far ones and the far ones show only above them.
    for (int i = kMaxRanks - 1; i >= 0; --i) {
        if (i >= count)
            continue;

        // 1 at the back, 0 at the front.
        float depth = count > 1 ? float(i) / float(count - 1) : 0.0;

        float scale = mix(frontHeight, backHeight, depth);
        float drift = (depth - 0.5) * parallax / bandCount;
        float top = max(smoothedAt(uv.x + drift) * scale, 0.0005);

        // Antialias the crest only. Every rank shares the same edge treatment,
        // so no rank looks cut out against its neighbour.
        float edgeAA = fwidth(height) * 1.2;
        float inside = 1.0 - smoothstep(top - edgeAA, top + edgeAA, height);
        if (inside <= 0.0)
            continue;

        vec4 rank = mix(frontColour, backColour, depth);

        // The foot of each rank is hotter than its crest, which is what keeps
        // the near ranks from reading as flat cut-outs.
        float withinRank = clamp(height / top, 0.0, 1.0);
        rank.rgb *= mix(1.14, 0.94, withinRank);

        colour = mix(colour, vec4(rank.rgb, 1.0), inside * rank.a);
    }

    fragColor = colour * qt_Opacity;
}
