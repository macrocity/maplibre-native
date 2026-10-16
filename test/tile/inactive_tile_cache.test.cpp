#include <mln/test/util.hpp>
#include <mln/annotation/annotation_manager.hpp>
#include <mln/util/tileset.hpp>
#include <mln/util/identity.hpp>
#include <mln/test/vector_tile_test.hpp>
#include <mln/style/conversion/json.hpp>
#include <mln/style/conversion/source.hpp>
#include <mln/style/source_impl.hpp>
#include <mln/style/sources/vector_source.hpp>
#include <mln/style/sources/raster_source.hpp>
#include <mln/renderer/style_diff.hpp>
#include <mln/renderer/tile_pyramid.hpp>
#include <mln/renderer/tile_render_data.hpp>
#include <mln/tile/tile_cache.hpp>
#include <atomic>
#include <limits>

using namespace mln;
namespace {
using VectorSource = style::VectorSource;

class CachedTile final : public Tile {
public:
    CachedTile(OverscaledTileID tileID, const std::string& source, std::atomic<int>& destroyed_, TileObserver* observer = nullptr)
        : Tile(Kind::Geometry, tileID, source, observer), destroyed(destroyed_) {
        renderable = loaded = true;
    }
    ~CachedTile() override { ++destroyed; }
    void cancel() override {}
    std::unique_ptr<TileRenderData> createRenderData() override { return nullptr; }
    bool layerPropertiesUpdated(const Immutable<style::LayerProperties>&) override { return false; }
private:
    std::atomic<int>& destroyed;
};

struct PyramidFixture {
    VectorTileTest test;
    std::atomic<int> destroyed{0};
    int created = 0;
    TilePyramid pyramid{test.threadPool};
    VectorSource source{"arbitrary-base-id", Tileset{{"https://example.test/{z}/{x}/{y}"}}};
    PyramidFixture() {
        Transform transform;
        transform.resize({64, 64});
        transform.jumpTo(CameraOptions().withCenter(LatLng()).withZoom(0));
        test.transformState = transform.getState();
        pyramid.setCacheEnabled(false);
    }
    ~PyramidFixture() {
        pyramid.clearAll();
        pyramid.reduceMemoryUse();
        test.threadPool.waitForEmpty();
    }
    void update(bool visible, bool relayout = false) {
        pyramid.update({}, visible, relayout, test.tileParameters, *source.baseImpl, 512, {0, 22}, std::nullopt,
                       [&](const OverscaledTileID& id, TileObserver* observer) {
                           ++created;
                           return std::make_unique<CachedTile>(id, source.getID(), destroyed, observer);
                       });
    }
};
} // namespace

TEST(InactiveTileCache, JsonBoundsAbsentExplicitZeroAndAnySourceId) {
    using namespace style::conversion;
    for (const std::string& suffix : {std::string{}, std::string{",\"maplibre:inactive-tile-cache-size\":0"},
                                      std::string{",\"maplibre:inactive-tile-cache-size\":8"}}) {
        Error error;
        auto source = convertJSON<std::unique_ptr<style::Source>>(
            "{\"type\":\"vector\",\"url\":\"https://example.test/tiles.json\"" + suffix + "}", error, "any-renamed-base");
        ASSERT_TRUE(source) << error.message;
        EXPECT_EQ((*source)->getID(), "any-renamed-base");
        if (suffix.empty()) EXPECT_EQ((*source)->getMaximumInactiveTileCacheSize(), std::nullopt);
        else EXPECT_EQ((*source)->getMaximumInactiveTileCacheSize(), suffix.back() == '0' ? 0u : 8u);
    }
    for (const char* value : {"-1", "9", "1e30", "1e300", "0.5", "null", "true", "\"8\""}) {
        Error error;
        auto source = convertJSON<std::unique_ptr<style::Source>>(
            std::string{"{\"type\":\"vector\",\"url\":\"https://example.test/tiles.json\",\"maplibre:inactive-tile-cache-size\":"} + value + "}",
            error, "not-world");
        EXPECT_FALSE(source) << value;
    }
}

