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

out highp vec2 v_at;
out highp vec2 v_body;
out float v_alpha;

// A soft shadow on the ground under each model part, the size of its body and a margin.
void main() {
    int record = (int(u_drawable.x) + gl_InstanceID) * 3;
    vec4 pose = u_records[record];
    vec4 tint = u_records[record + 1];
    vec4 size = u_records[record + 2];
    vec2 body = size.zy;
    vec2 at = a_pos * (body + u_shadow.y);
    float c = pose.z;
    float s = pose.w;
    vec2 ground = pose.xy + vec2(-c * at.x + s * at.y, -s * at.x - c * at.y) * size.x;
    gl_Position = u_matrix * vec4(ground, 0.03, 1.0);
    v_at = at;
    v_body = body;
    v_alpha = tint.a * u_shadow.x;
}
