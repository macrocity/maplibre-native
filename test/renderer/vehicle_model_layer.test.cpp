#if MLN_RENDER_BACKEND_OPENGL
#include <mln/test/util.hpp>
#include <mln/gl/drawable_gl.hpp>
#include <mln/gl/layer_group_gl.hpp>
#include <mln/renderer/layers/render_vehicle_model_layer.hpp>

namespace mln {

class RenderVehicleModelLayerTestPeer {
public:
    static std::shared_ptr<gl::LayerGroupGL> populate(RenderVehicleModelLayer& layer) {
        auto group = std::make_shared<gl::LayerGroupGL>(0, 3, "vehicles");
        const auto make = [&]() {
            auto drawable = std::make_unique<gl::DrawableGL>("vehicle");
            auto* borrowed = drawable.get();
            group->addDrawable(std::move(drawable));
            return RenderVehicleModelLayer::Slot{borrowed, nullptr};
        };
        layer.partSlots[0][0][0] = make();
        layer.shadowSlot = make();
        layer.labelSlot = make();
        layer.layerGroup = group;
        layer.builtDrawables = 3;
        return group;
    }

    static void expectCleared(const RenderVehicleModelLayer& layer) {
        for (const auto& kind : layer.partSlots) {
            for (const auto& part : kind) {
                for (const auto& slot : part) {
                    EXPECT_EQ(nullptr, slot.drawable);
                    EXPECT_EQ(nullptr, slot.segment);
                }
            }
        }
        EXPECT_EQ(nullptr, layer.shadowSlot.drawable);
        EXPECT_EQ(nullptr, layer.shadowSlot.segment);
        EXPECT_EQ(nullptr, layer.labelSlot.drawable);
        EXPECT_EQ(nullptr, layer.labelSlot.segment);
        EXPECT_EQ(0u, layer.builtDrawables);
    }

    static void disable(RenderVehicleModelLayer& layer) { layer.disable(); }

    static void expectDisabled(const RenderVehicleModelLayer& layer) {
        EXPECT_FALSE(layer.partSlots[0][0][0].drawable->getEnabled());
        EXPECT_FALSE(layer.shadowSlot.drawable->getEnabled());
        EXPECT_FALSE(layer.labelSlot.drawable->getEnabled());
    }
};

TEST(VehicleModelLayer, RemovingDrawablesInvalidatesBorrowedPointers) {
    RenderVehicleModelLayer layer(makeMutable<style::VehicleModelLayer::Impl>("vehicles"));
    auto group = RenderVehicleModelLayerTestPeer::populate(layer);
    RenderLayer& renderer = layer;
    EXPECT_EQ(3u, renderer.removeAllDrawables());
    EXPECT_EQ(0u, group->getDrawableCount());
    RenderVehicleModelLayerTestPeer::expectCleared(layer);
    RenderVehicleModelLayerTestPeer::disable(layer);
    EXPECT_EQ(0u, renderer.removeAllDrawables());
    group = RenderVehicleModelLayerTestPeer::populate(layer);
    RenderVehicleModelLayerTestPeer::disable(layer);
    RenderVehicleModelLayerTestPeer::expectDisabled(layer);
    EXPECT_EQ(3u, renderer.removeAllDrawables());
    RenderVehicleModelLayerTestPeer::expectCleared(layer);
}

} // namespace mln
#endif
