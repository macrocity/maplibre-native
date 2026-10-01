#pragma once

#include <mln/shaders/vehicle_model_layer_ubo.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/mtl/shader_program.hpp>

namespace mln {
namespace shaders {

constexpr auto vehicleModelShaderPrelude = R"(

enum {
    idVehicleModelPropsUBO = drawableReservedUBOCount,
    idVehicleModelInstancesUBO,
    idVehicleModelDrawableUBO,
    vehicleModelUBOCount
};

struct alignas(16) VehicleModelPropsUBO {
    /*   0 */ float4x4 matrix;
    /*  64 */ float4 light;
    /*  80 */ float4 shade;
    /*  96 */ float4 shadow;
    /* 112 */ float4 viewport;
    /* 128 */
};
static_assert(sizeof(VehicleModelPropsUBO) == 8 * 16, "wrong size");

struct alignas(16) VehicleModelDrawableUBO {
    /*  0 */ float base;
    /*  4 */ float pad1;
    /*  8 */ float pad2;
    /* 12 */ float pad3;
    /* 16 */
};
static_assert(sizeof(VehicleModelDrawableUBO) == 16, "wrong size");

// Where a model part stands: world points from the layer's origin, turned by its heading, a metre `size.x` points.
inline float2 vehicleModelGround(const float4 pose, const float4 size, const float2 local) {
    return pose.xy + float2(-pose.z * local.x + pose.w * local.y, -pose.w * local.x - pose.z * local.y) * size.x;
}

)";

template <>
struct ShaderSource<BuiltIn::VehicleModelShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "VehicleModelShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 3> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static constexpr std::array<TextureInfo, 0> textures{};

    static constexpr auto prelude = vehicleModelShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    float3 position [[attribute(0)]];
    float4 normal [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
    half4 color;
};

// One model part (metres, +X left, +Y up, +Z ahead), lit like the map's 3D buildings.
FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                uint instance [[instance_id]],
                                device const VehicleModelPropsUBO& props [[buffer(idVehicleModelPropsUBO)]],
                                device const float4* records [[buffer(idVehicleModelInstancesUBO)]],
                                device const VehicleModelDrawableUBO& drawable [[buffer(idVehicleModelDrawableUBO)]]) {
    const uint record = (uint(drawable.base) + instance) * 3;
    const float4 pose = records[record];
    const float4 tint = records[record + 1];
    const float4 size = records[record + 2];
    const float2 ground = vehicleModelGround(pose, size, vertx.position.xz);

    const float c = pose.z;
    const float s = pose.w;
    const float3 n = vertx.normal.x * float3(-c, s, 0.0) + vertx.normal.y * float3(0.0, 0.0, 1.0) + vertx.normal.z * float3(s, c, 0.0);
    const float flags = vertx.normal.w;
    const float3 base = flags > 0.5 && flags < 1.5 ? tint.rgb : vertx.color.rgb;
    const float direct = max(dot(n, props.light.xyz), 0.0);
    const float sky = 0.5 + 0.5 * n.z;
    float3 lit = base * (props.shade.x + props.shade.y * direct + props.shade.w * sky);
    if (flags > 1.5) {
        lit = base * props.shade.z + 0.15;
    }

    return {
        .position = props.matrix * float4(ground, vertx.position.y, 1.0),
        .color = half4(float4(min(lit, float3(1.0)) * tint.a, tint.a)),
    };
}

half4 fragment fragmentMain(FragmentStage in [[stage_in]]) {
    return in.color;
}
)";
};

template <>
struct ShaderSource<BuiltIn::VehicleModelShadowShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "VehicleModelShadowShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static constexpr std::array<TextureInfo, 0> textures{};

    static constexpr auto prelude = vehicleModelShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    float2 position [[attribute(0)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
    float2 at;
    float2 body;
    float alpha;
};

// A soft shadow on the ground under each model part, the size of its body and a margin.
FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                uint instance [[instance_id]],
                                device const VehicleModelPropsUBO& props [[buffer(idVehicleModelPropsUBO)]],
                                device const float4* records [[buffer(idVehicleModelInstancesUBO)]],
                                device const VehicleModelDrawableUBO& drawable [[buffer(idVehicleModelDrawableUBO)]]) {
    const uint record = (uint(drawable.base) + instance) * 3;
    const float4 pose = records[record];
    const float4 tint = records[record + 1];
    const float4 size = records[record + 2];
    const float2 body = size.zy;
    const float2 at = vertx.position * (body + props.shadow.y);
    return {
        .position = props.matrix * float4(vehicleModelGround(pose, size, at), 0.03, 1.0),
        .at = at,
        .body = body,
        .alpha = tint.a * props.shadow.x,
    };
}

half4 fragment fragmentMain(FragmentStage in [[stage_in]],
                            device const VehicleModelPropsUBO& props [[buffer(idVehicleModelPropsUBO)]]) {
    const float d = length(max(abs(in.at) - (in.body - 0.3), 0.0));
    return half4(0.0, 0.0, 0.0, in.alpha * (1.0 - smoothstep(0.0, props.shadow.y + 0.3, d)));
}
)";
};

template <>
struct ShaderSource<BuiltIn::VehicleModelLabelShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "VehicleModelLabelShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 1> textures;

    static constexpr auto prelude = vehicleModelShaderPrelude;
    static constexpr auto source = R"(

struct VertexStage {
    float2 position [[attribute(0)]];
};

struct FragmentStage {
    float4 position [[position, invariant]];
    float2 uv;
    float alpha;
};

// A line-name pill over a roof, flat to the screen and of a fixed size in points.
FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                uint instance [[instance_id]],
                                device const VehicleModelPropsUBO& props [[buffer(idVehicleModelPropsUBO)]],
                                device const float4* records [[buffer(idVehicleModelInstancesUBO)]],
                                device const VehicleModelDrawableUBO& drawable [[buffer(idVehicleModelDrawableUBO)]]) {
    const uint record = (uint(drawable.base) + instance) * 3;
    const float4 anchor = records[record];
    const float4 uv = records[record + 1];
    const float4 size = records[record + 2];
    float4 clip = props.matrix * float4(anchor.xyz, 1.0);
    const float2 points = float2((vertx.position.x * 2.0 - 1.0) * size.x, size.z + (1.0 - vertx.position.y) * size.y);
    clip.xy += points * 2.0 / props.viewport.xy * clip.w;
    return {
        .position = clip,
        .uv = mix(uv.xy, uv.zw, vertx.position),
        .alpha = anchor.w,
    };
}

half4 fragment fragmentMain(FragmentStage in [[stage_in]],
                            texture2d<float, access::sample> image [[texture(0)]]) {
    constexpr sampler linearSampler(coord::normalized, filter::linear, address::clamp_to_edge);
    return half4(image.sample(linearSampler, in.uv) * in.alpha);
}
)";
};

} // namespace shaders
} // namespace mln