TEST(InactiveTileCache, ImmutableCopyClampAndStyleDiff) {
    VectorTileTest test;
    const std::string json = R"({"version":8,"sources":{"base-renamed":{"type":"vector","tiles":["https://example.test/{z}/{x}/{y}"],"maplibre:inactive-tile-cache-size":8}},"layers":[]})";
    test.style.loadJSON(json);
    EXPECT_EQ(test.style.getJSON(), json); // Existing JSON serialization preserves the namespaced extension.
    auto* parsed = test.style.getSource("base-renamed");
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->getMaximumInactiveTileCacheSize(), 8u);
    VectorSource source("base-renamed", Tileset{{"old"}});
    const auto original = source.baseImpl;
    source.setMaximumInactiveTileCacheSize(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(source.getMaximumInactiveTileCacheSize(), 8u);
    EXPECT_EQ(original->getMaximumInactiveTileCacheSize(), std::nullopt);
    auto withLimit = source.baseImpl;
    source.setMaximumInactiveTileCacheSize(8);
    EXPECT_EQ(source.baseImpl, withLimit); // Same effective value must not notify/rebuild the impl.
    source.setVolatile(true); // createMutable copy must preserve the cap.
    EXPECT_EQ(source.getMaximumInactiveTileCacheSize(), 8u);
    source.loadDescription(*test.fileSource); // TileJSON replacement copies Source::Impl too.
    EXPECT_EQ(source.getMaximumInactiveTileCacheSize(), 8u);
    source.setTiles({"new"});
    EXPECT_EQ(source.getMaximumInactiveTileCacheSize(), 8u);
    auto before = makeMutable<std::vector<ImmutableSource>>(std::vector<ImmutableSource>{source.baseImpl});
    source.setMaximumInactiveTileCacheSize(0);
    auto after = makeMutable<std::vector<ImmutableSource>>(std::vector<ImmutableSource>{source.baseImpl});
    const auto difference = diffSources(before, after);
    EXPECT_EQ(difference.changed.size(), 1u);
    EXPECT_TRUE(difference.added.empty());
    EXPECT_TRUE(difference.removed.empty());
    EXPECT_EQ(difference.changed.at("base-renamed").before->getMaximumInactiveTileCacheSize(), 8u);
    EXPECT_EQ(difference.changed.at("base-renamed").after->getMaximumInactiveTileCacheSize(), 0u);
    source.setMaximumInactiveTileCacheSize(std::nullopt);
    EXPECT_EQ(source.getMaximumInactiveTileCacheSize(), std::nullopt);
    style::RasterSource raster("not-vector", Tileset{{"raster"}}, 512);
    raster.setMaximumInactiveTileCacheSize(8);
    EXPECT_EQ(raster.getMaximumInactiveTileCacheSize(), std::nullopt);
}

TEST(InactiveTileCache, DisabledDefaultStillRecreatesAndOverrideReuses) {
    PyramidFixture fixture;
    fixture.update(true);
    ASSERT_EQ(fixture.pyramid.getTiles().size(), 1u);
    const auto id = fixture.pyramid.getTiles().begin()->first;
    fixture.update(false);
    EXPECT_EQ(fixture.pyramid.getTile(id), nullptr);
    fixture.update(true);
    EXPECT_EQ(fixture.created, 2);
    fixture.source.setMaximumInactiveTileCacheSize(8);
    Tile* retained = fixture.pyramid.getTile(id);
    fixture.update(false);
    EXPECT_EQ(fixture.pyramid.getTile(id), retained);
    fixture.update(true);
    EXPECT_EQ(fixture.created, 2);
    EXPECT_EQ(fixture.pyramid.getTile(id), retained);
    fixture.update(false);
    fixture.source.setMaximumInactiveTileCacheSize(std::nullopt);
    fixture.update(false); // Removing opt-in with globalfalse must evict even while hidden.
    EXPECT_EQ(fixture.pyramid.getTile(id), nullptr);
}

TEST(InactiveTileCache, ExplicitZeroWinsGlobalTrueAndRelayoutInvalidates) {
    PyramidFixture fixture;
    fixture.pyramid.setCacheEnabled(true);
    fixture.source.setMaximumInactiveTileCacheSize(0);
    fixture.update(true);
    const auto id = fixture.pyramid.getTiles().begin()->first;
    fixture.update(false);
    EXPECT_EQ(fixture.pyramid.getTile(id), nullptr);
    fixture.source.setMaximumInactiveTileCacheSize(8);
    fixture.update(true);
    fixture.update(false);
    ASSERT_NE(fixture.pyramid.getTile(id), nullptr);
    fixture.update(false, true); // Retention cannot preserve a stale layout.
    EXPECT_EQ(fixture.pyramid.getTile(id), nullptr);
}

TEST(InactiveTileCache, EightEntryEvictionAndTrimDispatchWithoutNextFrame) {
    VectorTileTest test;
    std::atomic<int> destroyed{0};
    {
        TileCache cache(test.threadPool, 8);
        for (int x = 0; x < 9; ++x) {
            const OverscaledTileID id(4, 0, 4, x, 0);
            cache.add(id, std::make_unique<CachedTile>(id, "any-id", destroyed));
        }
        EXPECT_FALSE(cache.has(OverscaledTileID(4, 0, 4, 0, 0)));
        for (int x = 1; x < 9; ++x) EXPECT_TRUE(cache.has(OverscaledTileID(4, 0, 4, x, 0)));
        cache.clear();
        cache.deferPendingReleases();
        test.threadPool.waitForEmpty();
        EXPECT_EQ(destroyed.load(), 9);
    }
    PyramidFixture fixture;
    fixture.source.setMaximumInactiveTileCacheSize(8);
    fixture.update(true);
    const auto id = fixture.pyramid.getTiles().begin()->first;
    fixture.update(false);
    fixture.pyramid.reduceMemoryUse();
    fixture.test.threadPool.waitForEmpty();
    EXPECT_EQ(fixture.pyramid.getTile(id), nullptr);
    EXPECT_EQ(fixture.destroyed.load(), 1); // Actual trim retirement, no subsequent update/frame.
}
