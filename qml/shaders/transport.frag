// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shane Hartley
//
// Transport symbols, drawn rather than set.
//
// A tape deck silkscreens these five marks on the chassis, so by SPEC.md's rule
// they are legends and belong in `ink` — but they cannot be *typed*. The legend
// face carries none of them: IBM Plex Sans Condensed has no U+25B6, U+25C0,
// U+25A0, U+23F8 or U+23EE, and SPEC.md §Typography is explicit that no face
// outside its table of four appears on the control surface. An icon font would
// be a fifth face, and a PNG would be the founding defect of the project.
//
// D-003 already answers this: every element of the control surface is vector
// geometry or a fragment shader. These are half-plane intersections, which is a
// convex polygon expressed as arithmetic — exact at any size, antialiased from
// the screen-space derivative rather than from a fixed edge width, and costing
// one small quad each.
//
// Distances are signed and negative inside, so the five marks compose by taking
// the minimum of their parts: a skip mark is a triangle and a bar, and nothing
// has to know that it is two shapes rather than one.

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float glyph;   // 0 play, 1 pause, 2 stop, 3 previous, 4 next,
                   // 5 roll up, 6 roll down
    float aspect;  // width / height, so the marks stay square in a wide button
    vec4 inkColour;
};

// A right-pointing triangle: apex at centre.x + halfWidth,  base spanning the height.
// Three outward half-planes, intersected. Exact inside and along the edges,
// which is everywhere the antialiasing looks.
float triangleRight(vec2 p, vec2 centre, float halfWidth, float rise)
{
    vec2 apex = vec2(centre.x + halfWidth, centre.y);
    vec2 top = vec2(centre.x - halfWidth, centre.y - rise);
    vec2 bottom = vec2(centre.x - halfWidth, centre.y + rise);

    vec2 upper = normalize(vec2(rise, -(2.0 * halfWidth)));
    vec2 lower = normalize(vec2(rise, 2.0 * halfWidth));

    float a = dot(p - top, upper);
    float b = dot(p - bottom, lower);
    float c = (centre.x - halfWidth) - p.x;
    return max(max(a, b), c);
}

float triangleLeft(vec2 p, vec2 centre, float halfWidth, float rise)
{
    return triangleRight(vec2(2.0 * centre.x - p.x, p.y), centre, halfWidth, rise);
}

// The same triangle with the axes exchanged. Turning the coordinate rather than
// the shape means there is one triangle in this file and four directions of it,
// so a change to how a point is antialiased cannot apply to some of them only.
float triangleUp(vec2 p, vec2 centre, float halfWidth, float rise)
{
    return triangleRight(vec2(-p.y, p.x), vec2(-centre.y, centre.x), halfWidth, rise);
}

float triangleDown(vec2 p, vec2 centre, float halfWidth, float rise)
{
    return triangleRight(vec2(p.y, p.x), vec2(centre.y, centre.x), halfWidth, rise);
}

float box(vec2 p, vec2 centre, vec2 halfExtent)
{
    vec2 d = abs(p - centre) - halfExtent;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

void main()
{
    // Work in a square space centred on the origin, so a mark keeps its
    // proportions in a button of any shape. The button is wider than it is
    // tall, so the horizontal axis is the one that stretches.
    vec2 p = (qt_TexCoord0 - 0.5) * vec2(aspect, 1.0);

    int which = int(glyph + 0.5);
    float d;

    if (which == 0) {
        // Play. Slightly taller than wide, as the mark is drawn on a deck.
        d = triangleRight(p, vec2(0.0), 0.17, 0.20);
    } else if (which == 1) {
        // Pause. Two bars with a gap of about their own width.
        d = min(box(p, vec2(-0.10, 0.0), vec2(0.062, 0.20)),
                box(p, vec2( 0.10, 0.0), vec2(0.062, 0.20)));
    } else if (which == 2) {
        // Stop.
        d = box(p, vec2(0.0), vec2(0.17, 0.17));
    } else if (which == 3) {
        // Previous: a bar the head returns to, then the triangle.
        d = min(box(p, vec2(-0.20, 0.0), vec2(0.045, 0.19)),
                triangleLeft(p, vec2(0.04, 0.0), 0.15, 0.19));
    } else if (which == 4) {
        // Next, the same mark reflected.
        d = min(box(p, vec2(0.20, 0.0), vec2(0.045, 0.19)),
                triangleRight(p, vec2(-0.04, 0.0), 0.15, 0.19));
    } else if (which == 5) {
        // Roll up: the panel folds away to a strip. The bar is the strip that
        // will be left, and the triangle is the direction the rest goes.
        d = min(triangleUp(p, vec2(0.0, -0.06), 0.16, 0.20),
                box(p, vec2(0.0, 0.19), vec2(0.20, 0.045)));
    } else {
        // Roll down, the same mark inverted.
        d = min(triangleDown(p, vec2(0.0, 0.06), 0.16, 0.20),
                box(p, vec2(0.0, -0.19), vec2(0.20, 0.045)));
    }

    // Antialias from the derivative rather than from a constant: the edge is
    // then one pixel wide at every scale and device pixel ratio, which is the
    // whole of AV-005 in one line.
    float edge = fwidth(d) * 0.8;
    float coverage = 1.0 - smoothstep(-edge, edge, d);

    fragColor = vec4(inkColour.rgb, 1.0) * inkColour.a * coverage * qt_Opacity;
}
