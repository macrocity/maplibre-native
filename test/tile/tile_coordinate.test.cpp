#include <mln/test/util.hpp>

#include <mln/map/transform.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/tile/tile.hpp>
#include <mln/tile/tile_id.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/geometry.hpp>
#include <mln/util/tile_coordinate.hpp>
#include <mln/style/projection_definition.hpp>

#include <cmath>

using namespace mln;

TEST(TileCoordinate, FromLatLng) {
    size_t changeCount = 0;
    struct TransformObserver : public mln::TransformObserver {
        void onCameraWillChange(MapObserver::CameraChangeMode mode) final {
            if (mode == MapObserver::CameraChangeMode::Immediate && cameraWillChangeImmediateCallback) {
                cameraWillChangeImmediateCallback();
            }
        }

        void onCameraDidChange(MapObserver::CameraChangeMode mode) final {
            if (mode == MapObserver::CameraChangeMode::Immediate && cameraDidChangeImmediateCallback) {
                cameraDidChangeImmediateCallback();
            }
        }

        std::function<void()> cameraWillChangeImmediateCallback;
        std::function<void()> cameraDidChangeImmediateCallback;
    };

    TransformObserver observer;
    observer.cameraWillChangeImmediateCallback = [&]() {
        ASSERT_EQ(changeCount, 0u);
        ++changeCount;
    };
    observer.cameraDidChangeImmediateCallback = [&]() {
        ASSERT_EQ(changeCount, 1u);
    };

    Transform transform(observer);

    const double max = util::tileSize_D;
    transform.resize({static_cast<uint32_t>(max), static_cast<uint32_t>(max)});

    // Center, top-left, bottom-left, bottom-right, top-right edges.
    std::vector<std::pair<LatLng, ScreenCoordinate>> edges{
        {{}, {max / 2.0, max / 2.0}},
        {{util::LATITUDE_MAX, -util::LONGITUDE_MAX}, {0, max}},
        {{-util::LATITUDE_MAX, -util::LONGITUDE_MAX}, {0, 0}},
        {{-util::LATITUDE_MAX, util::LONGITUDE_MAX}, {max, 0}},
        {{util::LATITUDE_MAX, util::LONGITUDE_MAX}, {max, max}},
    };

    for (const auto& pair : edges) {
        const auto& latLng = pair.first;
        const auto& screenCoordinate = pair.second;
        const auto base = TileCoordinate::fromLatLng(0, latLng);

        // 16 is the maximum zoom level where we actually compute placements.
        for (uint8_t integerZoom = 0; integerZoom <= 16; ++integerZoom) {
            const double zoom = integerZoom;
            const double maxTilesPerAxis = std::pow(2.0, zoom);
            const Point<double> tilePoint = {
                latLng.longitude() == 0                      ? 0.5
                : latLng.longitude() == -util::LONGITUDE_MAX ? 0
                                                             : 1.0,
                latLng.latitude() == 0                     ? 0.5
                : latLng.latitude() == -util::LATITUDE_MAX ? 1.0
                                                           : 0,
            };

            const auto fromLatLng = TileCoordinate::fromLatLng(zoom, latLng);
            ASSERT_DOUBLE_EQ(fromLatLng.z, zoom);
            ASSERT_DOUBLE_EQ(fromLatLng.p.x, tilePoint.x * maxTilesPerAxis);
            ASSERT_NEAR(fromLatLng.p.y, tilePoint.y * maxTilesPerAxis, 1.0e-7);

            const auto fromScreenCoordinate = TileCoordinate::fromScreenCoordinate(
                transform.getState(), static_cast<uint8_t>(zoom), screenCoordinate);
            ASSERT_DOUBLE_EQ(fromScreenCoordinate.z, fromLatLng.z);
            ASSERT_NEAR(fromScreenCoordinate.p.x, fromLatLng.p.x, 0.99);
            ASSERT_NEAR(fromScreenCoordinate.p.y, fromLatLng.p.y, 0.99);

            const auto zoomed = base.zoomTo(zoom);
            ASSERT_DOUBLE_EQ(zoomed.z, zoom);
            ASSERT_DOUBLE_EQ(zoomed.p.x, fromLatLng.p.x);
            ASSERT_DOUBLE_EQ(zoomed.p.y, fromLatLng.p.y);
        }
    }
}

