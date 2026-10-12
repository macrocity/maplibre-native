#if MLN_RENDER_BACKEND_OPENGL

#include <mln/test/util.hpp>

#include <mln/gfx/headless_frontend.hpp>
#include <mln/style/layers/custom_layer.hpp>
#include <mln/gl/defines.hpp>
#include <mln/map/map.hpp>
#include <mln/map/map_options.hpp>
#include <mln/platform/gl_functions.hpp>
#include <mln/storage/resource_options.hpp>
#include <mln/style/layers/fill_layer.hpp>
#include <mln/style/style.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/io.hpp>
#include <mln/util/mat4.hpp>

#include <algorithm>
#include <cmath>
#include <mln/util/run_loop.hpp>

#include <memory>

using namespace mln;
using namespace mln::style;
using namespace mln::platform;

// Note that custom layers need to draw geometry with a z value of 1 to take
// advantage of depth-based fragment culling.
static const GLchar* vertexShaderSource = R"MBGL_SHADER(
attribute vec2 a_pos;
void main() {
    gl_Position = vec4(a_pos, 1, 1);
}
)MBGL_SHADER";

static const GLchar* fragmentShaderSource = R"MBGL_SHADER(
void main() {
    gl_FragColor = vec4(0, 0.5, 0, 0.5);
}
)MBGL_SHADER";

// Not using any mbgl-specific stuff (other than a basic error-checking macro)
// in the layer implementation because it is intended to reflect how someone
// using custom layers might actually write their own implementation.

class TestLayer : public mln::style::CustomLayerHost {
public:
    void initialize(const mln::style::CustomLayerInitParameters&) override {
        program = MBGL_CHECK_ERROR(glCreateProgram());
        vertexShader = MBGL_CHECK_ERROR(glCreateShader(GL_VERTEX_SHADER));
        fragmentShader = MBGL_CHECK_ERROR(glCreateShader(GL_FRAGMENT_SHADER));

        MBGL_CHECK_ERROR(glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr));
        MBGL_CHECK_ERROR(glCompileShader(vertexShader));
        MBGL_CHECK_ERROR(glAttachShader(program, vertexShader));
        MBGL_CHECK_ERROR(glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr));
        MBGL_CHECK_ERROR(glCompileShader(fragmentShader));
        MBGL_CHECK_ERROR(glAttachShader(program, fragmentShader));
        MBGL_CHECK_ERROR(glLinkProgram(program));
        a_pos = MBGL_CHECK_ERROR(glGetAttribLocation(program, "a_pos"));

        GLfloat triangle[] = {0, 0.5, 0.5, -0.5, -0.5, -0.5};
        MBGL_CHECK_ERROR(glGenBuffers(1, &buffer));
        MBGL_CHECK_ERROR(glBindBuffer(GL_ARRAY_BUFFER, buffer));
        MBGL_CHECK_ERROR(glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(GLfloat), triangle, GL_STATIC_DRAW));
    }

    void render(const mln::style::CustomLayerRenderParameters& parameters) override {
        // On Mercator the globe fields are inert: no globe, no transition, no clipping plane, and the globe matrix is
        // the tile matrix of the world.
        EXPECT_FALSE(parameters.globe);
        EXPECT_EQ(0.0, parameters.projectionTransition);
        EXPECT_EQ((std::array<double, 4>{0, 0, 0, 0}), parameters.globeClippingPlane);
        const double worldSize = mln::util::tileSize_D * std::pow(2.0, parameters.zoom);
        mln::mat4 world;
        mln::matrix::identity(world);
        mln::matrix::scale(world, world, worldSize / mln::util::EXTENT, worldSize / mln::util::EXTENT, 1);
        mln::mat4 expected;
        mln::matrix::multiply(expected, parameters.projectionMatrix, world);
        EXPECT_EQ(expected, parameters.globeProjectionMatrix);

        MBGL_CHECK_ERROR(glUseProgram(program));
        MBGL_CHECK_ERROR(glBindBuffer(GL_ARRAY_BUFFER, buffer));
        MBGL_CHECK_ERROR(glEnableVertexAttribArray(a_pos));
        MBGL_CHECK_ERROR(glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr));
        MBGL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_STRIP, 0, 3));
    }

    void contextLost() override {}

    void deinitialize() override {
        if (program) {
            MBGL_CHECK_ERROR(glDeleteBuffers(1, &buffer));
            MBGL_CHECK_ERROR(glDetachShader(program, vertexShader));
            MBGL_CHECK_ERROR(glDetachShader(program, fragmentShader));
            MBGL_CHECK_ERROR(glDeleteShader(vertexShader));
            MBGL_CHECK_ERROR(glDeleteShader(fragmentShader));
            MBGL_CHECK_ERROR(glDeleteProgram(program));
        }
    }

    GLuint program = 0;
    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    GLuint buffer = 0;
    GLuint a_pos = 0;
};

