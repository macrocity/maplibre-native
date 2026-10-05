#include <mln/renderer/layers/render_vehicle_model_layer.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/drawable_impl.hpp>
#include <mln/gfx/index_vector.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/gfx/vertex_attribute.hpp>
#include <mln/gfx/vertex_vector.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_static_data.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/shaders/vehicle_model_layer_ubo.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/mat4.hpp>
#include <mln/util/projection.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mln {

using namespace style;
using namespace shaders;

namespace {

inline const VehicleModelLayer::Impl& impl(const Immutable<style::Layer::Impl>& impl) {
    assert(impl->getTypeInfo() == VehicleModelLayer::Impl::staticTypeInfo());
    return static_cast<const VehicleModelLayer::Impl&>(*impl);
}

/// A model vertex as the GPU reads it: position (metres), normal and flags (1 livery, 2 lit lamp), colour.
struct ModelVertex {
    std::array<float, 3> position;
    std::array<float, 4> normal;
    std::array<float, 4> color;
};
static_assert(sizeof(ModelVertex) == 44);

/// A quad's corner, for the shadows (-1 to 1) and the pills (0 to 1).
struct CornerVertex {
    std::array<float, 2> corner;
};

enum SlotKind : std::size_t {
    Solid = 0,
    FadedDepth = 1,
    FadedColor = 2,
};

constexpr std::size_t recordFloats = vehicleModelRecordFloats;
constexpr std::size_t maxRecords = vehicleModelMaxRecords;

/// The light of the map's 3D buildings: MapLibre's default position, azimuth 210°, 30° from the zenith, on the map.
constexpr std::array<float, 4> lightDirection{-0.25f, -0.433f, 0.866f, 0.0f};

/// A place on a track at fractional sample `k`: straight between samples, the heading the short way round.
void sampleTrack(const double* track, uint32_t count, double k, double& lon, double& lat, double& bearing) {
    const auto last = static_cast<double>(count - 1);
    k = std::clamp(k, 0.0, last);
    const auto i0 = static_cast<uint32_t>(std::floor(k));
    const auto i1 = std::min(count - 1, i0 + 1);
    const double f = k - i0;
    const double* a = track + i0 * 3;
    const double* b = track + i1 * 3;
    lon = a[0] + (b[0] - a[0]) * f;
    lat = a[1] + (b[1] - a[1]) * f;
    const double turn = std::fmod(b[2] - a[2] + 540.0, 360.0) - 180.0;
    bearing = a[2] + turn * f;
}

/// Whether a track moves between samples `from` and `to`.
bool trackMoves(const double* track, uint32_t count, uint32_t from, uint32_t to) {
    to = std::min(to, count - 1);
    for (uint32_t i = from + 1; i <= to; ++i) {
        const double* a = track + (i - 1) * 3;
        const double* b = track + i * 3;
        if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) return true;
    }
    return false;
}

/// The first sample after `from` where a track has moved, or `count` when it stands to its end.
uint32_t nextMove(const double* track, uint32_t count, uint32_t from) {
    for (uint32_t i = from + 1; i < count; ++i) {
        const double* a = track + (i - 1) * 3;
        const double* b = track + i * 3;
        if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) return i - 1;
    }
    return count;
}

/// Little-endian readers over the models file.
struct Reader {
    const uint8_t* data;
    std::size_t length;
    std::size_t at = 0;
    bool ok = true;

    template <typename T>
    T read() {
        T value{};
        if (at + sizeof(T) > length) {
            ok = false;
            return value;
        }
        std::memcpy(&value, data + at, sizeof(T));
        at += sizeof(T);
        return value;
    }
};

std::array<double, 4> transform(const mat4& m, double x, double y, double z) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14],
            m[3] * x + m[7] * y + m[11] * z + m[15]};
}

} // namespace

RenderVehicleModelLayer::RenderVehicleModelLayer(Immutable<style::VehicleModelLayer::Impl> _impl)
    : RenderLayer(makeMutable<VehicleModelLayerProperties>(std::move(_impl))) {
    drawn.reserve(maxRecords);
    records.assign(maxRecords * recordFloats, 0.0f);
    plateRecords.reserve(maxRecords * recordFloats);
    labelOrder.reserve(maxRecords);
    hits.reserve(maxRecords);
}

RenderVehicleModelLayer::~RenderVehicleModelLayer() = default;

std::size_t RenderVehicleModelLayer::removeAllDrawables() {
    // The renderer also removes drawings directly, including on each Goldfish emulator frame.
    // Invalidate the borrowed drawable and segment pointers before their owner destroys them.
    partSlots = {};
    shadowSlot = {};
    labelSlot = {};
    plateSlot = {};
    builtDrawables = 0;
    return RenderLayer::removeAllDrawables();
}

void RenderVehicleModelLayer::evaluate(const PropertyEvaluationParameters&) {
    passes = RenderPass::Translucent;
    // The layer has no style properties: what it draws comes with its impl.
}

void RenderVehicleModelLayer::markContextDestroyed() {
    modelShader.reset();
    shadowShader.reset();
    labelShader.reset();
    labelAtlas.reset();
    atlasSource.reset();
    plateAtlas.reset();
    plateAtlasSource.reset();
    builtDrawables = 0;
}

