#include <mln/test/util.hpp>
#include "../../platform/android/MapLibreAndroid/src/cpp/android_emulator_render_policy.hpp"

using namespace mln::android::detail;

namespace {
GuestAngleDrawableReuseInputs verifiedDriver() {
    return {"1", true, 35, true, "angle", "angle", "Google Inc. (Apple)",
            "ANGLE (Apple, Vulkan 1.3.0 (Apple M3 Pro (0x1A070209)), Apple-0.2.2208)",
            "OpenGL ES 3.1.0 (ANGLE 2.1.0 git hash: unknown hash)"};
}
}

TEST(GuestAngleDrawableReuse, AutomaticAndExplicitOptInUseSameStrictGuard) {
    auto inputs = verifiedDriver();
    for (auto value : {"", "1"}) {
        inputs.overrideValue = value;
        EXPECT_EQ(GuestAngleDrawableReuseDecision::Enabled, guestAngleDrawableReuseDecision(inputs));
    }
    for (auto value : {"0", "true", "false", "yes", "auto", "01", " 1", "1 ", "\t1"}) {
        inputs.overrideValue = value;
        EXPECT_EQ(GuestAngleDrawableReuseDecision::Disabled, guestAngleDrawableReuseDecision(inputs));
    }
}

TEST(GuestAngleDrawableReuse, EmulatorApiAndCurrentContextRequired) {
    for (auto mode : {"", "1"}) {
        auto inputs = verifiedDriver();
        inputs.overrideValue = mode;
        inputs.emulator = false;
        EXPECT_EQ(GuestAngleDrawableReuseDecision::NotEmulator, guestAngleDrawableReuseDecision(inputs));
        inputs.emulator = true;
        for (int api : {0, 23, 32, 34, 36}) {
            inputs.apiLevel = api;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::UnsupportedApi, guestAngleDrawableReuseDecision(inputs));
        }
        inputs.apiLevel = 35;
        inputs.hasCurrentContext = false;
        EXPECT_EQ(GuestAngleDrawableReuseDecision::NoContext, guestAngleDrawableReuseDecision(inputs));
    }
}

TEST(GuestAngleDrawableReuse, BothEglPropertiesRequired) {
    for (auto mode : {"", "1"}) {
        for (auto value : {"", "emulation", "swiftshader", "ANGLE"}) {
            auto inputs = verifiedDriver();
            inputs.overrideValue = mode;
            inputs.hardwareEgl = value;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::WrongEgl, guestAngleDrawableReuseDecision(inputs));
            inputs.hardwareEgl = "angle";
            inputs.bootHardwareEgl = value;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::WrongEgl, guestAngleDrawableReuseDecision(inputs));
        }
    }
}

TEST(GuestAngleDrawableReuse, UnknownSoftwareAndClassicDriversKeepMitigation) {
    for (auto mode : {"", "1"}) {
        auto inputs = verifiedDriver();
        inputs.overrideValue = mode;
        for (auto value : {"", "Apple", "Google Inc.", "ARM"}) {
            inputs.vendor = value;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::WrongDriver, guestAngleDrawableReuseDecision(inputs));
        }
        inputs.vendor = "Google Inc. (Apple)";
        for (auto value : {"", "Android Emulator OpenGL ES Translator (Apple M3 Pro)",
                           "ANGLE (Apple, OpenGL ES 3.1 (Apple M3 Pro (0x1A070209)))",
                           "ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device))",
                           "ANGLE (Apple, Vulkan 1.3.0 (Apple M4 Pro (0x1A070209)))",
                           "ANGLE (Apple, Vulkan 1.3.0 (Apple M3 Pro (SwiftShader)))",
                           "ANGLE (Apple, Vulkan 1.3.0 (Apple M3 Pro (Translator)))",
                           "ANGLE (Apple, Vulkan 1.3.0 (Apple M3 Pro (llvmpipe)))",
                           "ANGLE (Apple, Vulkan 1.3.0 (Apple M3 Pro (lavapipe)))"}) {
            inputs.renderer = value;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::WrongDriver, guestAngleDrawableReuseDecision(inputs));
        }
        inputs.renderer = verifiedDriver().renderer;
        for (auto value : {"", "OpenGL ES 2.0", "Vulkan 1.3.0"}) {
            inputs.version = value;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::WrongDriver, guestAngleDrawableReuseDecision(inputs));
        }
    }
}

