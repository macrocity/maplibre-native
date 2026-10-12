#pragma once

#include <mln/map/zoom_history.hpp>
#include <mln/util/chrono.hpp>

namespace mln {

class CrossfadeParameters {
public:
    float fromScale;
    float toScale;
    float t;
};

class PropertyEvaluationParameters {
public:
    explicit PropertyEvaluationParameters(float z_)
        : z(z_),
          now(Clock::time_point::max()),

          defaultFadeDuration(0) {}

    PropertyEvaluationParameters(ZoomHistory zoomHistory_, TimePoint now_, Duration defaultFadeDuration_)
        : z(zoomHistory_.lastZoom),
          now(now_),
          zoomHistory(zoomHistory_),
          defaultFadeDuration(defaultFadeDuration_) {}

    /// A pattern (fill, line, background) or a dash array is laid out in the tiles of the whole zoom below, so on the
    /// screen it grows with the zoom to twice its size and halves at the next whole zoom. It is blended by the zoom
    /// between its size at the whole zoom below (`from`, scale 1) and the one it has at the next (`to`, scale 1/2,
    /// the same on the screen there): continuous through every zoom, and still when the zoom is. It used to blend over
    /// 300 ms after each whole zoom, from twice or half the size, so the pattern jumped as the zoom passed one.
    CrossfadeParameters getCrossfadeParameters() const {
        const float fraction = z - std::floor(z);
        return CrossfadeParameters{.fromScale = 1.0f, .toScale = 0.5f, .t = fraction};
    }

    float z;
    TimePoint now;
    ZoomHistory zoomHistory;
    Duration defaultFadeDuration;

    bool zoomChanged = true;
    bool layerChanged = false;
    bool hasCrossfade = true;
};

} // namespace mln
