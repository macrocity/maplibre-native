// Generated code, do not modify this file!
#pragma once
#include <mln/shaders/shader_source.hpp>

namespace mln {
namespace shaders {

template <>
struct ShaderSource<BuiltIn::VehicleModelLabelShader, gfx::Backend::Type::OpenGL> {
    static constexpr const char* name = "VehicleModelLabelShader";
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

layout(location = 0) in vec2 a_pos;

out vec2 v_uv;
out float v_alpha;
out float v_shade;

// A line-name pill over a roof, flat to the screen and of a fixed size in points; or, when the drawable says so
// (u_drawable.y), a licence plate flat on a body's face, in metres, lit like the body (macrocity/app#935).
void main() {
    int record = (int(u_drawable.x) + gl_InstanceID) * 3;
    vec4 anchor = u_records[record];
    vec4 uv = u_records[record + 1];
    vec4 size = u_records[record + 2];
    if (u_drawable.y > 0.5) {
        // anchor: the plate's middle on the ground, and the heading times the scale; size: the middle's height, the
        // half width (negative on a face behind), the half height, the opacity.
        float x = (a_pos.x * 2.0 - 1.0) * size.y;
        vec2 ground = anchor.xy - anchor.zw * x;
        gl_Position = u_matrix * vec4(ground, size.x + (a_pos.y * 2.0 - 1.0) * size.z, 1.0);
        v_uv = vec2(mix(uv.x, uv.z, a_pos.x), mix(uv.w, uv.y, a_pos.y));
        v_alpha = size.w;
        vec2 ahead = normalize(vec2(anchor.w, anchor.z)) * sign(size.y);
        float lit = u_shade.x + u_shade.y * max(dot(vec3(ahead, 0.0), u_light.xyz), 0.0) + u_shade.w * 0.5;
        v_shade = min(mix(lit, 1.0, 0.5), 1.0);
        return;
    }
    vec4 clip = u_matrix * vec4(anchor.xyz, 1.0);
    vec2 points = vec2((a_pos.x * 2.0 - 1.0) * size.x, size.z + (1.0 - a_pos.y) * size.y);
    clip.xy += points * 2.0 / u_viewport.xy * clip.w;
    gl_Position = clip;
    v_uv = mix(uv.xy, uv.zw, a_pos);
    v_alpha = anchor.w;
    v_shade = 1.0;
}
)";
    static constexpr const char* fragment = R"(uniform sampler2D u_image;

in vec2 v_uv;
in float v_alpha;
in float v_shade;

void main() {
    vec4 color = texture(u_image, v_uv);
    fragColor = vec4(color.rgb * v_shade, color.a) * v_alpha;
}
)";
};

} // namespace shaders
} // namespace mln
