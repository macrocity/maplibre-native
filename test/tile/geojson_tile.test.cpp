#include <mln/test/util.hpp>
#include <mln/test/fake_file_source.hpp>
#include <mln/test/stub_tile_observer.hpp>
#include <mln/tile/geojson_tile.hpp>
#include <mln/tile/tile_loader_impl.hpp>

#include <mln/annotation/annotation_manager.hpp>
#include <mln/map/transform.hpp>
#include <mln/renderer/image_manager.hpp>
#include <mln/renderer/tile_parameters.hpp>
#include <mln/style/layers/circle_layer.hpp>
#include <mln/style/layers/circle_layer_impl.hpp>
#include <mln/style/sources/geojson_source.hpp>
#include <mln/style/style.hpp>
#include <mln/text/glyph_manager.hpp>
#include <mln/util/run_loop.hpp>
#include <mln/gfx/dynamic_texture_atlas.hpp>

#include <mapbox/geojsonvt.hpp>

#include <memory>

using namespace mln;
using namespace mln::style;

class GeoJSONTileTest {
public:
    util::SimpleIdentity uniqueID;
    std::shared_ptr<FileSource> fileSource = std::make_shared<FakeFileSource>();
    TransformState transformState;
    util::RunLoop loop;
    AnnotationManager annotationManager{style};
    std::shared_ptr<ImageManager> imageManager = ImageManager::create();
    std::shared_ptr<GlyphManager> glyphManager = std::make_shared<GlyphManager>();
    gfx::DynamicTextureAtlasPtr dynamicTextureAtlas;

    Tileset tileset{{"https://example.com"}, {0, 22}, "none"};
    TileParameters tileParameters;
    style::Style style;

    GeoJSONTileTest()
        : tileParameters{.pixelRatio = 1.0,
                         .debugOptions = MapDebugOptions(),
                         .transformState = transformState,
                         .fileSource = fileSource,
                         .mode = MapMode::Continuous,
                         .annotationManager = annotationManager.makeWeakPtr(),
                         .imageManager = imageManager,
                         .glyphManager = glyphManager,
                         .prefetchZoomDelta = 0,
                         .threadPool = {Scheduler::GetBackground(), uniqueID},
                         .dynamicTextureAtlas = dynamicTextureAtlas},
          style{fileSource, 1, tileParameters.threadPool} {}
};

namespace {

class FakeGeoJSONData : public GeoJSONData {
public:
    FakeGeoJSONData(TileFeatures features_)
        : features(std::move(features_)) {}

    void getTile(const CanonicalTileID&, const std::function<void(TileFeatures)>& fn, bool) final {
        assert(fn);
        fn(features);
    }

    Features getChildren(const std::uint32_t) final { return {}; }

    Features getLeaves(const std::uint32_t, const std::uint32_t, const std::uint32_t) final { return {}; }

    std::uint8_t getClusterExpansionZoom(std::uint32_t) final { return 0; }

private:
    TileFeatures features;
};

} // namespace

TEST(GeoJSONTile, UncachedTilesMatchHierarchicalClipping) {
    util::RunLoop loop;
    const auto input = mapbox::geojson::parse(R"({"type":"FeatureCollection","features":[
        {"type":"Feature","id":1,"properties":{"name":"point"},"geometry":{"type":"Point","coordinates":[19.0546,47.4979]}},
        {"type":"Feature","properties":{"name":"points"},"geometry":{"type":"MultiPoint","coordinates":[[179.99,0],[-179.99,0],[19.06,47.50]]}},
        {"type":"Feature","properties":{"name":"line"},"geometry":{"type":"LineString","coordinates":[[-179,0],[0,50],[19.05,47.49],[19.06,47.50],[179,0]]}},
        {"type":"Feature","properties":{"name":"lines"},"geometry":{"type":"MultiLineString","coordinates":[[[19.04,47.48],[19.07,47.51]],[[179.9,-1],[-179.9,1]]]}},
        {"type":"Feature","properties":{"name":"polygon"},"geometry":{"type":"Polygon","coordinates":[[[19.04,47.48],[19.07,47.48],[19.07,47.51],[19.04,47.51],[19.04,47.48]],[[19.05,47.49],[19.05,47.50],[19.06,47.50],[19.06,47.49],[19.05,47.49]]]}},
        {"type":"Feature","properties":{"name":"polygons"},"geometry":{"type":"MultiPolygon","coordinates":[[[[179.8,-1],[179.9,-1],[179.9,1],[179.8,1],[179.8,-1]]],[[[-179.9,-1],[-179.8,-1],[-179.8,1],[-179.9,1],[-179.9,-1]]]]}}
    ]})");
    for (const uint16_t buffer : {0, 64, 128, 512, 2048}) {
        for (const double tolerance : {0.0, 0.375}) {
            for (const bool lineMetrics : {false, true}) {
                auto options = makeMutable<GeoJSONOptions>();
                options->maxzoom = 15;
                options->buffer = buffer;
                options->tolerance = tolerance;
                options->lineMetrics = lineMetrics;
                mapbox::geojsonvt::Options referenceOptions;
                referenceOptions.maxZoom = options->maxzoom;
                referenceOptions.extent = util::EXTENT;
                constexpr double scale = util::EXTENT / util::tileSize_D;
                referenceOptions.buffer = static_cast<uint16_t>(buffer * scale);
                referenceOptions.tolerance = tolerance * scale;
                referenceOptions.lineMetrics = lineMetrics;
                mapbox::geojsonvt::GeoJSONVT reference(input, referenceOptions);
                auto data = GeoJSONData::create(input, Scheduler::GetSequenced(), std::move(options));
                for (const uint8_t zoom : {0, 3, 5, 10, 14, 15, 5, 0}) {
                    const uint32_t n = 1u << zoom;
                    const uint32_t budapestX = static_cast<uint32_t>((19.0546 + 180) / 360 * n);
                    const uint32_t budapestY = static_cast<uint32_t>(0.3498 * n);
                    for (const uint32_t x : {0u, n - 1, budapestX}) {
                        for (const uint32_t y : {n / 2, budapestY}) {
                            SCOPED_TRACE(::testing::Message() << "buffer=" << buffer << " tolerance=" << tolerance
                                                              << " metrics=" << lineMetrics
                                                              << " tile=" << unsigned(zoom) << '/' << x << '/' << y);
                            bool replied = false;
                            data->getTile(
                                CanonicalTileID{zoom, x, y},
                                [&](GeoJSONData::TileFeatures actual) {
                                    replied = true;
                                    EXPECT_TRUE(reference.getTile(zoom, x, y).features == actual);
                                },
                                true);
                            EXPECT_TRUE(replied);
                        }
                    }
                }
            }
        }
    }
}

