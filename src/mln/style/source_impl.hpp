#pragma once

#include <mln/style/source.hpp>

#include <string>

namespace mln {

class RenderSource;

namespace style {

class SourceObserver;

class Source::Impl {
public:
    virtual ~Impl() = default;

    Impl& operator=(const Impl&) = delete;

    virtual std::optional<std::string> getAttribution() const = 0;
    void setPrefetchZoomDelta(std::optional<uint8_t> delta) noexcept;
    std::optional<uint8_t> getPrefetchZoomDelta() const noexcept;
    void setMinimumTileUpdateInterval(Duration interval) { minimumTileUpdateInterval = interval; }
    Duration getMinimumTileUpdateInterval() const { return minimumTileUpdateInterval; }
    void setMaxOverscaleFactorForParentTiles(std::optional<uint8_t> overscaleFactor) noexcept;
    std::optional<uint8_t> getMaxOverscaleFactorForParentTiles() const noexcept;

    std::optional<std::uint32_t> getMaximumInactiveTileCacheSize() const noexcept {
        return maximumInactiveTiles;
    }
    void setMaximumInactiveTileCacheSize(std::optional<std::uint32_t> count) noexcept {
        if (type != SourceType::Vector) return;
        maximumInactiveTiles = count ? std::optional<std::uint32_t>{
                                          *count > Source::maximumInactiveTileCacheSize
                                              ? Source::maximumInactiveTileCacheSize
                                              : *count}
                                    : std::nullopt;
    }

    bool isVolatile() const { return volatileFlag; }
    void setVolatile(bool set) { volatileFlag = set; }
    const SourceType type;
    const std::string id;

    virtual bool isUpdateSynchronous() const { return false; }

protected:
    std::optional<std::uint32_t> maximumInactiveTiles;
    std::optional<uint8_t> prefetchZoomDelta;
    std::optional<uint8_t> maxOverscaleFactor;
    Duration minimumTileUpdateInterval{Duration::zero()};
    bool volatileFlag = false;

    Impl(SourceType, std::string);
    Impl(const Impl&) = default;
};

} // namespace style
} // namespace mln
