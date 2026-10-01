#pragma once

#include <mln/style/layer.hpp>
#include <mln/util/color.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/image.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mln {
namespace style {

/**
 * The vehicles a `VehicleModelLayer` draws, and where they will be: positions sampled ahead of time, between which the
 * layer interpolates at the moment each frame is drawn. A host hands a new set over a few times a minute; nothing
 * needs to run on the host per frame.
 */
struct VehicleModelSamples {
    /// The time of the first sample, in milliseconds since the Unix epoch, and the time between samples.
    double startMs = 0;
    double stepMs = 500;
    /// Samples per track.
    uint32_t sampleCount = 0;

    struct Vehicle {
        std::string id;
        /// The model it is drawn as: its index in the models file.
        uint8_t kind = 0;
        /// The colour its livery is painted in.
        Color tint = Color::black();
        /// 0 to 1: a faded vehicle (a stale one, one fading in) is drawn without showing its inner faces.
        float opacity = 1;
        /// The key of its line-name pill (`setLabelImage`); empty for none.
        std::string labelKey;
        /// Its track: `sampleCount` × [longitude, latitude, bearing] in `tracks`, starting at `track · sampleCount · 3`.
        uint32_t track = 0;
        /// For a model of several parts (an articulated tram): the first of its parts' tracks in `partTracks`, each
        /// laid out like `track`; -1 to stand the parts straight along its own track.
        int32_t parts = -1;
    };

    std::vector<Vehicle> vehicles;
    std::vector<double> tracks;
    std::vector<double> partTracks;
};

/// What the layer drew last, for measuring.
struct VehicleModelStats {
    bool drawing = false;
    double zoom = 0;
    /// Vehicles drawn in the last frame, and model parts.
    uint32_t vehicles = 0;
    uint32_t parts = 0;
    /// Frames drawn with models over the last full second, and their mean CPU time in the layer, in milliseconds.
    double framesPerSecond = 0;
    double meanUpdateMs = 0;
};

class VehicleModelRuntime;

/**
 * Vehicles drawn as 3D models, moved along their predicted tracks inside the map's own frames: they follow every
 * gesture exactly, the 3D buildings hide them, and the map draws only while one on screen moves.
 */
class VehicleModelLayer final : public Layer {
public:
    explicit VehicleModelLayer(const std::string& id);
    VehicleModelLayer(const VehicleModelLayer&) = delete;
    ~VehicleModelLayer() final;

    /// The models, as the app's model file (`MCVM`, version 1): meshes in metres, +X left, +Y up, +Z ahead.
    void setModels(std::shared_ptr<const std::vector<uint8_t>> file);

    /// A new set of vehicles and their samples.
    void setVehicles(std::shared_ptr<const VehicleModelSamples>);

    /// The image of a line-name pill, drawn over the roofs of the vehicles that name it; `pixelRatio` pixels a point.
    void setLabelImage(const std::string& key, PremultipliedImage image, float pixelRatio);

    /// The zooms over which the models fade in; below `from` the layer draws nothing.
    void setZoomRange(float from, float to);

    /// The dark map's light and lamps.
    void setDark(bool dark);
    bool getDark() const;

    /// The vehicle drawn under a point of the map view, in points, as the last frame drew it: its pill first, then
    /// its body, the nearest first; none within `slop` points.
    std::optional<std::string> vehicleAt(const ScreenCoordinate&, double slop) const;

    /// Whether a vehicle on screen starts to move by `nowMs`, so the map must draw again although nothing else
    /// asked it to. A host asks this from its own frame clock.
    bool wantsFrame(double nowMs) const;

    VehicleModelStats getStats() const;

    class Impl;
    const Impl& impl() const;
    Mutable<Impl> mutableImpl() const;

private:
    std::optional<conversion::Error> setPropertyInternal(const std::string& name,
                                                         const conversion::Convertible& value) final;
    StyleProperty getProperty(const std::string&) const final;
    std::unique_ptr<Layer> cloneRef(const std::string& id) const final;
    Mutable<Layer::Impl> mutableBaseImpl() const final;
};

} // namespace style
} // namespace mln
