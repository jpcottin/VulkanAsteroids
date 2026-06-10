#version 450

layout(push_constant) uniform PC {
    vec4 mtx;
    vec2 trans;
    vec2 style;  // x: FillStyle (0 = flat, 1 = rock), y: per-draw noise seed
    vec4 color;
} pc;

layout(location = 0) in vec2 vLocal;
layout(location = 0) out vec4 outColor;

// Cheap 2D hash / value noise / 3-octave fbm — no textures, fully procedural.
float hash2(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.45);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i),                  hash2(i + vec2(1.0, 0.0)), u.x),
               mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.55;
    for (int i = 0; i < 3; i++) {
        v += amp * vnoise(p);
        p = p * 2.13 + 17.7;
        amp *= 0.5;
    }
    return v;
}

void main() {
    vec4 col = pc.color;

    if (pc.style.x > 0.5) {
        float seed = pc.style.y;

        // Rocky surface: fbm offset by the per-asteroid seed so no two rocks match.
        float n = fbm(vLocal * 3.2 + seed * 19.13);
        float shade = 0.70 + 0.55 * n;

        // Three crater pockets at hash-derived positions, each with a lit rim.
        for (int i = 0; i < 3; i++) {
            float fi = float(i) + 1.0;
            vec2 cp = vec2(hash2(vec2(seed, fi)), hash2(vec2(fi, seed + 3.7))) * 1.3 - 0.65;
            float cr = 0.14 + 0.16 * hash2(vec2(seed + fi, fi));
            float d = length(vLocal - cp);
            shade *= 1.0 - 0.40 * (1.0 - smoothstep(cr * 0.45, cr, d));
            float rim = smoothstep(cr * 0.85, cr, d) * (1.0 - smoothstep(cr, cr * 1.25, d));
            shade += 0.10 * rim;
        }

        // Fake sphericity: darken toward the silhouette, biased so the
        // upper-left (shape-local) stays lit. Light rotates with the rock,
        // which reads as tumbling terrain.
        float r = length(vLocal);
        float ndl = dot(vLocal / max(r, 1e-4), normalize(vec2(-0.55, -0.84)));
        shade *= 1.0 - 0.38 * smoothstep(0.35, 1.0, r) * (1.0 - 0.6 * max(ndl, 0.0));
        shade += 0.10 * max(ndl, 0.0) * (1.0 - r * 0.5);

        col.rgb *= shade;
    }

    outColor = col;
}
