#if MLN_RENDER_BACKEND_OPENGL
#include <mln/test/util.hpp>
#include <array>
#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/command_encoder.hpp>
#include <mln/gfx/upload_pass.hpp>
#include <mln/gfx/vertex_vector.hpp>
#include <mln/gl/context.hpp>
#include <mln/gl/headless_backend.hpp>
#include <mln/gl/vertex_array_binding_cache.hpp>

using namespace mln;

// Exercise the production cache against resources allocated by the actual GL upload pass.
// This tests descriptor invalidation, not DrawableGL draw/pixel equivalence.
TEST(VertexArrayBindingCache, CompleteDescriptorsAndIndexReplacement) {
    gl::HeadlessBackend backend({16, 16});
    gfx::BackendScope scope{backend};
    gl::Context context{backend};
    auto encoder = context.createCommandEncoder();
    auto upload = encoder->createUploadPass("vao-cache", backend.getDefaultRenderable());
    gfx::VertexVector<std::array<float, 2>> positions;
    positions.emplace_back(std::array<float, 2>{0, 0});
    auto vertices = upload->createVertexBuffer(positions);
    auto otherVertices = upload->createVertexBuffer(positions);
    const uint16_t data[] = {0, 0, 0};
    auto indexes = upload->createIndexBufferResource(data, sizeof(data), gfx::BufferUsageType::StaticDraw);
    auto otherIndexes = upload->createIndexBufferResource(data, sizeof(data), gfx::BufferUsageType::StaticDraw);
    const auto& index = static_cast<const gl::IndexBufferResource&>(*indexes);
    gl::VertexArrayBindingCache cache;
    gl::VertexArrayBindingCache::Bindings bindings(3);
    bindings[0] = gfx::AttributeBinding{{gfx::AttributeDataType::Float2, 0}, 8, &vertices.getResource(), 0, 0};
    bindings[2] = bindings[0];
    EXPECT_FALSE(cache.matches(index, bindings));
    cache.remember(index, bindings);
    EXPECT_TRUE(cache.matches(index, bindings));
    EXPECT_FALSE(cache.matchesIndex(static_cast<const gl::IndexBufferResource&>(*otherIndexes)));
    const auto changed = [&](auto mutate) {
        auto next = bindings;
        mutate(next);
        EXPECT_FALSE(cache.matches(index, next));
        EXPECT_TRUE(cache.matches(index, bindings));
    };
    changed([](auto& b) { b[0]->attribute.dataType = gfx::AttributeDataType::Short2; });
    changed([](auto& b) { ++b[0]->attribute.offset; });
    changed([](auto& b) { ++b[0]->vertexStride; });
    changed([&](auto& b) { b[0]->vertexBufferResource = &otherVertices.getResource(); });
    changed([](auto& b) { ++b[0]->vertexOffset; });
    changed([](auto& b) { ++b[0]->bufferIndex; });
    changed([](auto& b) { b[0].reset(); });
    changed([](auto& b) { b[1] = b[0]; });
    changed([](auto& b) { b.resize(2); });
    changed([](auto& b) { b.resize(4); });
    changed([](auto& b) { b[0]->vertexBufferResource = nullptr; });
    // Content uploads into the same allocation do not change its descriptor identity.
    positions.at(0)[0] = 0.5f;
    upload->updateVertexBuffer(vertices, positions);
    EXPECT_TRUE(cache.matches(index, bindings));
    for (unsigned frame = 0; frame < 10000; ++frame) EXPECT_TRUE(cache.matches(index, bindings));
}
#endif
