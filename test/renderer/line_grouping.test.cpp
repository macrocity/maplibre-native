#include <mln/test/util.hpp>
#include <mln/renderer/group_by_layout.hpp>
#include <mln/style/conversion/json.hpp>
#include <mln/style/conversion/layer.hpp>
#include <mln/style/layers/line_layer_impl.hpp>
#include <mln/style/layers/line_layer_properties.hpp>
#include <mln/style/layers/fill_layer_impl.hpp>
#include <mln/style/layers/fill_layer_properties.hpp>
#include <mln/util/io.hpp>
#include <mln/style/parser.hpp>

#include <set>
#include <stdexcept>

using namespace mln;

namespace {
Immutable<style::LayerProperties> line(const std::string& id,
                                      const std::string& filter = R"(["==",["get","kind"],"rail"])",
                                      const std::string& layout = "{}",
                                      const std::string& paint = "{}",
                                      const std::string& extra = "") {
    style::conversion::Error error;
    auto parsed = style::conversion::convertJSON<std::unique_ptr<style::Layer>>(
        "{\"id\":\"" + id + "\",\"type\":\"line\",\"source\":\"base\","
        "\"source-layer\":\"transport\"" + (filter.empty() ? "" : ",\"filter\":" + filter) +
        ",\"layout\":" + layout + ",\"paint\":" + paint + extra + "}", error);
    if (!parsed) throw std::runtime_error(error.message);
    return makeMutable<style::LineLayerProperties>(
        staticImmutableCast<style::LineLayer::Impl>((*parsed)->baseImpl));
}

LayoutGroupKey key(const Immutable<style::LayerProperties>& layer) {
    const auto& impl = *layer->baseImpl;
    return {impl.getTypeInfo(), impl.source, impl.sourceLayer, impl.minZoom, impl.maxZoom,
            impl.visibility, impl.filter.expression.value_or(nullptr), &impl};
}

std::vector<std::string> ids(const std::vector<Immutable<style::LayerProperties>>& group) {
    std::vector<std::string> result;
    for (const auto& layer : group) result.push_back(layer->baseImpl->id);
    return result;
}
} // namespace

TEST(LineGrouping, ParsedIndependentFiltersShareButGlobalComparisonDoesNotChange) {
    const auto a = line("outline", R"(["==",["get","kind"],"rail"])", "{}", R"({"line-width":4})");
    const auto b = line("core", R"(["==",["get","kind"],"rail"])", "{}", R"({"line-width":2})");
    const auto& ai = static_cast<const style::LineLayer::Impl&>(*a->baseImpl);
    const auto& bi = static_cast<const style::LineLayer::Impl&>(*b->baseImpl);
    ASSERT_NE(ai.filter.expression.value().get(), bi.filter.expression.value().get());
    EXPECT_NE(ai.filter, bi.filter); // Global Filter/relayout behavior intentionally unchanged.
    EXPECT_TRUE(ai.hasLayoutDifference(bi));
    EXPECT_FALSE(ai.hasLayoutDifferenceIgnoringFilter(bi));
    EXPECT_EQ(key(a), key(b));
    EXPECT_EQ(std::hash<LayoutGroupKey>{}(key(a)), std::hash<LayoutGroupKey>{}(key(b)));
    const auto groups = groupLayers({a, b});
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(ids(groups.begin()->second), (std::vector<std::string>{"outline", "core"}));
}

TEST(LineGrouping, CompleteFilterLayoutAndDataDrivenPaintRemainDistinct) {
    const auto base = line("a");
    const std::vector<Immutable<style::LayerProperties>> different{
        line("filter", R"(["==",["get","kind"],"road"])") ,
        line("cap", R"(["==",["get","kind"],"rail"])", R"({"line-cap":"round"})"),
        line("sort", R"(["==",["get","kind"],"rail"])", R"({"line-sort-key":["get","rank"]})"),
        line("paint", R"(["==",["get","kind"],"rail"])", "{}", R"({"line-width":["get","width"]})"),
        line("min", R"(["==",["get","kind"],"rail"])", "{}", "{}", ",\"minzoom\":8"),
        line("max", R"(["==",["get","kind"],"rail"])", "{}", "{}", ",\"maxzoom\":20"),
        line("hidden", R"(["==",["get","kind"],"rail"])", R"({"visibility":"none"})")};
    for (const auto& other : different) {
        EXPECT_NE(key(base), key(other)) << other->baseImpl->id;
        EXPECT_EQ(groupLayers({base, other}).size(), 2u) << other->baseImpl->id;
    }
    auto source = key(base);
    source.source = "other";
    EXPECT_NE(key(base), source);
    source = key(base);
    source.sourceLayer = "other";
    EXPECT_NE(key(base), source);
}

