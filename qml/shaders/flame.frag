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
    float ceiling;      // the frame's tallest band, 0 to 1
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

    // Loop-invariant: height varies across the screen but not across ranks, and
    // a derivative is not free. Computing it once per pixel rather than once per
    // rank is nine times fewer of them.
    float edgeAA = fwidth(height) * 1.2;

    // One comparison decides most of the panel. No silhouette can rise above
    // the frame's tallest band scaled by the tallest rank — the smoothing
    // weights sum to one and no band exceeds the ceiling — so a pixel above
    // that is empty, and the forty-five texture taps that would have proved it
    // rank by rank are not spent. This is what makes quiet passages cheap, and
    // quiet is the expensive case here: a tall silhouette lets a low pixel stop
    // at the first rank it is inside, whereas near silence nothing stops
    // anything. See BUG-016.
    if (height > ceiling * backHeight + edgeAA) {
        fragColor = vec4(0.0);
        return;
    }

    // Front to back, accumulating *under* what is already there — the near
    // ranks are written first and the far ones show only where the near ones
    // left the pixel unpainted. Identical output to painting back to front and
    // overwriting, but this order can stop early, and back-to-front cannot: it
    // has to draw every rank before it knows which ones were hidden.
    vec4 colour = vec4(0.0);

    for (int i = 0; i < count; ++i) {
        // 0 at the front, 1 at the back.
        float depth = count > 1 ? float(i) / float(count - 1) : 0.0;
        float scale = mix(frontHeight, backHeight, depth);

        // The same bound per rank, now against this rank's own scale rather
        // than the tallest. A pixel above it is not covered by this rank
        // whatever the spectrum is doing, so the five taps that would have
        // decided so are skipped — and the ranks are ordered front to back, so
        // this excludes the short near ones first.
        if (height > ceiling * scale + edgeAA)
            continue;

        float drift = (depth - 0.5) * parallax / bandCount;
        float top = max(smoothedAt(uv.x + drift) * scale, 0.0005);

        // Antialias the crest only. Every rank shares the same edge treatment,
        // so no rank looks cut out against its neighbour.
        float inside = 1.0 - smoothstep(top - edgeAA, top + edgeAA, height);
        if (inside <= 0.0)
            continue;

        vec4 rank = mix(frontColour, backColour, depth);

        // The foot of each rank is hotter than its crest, which is what keeps
        // the near ranks from reading as flat cut-outs.
        float withinRank = clamp(height / top, 0.0, 1.0);
        rank.rgb *= mix(1.14, 0.94, withinRank);

        float alpha = inside * rank.a;
        colour.rgb += (1.0 - colour.a) * rank.rgb * alpha;
        colour.a += (1.0 - colour.a) * alpha;

        // Opaque already: everything behind this is hidden, and the remaining
        // ranks would each cost five texture taps to contribute nothing. Below
        // the nearest crest that is true on the first iteration.
        if (colour.a >= 0.995)
            break;
    }

    fragColor = colour * qt_Opacity;
}
