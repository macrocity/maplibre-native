#ifdef GL_ES
precision mediump float;
#else

#if !defined(lowp)
#define lowp
#endif

#if !defined(mediump)
#define mediump
#endif

#if !defined(highp)
#define highp
#endif

#endif

out highp vec4 fragColor;

// The blend between two levels of detail (TilePyramid::lodBlend): whether a fill or line fragment of a tile is drawn.
// A tile takes the pixels whose dither value (white noise per pixel) is below its share (`fade`, 1 for all of them); a parent drawn under its
// children (`fade` < 0, -1 less its children's shares, 6 bits per quadrant of its extent) takes the others, and none
// outside its own extent. The dither depends on the pixel alone, so every layer of a pixel takes the same level.
highp float lod_blend(highp float fade, highp vec2 fragCoord, highp vec2 tilePos) {
    if (fade >= 1.0) {
        return 1.0;
    }
    // white noise from the pixel's integer position (a hash): no rows or diagonals for the eye to pick out
    highp uint h = uint(fragCoord.x) * 1973u + uint(fragCoord.y) * 9277u;
    h = (h << 13u) ^ h;
    h = h * (h * h * 15731u + 789221u) + 1376312589u;
    highp float dither = float(h & 0xffffu) / 65536.0;
    if (fade >= 0.0) {
        return dither < fade ? 1.0 : 0.0;
    }
    if (tilePos.x < 0.0 || tilePos.y < 0.0 || tilePos.x >= 8192.0 || tilePos.y >= 8192.0) {
        return 0.0;
    }
    highp uint shares = uint(-fade - 1.0 + 0.5);
    highp uint quadrant = (tilePos.x >= 4096.0 ? 1u : 0u) + (tilePos.y >= 4096.0 ? 2u : 0u);
    highp float share = float((shares >> (6u * quadrant)) & 63u) / 63.0;
    return dither < share ? 0.0 : 1.0;
}

#ifdef PROJECTION_GLOBE
// The line fragments beyond the zoom 0 tile's X extent; see antimeridianClipX in the vertex prelude.
bool clippedAtAntimeridian(float tileX) {
    return tileX < 0.0 || tileX >= 8192.0;
}
#endif
