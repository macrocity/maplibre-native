#if MLN_RENDER_BACKEND_OPENGL && !MLN_USE_SYMBOL_INSTANCING
#include <mln/test/util.hpp>
#include <limits>
#include <memory>
#include <vector>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/buckets/symbol_bucket.hpp>
#include <mln/style/variable_anchor_offset_collection.hpp>
#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/command_encoder.hpp>
#include <mln/gfx/upload_pass.hpp>
#include <mln/gl/context.hpp>
#include <mln/gl/headless_backend.hpp>

using namespace mln;

namespace {
// Same real SymbolInstance construction seam as test/text/cross_tile_symbol_index.test.cpp.
SymbolInstance makeSortSymbol(float x, float y, std::size_t feature, SymbolContent iconType = SymbolContent::IconRGBA) {
    GeometryCoordinates line;
    ImageMap imageMap;
    const ShapedTextOrientations shaping{};
    style::SymbolLayoutProperties::Evaluated layout;
    IndexedSubfeature subfeature(0, {}, {}, 0);
    Anchor anchor(x, y, 0, 0);
    const std::array<float, 2> offset{{0, 0}};
    std::vector<AnchorOffsetPair> offsets{{style::SymbolAnchorType::Left, offset}};
    VariableAnchorOffsetCollection variableOffsets(std::move(offsets));
    const auto placement = style::SymbolPlacementType::Point;
    auto shared = std::make_shared<SymbolInstanceSharedData>(std::move(line), shaping, std::nullopt, std::nullopt,
                                                           layout, placement, offset, imageMap, 0, iconType, false, false);
    return SymbolInstance(anchor, std::move(shared), shaping, std::nullopt, std::nullopt, 0, 0, placement,
                          offset, 0, 0, offset, subfeature, 0, feature, u"fixture", 0, 0, 0, variableOffsets, false, iconType);
}

PlacedSymbol placed(std::size_t start, std::size_t glyphs) {
    PlacedSymbol value({0, 0}, 0, 16, 16, {{0, 0}}, WritingModeType::Horizontal, {}, {}, std::nullopt);
    value.startIndex = start;
    value.glyphOffsets.resize(glyphs);
    return value;
}

std::unique_ptr<SymbolBucket> makeBucket(std::vector<SymbolInstance> instances = {}, bool sort = true) {
    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout = makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    return std::make_unique<SymbolBucket>(layout, std::map<std::string, Immutable<style::LayerProperties>>{},
                                        16.0f, 1.0f, 0, false, sort, "sort-fixture", std::move(instances),
                                        std::vector<SortKeyRange>{}, 1, false, std::vector<style::TextWritingModeType>{}, false);
}

std::unique_ptr<SymbolBucket> twoSymbols(bool sameFeatureIDs = false) {
    std::vector<SymbolInstance> instances;
    instances.push_back(makeSortSymbol(0, 0, sameFeatureIDs ? 7 : 10));
    instances.push_back(makeSortSymbol(0, 100, sameFeatureIDs ? 7 : 20, SymbolContent::IconSDF));
    instances[0].refPlacedRightTextIndex() = 0;
    instances[0].refPlacedIconIndex() = 0;
    instances[1].refPlacedRightTextIndex() = 1;
    instances[1].refPlacedIconIndex() = 0;
    auto bucket = makeBucket(std::move(instances));
    bucket->text.placedSymbols = {placed(0, 1), placed(4, 1)};
    bucket->icon.placedSymbols = {placed(8, 1)};
    bucket->sdfIcon.placedSymbols = {placed(12, 1)};
    for (auto* buffer : {&bucket->text, &bucket->icon, &bucket->sdfIcon}) buffer->segments.emplace_back(0, 0, 16, 6);
    return bucket;
}

// This is the GPU-ownership boundary of the real IndexVector. No sorter or index producer is mocked.
struct OwnedIndexStorage final : gfx::IndexBufferBase {
    explicit OwnedIndexStorage(int& destroyed_) : destroyed(destroyed_) {}
    ~OwnedIndexStorage() override { ++destroyed; }
    int& destroyed;
};
void markUploaded(SymbolBucket& bucket, gfx::UploadPass& upload, int& destroyed) {
    for (auto* buffer : {&bucket.text, &bucket.icon, &bucket.sdfIcon}) {
        buffer->triangles.setBuffer(std::make_unique<OwnedIndexStorage>(destroyed));
        buffer->triangles.setDirty(false);
    }
    bucket.upload(upload); // Real bucket upload flags, not a test-only state setter.
}
} // namespace

