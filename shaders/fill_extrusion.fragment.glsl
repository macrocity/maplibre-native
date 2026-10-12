in vec4 v_color;

in highp vec3 v_lod;

void main() {
    // the blend between two levels of detail (TilePyramid::lodBlend): a building of the level this pixel does not
    // take writes neither colour nor depth
    if (lod_blend(v_lod.z, gl_FragCoord.xy, v_lod.xy) < 0.5) {
        discard;
    }
    fragColor = v_color;

#ifdef OVERDRAW_INSPECTOR
    fragColor = vec4(1.0);
#endif
}
