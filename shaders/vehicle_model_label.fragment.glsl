uniform sampler2D u_image;

in vec2 v_uv;
in float v_alpha;
in float v_shade;

void main() {
    vec4 color = texture(u_image, v_uv);
    fragColor = vec4(color.rgb * v_shade, color.a) * v_alpha;
}
