#pragma once

#include <mln/util/subdivision_granularity.hpp>
#include <mln/style/layer_properties.hpp>
#include <mln/style/source_impl.hpp>
#include <mln/style/types.hpp>
#include <mln/tile/tile.hpp>
#include <mln/tile/tile_cache.hpp>
#include <mln/tile/tile_id.hpp>
#include <mln/tile/tile_observer.hpp>

#include <mln/util/mat4.hpp>
#include <mln/util/feature.hpp>
#include <mln/util/range.hpp>
#include <mln/util/chrono.hpp>

#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include <functional>
#include <map>
#include <set>

namespace mln {

class PaintParameters;
class TransformState;
class RenderLayer;
class RenderedQueryOptions;
class SourceQueryOptions;
class TileParameters;
class SourcePrepareParameters;

class TilePyramid {
public:
    TilePyramid(const TaggedScheduler& threadPool_);
    ~TilePyramid();

    bool isLoaded() const;

    void update(const std::vector<Immutable<style::LayerProperties>>& visibleLayers,
                bool needsRendering,
                bool needsRelayout,
                const TileParameters&,
                const style::Source::Impl&,
                uint16_t tileSize,
                Range<uint8_t> zoomRange,
                std::optional<LatLngBounds> bounds,
                std::function<std::unique_ptr<Tile>(const OverscaledTileID&, TileObserver*)> createTile);

    const std::map<UnwrappedTileID, std::reference_wrapper<Tile>>& getRenderedTiles() const { return renderedTiles; }
    Tile* getTile(const OverscaledTileID&);
    const Tile* getRenderedTile(const UnwrappedTileID&) const;

    void handleWrapJump(float lng);

    std::unordered_map<std::string, std::vector<Feature>> queryRenderedFeatures(
        const ScreenLineString& geometry,
        const TransformState& transformState,
        const std::unordered_map<std::string, const RenderLayer*>&,
        const RenderedQueryOptions& options,
        const mat4& projMatrix,
        const mln::SourceFeatureState& featureState) const;

    std::vector<Feature> querySourceFeatures(const SourceQueryOptions&) const;

    void setCacheEnabled(bool);
    void reduceMemoryUse();

    void setObserver(TileObserver*);
    void dumpDebugLogs() const;

    const std::map<OverscaledTileID, std::unique_ptr<Tile>>& getTiles() const { return tiles; }
    void clearAll();

    void updateFadingTiles();
    bool hasFadingTiles() const { return fadingTiles || lodArriving; }

    /// The blend between two levels of detail. Vector tiles of two zooms are generalised differently (a park's
    /// outline, which small parks there are, a road's shape), and the cover swapping one level for the next in a frame
    /// made the map jump at every whole zoom. Each tile of the cover replaced its parent where the zoom (on the globe,
    /// its distance from the camera too) passed a whole zoom; for `lodBlendZooms` past that, fill and line layers draw
    /// the parent as well. Every pixel takes either level, never both, so a half transparent layer is never drawn
    /// twice: a dither, its share of the children growing from none to all with the zoom, so the change follows the
    /// zoom and stops when it stops. A level not loaded yet leaves the other one whole and comes in over
    /// `lodArrivalDuration` when it is.
    static constexpr double lodBlendZooms = 0.5;
    static constexpr Duration lodArrivalDuration = std::chrono::milliseconds(300);
    /// A child's share of the pixels is one of `lodShareSteps` + 1 values, so that a parent's `tile_fade` holds its
    /// four children's (one per quadrant of its extent, `lodShareBits` each) exactly in a float.
    static constexpr int lodShareBits = 6;
    static constexpr int lodShareSteps = (1 << lodShareBits) - 1;
    struct LodBlend {
        /// The tile's `tile_fade` in fill and line shaders: the share of the pixels it draws (a child); or, for a
        /// parent under its children, -1 less its children's shares packed per quadrant, the pixels it draws being
        /// the others.
        float fade = 1.0f;
        /// Drawn under its children, outside the tile clipping masks; the shaders keep it to its own extent.
        bool parent = false;
        /// Not in the cover: only fill and line layers draw it.
        bool extra = false;
        /// In the cover, but drawn by the parent that stands for it in fill and line layers.
        bool hidden = false;
    };
    LodBlend lodBlend(const UnwrappedTileID&) const;

private:
    void addRenderTile(const UnwrappedTileID& tileID, Tile& tile);

    std::map<OverscaledTileID, std::unique_ptr<Tile>> tiles;
    TileCache cache;
    SubdivisionGranularitySetting lastSubdivisionGranularity = SubdivisionGranularitySetting::none();

    std::map<UnwrappedTileID, std::reference_wrapper<Tile>> renderedTiles; // Sorted by tile id.
    TileObserver* observer = nullptr;

    float prevLng = 0;

    bool fadingTiles = false;
    bool cacheEnabled = true;

    // the level blend (`lodBlend`): each child's, and this frame's tiles
    struct LodState {
        enum class Kind { Both, ParentOnly } kind;
        double shown = 1;
        double from = 1;
        TimePoint since;
    };
    std::map<UnwrappedTileID, LodState> lodStates;
    std::map<UnwrappedTileID, LodBlend> lodBlends;
    bool lodArriving = false;
    void updateLodBlends(const TileParameters&,
                         const std::vector<OverscaledTileID>& idealTiles,
                         const std::vector<double>& splitZooms,
                         Range<uint8_t> zoomRange,
                         const std::function<Tile*(const OverscaledTileID&)>& getTile,
                         const std::function<Tile*(const OverscaledTileID&)>& createTile,
                         const std::function<void(Tile&, TileNecessity)>& retainTile);
};

} // namespace mln
