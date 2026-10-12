#include <mln/renderer/tile_pyramid.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_source.hpp>
#include <mln/renderer/tile_parameters.hpp>
#include <mln/renderer/query.hpp>
#include <mln/map/transform.hpp>
#include <mln/math/clamp.hpp>
#include <mln/actor/scheduler.hpp>
#include <mln/util/tile_cover.hpp>
#include <mln/util/tile_range.hpp>
#include <mln/util/enum.hpp>
#include <mln/util/logging.hpp>

#include <mln/algorithm/update_renderables.hpp>

#include <mapbox/geometry/envelope.hpp>

#include <cmath>
#include <algorithm>

namespace mln {

using namespace style;

namespace {
TileObserver nullObserver;
const std::map<OverscaledTileID, std::unique_ptr<Tile>> emptyPrefetchedTiles;
} // namespace

TilePyramid::TilePyramid(const TaggedScheduler& threadPool_)
    : cache(threadPool_),
      observer(&nullObserver) {}

TilePyramid::~TilePyramid() = default;

bool TilePyramid::isLoaded() const {
    for (const auto& pair : tiles) {
        if (!pair.second->isComplete()) {
            return false;
        }
    }

    return true;
}

Tile* TilePyramid::getTile(const OverscaledTileID& tileID) {
    auto it = tiles.find(tileID);
    return it == tiles.end() ? cache.get(tileID) : it->second.get();
}

const Tile* TilePyramid::getRenderedTile(const UnwrappedTileID& tileID) const {
    auto it = renderedTiles.find(tileID);
    return it != renderedTiles.end() ? &it->second.get() : nullptr;
}

void TilePyramid::update(const std::vector<Immutable<style::LayerProperties>>& layers,
                         const bool needsRendering,
                         const bool needsRelayout,
                         const TileParameters& parameters,
                         const style::Source::Impl& sourceImpl,
                         const uint16_t tileSize,
                         const Range<uint8_t> zoomRange,
                         std::optional<LatLngBounds> bounds,
                         std::function<std::unique_ptr<Tile>(const OverscaledTileID&, TileObserver*)> createTile) {
    const auto& subdivisionGranularity = parameters.subdivisionGranularity;
    const bool relayout = needsRelayout || subdivisionGranularity != lastSubdivisionGranularity;
    lastSubdivisionGranularity = subdivisionGranularity;

    // If we need a relayout, abandon any cached tiles; they're now stale.
    if (relayout) {
        cache.clear();
    }

    // If we're not going to render anything, move our existing tiles into
    // the cache (if they're not stale) or abandon them, and return.
    if (!needsRendering) {
        for (auto& entry : tiles) {
            if (!relayout) {
                // These tiles are invisible, we set optional necessity
                // for them and thus suppress network requests on
                // tiles expiration (see `OnlineFileRequest`).
                entry.second->setNecessity(TileNecessity::Optional);
                cache.add(entry.first, std::move(entry.second));
            } else {
                cache.deferredRelease(std::move(entry.second));
            }
        }

        tiles.clear();
        renderedTiles.clear();
        cache.deferPendingReleases();

        return;
    }

    handleWrapJump(static_cast<float>(parameters.transformState.getLatLng().longitude()));

    // Optionally shift the zoom level
    double zoom = util::clamp<double>(parameters.transformState.getZoom() + parameters.tileLodZoomShift,
                                      parameters.transformState.getMinZoom(),
                                      parameters.transformState.getMaxZoom());

    const auto type = sourceImpl.type;
    // Determine the overzooming/underzooming amounts and required tiles.
    int32_t overscaledZoom = util::coveringZoomLevel(zoom, type, tileSize);
    int32_t tileZoom = overscaledZoom;
    int32_t panZoom = zoomRange.max;

    const std::optional<uint8_t>& sourcePrefetchZoomDelta = sourceImpl.getPrefetchZoomDelta();
    const std::optional<uint8_t>& maxParentTileOverscaleFactor = sourceImpl.getMaxOverscaleFactorForParentTiles();
    const Duration minimumUpdateInterval = sourceImpl.getMinimumTileUpdateInterval();
    const bool isVolatile = sourceImpl.isVolatile();

    std::vector<OverscaledTileID> idealTiles;
    std::vector<double> splitZooms;
    std::vector<OverscaledTileID> panTiles;

    util::TileCoverParameters tileCoverParameters = {.transformState = parameters.transformState,
                                                     .tileLodMinRadius = parameters.tileLodMinRadius,
                                                     .tileLodScale = parameters.tileLodScale,
                                                     .tileLodPitchThreshold = parameters.tileLodPitchThreshold,
                                                     .tileLodMode = parameters.tileLodMode};

    if (std::cmp_greater_equal(overscaledZoom, zoomRange.min)) {
        int32_t idealZoom = std::min<int32_t>(zoomRange.max, overscaledZoom);

        // Make sure we're not reparsing overzoomed raster tiles.
        if (type == SourceType::Raster) {
            tileZoom = idealZoom;
        }

        // Only attempt prefetching in continuous mode.
        if (parameters.mode == MapMode::Continuous && type != style::SourceType::GeoJSON &&
            type != style::SourceType::Annotations) {
            // Request lower zoom level tiles (if configured to do so) in an attempt
            // to show something on the screen faster at the cost of a little of bandwidth.
            const uint8_t prefetchZoomDelta = sourcePrefetchZoomDelta ? *sourcePrefetchZoomDelta
                                                                      : parameters.prefetchZoomDelta;
            if (prefetchZoomDelta) {
                panZoom = std::max<int32_t>(tileZoom - prefetchZoomDelta, zoomRange.min);
            }

            if (panZoom < idealZoom) {
                panTiles = util::tileCover(tileCoverParameters, panZoom, zoomRange);
            }
        }

        idealTiles = util::tileCover(tileCoverParameters, idealZoom, zoomRange, tileZoom, &splitZooms);
        if (parameters.mode == MapMode::Tile && type != SourceType::Raster && type != SourceType::RasterDEM &&
            idealTiles.size() > 1) {
            mln::Log::Warning(mln::Event::General,
                              "Provided camera options returned " + std::to_string(idealTiles.size()) +
                                  " tiles, only " + util::toString(idealTiles[0]) + " is taken in Tile mode.");
            idealTiles = {idealTiles[0]};
        }
    }

    // Stores a list of all the tiles that we're definitely going to retain.
    // There are two kinds of tiles we need: the ideal tiles determined by the
    // tile cover. They may not yet be in use because they're still loading. In
    // addition to that, we also need to retain all tiles that we're actively
    // using, e.g. as a replacement for tile that aren't loaded yet.
    std::set<OverscaledTileID> retain;

    auto retainTileFn = [&](Tile& tile, TileNecessity necessity) -> void {
        if (retain.emplace(tile.id).second) {
            tile.setUpdateParameters({.minimumUpdateInterval = minimumUpdateInterval, .isVolatile = isVolatile});
            tile.setNecessity(necessity);
        }

        if (relayout) {
            tile.setSubdivisionGranularity(subdivisionGranularity);
            tile.setLayers(layers);
        }
    };
    auto getTileFn = [&](const OverscaledTileID& tileID) -> Tile* {
        auto it = tiles.find(tileID);
        return it == tiles.end() ? nullptr : it->second.get();
    };

    // The min and max zoom for TileRange are based on the updateRenderables
    // algorithm. Tiles are created at the ideal tile zoom or at lower zoom
    // levels. Child tiles are used from the cache, but not created.
    // A cover that picks each tile's zoom by its distance from the camera asks for tiles finer than the nominal
    // zoom, up to the source's maximum; the globe cover does, like `TileLodMode::Distance`.
    std::optional<util::TileRange> tileRange = std::nullopt;
    if (bounds) {
        const bool variableZoom = parameters.tileLodMode == TileLodMode::Distance ||
                                  parameters.transformState.isGlobeRendering();
        const int32_t maxZoom = variableZoom ? zoomRange.max : std::min(tileZoom, static_cast<int32_t>(zoomRange.max));
        tileRange = util::TileRange::fromLatLngBounds(*bounds, zoomRange.min, maxZoom);
    }
    auto createTileFn = [&](const OverscaledTileID& tileID) -> Tile* {
        if (tileRange && !tileRange->contains(tileID.canonical)) {
            return nullptr;
        }
        std::unique_ptr<Tile> tile = cache.pop(tileID);
        if (!tile) {
            tile = createTile(tileID, observer);
            if (!tile) return nullptr;
            tile->setSubdivisionGranularity(subdivisionGranularity);
            tile->setLayers(layers);
        }

        return tiles.emplace(tileID, std::move(tile)).first->second.get();
    };

    auto previouslyRenderedTiles = std::move(renderedTiles);

    auto renderTileFn = [&](const UnwrappedTileID& tileID, Tile& tile) {
        addRenderTile(tileID, tile);
        previouslyRenderedTiles.erase(tileID); // Still rendering this tile, no need for special fading logic.
        tile.markRenderedIdeal();
    };

    renderedTiles.clear();

    if (!panTiles.empty()) {
        algorithm::updateRenderables(
            getTileFn,
            createTileFn,
            retainTileFn,
            [](const UnwrappedTileID&, Tile&) {},
            panTiles,
            emptyPrefetchedTiles,
            zoomRange,
            maxParentTileOverscaleFactor);
    }

    algorithm::updateRenderables(getTileFn,
                                 createTileFn,
                                 retainTileFn,
                                 renderTileFn,
                                 idealTiles,
                                 tiles,
                                 zoomRange,
                                 maxParentTileOverscaleFactor);

    for (auto previouslyRenderedTile : previouslyRenderedTiles) {
        Tile& tile = previouslyRenderedTile.second;
        tile.markRenderedPreviously();
        if (tile.holdForFade()) {
            // Since it was rendered in the last frame, we know we have it
            // Don't mark the tile "Required" to avoid triggering a new network request
            retainTileFn(tile, TileNecessity::Optional);
            addRenderTile(previouslyRenderedTile.first, tile);
        }
    }

    updateLodBlends(parameters, idealTiles, splitZooms, zoomRange, getTileFn, createTileFn, retainTileFn);

    if (type != SourceType::Annotations && cacheEnabled) {
        auto conservativeCacheSize = static_cast<size_t>(
            std::max(static_cast<double>(parameters.transformState.getSize().width) / tileSize, 1.0) *
            std::max(static_cast<double>(parameters.transformState.getSize().height) / tileSize, 1.0) *
            (parameters.transformState.getMaxZoom() - parameters.transformState.getMinZoom() + 1) * 0.5);
        cache.setSize(conservativeCacheSize);
    } else {
        cache.setSize(0);
    }

    // Remove stale tiles. This goes through the (sorted!) tiles map and retain
    // set in lockstep and removes items from tiles that don't have the
    // corresponding key in the retain set.
    {
        auto tilesIt = tiles.begin();
        auto retainIt = retain.begin();
        while (tilesIt != tiles.end()) {
            if (retainIt == retain.end() || tilesIt->first < *retainIt) {
                // Remove the tile from the map.
                // If it requires re-layout, discard it asynchronously, otherwise keep it in the cache
                const auto key = tilesIt->first;
                if (std::unique_ptr<Tile> tile = std::move(tiles.extract(tilesIt++).mapped())) {
                    if (relayout) {
                        cache.deferredRelease(std::move(tile));
                    } else {
                        tile->setNecessity(TileNecessity::Optional);
                        cache.add(key, std::move(tile));
                    }
                }
            } else {
                if (!(*retainIt < tilesIt->first)) {
                    ++tilesIt;
                }
                ++retainIt;
            }
        }
    }

    for (auto& pair : tiles) {
        pair.second->setShowCollisionBoxes(parameters.debugOptions & MapDebugOptions::Collision);
    }

    // Initialize renderable tiles and update the contained layer render data.
    for (auto& entry : renderedTiles) {
        Tile& tile = entry.second;
        assert(tile.isRenderable());
        tile.usedByRenderedLayers = false;

        const bool holdForFade = tile.holdForFade();
        for (const auto& layerProperties : layers) {
            const auto* typeInfo = layerProperties->baseImpl->getTypeInfo();
            if (holdForFade && typeInfo->fadingTiles == LayerTypeInfo::FadingTiles::NotRequired) {
                continue;
            }
            tile.usedByRenderedLayers |= tile.layerPropertiesUpdated(layerProperties);
        }
    }

    cache.deferPendingReleases();
}

void TilePyramid::handleWrapJump(float lng) {
    // On top of the regular z/x/y values, TileIDs have a `wrap` value that specify
    // which cppy of the world the tile belongs to. For example, at `lng: 10` you
    // might render z/x/y/0 while at `lng: 370` you would render z/x/y/1.
    //
    // When lng values get wrapped (going from `lng: 370` to `long: 10`) you expect
    // to see the same thing on the screen (370 degrees and 10 degrees is the same
    // place in the world) but all the TileIDs will have different wrap values.
    //
    // In order to make this transition seamless, we calculate the rounded difference of
    // "worlds" between the last frame and the current frame. If the map panned by
    // a world, then we can assign all the tiles new TileIDs with updated wrap values.
    // For example, assign z/x/y/1 a new id: z/x/y/0. It is the same tile, just rendered
    // in a different position.
    //
    // This enables us to reuse the tiles at more ideal locations and prevent flickering.

    const float lngDifference = lng - prevLng;
    const float worldDifference = lngDifference / 360.f;
    const auto wrapDelta = static_cast<int16_t>(std::round(worldDifference));
    prevLng = lng;

    if (wrapDelta) {
        std::map<OverscaledTileID, std::unique_ptr<Tile>> newTiles;
        std::map<UnwrappedTileID, std::reference_wrapper<Tile>> newRenderTiles;
        for (auto& tile : tiles) {
            auto newID = tile.second->id.unwrapTo(tile.second->id.wrap + wrapDelta);
            tile.second->id = newID;
            newTiles.emplace(newID, std::move(tile.second));
        }
        tiles = std::move(newTiles);

        for (auto& tile : renderedTiles) {
            UnwrappedTileID newID = tile.first.unwrapTo(tile.first.wrap + wrapDelta);
            newRenderTiles.emplace(newID, tile.second);
        }
        renderedTiles = std::move(newRenderTiles);
    }
}

std::unordered_map<std::string, std::vector<Feature>> TilePyramid::queryRenderedFeatures(
    const ScreenLineString& geometry,
    const TransformState& transformState,
    const std::unordered_map<std::string, const RenderLayer*>& layers,
    const RenderedQueryOptions& options,
    const mat4& projMatrix,
    const SourceFeatureState& featureState) const {
    std::unordered_map<std::string, std::vector<Feature>> result;
    if (renderedTiles.empty() || geometry.empty()) {
        return result;
    }

    LineString<double> queryGeometry;
    queryGeometry.reserve(geometry.size());

    for (const auto& p : geometry) {
        queryGeometry.push_back(
            TileCoordinate::fromScreenCoordinate(transformState, 0, {p.x, transformState.getSize().height - p.y}).p);
    }

    mapbox::geometry::box<double> box = mapbox::geometry::envelope(queryGeometry);

    auto cmp = [](const UnwrappedTileID& a, const UnwrappedTileID& b) {
        return std::tie(a.canonical.z, a.canonical.y, a.wrap, a.canonical.x) <
               std::tie(b.canonical.z, b.canonical.y, b.wrap, b.canonical.x);
    };

    std::map<UnwrappedTileID, std::reference_wrapper<Tile>, decltype(cmp)> sortedTiles{
        renderedTiles.begin(), renderedTiles.end(), cmp};

    auto maxPitchScaleFactor = transformState.maxPitchScaleFactor();

    for (const auto& entry : sortedTiles) {
        const UnwrappedTileID& id = entry.first;
        Tile& tile = entry.second;
        // a parent drawn only under its children to blend the two (`lodBlend`): its features are not the map's
        if (lodBlend(id).extra) continue;

        const auto scale = static_cast<float>(transformState.getScale() /
                                              (1 << id.canonical.z)); // equivalent to std::pow(2,
                                                                      // transformState.getZoom() - id.canonical.z);
        auto queryPadding = maxPitchScaleFactor * tile.getQueryPadding(layers) * util::EXTENT / util::tileSize_D /
                            scale;

        GeometryCoordinate tileSpaceBoundsMin = TileCoordinate::toGeometryCoordinate(id, box.min);
        if (tileSpaceBoundsMin.x - queryPadding >= util::EXTENT ||
            tileSpaceBoundsMin.y - queryPadding >= util::EXTENT) {
            continue;
        }

        GeometryCoordinate tileSpaceBoundsMax = TileCoordinate::toGeometryCoordinate(id, box.max);
        if (tileSpaceBoundsMax.x + queryPadding < 0 || tileSpaceBoundsMax.y + queryPadding < 0) {
            continue;
        }

        GeometryCoordinates tileSpaceQueryGeometry;
        tileSpaceQueryGeometry.reserve(queryGeometry.size());
        for (const auto& c : queryGeometry) {
            tileSpaceQueryGeometry.push_back(TileCoordinate::toGeometryCoordinate(id, c));
        }

        tile.queryRenderedFeatures(
            result, tileSpaceQueryGeometry, transformState, layers, options, projMatrix, featureState);
    }

    return result;
}

std::vector<Feature> TilePyramid::querySourceFeatures(const SourceQueryOptions& options) const {
    std::vector<Feature> result;

    for (const auto& pair : tiles) {
        pair.second->querySourceFeatures(result, options);
    }

    return result;
}

void TilePyramid::setCacheEnabled(bool enable) {
    cacheEnabled = enable;
}

void TilePyramid::reduceMemoryUse() {
    cache.clear();
}

void TilePyramid::setObserver(TileObserver* observer_) {
    observer = observer_;
}

void TilePyramid::dumpDebugLogs() const {
    for (const auto& pair : tiles) {
        pair.second->dumpDebugLogs();
    }
}

void TilePyramid::clearAll() {
    fadingTiles = false;
    lodArriving = false;
    lodStates.clear();
    lodBlends.clear();
    tiles.clear();
    renderedTiles.clear();
    cache.clear();
}

void TilePyramid::addRenderTile(const UnwrappedTileID& tileID, Tile& tile) {
    assert(tile.isRenderable());
    renderedTiles.emplace(tileID, tile);
}

TilePyramid::LodBlend TilePyramid::lodBlend(const UnwrappedTileID& id) const {
    const auto found = lodBlends.find(id);
    return found == lodBlends.end() ? LodBlend{} : found->second;
}

void TilePyramid::updateLodBlends(const TileParameters& parameters,
                                  const std::vector<OverscaledTileID>& idealTiles,
                                  const std::vector<double>& splitZooms,
                                  Range<uint8_t> zoomRange,
                                  const std::function<Tile*(const OverscaledTileID&)>& getTile,
                                  const std::function<Tile*(const OverscaledTileID&)>& createTile,
                                  const std::function<void(Tile&, TileNecessity)>& retainTile) {
    lodBlends.clear();
    lodArriving = false;
    if (parameters.mode != MapMode::Continuous || splitZooms.size() != idealTiles.size()) {
        lodStates.clear();
        return;
    }
    const TimePoint now = Clock::now();
    const auto smooth = [](double t) {
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3 - 2 * t);
    };
    // whether a tile of the cover other than the ideal ones (MapLibre's stand-ins) lies inside `area`
    const auto covered = [&](const UnwrappedTileID& area) {
        for (const auto& entry : renderedTiles) {
            const UnwrappedTileID& id = entry.first;
            if (id.wrap != area.wrap || id.canonical.z <= area.canonical.z) continue;
            const int dz = id.canonical.z - area.canonical.z;
            if ((id.canonical.x >> dz) == area.canonical.x && (id.canonical.y >> dz) == area.canonical.y) return true;
        }
        return false;
    };

