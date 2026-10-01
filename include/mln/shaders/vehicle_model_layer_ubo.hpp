#pragma once

#include <mln/shaders/layer_ubo.hpp>

namespace mln {
namespace shaders {

/// The vehicle model layer's own uniforms: the camera, the light and the shadow, the same for every part.
struct alignas(16) VehicleModelPropsUBO {
    /// The map's near-clipped projection (the one its 3D buildings use), moved to the layer's origin: world points
    /// from the origin in x and y, metres in z.
    /*   0 */ std::array<float, 4 * 4> matrix;
    /// The direction to the light, east, north and up.
    /*  64 */ std::array<float, 4> light;
    /// Ambient, direct and sky light, and the brightness of a lit lamp.
    /*  80 */ std::array<float, 4> shade;
    /// The shadow's strength and the margin it reaches past the body, in metres.
    /*  96 */ std::array<float, 4> shadow;
    /// The view's width and height in points.
    /* 112 */ std::array<float, 4> viewport;
    /* 128 */
};
static_assert(sizeof(VehicleModelPropsUBO) == 8 * 16);

/// A drawable's first record in the instances.
struct alignas(16) VehicleModelDrawableUBO {
    /*  0 */ float base;
    /*  4 */ float pad1;
    /*  8 */ float pad2;
    /* 12 */ float pad3;
    /* 16 */
};
static_assert(sizeof(VehicleModelDrawableUBO) == 16);

/// Three vec4 a record: a model part (place, heading; tint, opacity; scale, half length, half width) or a pill
/// (anchor, opacity; texture rectangle; size and gap in points).
constexpr std::size_t vehicleModelRecordFloats = 12;
/// The records of one frame: every drawn part and every pill. The uniform block has this fixed size (GLSL ES needs
/// it whole), so a frame updates it in place and never allocates.
constexpr std::size_t vehicleModelMaxRecords = 256;

struct alignas(16) VehicleModelInstancesUBO {
    std::array<float, vehicleModelRecordFloats * vehicleModelMaxRecords> records;
};
static_assert(sizeof(VehicleModelInstancesUBO) == 256 * 48);

} // namespace shaders
} // namespace mln