bool RenderVehicleModelLayer::loadModels(const std::vector<uint8_t>& file) {
    Reader in{file.data(), file.size()};
    if (file.size() < 12 || std::memcmp(file.data(), "MCVM", 4) != 0) return false;
    in.at = 4;
    const auto version = in.read<uint32_t>();
    const auto count = in.read<uint32_t>();
    if (!in.ok || version < 1 || version > 3 || count > maxKinds) return false;
    std::array<Model, maxKinds> loaded;
    for (uint32_t m = 0; m < count; ++m) {
        const auto kind = in.read<uint32_t>();
        const auto partCount = in.read<uint32_t>();
        const auto length = in.read<float>();
        const auto height = in.read<float>();
        if (!in.ok || kind >= maxKinds || partCount == 0 || partCount > maxParts) return false;
        auto& model = loaded[kind];
        model.length = length;
        if (version >= 2) {
            // Version 2 gives each model its own width and the height its pill stands at (over the body, not over
            // a pantograph or the trolley poles).
            model.halfWidth = in.read<float>();
            model.labelHeight = in.read<float>() + 0.4f;
        } else {
            constexpr uint32_t tram = 2;
            model.labelHeight = std::min(height, kind == tram ? 3.7f : 3.3f) + 0.4f;
            model.halfWidth = kind == tram ? 1.2f : 1.28f;
        }
        if (version >= 3) {
            // Version 3 says where the model carries its licence plates (macrocity/app#935): none, or one or more
            // faces, each on a part, in that part's frame.
            const auto plateCount = in.read<uint32_t>();
            if (!in.ok || plateCount > 4) return false;
            for (uint32_t i = 0; i < plateCount; ++i) {
                PlateMount mount;
                mount.part = in.read<uint32_t>();
                mount.y = in.read<float>();
                mount.z = in.read<float>();
                mount.halfWidth = in.read<float>();
                mount.halfHeight = in.read<float>();
                if (!in.ok || mount.part >= partCount || !(mount.halfWidth > 0) || !(mount.halfHeight > 0) ||
                    mount.z == 0) {
                    return false;
                }
                model.plates.push_back(mount);
            }
        }
        for (uint32_t p = 0; p < partCount; ++p) {
            Part part;
            part.centerZ = in.read<float>();
            part.halfLength = in.read<float>();
            const auto vertexCount = in.read<uint32_t>();
            const auto indexCount = in.read<uint32_t>();
            if (version >= 2 && vertexCount == 0 && indexCount == 0) {
                // A part drawn as a part of a model before it (a train's coaches): it shares that part's buffers.
                const auto fromKind = in.read<uint32_t>();
                const auto fromPart = in.read<uint32_t>();
                if (!in.ok || fromKind >= maxKinds || fromPart >= loaded[fromKind].parts.size()) return false;
                const auto& from = loaded[fromKind].parts[fromPart];
                part.vertices = from.vertices;
                part.vertexCount = from.vertexCount;
                part.indices = from.indices;
                part.indexCount = from.indexCount;
                model.parts.push_back(std::move(part));
                continue;
            }
            // Version 1: f32 x, y, z; i8 normal; u8 flags; u8 rgba. Version 2: i16 x, y, z in millimetres; i8 normal;
            // u8 flags; u8 rgb; a byte of padding.
            const std::size_t vertexBytes = version >= 2 ? 14 : 20;
            if (!in.ok || vertexCount > std::numeric_limits<uint16_t>::max() || indexCount % 3 != 0 ||
                in.at + vertexCount * vertexBytes + (indexCount * 2 + 3) / 4 * 4 > file.size()) {
                return false;
            }
            auto vertices = std::make_shared<gfx::VertexVector<ModelVertex>>();
            for (uint32_t v = 0; v < vertexCount; ++v) {
                const uint8_t* at = file.data() + in.at + v * vertexBytes;
                std::array<float, 3> position;
                std::size_t n = 12;
                if (version >= 2) {
                    std::array<int16_t, 3> mm;
                    std::memcpy(mm.data(), at, 6);
                    position = {mm[0] / 1000.0f, mm[1] / 1000.0f, mm[2] / 1000.0f};
                    n = 6;
                } else {
                    std::memcpy(position.data(), at, 12);
                }
                const auto normal = [&](std::size_t i) { return static_cast<float>(static_cast<int8_t>(at[n + i])) / 127.0f; };
                const uint8_t* color = at + n + 4;
                vertices->emplace_back(ModelVertex{
                    position,
                    {normal(0), normal(1), normal(2), static_cast<float>(at[n + 3])},
                    {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, version >= 2 ? 1.0f : color[3] / 255.0f},
                });
            }
            in.at += vertexCount * vertexBytes;
            auto indices = std::make_shared<gfx::IndexVector<gfx::Triangles>>();
            for (uint32_t i = 0; i < indexCount; i += 3) {
                uint16_t a, b, c;
                std::memcpy(&a, file.data() + in.at + i * 2, 2);
                std::memcpy(&b, file.data() + in.at + i * 2 + 2, 2);
                std::memcpy(&c, file.data() + in.at + i * 2 + 4, 2);
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount) return false;
                indices->emplace_back(a, b, c);
            }
            in.at += (indexCount * 2 + 3) / 4 * 4;
            part.vertexCount = vertexCount;
            part.indexCount = indexCount;
            part.vertices = std::move(vertices);
            part.indices = std::move(indices);
            model.parts.push_back(std::move(part));
        }
    }
    models = std::move(loaded);
    return true;
}

