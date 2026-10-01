#include "vehicle_model_layer.hpp"

#include "bitmap.hpp"

#include <mln/style/layer_impl.hpp>

#include <algorithm>

namespace mln {
namespace android {

VehicleModelLayer::VehicleModelLayer(jni::JNIEnv& env, jni::String& layerId)
    : Layer(std::make_unique<mln::style::VehicleModelLayer>(jni::Make<std::string>(env, layerId))) {}

VehicleModelLayer::VehicleModelLayer(mln::style::VehicleModelLayer& coreLayer)
    : Layer(coreLayer) {}

VehicleModelLayer::VehicleModelLayer(std::unique_ptr<mln::style::VehicleModelLayer> coreLayer)
    : Layer(std::move(coreLayer)) {}

VehicleModelLayer::~VehicleModelLayer() = default;

mln::style::VehicleModelLayer* VehicleModelLayer::layer() {
    auto* core = layerPtr.get();
    return core ? static_cast<mln::style::VehicleModelLayer*>(core) : nullptr;
}

void VehicleModelLayer::setModels(jni::JNIEnv& env, const jni::Array<jni::jbyte>& data) {
    auto* core = layer();
    if (!core || !data) return;
    const auto bytes = jni::Make<std::vector<jni::jbyte>>(env, data);
    core->setModels(std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end()));
}

void VehicleModelLayer::setVehicles(jni::JNIEnv& env,
                                    jni::jdouble startMs,
                                    jni::jdouble stepMs,
                                    jni::jint sampleCount,
                                    const jni::Array<jni::String>& ids,
                                    const jni::Array<jni::jbyte>& kinds,
                                    const jni::Array<jni::jint>& tints,
                                    const jni::Array<jni::jfloat>& opacities,
                                    const jni::Array<jni::String>& labelKeys,
                                    const jni::Array<jni::jdouble>& tracks,
                                    const jni::Array<jni::jint>& partStarts,
                                    const jni::Array<jni::jdouble>& partTracks) {
    auto* core = layer();
    if (!core || sampleCount <= 0 || !ids || !kinds || !tints || !opacities || !labelKeys || !tracks || !partStarts) {
        return;
    }
    const auto count = std::min({ids.Length(env), kinds.Length(env), tints.Length(env), opacities.Length(env),
                                 labelKeys.Length(env), partStarts.Length(env)});
    auto samples = std::make_shared<mln::style::VehicleModelSamples>();
    samples->startMs = startMs;
    samples->stepMs = std::max(1.0, double(stepMs));
    samples->sampleCount = static_cast<uint32_t>(sampleCount);
    samples->tracks = jni::Make<std::vector<double>>(env, tracks);
    if (samples->tracks.size() < count * std::size_t(sampleCount) * 3) return;
    if (partTracks) samples->partTracks = jni::Make<std::vector<double>>(env, partTracks);
    const auto kindValues = jni::Make<std::vector<jni::jbyte>>(env, kinds);
    const auto tintValues = jni::Make<std::vector<jni::jint>>(env, tints);
    const auto opacityValues = jni::Make<std::vector<jni::jfloat>>(env, opacities);
    const auto partValues = jni::Make<std::vector<jni::jint>>(env, partStarts);
    samples->vehicles.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        mln::style::VehicleModelSamples::Vehicle vehicle;
        vehicle.id = jni::Make<std::string>(env, ids.Get(env, i));
        vehicle.kind = static_cast<uint8_t>(kindValues[i]);
        const auto rgba = static_cast<uint32_t>(tintValues[i]);
        vehicle.tint = Color(((rgba >> 24) & 0xff) / 255.0f,
                             ((rgba >> 16) & 0xff) / 255.0f,
                             ((rgba >> 8) & 0xff) / 255.0f,
                             (rgba & 0xff) / 255.0f);
        vehicle.opacity = opacityValues[i];
        vehicle.labelKey = jni::Make<std::string>(env, labelKeys.Get(env, i));
        vehicle.track = static_cast<uint32_t>(i);
        vehicle.parts = partValues[i];
        samples->vehicles.push_back(std::move(vehicle));
    }
    core->setVehicles(std::move(samples));
}

void VehicleModelLayer::setLabelImage(jni::JNIEnv& env,
                                      const jni::String& key,
                                      const jni::Object<Bitmap>& bitmap,
                                      jni::jfloat pixelRatio) {
    auto* core = layer();
    if (!core || !bitmap) return;
    core->setLabelImage(jni::Make<std::string>(env, key), Bitmap::GetImage(env, bitmap), pixelRatio);
}