TEST(CustomLayer, Basic) {
    if (gfx::Backend::GetType() != gfx::Backend::Type::OpenGL) {
        return;
    }

    util::RunLoop loop;

    HeadlessFrontend frontend{1};
    Map map(frontend,
            MapObserver::nullObserver(),
            MapOptions().withMapMode(MapMode::Static).withSize(frontend.getSize()),
            ResourceOptions().withCachePath(":memory:").withAssetPath("test/fixtures/api/assets"));
    map.getStyle().loadJSON(util::read_file("test/fixtures/api/water.json"));
    map.jumpTo(CameraOptions().withCenter(LatLng{37.8, -122.5}).withZoom(10.0));
    map.getStyle().addLayer(std::make_unique<CustomLayer>("custom", std::make_unique<TestLayer>()));

    auto layer = std::make_unique<FillLayer>("landcover", "mapbox");
    layer->setSourceLayer("landcover");
    layer->setFillColor(Color{1.0, 1.0, 0.0, 1.0});
    map.getStyle().addLayer(std::move(layer));

    test::checkImage("test/fixtures/custom_layer/basic", frontend.render(map).image, 0.0006, 0.1);
}

// Binds a program of its own and draws the same triangle as TestLayer. With `restoreProgram` it puts back the
// program it found, as a careful host does; without, it leaves its own program bound.
class ProgramLeakLayer : public TestLayer {
public:
    explicit ProgramLeakLayer(bool restoreProgram_)
        : restoreProgram(restoreProgram_) {}

    void render(const mln::style::CustomLayerRenderParameters&) override {
        GLint found = 0;
        MBGL_CHECK_ERROR(glGetIntegerv(GL_CURRENT_PROGRAM, &found));
        MBGL_CHECK_ERROR(glUseProgram(program));
        MBGL_CHECK_ERROR(glBindBuffer(GL_ARRAY_BUFFER, buffer));
        MBGL_CHECK_ERROR(glEnableVertexAttribArray(a_pos));
        MBGL_CHECK_ERROR(glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr));
        MBGL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_STRIP, 0, 3));
        MBGL_CHECK_ERROR(glDisableVertexAttribArray(a_pos));
        if (restoreProgram) {
            MBGL_CHECK_ERROR(glUseProgram(static_cast<GLuint>(found)));
        }
    }

    const bool restoreProgram;
};

// A custom layer may leave any program bound. The layer after it must still draw with its own program, also when that
// program is the one the drawable before the custom layer used: the renderer's cached program is stale, only marked
// dirty. Both fill layers share one fill program (no outline, constant colour), so the water layer's first drawable
// needs exactly the program the landcover layer's last drawable used.
TEST(CustomLayer, DrawableAfterCustomLayerUsesItsOwnProgram) {
    if (gfx::Backend::GetType() != gfx::Backend::Type::OpenGL) {
        return;
    }

    const auto render = [](bool restoreProgram) {
        util::RunLoop loop;
        HeadlessFrontend frontend{1};
        Map map(frontend,
                MapObserver::nullObserver(),
                MapOptions().withMapMode(MapMode::Static).withSize(frontend.getSize()),
                ResourceOptions().withCachePath(":memory:").withAssetPath("test/fixtures/api/assets"));
        map.getStyle().loadJSON(util::read_file("test/fixtures/api/water.json"));
        map.jumpTo(CameraOptions().withCenter(LatLng{37.8, -122.5}).withZoom(10.0));

        auto water = map.getStyle().removeLayer("water");
        auto landcover = std::make_unique<FillLayer>("landcover", "mapbox");
        landcover->setSourceLayer("landcover");
        landcover->setFillColor(Color{1.0, 1.0, 0.0, 1.0});
        landcover->setFillAntialias(false);
        map.getStyle().addLayer(std::move(landcover));
        map.getStyle().addLayer(
            std::make_unique<CustomLayer>("custom", std::make_unique<ProgramLeakLayer>(restoreProgram)));
        static_cast<FillLayer&>(*water).setFillAntialias(false);
        map.getStyle().addLayer(std::move(water));

        return frontend.render(map).image;
    };

    const auto expected = render(true);
    const auto actual = render(false);
    ASSERT_EQ(expected.size, actual.size);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < actual.bytes(); i += 4) {
        differing += !std::equal(actual.data.get() + i, actual.data.get() + i + 4, expected.data.get() + i);
    }
    EXPECT_EQ(0u, differing) << "pixels that differ from the render whose custom layer restored the program";
}

#endif
