#pragma once

#include "layer.hpp"

#include <jni/jni.hpp>
#include <mln/layermanager/vehicle_model_layer_factory.hpp>
#include <mln/style/layers/vehicle_model_layer.hpp>

namespace mln {
namespace android {

class Bitmap;

class VehicleModelLayer : public Layer {
public:
    using SuperTag = Layer;
    static constexpr auto Name() { return "org/maplibre/android/style/layers/VehicleModelLayer"; };

    VehicleModelLayer(jni::JNIEnv&, jni::String&);
    VehicleModelLayer(mln::style::VehicleModelLayer&);
    VehicleModelLayer(std::unique_ptr<mln::style::VehicleModelLayer>);
    ~VehicleModelLayer();

    void setModels(jni::JNIEnv&, const jni::Array<jni::jbyte>&);
    void setVehicles(jni::JNIEnv&,
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
                     const jni::Array<jni::jdouble>& partTracks);
    void setLabelImage(jni::JNIEnv&, const jni::String& key, const jni::Object<Bitmap>& bitmap, jni::jfloat pixelRatio);
    void setZoomRange(jni::JNIEnv&, jni::jfloat from, jni::jfloat to);
    void setDark(jni::JNIEnv&, jni::jboolean dark);
    jni::Local<jni::String> vehicleAt(jni::JNIEnv&, jni::jfloat x, jni::jfloat y, jni::jfloat slop);
    jni::jboolean wantsFrame(jni::JNIEnv&, jni::jdouble nowMs);
    jni::Local<jni::Array<jni::jdouble>> getStats(jni::JNIEnv&);

private:
    mln::style::VehicleModelLayer* layer();
}; // class VehicleModelLayer

class VehicleModelJavaLayerPeerFactory final : public JavaLayerPeerFactory, public mln::VehicleModelLayerFactory {
public:
    ~VehicleModelJavaLayerPeerFactory() override;

    // JavaLayerPeerFactory overrides.
    jni::Local<jni::Object<Layer>> createJavaLayerPeer(jni::JNIEnv&, mln::style::Layer&) final;
    jni::Local<jni::Object<Layer>> createJavaLayerPeer(jni::JNIEnv& env, std::unique_ptr<mln::style::Layer>) final;

    void registerNative(jni::JNIEnv&) final;

    LayerFactory* getLayerFactory() final { return this; }

}; // class VehicleModelJavaLayerPeerFactory

} // namespace android
} // namespace mln