TEST(Buckets, SymbolSortOutput_UnchangedKeepsEveryIndexOwnerAndDirtyState) {
    gl::HeadlessBackend backend({16, 16});
    gfx::BackendScope scope{backend};
    gl::Context context{backend};
    auto encoder = context.createCommandEncoder();
    auto upload = encoder->createUploadPass("symbol-sort", backend.getDefaultRenderable());
    int destroyed = 0;
    auto bucket = twoSymbols();
    bucket->sortFeatures(0);
    EXPECT_EQ(bucket->text.triangles.vector(), (std::vector<uint16_t>{0, 1, 2, 1, 2, 3, 4, 5, 6, 5, 6, 7}));
    EXPECT_EQ(bucket->icon.triangles.vector(), (std::vector<uint16_t>{8, 9, 10, 9, 10, 11}));
    EXPECT_EQ(bucket->sdfIcon.triangles.vector(), (std::vector<uint16_t>{12, 13, 14, 13, 14, 15}));
    markUploaded(*bucket, *upload, destroyed);
    auto* text = bucket->text.triangles.getBuffer();
    auto* icon = bucket->icon.triangles.getBuffer();
    auto* sdf = bucket->sdfIcon.triangles.getBuffer();
    for (int frame = 1; frame <= 100; ++frame) bucket->sortFeatures(float(frame) * 0.0001f);
    EXPECT_EQ(destroyed, 0);
    EXPECT_EQ(bucket->text.triangles.getBuffer(), text);
    EXPECT_EQ(bucket->icon.triangles.getBuffer(), icon);
    EXPECT_EQ(bucket->sdfIcon.triangles.getBuffer(), sdf);
    EXPECT_FALSE(bucket->text.triangles.getDirty());
    EXPECT_FALSE(bucket->icon.triangles.getDirty());
    EXPECT_FALSE(bucket->sdfIcon.triangles.getDirty());
    EXPECT_TRUE(bucket->sortUploaded);
    EXPECT_FALSE(bucket->needsUpload());
    bucket->icon.triangles.setDirty(true);
    bucket->sortUploaded = false;
    bucket->sortFeatures(0.02f);
    EXPECT_TRUE(bucket->icon.triangles.getDirty());
    EXPECT_FALSE(bucket->sortUploaded); // Pre-existing pending work cannot be marked clean.
    EXPECT_EQ(bucket->text.triangles.getBuffer(), text);
}

TEST(Buckets, SymbolSortOutput_ChangedFullSequenceInvalidatesEvenWhenFeatureIDsMatch) {
    gl::HeadlessBackend backend({16, 16});
    gfx::BackendScope scope{backend};
    gl::Context context{backend};
    auto encoder = context.createCommandEncoder();
    auto upload = encoder->createUploadPass("symbol-sort-change", backend.getDefaultRenderable());
    int destroyed = 0;
    auto bucket = twoSymbols(true);
    bucket->sortFeatures(0);
    const auto oldQueryOrder = *bucket->featureSortOrder;
    markUploaded(*bucket, *upload, destroyed);
    bucket->sortFeatures(3.14159265358979323846f);
    EXPECT_EQ(*bucket->featureSortOrder, oldQueryOrder); // [7,7] alone cannot detect the required reorder.
    EXPECT_EQ(bucket->text.triangles.vector(), (std::vector<uint16_t>{4, 5, 6, 5, 6, 7, 0, 1, 2, 1, 2, 3}));
    EXPECT_EQ(destroyed, 3); // Changed path preserves baseline's clear/rebuild behavior for all streams.
    EXPECT_EQ(bucket->text.triangles.getBuffer(), nullptr);
    EXPECT_EQ(bucket->icon.triangles.getBuffer(), nullptr);
    EXPECT_EQ(bucket->sdfIcon.triangles.getBuffer(), nullptr);
    EXPECT_TRUE(bucket->text.triangles.getDirty());
    EXPECT_TRUE(bucket->needsUpload());
    EXPECT_FALSE(bucket->sortUploaded);
}

