layout (std140) uniform FillEvaluatedPropsUBO {
    highp vec4 u_color;
    highp vec4 u_outline_color;
    highp float u_opacity;
    highp float u_fade;
    highp float u_from_scale;
    highp float u_to_scale;
};

in float v_width;
in vec2 v_normal;
in float v_gamma_scale;

in highp vec3 v_lod;

void main() {
    // Calculate the distance of the pixel from the line in pixels.
    float dist = length(v_normal) * v_width;

    // Calculate the antialiasing fade factor. This is either when fading in
    // the line in case of an offset line (v_width2.t) or when fading out
    // (v_width2.s)
    float blur2 = (1.0 / DEVICE_PIXEL_RATIO) * v_gamma_scale;
    float alpha = clamp(min(dist + blur2, v_width - dist) / blur2, 0.0, 1.0);

    fragColor = u_outline_color * (alpha * u_opacity);

    // the blend between two levels of detail (TilePyramid::lodBlend): a pixel of the other level is left
    // whole, colour and depth (the two levels' depths differ by a hair in a tilted view, and fought)
    if (lod_blend(v_lod.z, gl_FragCoord.xy, v_lod.xy) < 0.5) {
        discard;
    }

#ifdef OVERDRAW_INSPECTOR
    fragColor = vec4(1.0);
#endif
}