TEST(TileCoordinate, ToGeometryCoordinate) {
    std::vector<Point<double>> edges{{0.5, 0.5}, {0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (uint8_t zoom = 0; zoom <= 16; ++zoom) {
        auto maxTilesPerAxis = static_cast<uint32_t>(std::pow(2, zoom));
        for (const auto& edge : edges) {
            uint32_t tileX = edge.x == 0   ? 0
                             : edge.x == 1 ? maxTilesPerAxis - 1
                                           : static_cast<uint32_t>((maxTilesPerAxis / 2.0) - 1);
            uint32_t tileY = edge.y == 0   ? 0
                             : edge.y == 1 ? maxTilesPerAxis - 1
                                           : static_cast<uint32_t>((maxTilesPerAxis / 2.0) - 1);
            UnwrappedTileID unwrapped(0, CanonicalTileID{zoom, tileX, tileY});

            auto tilePointX = ((edge.x * maxTilesPerAxis) - tileX) * util::EXTENT;
            auto tilePointY = ((edge.y * maxTilesPerAxis) - tileY) * util::EXTENT;
            GeometryCoordinate point = TileCoordinate::toGeometryCoordinate(unwrapped, edge);
            ASSERT_DOUBLE_EQ(point.x, tilePointX);
            ASSERT_DOUBLE_EQ(point.y, tilePointY);
        }
    }
}

namespace {

void setUpGlobe(Transform& transform, const LatLng& center, double zoom) {
    transform.resize({800, 600});
    transform.setProjectionDefinition(ProjectionDefinition("vertical-perspective"));
    transform.jumpTo(CameraOptions().withCenter(center).withZoom(zoom));
}

} // namespace

// The feature query turns its screen box into tile coordinates with `fromScreenCoordinate`. On the globe the result
// must be the tile coordinate of the ground under the pixel at the zoom asked for, as on Mercator, or the box misses
// every rendered tile and the query finds no line, fill or circle.
TEST(TileCoordinate, FromScreenCoordinateOnTheGlobe) {
    for (const double zoom : {0.5, 2.0, 5.0, 8.5, 11.0}) {
        Transform transform;
        setUpGlobe(transform, {47.5, 19.0}, zoom);
        const TransformState& state = transform.getState();
        ASSERT_TRUE(state.isGlobeRendering());
        const double height = state.getSize().height;

        for (const ScreenCoordinate& point : {ScreenCoordinate{400, 300},
                                              ScreenCoordinate{120, 80},
                                              ScreenCoordinate{700, 520},
                                              ScreenCoordinate{400, 10}}) {
            // `Transform` takes a top-left pixel; `TransformState`, and so the query, a bottom-left one.
            const LatLng latLng = transform.screenCoordinateToLatLng(point, LatLng::Unwrapped);
            for (const uint8_t atZoom : {0, 5, 12}) {
                const auto expected = TileCoordinate::fromLatLng(atZoom, latLng);
                const auto actual = TileCoordinate::fromScreenCoordinate(state, atZoom, {point.x, height - point.y});
                EXPECT_DOUBLE_EQ(atZoom, actual.z);
                EXPECT_NEAR(expected.p.x, actual.p.x, 1e-6 * std::pow(2.0, atZoom))
                    << "zoom " << zoom << " at zoom " << int(atZoom) << " point " << point.x << "," << point.y;
                EXPECT_NEAR(expected.p.y, actual.p.y, 1e-6 * std::pow(2.0, atZoom))
                    << "zoom " << zoom << " at zoom " << int(atZoom) << " point " << point.x << "," << point.y;
            }
        }
    }
}

// Across the antimeridian the tiles the globe draws keep the wrap nearest to the center, so a query point there keeps
// its longitude within half a world of the center instead of jumping to the far edge of the world.
TEST(TileCoordinate, FromScreenCoordinateOnTheGlobeAcrossTheAntimeridian) {
    Transform transform;
    setUpGlobe(transform, {0.0, 179.0}, 4.0);
    const TransformState& state = transform.getState();
    const double height = state.getSize().height;
    const uint8_t atZoom = 4;
    const double worldTiles = std::pow(2.0, atZoom);

    // Right of the center is east, past 180°: beyond the right edge of the world at wrap 0.
    const auto east = TileCoordinate::fromScreenCoordinate(state, atZoom, {700, height / 2});
    EXPECT_GT(east.p.x, worldTiles);
    EXPECT_LT(east.p.x, worldTiles * 1.25);
    const auto west = TileCoordinate::fromScreenCoordinate(state, atZoom, {100, height / 2});
    EXPECT_LT(west.p.x, worldTiles);
    EXPECT_GT(west.p.x, worldTiles * 0.75);
}
