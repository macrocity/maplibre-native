#include <mln/layermanager/vehicle_model_layer_factory.hpp>
#include <mln/renderer/layers/render_vehicle_model_layer.hpp>
#include <mln/style/layers/vehicle_model_layer.hpp>
#include <mln/style/layers/vehicle_model_layer_impl.hpp>

namespace mln {

const style::LayerTypeInfo* VehicleModelLayerFactory::getTypeInfo() const noexcept {
    return style::VehicleModelLayer::Impl::staticTypeInfo();
}

std::unique_ptr<style::Layer> VehicleModelLayerFactory::createLayer(const std::string& id,
                                                                    const style::conversion::Convertible&) noexcept {
    // Made by its host with its models and vehicles, not from a style document; the style's JSON names nothing to load.
    return std::make_unique<style::VehicleModelLayer>(id);
}

std::unique_ptr<RenderLayer> VehicleModelLayerFactory::createRenderLayer(Immutable<style::Layer::Impl> impl) noexcept {
    return std::make_unique<RenderVehicleModelLayer>(staticImmutableCast<style::VehicleModelLayer::Impl>(impl));
}

} // namespace mln