void RenderVehicleModelLayer::buildDrawables(gfx::Context& context, UniqueChangeRequestVec& changes) {
    if (!layerGroup) {
        if (auto group = context.createLayerGroup(layerIndex, /*initialCapacity=*/64, getID())) {
            setLayerGroup(std::move(group), changes);
        } else {
            return;
        }
    }
    auto& group = static_cast<LayerGroup&>(*layerGroup);
    removeAllDrawables();

    const auto builder = context.createDrawableBuilder(getID());
    // `is3D`: the depth test of the 3D buildings, on OpenGL (their depth range) and on Metal, whatever 2D layer order
    // the layer stands at.

    const auto make = [&](const std::string& name,
                          const gfx::ShaderProgramBasePtr& shader,
                          gfx::DrawPriority priority,
                          bool depth,
                          gfx::DepthMaskType depthType,
                          bool color,
                          const gfx::VertexAttributeArrayPtr& attributes,
                          std::size_t vertexCount,
                          gfx::AttributeDataType vertexType,
                          const std::shared_ptr<gfx::IndexVectorBase>& indices,
                          std::size_t indexCount) -> Slot {
        auto& drawable = builder->getCurrentDrawable(true);
        drawable->setName(name);
        drawable->setRenderPass(RenderPass::Translucent);
        drawable->setDrawPriority(priority);
        drawable->setShader(shader);
        drawable->setIs3D(depth);
        drawable->setEnableDepth(depth);
        drawable->setDepthType(depthType);
        drawable->setEnableStencil(false);
        drawable->setEnableColor(color);
        drawable->setColorMode(gfx::ColorMode::alphaBlended());
        drawable->setCullFaceMode(gfx::CullFaceMode::disabled());
        drawable->setVertices({}, vertexCount, vertexType);
        drawable->setVertexAttributes(attributes);

        auto segment = builder->createSegment(gfx::Triangles(),
                                              SegmentBase(0, 0, vertexCount, indexCount, /*baseInstance=*/0, 0));
        Slot slot{drawable.get(), &segment->getSegment()};
        std::vector<gfx::Drawable::UniqueDrawSegment> segments;
        segments.push_back(std::move(segment));
        drawable->setIndexData(indices, std::move(segments));
        drawable->setEnabled(false);

        group.addDrawable(std::move(drawable));
        ++stats.drawablesAdded;
        ++builtDrawables;
        return slot;
    };

    for (std::size_t kind = 0; kind < maxKinds; ++kind) {
        for (std::size_t p = 0; p < models[kind].parts.size(); ++p) {
            const auto& part = models[kind].parts[p];
            auto attributes = context.createVertexAttributeArray();
            if (const auto& attr = attributes->set(idVehicleModelPosVertexAttribute)) {
                attr->setSharedRawData(part.vertices,
                                       offsetof(ModelVertex, position),
                                       0,
                                       sizeof(ModelVertex),
                                       gfx::AttributeDataType::Float3);
            }
            if (const auto& attr = attributes->set(idVehicleModelNormalVertexAttribute)) {
                attr->setSharedRawData(
                    part.vertices, offsetof(ModelVertex, normal), 0, sizeof(ModelVertex), gfx::AttributeDataType::Float4);
            }
            if (const auto& attr = attributes->set(idVehicleModelColorVertexAttribute)) {
                attr->setSharedRawData(
                    part.vertices, offsetof(ModelVertex, color), 0, sizeof(ModelVertex), gfx::AttributeDataType::Float4);
            }
            const auto name = "vehicle-model-" + std::to_string(kind) + "-" + std::to_string(p);
            auto& slots = partSlots[kind][p];
            slots[Solid] = make(name,
                                modelShader,
                                1,
                                true,
                                gfx::DepthMaskType::ReadWrite,
                                true,
                                attributes,
                                part.vertexCount,
                                gfx::AttributeDataType::Float3,
                                part.indices,
                                part.indexCount);
            slots[FadedDepth] = make(name + "-faded-depth",
                                     modelShader,
                                     2,
                                     true,
                                     gfx::DepthMaskType::ReadWrite,
                                     false,
                                     attributes,
                                     part.vertexCount,
                                     gfx::AttributeDataType::Float3,
                                     part.indices,
                                     part.indexCount);
            slots[FadedColor] = make(name + "-faded",
                                     modelShader,
                                     3,
                                     true,
                                     gfx::DepthMaskType::ReadOnly,
                                     true,
                                     attributes,
                                     part.vertexCount,
                                     gfx::AttributeDataType::Float3,
                                     part.indices,
                                     part.indexCount);
        }
    }

    const auto quad = [&](float low) {
        auto vertices = std::make_shared<gfx::VertexVector<CornerVertex>>();
        vertices->emplace_back(CornerVertex{{low, low}});
        vertices->emplace_back(CornerVertex{{1.0f, low}});
        vertices->emplace_back(CornerVertex{{low, 1.0f}});
        vertices->emplace_back(CornerVertex{{1.0f, 1.0f}});
        auto attributes = context.createVertexAttributeArray();
        if (const auto& attr = attributes->set(idVehicleModelPosVertexAttribute)) {
            attr->setSharedRawData(vertices, 0, 0, sizeof(CornerVertex), gfx::AttributeDataType::Float2);
        }
        return attributes;
    };
    auto quadIndices = std::make_shared<gfx::IndexVector<gfx::Triangles>>();
    quadIndices->emplace_back(0, 1, 2);
    quadIndices->emplace_back(1, 3, 2);

    shadowSlot = make("vehicle-model-shadows",
                      shadowShader,
                      0,
                      true,
                      gfx::DepthMaskType::ReadOnly,
                      true,
                      quad(-1.0f),
                      4,
                      gfx::AttributeDataType::Float2,
                      quadIndices,
                      6);
    // The licence plates: the pills' shader, placed on the bodies in metres (its drawable says so), with the depth of
    // the models, after them and over a faded one's colour.
    plateSlot = make("vehicle-model-plates",
                     labelShader,
                     4,
                     true,
                     gfx::DepthMaskType::ReadOnly,
                     true,
                     quad(0.0f),
                     4,
                     gfx::AttributeDataType::Float2,
                     quadIndices,
                     6);
    labelSlot = make("vehicle-model-labels",
                     labelShader,
                     5,
                     false,
                     gfx::DepthMaskType::ReadOnly,
                     true,
                     quad(0.0f),
                     4,
                     gfx::AttributeDataType::Float2,
                     quadIndices,
                     6);
}

