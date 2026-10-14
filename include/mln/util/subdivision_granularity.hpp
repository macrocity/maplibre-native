#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace mln {

/// How finely tile geometry is split so the vertex shader can bend it onto the globe; port of GL JS
/// `subdivision_granularity_settings.ts`.
class SubdivisionGranularityExpression {
public:
    constexpr SubdivisionGranularityExpression(uint32_t baseZoomGranularity_,
                                               uint32_t minGranularity_,
                                               uint8_t flatFromZoom_ = 32)
        : baseZoomGranularity(baseZoomGranularity_),
          minGranularity(minGranularity_),
          flatFromZoom(flatFromZoom_) {
        assert(minGranularity <= baseZoomGranularity || baseZoomGranularity == 0);
    }

    /// The base granularity halves with every zoom level, down to the minimum and never below 1. Tiles of a zoom the
    /// projection only shows flat are not split at all.
    uint32_t getGranularityForZoomLevel(uint8_t zoom) const {
        if (zoom >= flatFromZoom) return 1;
        return std::max({zoom < 32 ? baseZoomGranularity >> zoom : 0u, minGranularity, 1u});
    }

    bool operator==(const SubdivisionGranularityExpression&) const = default;

private:
    uint32_t baseZoomGranularity;
    uint32_t minGranularity;
    uint8_t flatFromZoom;
};

struct SubdivisionGranularitySetting {
    SubdivisionGranularityExpression fill{0, 0};
    SubdivisionGranularityExpression line{0, 0};
    SubdivisionGranularityExpression tile{0, 0};
    SubdivisionGranularityExpression stencil{0, 0};
    /// Circle vertices per axis: 1, 3, 5 or 7.
    uint8_t circle = 1;

    bool operator==(const SubdivisionGranularitySetting&) const = default;

    static constexpr SubdivisionGranularitySetting none() { return {}; }

    /// GL JS's globe values. Fill and line geometry of tiles from `flatFromZoom` on, which the projection draws flat
    /// (a zoom step to Mercator), is left whole: splitting every building and block there only cost layout time and
    /// vertices.
    static constexpr SubdivisionGranularitySetting globe(uint8_t flatFromZoom = 32) {
        return {.fill = {128, 2, flatFromZoom},
                .line = {512, 0, flatFromZoom},
                .tile = {128, 32},
                .stencil = {128, 1},
                .circle = 3};
    }
};

} // namespace mln