TEST(Buckets, SymbolSortOutput_EmptyGeometryStillPublishesChangedQueryOrdering) {
    gl::HeadlessBackend backend({16, 16});
    gfx::BackendScope scope{backend};
    gl::Context context{backend};
    auto encoder = context.createCommandEncoder();
    auto upload = encoder->createUploadPass("symbol-sort-empty", backend.getDefaultRenderable());
    int destroyed = 0;
    std::vector<SymbolInstance> instances;
    instances.push_back(makeSortSymbol(0, 0, 10));
    instances.push_back(makeSortSymbol(0, 100, 20));
    auto bucket = makeBucket(std::move(instances));
    bucket->sortFeatures(0);
    EXPECT_EQ(*bucket->featureSortOrder, (std::vector<std::size_t>{10, 20}));
    markUploaded(*bucket, *upload, destroyed);
    auto* oldBuffer = bucket->text.triangles.getBuffer();
    bucket->sortFeatures(3.14159265358979323846f);
    EXPECT_EQ(*bucket->featureSortOrder, (std::vector<std::size_t>{20, 10}));
    EXPECT_EQ(bucket->text.triangles.getBuffer(), oldBuffer);
    EXPECT_EQ(destroyed, 0);
    EXPECT_TRUE(bucket->sortUploaded);
    const auto query = bucket->featureSortOrder;
    bucket->sortFeatures(3.14159265358979323846f); // Same-angle early return is unchanged.
    EXPECT_EQ(bucket->featureSortOrder, query);
}

TEST(Buckets, SymbolSortOutput_PartialGlyphVariantsAndSixteenBitBoundaryStayExact) {
    std::vector<SymbolInstance> instances;
    instances.push_back(makeSortSymbol(0, 0, 10));
    instances[0].refPlacedRightTextIndex() = 0;
    instances[0].refPlacedCenterTextIndex() = 1;
    instances[0].refPlacedLeftTextIndex() = 2;
    instances[0].refPlacedVerticalTextIndex() = 3;
    instances[0].refPlacedIconIndex() = 0;
    auto bucket = makeBucket(std::move(instances));
    bucket->text.placedSymbols = {placed(0, 0), placed(0, 2), placed(65532, 1), placed(4, 1)};
    bucket->icon.placedSymbols = {placed(8, 0)};
    bucket->sortFeatures(0);
    EXPECT_EQ(bucket->text.triangles.vector(), (std::vector<uint16_t>{0, 1, 2, 1, 2, 3, 4, 5, 6, 5, 6, 7,
                                                                    65532, 65533, 65534, 65533, 65534, 65535,
                                                                    4, 5, 6, 5, 6, 7}));
    EXPECT_TRUE(bucket->icon.triangles.empty());
    EXPECT_TRUE(bucket->sdfIcon.triangles.empty());
    bucket->sortFeatures(0.001f);
    EXPECT_EQ(bucket->text.triangles.elements(), 24u);
    bucket->text.placedSymbols[1].glyphOffsets.clear();
    bucket->text.placedSymbols[2].glyphOffsets.clear();
    bucket->text.placedSymbols[3].glyphOffsets.clear();
    bucket->sortFeatures(0.002f);
    EXPECT_TRUE(bucket->text.triangles.empty()); // Real zero-output replacement still clears old geometry.
}

TEST(Buckets, SymbolSortOutput_DisabledAndMultiSegmentGuardsKeepBaselineBehavior) {
    auto disabled = makeBucket({}, false);
    disabled->sortFeatures(0);
    EXPECT_EQ(disabled->sortedAngle, std::numeric_limits<float>::max());
    auto multi = twoSymbols();
    multi->text.segments.emplace_back(4, 6, 4, 6);
    multi->sortFeatures(0);
    EXPECT_EQ(multi->featureSortOrder, nullptr);
    EXPECT_TRUE(multi->text.triangles.empty());
    EXPECT_EQ(multi->sortedAngle, 0); // Existing pre-guard angle assignment is preserved.
}
#endif