    struct Parent {
        Tile* tile = nullptr;
        std::array<int, 4> share{{lodShareSteps, lodShareSteps, lodShareSteps, lodShareSteps}};
        bool drawn = false;
    };
    std::map<UnwrappedTileID, Parent> parents;
    std::map<UnwrappedTileID, LodState> states;
    for (std::size_t i = 0; i < idealTiles.size(); i++) {
        const OverscaledTileID& id = idealTiles[i];
        // how far past the zoom where the tile replaced its parent the cover is; a whole zoom or more, or a tile no
        // zoom brought in, and there is nothing to blend
        const double past = splitZooms[i] - id.canonical.z;
        if (!(past >= 0 && past < 1) || id.canonical.z == 0 || id.canonical.z - 1 < zoomRange.min ||
            id.overscaledZ != id.canonical.z) {
            continue;
        }
        const UnwrappedTileID child = id.toUnwrapped();
        const UnwrappedTileID parentID(id.wrap, CanonicalTileID(id.canonical.z - 1, id.canonical.x >> 1, id.canonical.y >> 1));
        const OverscaledTileID parentTileID(parentID.canonical.z, parentID.wrap, parentID.canonical);
        const bool blending = past < lodBlendZooms;
        Tile* parent = getTile(parentTileID);
        // within the blend the parent is the level the zoom goes back to: keep it, or ask for it (a zoom out)
        if (!parent && blending) parent = createTile(parentTileID);
        if (parent && blending) retainTile(*parent, TileNecessity::Required);
        if (!parent || !parent->isRenderable()) continue;
        const Tile* tile = getTile(id);
        const bool ready = tile && tile->isRenderable();
        const auto previous = lodStates.find(child);

        LodState::Kind kind;
        double target;
        if (ready && (blending || previous != lodStates.end())) {
            kind = LodState::Kind::Both;
            target = smooth(past / lodBlendZooms);
        } else if (!ready && !covered(child)) {
            // still loading, and no finer stand-in for it: the parent draws its place whole until it comes
            kind = LodState::Kind::ParentOnly;
            target = 0;
        } else {
            continue;
        }
        LodState state = previous != lodStates.end()
                             ? previous->second
                             : LodState{.kind = kind, .shown = target, .from = target, .since = now - lodArrivalDuration};
        if (state.kind != kind) {
            // a level that has just loaded comes in from what was drawn; one that is gone goes at once
            state.from = kind == LodState::Kind::ParentOnly ? 0.0 : state.shown;
            state.since = kind == LodState::Kind::ParentOnly ? now - lodArrivalDuration : now;
            state.kind = kind;
        }
        const double arrived = smooth(std::chrono::duration<double>(now - state.since).count() /
                                      std::chrono::duration<double>(lodArrivalDuration).count());
        state.shown = state.from + (target - state.from) * arrived;
        if (arrived < 1) lodArriving = true;
        // the child whole and nothing left to bring in: its blend is over
        if (state.shown >= 1 && arrived >= 1) continue;
        states.emplace(child, state);

        // the share of the pixels the child draws, in steps the parent can tell apart per quadrant
        const int share = static_cast<int>(std::lround(std::clamp(state.shown, 0.0, 1.0) * lodShareSteps));
        if (share >= lodShareSteps) continue;
        Parent& entry = parents[parentID];
        entry.tile = parent;
        entry.share[(id.canonical.x & 1) + 2 * (id.canonical.y & 1)] = share;
        if (share <= 0) {
            lodBlends[child].hidden = true;
        } else {
            lodBlends[child].fade = static_cast<float>(share) / lodShareSteps;
        }
    }
    lodStates = std::move(states);

