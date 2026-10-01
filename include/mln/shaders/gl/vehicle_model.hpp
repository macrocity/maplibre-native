// Generated code, do not modify this file!
#pragma once
#include <mln/shaders/shader_source.hpp>

namespace mln {
namespace shaders {

template <>
struct ShaderSource<BuiltIn::VehicleModelShader, gfx::Backend::Type::OpenGL> {
    static constexpr const char* name = "VehicleModelShader";
    static constexpr const char* vertex = R"(layout (std140) uniform VehicleModelPropsUBO {
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

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_normal;
layout(location = 2) in vec4 a_color;

invariant gl_Position;
out vec4 v_color;

// One model part: its record holds where it stands (world points from the layer's origin), its heading, its tint and
// opacity, and its scale (world points a metre). The model is in metres, +X left, +Y up, +Z ahead.
void main() {
    int record = (int(u_drawable.x) + gl_InstanceID) * 3;
    vec4 pose = u_records[record];
    vec4 tint = u_records[record + 1];
    vec4 size = u_records[record + 2];
    float c = pose.z;
    float s = pose.w;
    vec2 ground = pose.xy + vec2(-c * a_pos.x + s * a_pos.z, -s * a_pos.x - c * a_pos.z) * size.x;
    gl_Position = u_matrix * vec4(ground, a_pos.y, 1.0);

    // East, north and up on the ground.
    vec3 n = a_normal.x * vec3(-c, s, 0.0) + a_normal.y * vec3(0.0, 0.0, 1.0) + a_normal.z * vec3(s, c, 0.0);
    float flags = a_normal.w;
    vec3 base = flags > 0.5 && flags < 1.5 ? tint.rgb : a_color.rgb;
    float direct = max(dot(n, u_light.xyz), 0.0);
    float sky = 0.5 + 0.5 * n.z;
    vec3 lit = base * (u_shade.x + u_shade.y * direct + u_shade.w * sky);
    if (flags > 1.5) {
        lit = base * u_shade.z + 0.15;
    }
    v_color = vec4(min(lit, vec3(1.0)) * tint.a, tint.a);
}
)";
    static constexpr const char* fragment = R"(in vec4 v_color;

void main() {
    fragColor = v_color;
}
)";
};

} // namespace shaders
} // namespace mln
