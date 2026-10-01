layout (std140) uniform VehicleModelPropsUBO {
    highp mat4 u_matrix;
    highp vec4 u_light;
    highp vec4 u_shade;
    highp vec4 u_shadow;
    highp vec4 u_viewport;
};

layout (std140) uniform VehicleModelInstancesUBO {
    highp vec4 u_records[768];
};

layout (std140) uniform VehicleModelDrawableUBO {
    highp vec4 u_drawable;
};

layout(location = 0) in vec2 a_pos;

out vec2 v_uv;
out float v_alpha;

// A line-name pill over a roof, flat to the screen and of a fixed size in points.
void main() {
    int record = (int(u_drawable.x) + gl_InstanceID) * 3;
    vec4 anchor = u_records[record];
    vec4 uv = u_records[record + 1];
    vec4 size = u_records[record + 2];
    vec4 clip = u_matrix * vec4(anchor.xyz, 1.0);
    vec2 points = vec2((a_pos.x * 2.0 - 1.0) * size.x, size.z + (1.0 - a_pos.y) * size.y);
    clip.xy += points * 2.0 / u_viewport.xy * clip.w;
    gl_Position = clip;
    v_uv = mix(uv.xy, uv.zw, a_pos);
    v_alpha = anchor.w;
}
