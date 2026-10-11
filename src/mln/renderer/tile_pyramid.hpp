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
    bool hasFadingTiles() const { return fadingTiles || geometryFading; }

    /// The geometry crossfade as the tile cover changes (a whole zoom crossed, a tile loaded over the placeholder that
    /// stood in for it): vector tiles of one source are generalised differently at each zoom, and swapping one set for
    /// the next in a frame made the map jump. A tile newly drawn comes in over the first half of `geometryFadeDuration`;
    /// a tile no longer in the cover stays drawn under it ("retiring") and goes over the second half. Where both draw
    /// the same thing the new one already covers the old one, so nothing dims; what only the old one drew fades out.
    /// Only layers that take the retiring tiles (`RenderLayer::fadesTileGeometry`, fill and line) draw them.
    static constexpr Duration geometryFadeDuration = std::chrono::milliseconds(300);
    struct TileFade {
        float opacity = 1.0f;
        bool retiring = false;
    };
    /// The tile's crossfade at `now` (Continuous mode only; a whole tile otherwise).
    TileFade tileFade(const UnwrappedTileID&, TimePoint now) const;

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

    // the geometry crossfade (`tileFade`): when each drawn tile came in, when each retiring one left the cover
    bool continuous = false;
    bool geometryFading = false;
    std::map<UnwrappedTileID, TimePoint> shownAt;
    std::map<UnwrappedTileID, TimePoint> retiredAt;
    std::set<UnwrappedTileID> retiring;
};

} // namespace mln