TEST(GeoJSONTile, SynchronousUpdate) {
    GeoJSONTileTest test;

    CircleLayer layer("circle", "source");

    mapbox::feature::feature_collection<int16_t> features;
    features.push_back(mapbox::feature::feature<int16_t>{mapbox::geometry::point<int16_t>(0, 0)});
    auto data = std::make_shared<FakeGeoJSONData>(std::move(features));
    TileParameters tileParameters = test.tileParameters;
    tileParameters.isUpdateSynchronous = true;
    GeoJSONTile tile(OverscaledTileID(0, 0, 0), "source", tileParameters, data);
    Immutable<LayerProperties> layerProperties = makeMutable<CircleLayerProperties>(
        staticImmutableCast<CircleLayer::Impl>(layer.baseImpl));
    std::vector<Immutable<LayerProperties>> layers{layerProperties};
    tile.setLayers(layers);
    ASSERT_TRUE(tile.isComplete());
    ASSERT_TRUE(tile.isRenderable());
}

TEST(GeoJSONTile, Issue7648) {
    GeoJSONTileTest test;

    CircleLayer layer("circle", "source");

    mapbox::feature::feature_collection<int16_t> features;
    features.push_back(mapbox::feature::feature<int16_t>{mapbox::geometry::point<int16_t>(0, 0)});
    auto data = std::make_shared<FakeGeoJSONData>(std::move(features));
    GeoJSONTile tile(OverscaledTileID(0, 0, 0), "source", test.tileParameters, data);
    Immutable<LayerProperties> layerProperties = makeMutable<CircleLayerProperties>(
        staticImmutableCast<CircleLayer::Impl>(layer.baseImpl));
    StubTileObserver observer;
    observer.tileChanged = [&](const Tile&) {
        // Once present, the bucket should never "disappear", which would cause
        // flickering.
        ASSERT_TRUE(tile.layerPropertiesUpdated(layerProperties));
    };

    std::vector<Immutable<LayerProperties>> layers{layerProperties};
    tile.setLayers(layers);
    tile.setObserver(&observer);

    while (!tile.isComplete()) {
        test.loop.runOnce();
    }

    tile.updateData(data, false, test.tileParameters.isUpdateSynchronous);
    while (!tile.isComplete()) {
        test.loop.runOnce();
    }
}

// Tests that tiles remain renderable if they have been renderable and then had
// an error sent to them, e.g. when revalidating/refreshing the request.
TEST(GeoJSONTile, Issue9927) {
    GeoJSONTileTest test;

    CircleLayer layer("circle", "source");

    mapbox::feature::feature_collection<int16_t> features;
    features.push_back(mapbox::feature::feature<int16_t>{mapbox::geometry::point<int16_t>(0, 0)});
    auto data = std::make_shared<FakeGeoJSONData>(std::move(features));
    GeoJSONTile tile(OverscaledTileID(0, 0, 0), "source", test.tileParameters, data);

    Immutable<LayerProperties> layerProperties = makeMutable<CircleLayerProperties>(
        staticImmutableCast<CircleLayer::Impl>(layer.baseImpl));
    std::vector<Immutable<LayerProperties>> layers{layerProperties};
    tile.setLayers(layers);

    while (!tile.isComplete()) {
        test.loop.runOnce();
    }

    ASSERT_TRUE(tile.isRenderable());
    ASSERT_TRUE(tile.layerPropertiesUpdated(layerProperties));

    // Make sure that once we've had a renderable tile and then receive
    // erroneous data, we retain the previously rendered data and keep the tile
    // renderable.
    tile.setError(std::make_exception_ptr(std::runtime_error("Connection offline")));
    ASSERT_TRUE(tile.isRenderable());
    ASSERT_TRUE(tile.layerPropertiesUpdated(layerProperties));

    // Then simulate a parsing failure and make sure that we keep it renderable
    // in this situation as well. We're using 3 as a correlationID since we've
    // done two three calls that increment this counter (as part of the
    // GeoJSONTile constructor, setLayers, and setPlacementConfig).
    tile.onError(std::make_exception_ptr(std::runtime_error("Parse error")), 3);
    ASSERT_TRUE(tile.isRenderable());
    ASSERT_TRUE(tile.layerPropertiesUpdated(layerProperties));
}