void VehicleModelLayer::setZoomRange(jni::JNIEnv&, jni::jfloat from, jni::jfloat to) {
    if (auto* core = layer()) core->setZoomRange(from, to);
}

void VehicleModelLayer::setDark(jni::JNIEnv&, jni::jboolean dark) {
    if (auto* core = layer()) core->setDark(dark);
}

jni::Local<jni::String> VehicleModelLayer::vehicleAt(jni::JNIEnv& env, jni::jfloat x, jni::jfloat y, jni::jfloat slop) {
    auto* core = layer();
    if (!core) return jni::Local<jni::String>();
    const auto found = core->vehicleAt({x, y}, slop);
    return found ? jni::Make<jni::String>(env, *found) : jni::Local<jni::String>();
}

jni::jboolean VehicleModelLayer::wantsFrame(jni::JNIEnv&, jni::jdouble nowMs) {
    auto* core = layer();
    return core && core->wantsFrame(nowMs);
}

jni::Local<jni::Array<jni::jdouble>> VehicleModelLayer::getStats(jni::JNIEnv& env) {
    auto* core = layer();
    const auto stats = core ? core->getStats() : mln::style::VehicleModelStats{};
    const std::vector<double> values{stats.drawing ? 1.0 : 0.0,
                                     stats.zoom,
                                     double(stats.vehicles),
                                     double(stats.parts),
                                     stats.framesPerSecond,
                                     stats.meanUpdateMs};
    return jni::Make<jni::Array<jni::jdouble>>(env, values);
}

namespace {
jni::Local<jni::Object<Layer>> createJavaPeer(jni::JNIEnv& env, Layer* layer) {
    static auto& javaClass = jni::Class<VehicleModelLayer>::Singleton(env);
    static auto constructor = javaClass.GetConstructor<jni::jlong>(env);
    return javaClass.New(env, constructor, reinterpret_cast<jni::jlong>(layer));
}
} // namespace

VehicleModelJavaLayerPeerFactory::~VehicleModelJavaLayerPeerFactory() = default;

jni::Local<jni::Object<Layer>> VehicleModelJavaLayerPeerFactory::createJavaLayerPeer(jni::JNIEnv& env,
                                                                                     mln::style::Layer& layer) {
    assert(layer.baseImpl->getTypeInfo() == getTypeInfo());
    return createJavaPeer(env, new VehicleModelLayer(static_cast<mln::style::VehicleModelLayer&>(layer)));
}

jni::Local<jni::Object<Layer>> VehicleModelJavaLayerPeerFactory::createJavaLayerPeer(
    jni::JNIEnv& env, std::unique_ptr<mln::style::Layer> layer) {
    assert(layer->baseImpl->getTypeInfo() == getTypeInfo());
    return createJavaPeer(env,
                          new VehicleModelLayer(std::unique_ptr<mln::style::VehicleModelLayer>(
                              static_cast<mln::style::VehicleModelLayer*>(layer.release()))));
}

void VehicleModelJavaLayerPeerFactory::registerNative(jni::JNIEnv& env) {
    // Lookup the class
    static auto& javaClass = jni::Class<VehicleModelLayer>::Singleton(env);

#define METHOD(MethodPtr, name) jni::MakeNativePeerMethod<decltype(MethodPtr), (MethodPtr)>(name)

    // Register the peer
    jni::RegisterNativePeer<VehicleModelLayer>(env,
                                               javaClass,
                                               "nativePtr",
                                               jni::MakePeer<VehicleModelLayer, jni::String&>,
                                               "initialize",
                                               "finalize",
                                               METHOD(&VehicleModelLayer::setModels, "nativeSetModels"),
                                               METHOD(&VehicleModelLayer::setVehicles, "nativeSetVehicles"),
                                               METHOD(&VehicleModelLayer::setLabelImage, "nativeSetLabelImage"),
                                               METHOD(&VehicleModelLayer::setZoomRange, "nativeSetZoomRange"),
                                               METHOD(&VehicleModelLayer::setDark, "nativeSetDark"),
                                               METHOD(&VehicleModelLayer::vehicleAt, "nativeVehicleAt"),
                                               METHOD(&VehicleModelLayer::wantsFrame, "nativeWantsFrame"),
                                               METHOD(&VehicleModelLayer::getStats, "nativeGetStats"));
}

} // namespace android
} // namespace mln