void RenderVehicleModelLayer::buildLabelAtlas(gfx::Context& context, const VehicleModelLabelImages& images) {
    atlas.clear();
    // Shelves of pills, tallest first, in a texture 1024 pixels wide.
    constexpr uint32_t atlasWidth = 1024;
    std::vector<std::pair<std::string, const VehicleModelLabelImage*>> sorted;
    sorted.reserve(images.size());
    for (const auto& [key, image] : images) {
        if (image.image && image.image->valid() && image.image->size.width <= atlasWidth) sorted.emplace_back(key, &image);
    }
    std::ranges::sort(sorted, [](const auto& a, const auto& b) {
        return a.second->image->size.height > b.second->image->size.height;
    });
    struct Placement {
        uint32_t x, y;
    };
    std::vector<Placement> placed(sorted.size());
    uint32_t x = 0, y = 0, shelf = 0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const auto size = sorted[i].second->image->size;
        if (x + size.width > atlasWidth) {
            x = 0;
            y += shelf + 1;
            shelf = 0;
        }
        placed[i] = {x, y};
        x += size.width + 1;
        shelf = std::max(shelf, size.height);
    }
    const uint32_t atlasHeight = std::max(1u, y + shelf);
    PremultipliedImage image({atlasWidth, atlasHeight});
    image.fill(0);
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const auto& source = *sorted[i].second;
        const auto size = source.image->size;
        PremultipliedImage::copy(*source.image, image, {0, 0}, {placed[i].x, placed[i].y}, size);
        atlas[sorted[i].first] = AtlasEntry{
            {static_cast<float>(placed[i].x) / atlasWidth,
             static_cast<float>(placed[i].y) / atlasHeight,
             static_cast<float>(placed[i].x + size.width) / atlasWidth,
             static_cast<float>(placed[i].y + size.height) / atlasHeight},
            size.width / source.pixelRatio,
            size.height / source.pixelRatio,
        };
    }
    if (!labelAtlas) {
        labelAtlas = context.createTexture2D();
        labelAtlas->setSamplerConfiguration({.filter = gfx::TextureFilterType::Linear,
                                             .wrapU = gfx::TextureWrapType::Clamp,
                                             .wrapV = gfx::TextureWrapType::Clamp});
    }
    labelAtlas->upload(image);
    if (labelSlot.drawable) labelSlot.drawable->setTexture(labelAtlas, idVehicleModelLabelTexture);
}

void RenderVehicleModelLayer::buildPlateAtlas(gfx::Context& context, const VehicleModelPlateImages& images) {
    plateUVs.clear();
    // Shelves of plates in a texture 1024 pixels wide, at most 2048 high: the plates past that are not drawn.
    constexpr uint32_t atlasWidth = 1024;
    constexpr uint32_t maxHeight = 2048;
    struct Placement {
        const std::string* id;
        const PremultipliedImage* image;
        uint32_t x, y;
    };
    std::vector<Placement> placed;
    placed.reserve(images.size());
    uint32_t x = 0, y = 0, shelf = 0;
    for (const auto& [id, image] : images) {
        if (!image || !image->valid() || image->size.width > atlasWidth || image->size.height > maxHeight) continue;
        const auto size = image->size;
        if (x + size.width > atlasWidth) {
            x = 0;
            y += shelf + 1;
            shelf = 0;
        }
        if (y + size.height > maxHeight) break;
        placed.push_back({&id, image.get(), x, y});
        x += size.width + 1;
        shelf = std::max(shelf, size.height);
    }
    const uint32_t atlasHeight = std::max(1u, y + shelf);
    PremultipliedImage image({atlasWidth, atlasHeight});
    image.fill(0);
    for (const auto& p : placed) {
        const auto size = p.image->size;
        PremultipliedImage::copy(*p.image, image, {0, 0}, {p.x, p.y}, size);
        // Half a texel in from each edge, so the linear filter never reads the neighbour.
        plateUVs[*p.id] = {(p.x + 0.5f) / atlasWidth,
                           (p.y + 0.5f) / atlasHeight,
                           (p.x + size.width - 0.5f) / atlasWidth,
                           (p.y + size.height - 0.5f) / atlasHeight};
    }
    if (!plateAtlas) {
        plateAtlas = context.createTexture2D();
        plateAtlas->setSamplerConfiguration({.filter = gfx::TextureFilterType::Linear,
                                             .wrapU = gfx::TextureWrapType::Clamp,
                                             .wrapV = gfx::TextureWrapType::Clamp});
    }
    plateAtlas->upload(image);
    if (plateSlot.drawable) plateSlot.drawable->setTexture(plateAtlas, idVehicleModelLabelTexture);
}

