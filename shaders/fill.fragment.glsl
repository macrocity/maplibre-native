layout (std140) uniform FillEvaluatedPropsUBO {
    highp vec4 u_color;
    highp vec4 u_outline_color;
    highp float u_opacity;
    highp float u_fade;
    highp float u_from_scale;
    highp float u_to_scale;
};

#pragma mapbox: define highp vec4 color
#pragma mapbox: define lowp float opacity

in highp vec3 v_lod;

void main() {
    #pragma mapbox: initialize highp vec4 color
    #pragma mapbox: initialize lowp float opacity

    fragColor = color * opacity;

    // the blend between two levels of detail (TilePyramid::lodBlend): a pixel of the other level is left
    // whole, colour and depth (the two levels' depths differ by a hair in a tilted view, and fought)
    if (lod_blend(v_lod.z, gl_FragCoord.xy, v_lod.xy) < 0.5) {
        discard;
    }

#ifdef OVERDRAW_INSPECTOR
    fragColor = vec4(1.0);
#endif
}