TEST(LineGrouping, SharedDataDrivenPaintAndDashPaintRetainAllLayerIdsAndOrder) {
    const auto a = line("rail", R"(["has","kind"])", "{}",
                        R"({"line-color":["match",["get","service"],"yard","red","blue"],"line-dasharray":[1,0]})");
    const auto unrelated = line("other", R"(["has","different"])");
    const auto b = line("hatch", R"(["has","kind"])", "{}",
                        R"({"line-color":["match",["get","service"],"yard","red","blue"],"line-dasharray":[1,2]})");
    const auto groups = groupLayers({a, unrelated, b});
    ASSERT_EQ(groups.size(), 2u);
    const auto found = groups.find(key(a));
    ASSERT_NE(found, groups.end());
    EXPECT_EQ(ids(found->second), (std::vector<std::string>{"rail", "hatch"}));
    EXPECT_EQ(found->second[0], a);
    EXPECT_EQ(found->second[1], b); // Retain each original paint/impl, not a leader substitution.
    const auto reverse = groupLayers({b, unrelated, a});
    EXPECT_EQ(ids(reverse.find(key(a))->second), (std::vector<std::string>{"hatch", "rail"}));
}

TEST(LineGrouping, LiteralFilterValuesAndNestedBranchChangesCannotMerge) {
    const auto a = line("a", R"(["all",["has","kind"],["==",["get","rank"],1]])");
    const auto equal = line("equal", R"(["all",["has","kind"],["==",["get","rank"],1]])");
    const auto changed = line("changed", R"(["all",["has","kind"],["==",["get","rank"],2]])");
    const auto typed = line("typed", R"(["all",["has","kind"],["==",["get","rank"],"1"]])");
    EXPECT_EQ(groupLayers({a, equal, changed, typed}).size(), 3u);
}

TEST(LineGrouping, NullFiltersAndOtherLayerTypesKeepExistingBehavior) {
    const auto a = line("a", "");
    const auto b = line("b", "");
    EXPECT_EQ(groupLayers({a, b}).size(), 1u);
    EXPECT_EQ(groupLayers({a, line("filtered")}).size(), 2u);
    const auto fill = [](const std::string& id) -> Immutable<style::LayerProperties> {
        style::conversion::Error error;
        auto parsed = style::conversion::convertJSON<std::unique_ptr<style::Layer>>(
            "{\"id\":\"" + id + "\",\"type\":\"fill\",\"source\":\"base\","
            "\"source-layer\":\"transport\",\"filter\":[\"has\",\"kind\"]}", error);
        if (!parsed) throw std::runtime_error(error.message);
        return makeMutable<style::FillLayerProperties>(
            staticImmutableCast<style::FillLayer::Impl>((*parsed)->baseImpl));
    };
    const auto f1 = fill("fill1");
    const auto f2 = fill("fill2");
    EXPECT_NE(key(f1), key(f2)); // Structural filter grouping remains line-only.
    EXPECT_EQ(groupLayers({f1, f2}).size(), 2u);
    EXPECT_EQ(groupLayers({a, f1}).size(), 2u);
}

// Actual seven pairs' geometry/paint fields, sanitized to generic IDs/source names and
// example.test tiles. No private metadata, provider URL, sprite/glyph asset or credential.
TEST(LineGrouping, SanitizedSevenParsedStylePairsPreserveEveryLayerAndOrder) {
    style::Parser parser;
    ASSERT_FALSE(parser.parse(util::read_file("test/fixtures/style/line_grouping.json")));
    ASSERT_EQ(parser.layers.size(), 14u);
    std::vector<Immutable<style::LayerProperties>> lines;
    for (const auto& layer : parser.layers) {
        ASSERT_EQ(layer->getTypeInfo(), style::LineLayer::Impl::staticTypeInfo());
        lines.push_back(makeMutable<style::LineLayerProperties>(
            staticImmutableCast<style::LineLayer::Impl>(layer->baseImpl)));
    }
    const auto groups = groupLayers(lines);
    EXPECT_EQ(groups.size(), 7u);
    std::set<std::vector<std::string>> actual;
    for (const auto& [groupKey, group] : groups) {
        (void)groupKey;
        actual.insert(ids(group));
    }
    std::set<std::vector<std::string>> expected;
    for (int i = 0; i < 7; ++i) {
        expected.insert({"pair-" + std::to_string(i) + "-first", "pair-" + std::to_string(i) + "-second"});
    }
    EXPECT_EQ(actual, expected);
}
