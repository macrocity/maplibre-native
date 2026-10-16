#pragma once

#include <mln/style/layer_impl.hpp>
#include <mln/style/layers/line_layer.hpp>
#include <mln/style/layers/line_layer_properties.hpp>

namespace mln {
namespace style {

class LineLayer::Impl : public Layer::Impl {
public:
    using Layer::Impl::Impl;

    bool hasLayoutDifference(const Layer::Impl&) const override;
    // Internal grouping contract: caller has already compared filters structurally.
    // All other geometry-affecting layout and data-driven paint checks are shared.
    bool hasLayoutDifferenceIgnoringFilter(const LineLayer::Impl&) const;
    void stringifyLayout(rapidjson::Writer<rapidjson::StringBuffer>&) const override;

    expression::Dependency getDependencies() const noexcept override {
        return layout.getDependencies() | paint.getDependencies();
    }

    LineLayoutProperties::Unevaluated layout;
    LinePaintProperties::Transitionable paint;

    DECLARE_LAYER_TYPE_INFO;
};

} // namespace style
} // namespace mln
