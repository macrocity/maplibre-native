#include <mln/style/layers/vehicle_model_layer.hpp>
#include <mln/style/layers/vehicle_model_layer_impl.hpp>
#include <mln/style/layer_observer.hpp>

#include <algorithm>

namespace mln {
namespace style {

namespace {
const LayerTypeInfo typeInfoVehicleModel{.type = "vehicle-model",
                                         .source = LayerTypeInfo::Source::NotRequired,
                                         .pass3d = LayerTypeInfo::Pass3D::NotRequired,
                                         .layout = LayerTypeInfo::Layout::NotRequired,
                                         .fadingTiles = LayerTypeInfo::FadingTiles::NotRequired,
                                         .crossTileIndex = LayerTypeInfo::CrossTileIndex::NotRequired,
                                         .tileKind = LayerTypeInfo::TileKind::NotRequired};
} // namespace

VehicleModelLayer::Impl::Impl(const std::string& id_)
    : Layer::Impl(id_, std::string()),
      labels(std::make_shared<const VehicleModelLabelImages>()),
      runtime(std::make_shared<VehicleModelRuntime>()) {}

bool VehicleModelLayer::Impl::hasLayoutDifference(const Layer::Impl&) const {
    return false;
}

void VehicleModelLayer::Impl::stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const {}

// static
const LayerTypeInfo* VehicleModelLayer::Impl::staticTypeInfo() noexcept {
    return &typeInfoVehicleModel;
}

VehicleModelLayer::VehicleModelLayer(const std::string& layerID)
    : Layer(makeMutable<Impl>(layerID)) {}

VehicleModelLayer::~VehicleModelLayer() = default;

const VehicleModelLayer::Impl& VehicleModelLayer::impl() const {
    return static_cast<const Impl&>(*baseImpl);
}

Mutable<VehicleModelLayer::Impl> VehicleModelLayer::mutableImpl() const {
    return makeMutable<Impl>(impl());
}

Mutable<Layer::Impl> VehicleModelLayer::mutableBaseImpl() const {
    return staticMutableCast<Layer::Impl>(mutableImpl());
}

std::unique_ptr<Layer> VehicleModelLayer::cloneRef(const std::string&) const {
    assert(false);
    return nullptr;
}

std::optional<conversion::Error> VehicleModelLayer::setPropertyInternal(const std::string& name,
                                                                        const conversion::Convertible&) {
    return conversion::Error{"layer '" + getID() + "' doesn't support property '" + name + "'"};
}

StyleProperty VehicleModelLayer::getProperty(const std::string&) const {
    return {};
}

void VehicleModelLayer::setModels(std::shared_ptr<const std::vector<uint8_t>> file) {
    auto impl_ = mutableImpl();
    impl_->models = std::move(file);
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void VehicleModelLayer::setVehicles(std::shared_ptr<const VehicleModelSamples> samples) {
    auto impl_ = mutableImpl();
    impl_->samples = std::move(samples);
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void VehicleModelLayer::setLabelImage(const std::string& key, PremultipliedImage image, float pixelRatio) {
    auto labels = std::make_shared<VehicleModelLabelImages>(*impl().labels);
    (*labels)[key] = {std::make_shared<const PremultipliedImage>(std::move(image)), std::max(0.25f, pixelRatio)};
    auto impl_ = mutableImpl();
    impl_->labels = std::move(labels);
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void VehicleModelLayer::setZoomRange(float from, float to) {
    if (from == impl().fadeFrom && to == impl().fadeTo) return;
    auto impl_ = mutableImpl();
    impl_->fadeFrom = from;
    impl_->fadeTo = std::max(from + 0.01f, to);
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

void VehicleModelLayer::setDark(bool dark) {
    if (dark == impl().dark) return;
    auto impl_ = mutableImpl();
    impl_->dark = dark;
    baseImpl = std::move(impl_);
    observer->onLayerChanged(*this);
}

bool VehicleModelLayer::getDark() const {
    return impl().dark;
}

std::optional<std::string> VehicleModelLayer::vehicleAt(const ScreenCoordinate& point, double slop) const {
    const auto& runtime = *impl().runtime;
    std::lock_guard lock(runtime.mutex);
    if (!runtime.stats.drawing || !runtime.samples) return std::nullopt;
    const auto x = static_cast<float>(point.x);
    const auto y = static_cast<float>(point.y);
    const auto s = static_cast<float>(slop);
    const auto inside = [&](float minX, float minY, float maxX, float maxY) {
        return x >= minX - s && x <= maxX + s && y >= minY - s && y <= maxY + s;
    };
    // A pill lies over every body, and the nearer of two over the farther.
    for (const bool pills : {true, false}) {
        const VehicleModelRuntime::Hit* best = nullptr;
        for (const auto& hit : runtime.hits) {
            const bool in = pills ? hit.hasPill && inside(hit.pillMinX, hit.pillMinY, hit.pillMaxX, hit.pillMaxY)
                                  : inside(hit.bodyMinX, hit.bodyMinY, hit.bodyMaxX, hit.bodyMaxY);
            if (in && (!best || hit.depth < best->depth)) best = &hit;
        }
        if (best && best->vehicle < runtime.samples->vehicles.size()) {
            return runtime.samples->vehicles[best->vehicle].id;
        }
    }
    return std::nullopt;
}

bool VehicleModelLayer::wantsFrame(double nowMs) const {
    const auto& runtime = *impl().runtime;
    std::lock_guard lock(runtime.mutex);
    return runtime.stats.drawing && nowMs >= runtime.wakeAtMs;
}

VehicleModelStats VehicleModelLayer::getStats() const {
    const auto& runtime = *impl().runtime;
    std::lock_guard lock(runtime.mutex);
    return runtime.stats;
}

} // namespace style
} // namespace mln
