#pragma once

#include <mln/renderer/render_layer.hpp>
#include <mln/style/layers/vehicle_model_layer_impl.hpp>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace mln {

namespace gfx {
class Drawable;
class IndexVectorBase;
class Texture2D;
class VertexVectorBase;
} // namespace gfx

class SegmentBase;

/**
 * Draws the vehicles of a `style::VehicleModelLayer` as instanced 3D models, with the same depth as the 3D buildings,
 * at the place their samples give them at the moment the frame is drawn. Every model part is one draw call for all
 * the vehicles, with its records (place, heading, tint) in one uniform buffer that a frame updates in place.
 */
class RenderVehicleModelLayer final : public RenderLayer {
public:
    explicit RenderVehicleModelLayer(Immutable<style::VehicleModelLayer::Impl>);
    ~RenderVehicleModelLayer() override;

    void update(gfx::ShaderRegistry&,
                gfx::Context&,
                const TransformState&,
                const std::shared_ptr<UpdateParameters>&,
                const PaintParameters&,
                const RenderTree&,
                UniqueChangeRequestVec&) override;

    static constexpr std::size_t maxKinds = 3;
    static constexpr std::size_t maxParts = 6;

    struct Part {
        float centerZ = 0;
        float halfLength = 0;
        std::shared_ptr<gfx::VertexVectorBase> vertices;
        std::size_t vertexCount = 0;
        std::shared_ptr<gfx::IndexVectorBase> indices;
        std::size_t indexCount = 0;
    };

    struct Model {
        std::vector<Part> parts;
        float halfWidth = 1.28f;
        float labelHeight = 3.7f;
    };

private:
    void transition(const TransitionParameters&) override {}
    void evaluate(const PropertyEvaluationParameters&) override;
    bool hasTransition() const override { return moving; }
    bool hasCrossfade() const override { return false; }
    bool is3D() const override { return true; }
    void prepare(const LayerPrepareParameters&) override {}
    void markContextDestroyed() override;

    /// Reads the models file; false when it is not one.
    bool loadModels(const std::vector<uint8_t>&);
    void buildDrawables(gfx::Context&, UniqueChangeRequestVec&);
    void buildLabelAtlas(gfx::Context&, const style::VehicleModelLabelImages&);
    void disable();

    std::array<Model, maxKinds> models;
    std::shared_ptr<const std::vector<uint8_t>> loadedModels;

    gfx::ShaderProgramBasePtr modelShader;
    gfx::ShaderProgramBasePtr shadowShader;
    gfx::ShaderProgramBasePtr labelShader;

    /// A drawable and the segment whose instance count a frame sets.
    struct Slot {
        gfx::Drawable* drawable = nullptr;
        SegmentBase* segment = nullptr;
    };
    /// Per model part: solid, then a faded one's depth, then its colour over that depth.
    std::array<std::array<std::array<Slot, 3>, maxParts>, maxKinds> partSlots;
    Slot shadowSlot;
    Slot labelSlot;
    std::size_t builtDrawables = 0;

    /// The line-name pills packed into one texture, and where each is in it (u0, v0, u1, v1; width and height in points).
    std::shared_ptr<gfx::Texture2D> labelAtlas;
    std::shared_ptr<const style::VehicleModelLabelImages> atlasSource;
    struct AtlasEntry {
        std::array<float, 4> uv;
        float width = 0;
        float height = 0;
    };
    std::map<std::string, AtlasEntry> atlas;

    /// One frame's work, kept between frames so a frame allocates nothing.
    struct Drawn {
        uint32_t vehicle;
        double lon;
        double lat;
        double bearing;
        float alpha;
        float depth;
    };
    std::vector<Drawn> drawn;
    std::vector<float> records;
    std::vector<uint32_t> labelOrder;
    std::vector<style::VehicleModelRuntime::Hit> hits;

    bool moving = false;

    // Counting frames for the host's measurements.
    std::chrono::steady_clock::time_point secondStart;
    uint32_t secondFrames = 0;
    double secondUpdateMs = 0;
    double framesPerSecond = 0;
    double meanUpdateMs = 0;
};

} // namespace mln
