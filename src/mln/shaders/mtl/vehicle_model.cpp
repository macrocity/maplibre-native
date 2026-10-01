#include <mln/shaders/mtl/vehicle_model.hpp>
#include <mln/shaders/shader_defines.hpp>

namespace mln {
namespace shaders {

using VehicleModelShaderSource = ShaderSource<BuiltIn::VehicleModelShader, gfx::Backend::Type::Metal>;

const std::array<AttributeInfo, 3> VehicleModelShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Float3, vehicleModelUBOCount + 0, idVehicleModelPosVertexAttribute},
    AttributeInfo{1, gfx::AttributeDataType::Float4, vehicleModelUBOCount + 1, idVehicleModelNormalVertexAttribute},
    AttributeInfo{2, gfx::AttributeDataType::Float4, vehicleModelUBOCount + 2, idVehicleModelColorVertexAttribute},
};

using VehicleModelShadowShaderSource = ShaderSource<BuiltIn::VehicleModelShadowShader, gfx::Backend::Type::Metal>;

const std::array<AttributeInfo, 1> VehicleModelShadowShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Float2, vehicleModelUBOCount + 0, idVehicleModelPosVertexAttribute},
};

using VehicleModelLabelShaderSource = ShaderSource<BuiltIn::VehicleModelLabelShader, gfx::Backend::Type::Metal>;

const std::array<AttributeInfo, 1> VehicleModelLabelShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Float2, vehicleModelUBOCount + 0, idVehicleModelPosVertexAttribute},
};
const std::array<TextureInfo, 1> VehicleModelLabelShaderSource::textures = {
    TextureInfo{0, idVehicleModelLabelTexture},
};

} // namespace shaders
} // namespace mln