    for (auto& [parentID, entry] : parents) {
        retainTile(*entry.tile, TileNecessity::Required);
        int code = 0;
        for (int q = 0; q < 4; q++) code |= entry.share[q] << (lodShareBits * q);
        LodBlend& blend = lodBlends[parentID];
        blend.fade = -1.0f - static_cast<float>(code);
        blend.parent = true;
        blend.hidden = false;
        if (!renderedTiles.count(parentID)) {
            blend.extra = true;
            addRenderTile(parentID, *entry.tile);
        }
    }

    // Any other tile of the cover in a quadrant a parent draws whole (MapLibre's stand-ins for a child still loading)
    // gives way to it in fill and line layers.
    if (parents.empty()) return;
    for (const auto& item : renderedTiles) {
        const UnwrappedTileID& id = item.first;
        if (lodBlends.count(id)) continue;
        for (int z = id.canonical.z - 1; z >= 0; z--) {
            const int dz = id.canonical.z - z;
            const auto found = parents.find(UnwrappedTileID(
                id.wrap, CanonicalTileID(static_cast<uint8_t>(z), id.canonical.x >> dz, id.canonical.y >> dz)));
            if (found == parents.end()) continue;
            const int quadrant = ((id.canonical.x >> (dz - 1)) & 1) + 2 * ((id.canonical.y >> (dz - 1)) & 1);
            if (found->second.share[quadrant] <= 0) lodBlends[id].hidden = true;
            break;
        }
    }
}

void TilePyramid::updateFadingTiles() {
    fadingTiles = false;
    for (auto& entry : renderedTiles) {
        Tile& tile = entry.second;
        if (tile.holdForFade()) {
            fadingTiles = true;
            tile.performedFadePlacement();
        }
    }
}

} // namespace mln
