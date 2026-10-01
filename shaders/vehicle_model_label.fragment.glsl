uniform sampler2D u_image;

in vec2 v_uv;
in float v_alpha;

void main() {
    fragColor = texture(u_image, v_uv) * v_alpha;
}