void RenderVehicleModelLayer::disable() {
    for (auto& kind : partSlots) {
        for (auto& part : kind) {
            for (auto& slot : part) {
                if (slot.drawable) slot.drawable->setEnabled(false);
            }
        }
    }
    if (shadowSlot.drawable) shadowSlot.drawable->setEnabled(false);
    if (labelSlot.drawable) labelSlot.drawable->setEnabled(false);
    if (plateSlot.drawable) plateSlot.drawable->setEnabled(false);
}

void RenderVehicleModelLayer::update(gfx::ShaderRegistry& shaders,
                                     gfx::Context& context,
                                     const TransformState& state,
                                     const std::shared_ptr<UpdateParameters>&,
                                     const PaintParameters& paintParameters,
                                     const RenderTree&,
                                     UniqueChangeRequestVec& changes) {
    const auto began = std::chrono::steady_clock::now();
    const auto& layer = impl(baseImpl);
    auto& runtime = *layer.runtime;
    const double zoom = state.getZoom();
    const double fade = std::clamp((zoom - layer.fadeFrom) / (layer.fadeTo - layer.fadeFrom), 0.0, 1.0);

    const auto stop = [&]() {
        disable();
        moving = false;
        std::lock_guard lock(runtime.mutex);
        runtime.hits.clear();
        runtime.stats.drawing = false;
        runtime.stats.zoom = zoom;
        runtime.stats.vehicles = 0;
        runtime.stats.parts = 0;
        runtime.wakeAtMs = std::numeric_limits<double>::infinity();
    };

    // Below the hand-over the map is flat Mercator or a globe; the models are drawn only when zoomed in close.
    const auto& samples = layer.samples;
    if (fade <= 0.0 || !layer.models || !samples || samples->sampleCount == 0 || samples->vehicles.empty() ||
        state.isGlobeRendering()) {
        stop();
        return;
    }

    if (layer.models != loadedModels) {
        loadedModels = layer.models;
        if (!loadModels(*layer.models)) {
            models = {};
        }
        builtDrawables = 0;
    }

    if (!modelShader) modelShader = context.getGenericShader(shaders, "VehicleModelShader", gfx::ProjectionVariant::Mercator);
    if (!shadowShader) {
        shadowShader = context.getGenericShader(shaders, "VehicleModelShadowShader", gfx::ProjectionVariant::Mercator);
    }
    if (!labelShader) {
        labelShader = context.getGenericShader(shaders, "VehicleModelLabelShader", gfx::ProjectionVariant::Mercator);
    }
    if (!modelShader || !shadowShader || !labelShader) {
        stop();
        return;
    }

    // The map can take every drawable away (a theme change, a lost context): they are built again.
    if (builtDrawables == 0 || !layerGroup || layerGroup->getDrawableCount() != builtDrawables) {
        buildDrawables(context, changes);
        atlasSource.reset();
        plateAtlasSource.reset();
    }
    if (!layerGroup || builtDrawables == 0) {
        stop();
        return;
    }
    if (layer.labels != atlasSource) {
        atlasSource = layer.labels;
        buildLabelAtlas(context, *layer.labels);
    }

    // The licence plates, zoomed in close (macrocity/app#935): each vehicle's plate in their atlas, found once for
    // each set of vehicles and of plates, so a frame looks up nothing by name.
    const double plateFade = std::clamp((zoom - layer.plateFrom) / (layer.plateTo - layer.plateFrom), 0.0, 1.0);
    const bool platesShown = plateFade > 0.0 && layer.plates && !layer.plates->empty();
    if (platesShown && layer.plates != plateAtlasSource) {
        plateAtlasSource = layer.plates;
        buildPlateAtlas(context, *layer.plates);
    }
    if (platesShown && (samples != platesFor || plateAtlasSource != platesFrom)) {
        platesFor = samples;
        platesFrom = plateAtlasSource;
        vehiclePlates.assign(samples->vehicles.size(), -1);
        vehiclePlateUVs.clear();
        for (std::size_t v = 0; v < samples->vehicles.size(); ++v) {
            const auto& vehicle = samples->vehicles[v];
            if (vehicle.kind >= maxKinds || models[vehicle.kind].plates.empty()) continue;
            const auto found = plateUVs.find(vehicle.id);
            if (found == plateUVs.end()) continue;
            vehiclePlates[v] = static_cast<int32_t>(vehiclePlateUVs.size());
            vehiclePlateUVs.push_back(found->second);
        }
    }

    // The camera: world points from an origin at the map's centre (small numbers, exact in float) and metres up.
    const double scale = state.getScale();
    const double worldSize = Projection::worldSize(scale);
    const auto origin = Projection::project(state.getLatLng(), scale);
    mat4 matrix = paintParameters.transformParams.nearClippedProjMatrix;
    matrix::translate(matrix, matrix, origin.x, origin.y, 0);
    const auto size = state.getSize();
    const double width = size.width;
    const double height = size.height;

    const double nowMs = std::chrono::duration<double, std::milli>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    const uint32_t sampleCount = samples->sampleCount;
    const double k = std::clamp((nowMs - samples->startMs) / samples->stepMs, 0.0, double(sampleCount - 1));
    const auto i0 = static_cast<uint32_t>(std::floor(k));
    const double* tracks = samples->tracks.data();
    const std::size_t perTrack = std::size_t(sampleCount) * 3;
    const std::size_t trackCount = samples->tracks.size() / std::max<std::size_t>(1, perTrack);
    const std::size_t partTrackCount = samples->partTracks.size() / std::max<std::size_t>(1, perTrack);

    const auto toScreen = [&](const std::array<double, 4>& clip, float& sx, float& sy) {
        sx = static_cast<float>((clip[0] / clip[3] + 1.0) * 0.5 * width);
        sy = static_cast<float>((1.0 - clip[1] / clip[3]) * 0.5 * height);
    };
    const auto metresPerPoint = [&](double lat) {
        return worldSize / (util::M2PI * util::EARTH_RADIUS_M * std::cos(util::deg2rad(lat)));
    };

    // The vehicles on screen.
    drawn.clear();
    bool anyMoves = false;
    double wakeAt = std::numeric_limits<double>::infinity();
    for (uint32_t v = 0; v < samples->vehicles.size(); ++v) {
        const auto& vehicle = samples->vehicles[v];
        if (vehicle.kind >= maxKinds || models[vehicle.kind].parts.empty() || vehicle.track >= trackCount) continue;
        const float alpha = static_cast<float>(std::clamp(double(vehicle.opacity), 0.0, 1.0) * fade);
        if (alpha <= 0.01f) continue;
        const double* track = tracks + vehicle.track * perTrack;
        Drawn item{v, 0, 0, 0, alpha, 0};
        sampleTrack(track, sampleCount, k, item.lon, item.lat, item.bearing);
        const auto p = Projection::project(LatLng(item.lat, item.lon), scale);
        const auto clip = transform(matrix, p.x - origin.x, p.y - origin.y, 1.5);
        // A long vehicle (a tram, a train) shows while its middle is further off the screen.
        const double reach = 1.4 + models[vehicle.kind].length / 45.0;
        if (clip[3] <= 0 || std::abs(clip[0]) > reach * clip[3] || std::abs(clip[1]) > reach * clip[3]) continue;
        item.depth = static_cast<float>(clip[3]);
        drawn.push_back(item);
        if (trackMoves(track, sampleCount, i0, i0 + 2)) {
            anyMoves = true;
        } else {
            const auto next = nextMove(track, sampleCount, i0);
            if (next < sampleCount) wakeAt = std::min(wakeAt, samples->startMs + next * samples->stepMs);
        }
    }

    // Records: every part, grouped by model part and look so each draw call reads one run of them, then the pills.
    struct Group {
        uint32_t count = 0;
        uint32_t base = 0;
        uint32_t written = 0;
    };
    std::array<std::array<std::array<Group, 2>, maxParts>, maxKinds> groups{};
    uint32_t partTotal = 0;
    for (const auto& item : drawn) {
        const auto& vehicle = samples->vehicles[item.vehicle];
        const auto parts = static_cast<uint32_t>(models[vehicle.kind].parts.size());
        if (partTotal + parts > maxRecords) break;
        const bool faded = item.alpha < 0.995f;
        for (uint32_t p = 0; p < parts; ++p) groups[vehicle.kind][p][faded].count++;
        partTotal += parts;
    }
    uint32_t next = 0;
    for (auto& kind : groups) {
        for (auto& part : kind) {
            for (auto& look : part) {
                look.base = next;
                next += look.count;
            }
        }
    }

    hits.clear();
    plateRecords.clear();
    float* out = records.data();
    uint32_t partsWritten = 0;
    for (const auto& item : drawn) {
        const auto& vehicle = samples->vehicles[item.vehicle];
        const auto& model = models[vehicle.kind];
        const auto parts = static_cast<uint32_t>(model.parts.size());
        if (partsWritten + parts > partTotal) break;
        partsWritten += parts;
        const bool faded = item.alpha < 0.995f;
        style::VehicleModelRuntime::Hit hit;
        hit.vehicle = item.vehicle;
        hit.depth = item.depth;
        hit.bodyMinX = hit.bodyMinY = std::numeric_limits<float>::infinity();
        hit.bodyMaxX = hit.bodyMaxY = -std::numeric_limits<float>::infinity();
        for (uint32_t p = 0; p < parts; ++p) {
            const auto& part = model.parts[p];
            double lon = item.lon, lat = item.lat, bearing = item.bearing;
            if (parts > 1) {
                const auto partTrack = static_cast<std::size_t>(vehicle.parts) + p;
                if (vehicle.parts >= 0 && partTrack < partTrackCount) {
                    sampleTrack(samples->partTracks.data() + partTrack * perTrack, sampleCount, k, lon, lat, bearing);
                } else {
                    // No rail: the parts stand straight along the vehicle's own heading.
                    const double h = util::deg2rad(item.bearing);
                    lat = item.lat + std::cos(h) * part.centerZ / 111320.0;
                    lon = item.lon + std::sin(h) * part.centerZ / (111320.0 * std::cos(util::deg2rad(item.lat)));
                }
            }
            const auto p2 = Projection::project(LatLng(lat, lon), scale);
            const double dx = p2.x - origin.x;
            const double dy = p2.y - origin.y;
            const double h = util::deg2rad(bearing);
            const double c = std::cos(h), s = std::sin(h);
            const double kpm = metresPerPoint(lat);
            auto& group = groups[vehicle.kind][p][faded];
            float* r = out + std::size_t(group.base + group.written++) * recordFloats;
            r[0] = static_cast<float>(dx);
            r[1] = static_cast<float>(dy);
            r[2] = static_cast<float>(c);
            r[3] = static_cast<float>(s);
            r[4] = vehicle.tint.r;
            r[5] = vehicle.tint.g;
            r[6] = vehicle.tint.b;
            r[7] = item.alpha;
            r[8] = static_cast<float>(kpm);
            r[9] = part.halfLength;
            r[10] = model.halfWidth;
            r[11] = 0;
            // Its plates: flat on the face at either end, turned with it. One seen from behind, edge on or too
            // small to read is not drawn; a small one fades in as it grows.
            const int32_t plate = platesShown && item.vehicle < vehiclePlates.size() ? vehiclePlates[item.vehicle] : -1;
            for (const auto& mount : model.plates) {
                if (plate < 0 || mount.part != p || plateRecords.size() + recordFloats > plateRecords.capacity()) break;
                const double facing = mount.z > 0 ? 1.0 : -1.0;
                const double hw = mount.halfWidth * facing;
                const double cx = dx + s * mount.z * kpm;
                const double cy = dy - c * mount.z * kpm;
                // The corners as the one looking at the plate sees them: bottom left, bottom right, top right.
                std::array<float, 6> corner{};
                bool behind = false;
                for (int i = 0; i < 3; ++i) {
                    const double lx = (i == 0 ? -1 : 1) * hw;
                    const double up = mount.y + (i == 2 ? 1 : -1) * mount.halfHeight;
                    const auto clip = transform(matrix, cx - c * lx * kpm, cy - s * lx * kpm, up);
                    if (clip[3] <= 0) {
                        behind = true;
                        break;
                    }
                    toScreen(clip, corner[i * 2], corner[i * 2 + 1]);
                }
                if (behind) continue;
                // Bottom edge across, and the right edge up: their cross product is negative when the plate faces
                // the camera (screen y runs down), its size the plate's area on screen.
                const float ax = corner[2] - corner[0], ay = corner[3] - corner[1];
                const float bx = corner[4] - corner[2], by = corner[5] - corner[3];
                const float cross = ax * by - ay * bx;
                const float across = std::hypot(ax, ay);
                if (cross >= 0 || across < 1e-3f) continue;
                const float tall = -cross / across;
                const float legible = std::clamp((across - 6.0f) / 6.0f, 0.0f, 1.0f) *
                                      std::clamp((tall - 1.0f) / 1.0f, 0.0f, 1.0f);
                const float plateAlpha = item.alpha * static_cast<float>(plateFade) * legible;
                if (plateAlpha <= 0.01f) continue;
                const auto& uv = vehiclePlateUVs[plate];
                plateRecords.insert(plateRecords.end(),
                                    {static_cast<float>(cx),
                                     static_cast<float>(cy),
                                     static_cast<float>(c * kpm),
                                     static_cast<float>(s * kpm),
                                     uv[0],
                                     uv[1],
                                     uv[2],
                                     uv[3],
                                     mount.y,
                                     static_cast<float>(hw),
                                     mount.halfHeight,
                                     plateAlpha});
            }
            // The part's box on screen, for taps.
            for (int corner = 0; corner < 8; ++corner) {
                const double lx = (corner & 1 ? 1 : -1) * model.halfWidth;
                const double lz = (corner & 2 ? 1 : -1) * part.halfLength;
                const double up = corner & 4 ? model.labelHeight - 0.4 : 0.0;
                const auto clip = transform(
                    matrix, dx + (-c * lx + s * lz) * kpm, dy + (-s * lx - c * lz) * kpm, up);
                if (clip[3] <= 0) continue;
                float sx, sy;
                toScreen(clip, sx, sy);
                hit.bodyMinX = std::min(hit.bodyMinX, sx);
                hit.bodyMaxX = std::max(hit.bodyMaxX, sx);
                hit.bodyMinY = std::min(hit.bodyMinY, sy);
                hit.bodyMaxY = std::max(hit.bodyMaxY, sy);
            }
        }
        if (hit.bodyMinX <= hit.bodyMaxX) hits.push_back(hit);
    }

    // The pills, the farthest first so a nearer one lies over it.
    labelOrder.clear();
    for (uint32_t i = 0; i < hits.size(); ++i) {
        if (atlas.contains(samples->vehicles[hits[i].vehicle].labelKey)) labelOrder.push_back(i);
    }
    std::ranges::sort(labelOrder, [&](uint32_t a, uint32_t b) { return hits[a].depth > hits[b].depth; });
    const uint32_t labelBase = partTotal;
    uint32_t labelCount = 0;
    for (const auto i : labelOrder) {
        if (labelBase + labelCount >= maxRecords) break;
        auto& hit = hits[i];
        const auto& vehicle = samples->vehicles[hit.vehicle];
        const auto& entry = atlas.at(vehicle.labelKey);
        const auto& item = *std::ranges::find_if(drawn, [&](const Drawn& d) { return d.vehicle == hit.vehicle; });
        const auto p = Projection::project(LatLng(item.lat, item.lon), scale);
        const float labelHeight = models[vehicle.kind].labelHeight;
        constexpr float gap = 4;
        float* r = out + std::size_t(labelBase + labelCount++) * recordFloats;
        r[0] = static_cast<float>(p.x - origin.x);
        r[1] = static_cast<float>(p.y - origin.y);
        r[2] = labelHeight;
        r[3] = item.alpha;
        std::copy(entry.uv.begin(), entry.uv.end(), r + 4);
        r[8] = entry.width / 2;
        r[9] = entry.height;
        r[10] = gap;
        r[11] = 0;
        const auto clip = transform(matrix, p.x - origin.x, p.y - origin.y, labelHeight);
        if (clip[3] > 0) {
            float sx, sy;
            toScreen(clip, sx, sy);
            hit.hasPill = true;
            hit.pillMinX = sx - entry.width / 2;
            hit.pillMaxX = sx + entry.width / 2;
            hit.pillMaxY = sy - gap;
            hit.pillMinY = sy - gap - entry.height;
        }
    }

    // The plates, after the pills, as many as there is room for.
    const uint32_t plateBase = labelBase + labelCount;
    const auto plateCount = static_cast<uint32_t>(
        std::min<std::size_t>(plateRecords.size() / recordFloats, maxRecords - std::min<std::size_t>(maxRecords, plateBase)));
    if (plateCount > 0) {
        std::copy_n(plateRecords.data(), std::size_t(plateCount) * recordFloats, out + std::size_t(plateBase) * recordFloats);
    }

    // The draw calls: one per model part and look, the shadows of every part, the pills, the plates.
    const auto setSlot = [&](const Slot& slot, uint32_t base, uint32_t count, float mode = 0) {
        if (!slot.drawable) return;
        slot.drawable->setEnabled(count > 0);
        if (count == 0) return;
        slot.segment->instanceCount = count;
        const VehicleModelDrawableUBO ubo{static_cast<float>(base), mode, 0, 0};
        slot.drawable->mutableUniformBuffers().createOrUpdate(idVehicleModelDrawableUBO, &ubo, sizeof(ubo), context);
    };
    for (std::size_t kind = 0; kind < maxKinds; ++kind) {
        for (std::size_t p = 0; p < maxParts; ++p) {
            const auto& solid = groups[kind][p][0];
            const auto& faded = groups[kind][p][1];
            setSlot(partSlots[kind][p][Solid], solid.base, solid.count);
            setSlot(partSlots[kind][p][FadedDepth], faded.base, faded.count);
            setSlot(partSlots[kind][p][FadedColor], faded.base, faded.count);
        }
    }
    setSlot(shadowSlot, 0, partTotal);
    setSlot(labelSlot, labelBase, labelAtlas ? labelCount : 0);
    // Mode 1: the pills' shader draws plates on the bodies.
    setSlot(plateSlot, plateBase, plateAtlas ? plateCount : 0, 1);

    VehicleModelPropsUBO props;
    props.matrix = util::cast<float>(matrix);
    props.light = lightDirection;
    props.shade = layer.dark ? std::array<float, 4>{0.42f, 0.42f, 1.5f, 0.12f}
                             : std::array<float, 4>{0.5f, 0.45f, 1.25f, 0.12f};
    props.shadow = {layer.dark ? 0.45f : 0.28f, 0.9f, 0, 0};
    props.viewport = {static_cast<float>(width), static_cast<float>(height), 0, 0};
    auto& groupBuffers = layerGroup->mutableUniformBuffers();
    groupBuffers.createOrUpdate(idVehicleModelPropsUBO, &props, sizeof(props), context);
    groupBuffers.createOrUpdate(
        idVehicleModelInstancesUBO, records.data(), records.size() * sizeof(float), context);

    moving = anyMoves;

    const auto ended = std::chrono::steady_clock::now();
    const double updateMs = std::chrono::duration<double, std::milli>(ended - began).count();
    if (secondFrames == 0) secondStart = began;
    secondFrames++;
    secondUpdateMs += updateMs;
    const double second = std::chrono::duration<double>(ended - secondStart).count();
    if (second >= 1.0) {
        framesPerSecond = second < 2.0 ? secondFrames / second : 0.0;
        meanUpdateMs = secondUpdateMs / secondFrames;
        secondFrames = 0;
        secondUpdateMs = 0;
    }

    std::lock_guard lock(runtime.mutex);
    runtime.samples = samples;
    runtime.hits.assign(hits.begin(), hits.end());
    runtime.wakeAtMs = anyMoves ? nowMs : wakeAt;
    runtime.stats.drawing = true;
    runtime.stats.zoom = zoom;
    runtime.stats.vehicles = static_cast<uint32_t>(hits.size());
    runtime.stats.parts = partTotal;
    runtime.stats.framesPerSecond = framesPerSecond;
    runtime.stats.meanUpdateMs = meanUpdateMs;
}

} // namespace mln
