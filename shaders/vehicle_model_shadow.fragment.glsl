layout (std140) uniform VehicleModelPropsUBO {
    highp mat4 u_matrix;
    highp vec4 u_light;
    highp vec4 u_shade;
    highp vec4 u_shadow;
    highp vec4 u_viewport;
};

in highp vec2 v_at;
in highp vec2 v_body;
in float v_alpha;

void main() {
    highp float d = length(max(abs(v_at) - (v_body - 0.3), 0.0));
    fragColor = vec4(0.0, 0.0, 0.0, v_alpha * (1.0 - smoothstep(0.0, u_shadow.y + 0.3, d)));
}
