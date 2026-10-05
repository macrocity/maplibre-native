#pragma once

#include <mln/style/layers/vehicle_model_layer.hpp>
#include <mln/style/layer_impl.hpp>
#include <mln/style/layer_properties.hpp>

#include <map>
#include <mutex>

namespace mln {
namespace style {

/// A line-name pill's image, and its pixels per point.
struct VehicleModelLabelImage {
    std::shared_ptr<const PremultipliedImage> image;
    float pixelRatio = 1;
};

using VehicleModelLabelImages = std::map<std::string, VehicleModelLabelImage>;

/// The licence plates' images, by the id of the vehicle that carries each.
using VehicleModelPlateImages = std::map<std::string, std::shared_ptr<const PremultipliedImage>>;

/**
 * What the render thread leaves for the host between frames: where each vehicle was drawn (for taps), the next
 * time one starts to move, and the frame counts. The render layer writes it once a frame; the host reads it.
 */
class VehicleModelRuntime {
public:
    struct Hit {
        /// The vehicle's index in `samples`.
        uint32_t vehicle = 0;
        /// Its body's and its pill's boxes on screen, in points (the pill's empty when it has none).
        float bodyMinX = 0, bodyMinY = 0, bodyMaxX = 0, bodyMaxY = 0;
        float pillMinX = 0, pillMinY = 0, pillMaxX = 0, pillMaxY = 0;
        bool hasPill = false;
        /// Its distance from the camera, for the nearest of two.
        float depth = 0;
    };

    mutable std::mutex mutex;
    std::shared_ptr<const VehicleModelSamples> samples;
    std::vector<Hit> hits;
    VehicleModelStats stats;
    /// The time a vehicle on screen next starts to move, in ms since the epoch; infinite when none will.
    double wakeAtMs = std::numeric_limits<double>::infinity();
};

class VehicleModelLayer::Impl : public Layer::Impl {
public:
    explicit Impl(const std::string& id);

    bool hasLayoutDifference(const Layer::Impl&) const override;
    void stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const override;

    std::shared_ptr<const std::vector<uint8_t>> models;
    std::shared_ptr<const VehicleModelSamples> samples;
    std::shared_ptr<const VehicleModelLabelImages> labels;
    std::shared_ptr<const VehicleModelPlateImages> plates;
    float fadeFrom = 16.75f;
    float fadeTo = 17.0f;
    float plateFrom = 18.0f;
    float plateTo = 19.0f;
    bool dark = false;
    std::shared_ptr<VehicleModelRuntime> runtime;

    DECLARE_LAYER_TYPE_INFO;
};

class VehicleModelLayerProperties final : public LayerProperties {
public:
    explicit VehicleModelLayerProperties(Immutable<VehicleModelLayer::Impl> impl)
        : LayerProperties(std::move(impl)) {}

    expression::Dependency getDependencies() const noexcept override { return expression::Dependency::None; }
};

} // namespace style
} // namespace mln