TEST(GuestAngleDrawableReuse, ContextRecreationAndOptOutReevaluateWithoutStickyState) {
    for (auto mode : {"", "1"}) {
        auto good = verifiedDriver();
        good.overrideValue = mode;
        EXPECT_EQ(GuestAngleDrawableReuseDecision::Enabled, guestAngleDrawableReuseDecision(good));
        auto lost = good;
        lost.hasCurrentContext = false;
        EXPECT_NE(GuestAngleDrawableReuseDecision::Enabled, guestAngleDrawableReuseDecision(lost));
        auto replacement = good;
        replacement.renderer = "Android Emulator OpenGL ES Translator (Apple M3 Pro)";
        EXPECT_NE(GuestAngleDrawableReuseDecision::Enabled, guestAngleDrawableReuseDecision(replacement));
        EXPECT_EQ(GuestAngleDrawableReuseDecision::Enabled, guestAngleDrawableReuseDecision(good));
        for (auto value : {"0", "unknown"}) {
            auto disabled = good;
            disabled.overrideValue = value;
            EXPECT_EQ(GuestAngleDrawableReuseDecision::Disabled, guestAngleDrawableReuseDecision(disabled));
        }
        EXPECT_EQ(GuestAngleDrawableReuseDecision::Enabled, guestAngleDrawableReuseDecision(good));
    }
}

#if MLN_RENDER_BACKEND_OPENGL
#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/headless_frontend.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/gfx/context.hpp>
#include <mln/map/map.hpp>
#include <mln/map/map_options.hpp>
#include <mln/renderer/renderer.hpp>
#include <mln/storage/resource_options.hpp>
#include <mln/style/style.hpp>
#include <mln/util/run_loop.hpp>
#include <iostream>
#include <vector>

TEST(GuestAngleDrawableReuse, ActualRendererPixelsAndQueriesMatchBothPaths) {
    using namespace mln;
    struct Result {
        PremultipliedImage warm;
        PremultipliedImage repeated;
        PremultipliedImage changed;
        std::size_t queried = 0;
        std::size_t allocationsWhileStatic = 0;
    };
    const auto run = [](bool mitigation) {
        util::RunLoop loop;
        HeadlessFrontend frontend{Size{64, 64}, 1};
        Map map(frontend, MapObserver::nullObserver(),
                MapOptions().withMapMode(MapMode::Static).withSize(frontend.getSize()),
                ResourceOptions().withCachePath(":memory:"));
        frontend.getRenderer()->enableAndroidEmulatorGoldfishMitigation(mitigation);
        std::string style = R"JSON({"version":8,"sources":{"geometry":{"type":"geojson","data":{
          "type":"FeatureCollection","features":[{"type":"Feature","id":7,"properties":{},
          "geometry":{"type":"LineString","coordinates":[[-1,0],[1,0]]}}]}}},"layers":[
          {"id":"background","type":"background","paint":{"background-color":"#000000"}},
          {"id":"line","type":"line","source":"geometry","paint":{"line-width":8,"line-color":"#ff0000"}}]})JSON";
        map.getStyle().loadJSON(style);
        map.jumpTo(CameraOptions().withCenter(LatLng{0, 0}).withZoom(5));
        auto warm = frontend.render(map);
        Result result;
        result.warm = std::move(warm.image);
        result.queried = frontend.getRenderer()->queryRenderedFeatures(ScreenBox{{0, 0}, {64, 64}}).size();
        gfx::BackendScope scope{*frontend.getBackend()};
        const auto before = frontend.getBackend()->getContext().renderingStats().totalBuffers;
        for (int frame = 0; frame < 16; ++frame) {
            frontend.renderFrame();
            result.repeated = frontend.readStillImage();
            EXPECT_EQ(result.warm, result.repeated);
        }
        result.allocationsWhileStatic = frontend.getBackend()->getContext().renderingStats().totalBuffers - before;
        style.replace(style.find("#ff0000"), 7, "#00ff00");
        map.getStyle().loadJSON(style);
        auto changed = frontend.render(map);
        result.changed = std::move(changed.image);
        return result;
    };
    auto inputs = verifiedDriver();
    const auto retained = run(guestAngleDrawableReuseDecision(inputs) != GuestAngleDrawableReuseDecision::Enabled);
    inputs.overrideValue = "";
    const auto automatic = run(guestAngleDrawableReuseDecision(inputs) != GuestAngleDrawableReuseDecision::Enabled);
    inputs.overrideValue = "0";
    const auto discarded = run(guestAngleDrawableReuseDecision(inputs) != GuestAngleDrawableReuseDecision::Enabled);
    ASSERT_TRUE(retained.warm.valid());
    ASSERT_TRUE(retained.changed.valid());
    EXPECT_EQ(automatic.warm, retained.warm);
    EXPECT_EQ(automatic.repeated, retained.repeated);
    EXPECT_EQ(automatic.changed, retained.changed);
    EXPECT_EQ(automatic.queried, retained.queried);
    EXPECT_EQ(discarded.warm, retained.warm);
    EXPECT_EQ(discarded.repeated, retained.repeated);
    EXPECT_EQ(discarded.changed, retained.changed);
    EXPECT_NE(retained.warm, retained.changed);
    EXPECT_GT(retained.queried, 0u);
    EXPECT_EQ(discarded.queried, retained.queried);
    // Record actual allocation behavior; host-driver counts are not an emulator FPS assertion.
    std::cout << "static16 buffer allocations mitigation=" << discarded.allocationsWhileStatic
              << " retained=" << retained.allocationsWhileStatic << std::endl;
}
#endif
