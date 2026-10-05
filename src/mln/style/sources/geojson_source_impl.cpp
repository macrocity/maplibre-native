#include <mln/style/sources/geojson_source_impl.hpp>
#include <mln/tile/tile_id.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/feature.hpp>
#include <mln/util/string.hpp>
#include <mln/util/thread_pool.hpp>
#include <mln/util/identity.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif

#include <mapbox/geojsonvt.hpp>
#include <supercluster.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cmath>

namespace mln {
namespace style {

class GeoJSONVTData final : public GeoJSONData {
    // Keep one projected source. The geojson-vt index otherwise retains feature and property
    // copies for every ancestor and sibling generated while the camera visits new tiles.
    struct State {
        const mapbox::geojsonvt::Options options;
        const mapbox::geojsonvt::detail::vt_features features;

        static mapbox::geojsonvt::detail::vt_features project(const GeoJSON& geoJSON,
                                                              const mapbox::geojsonvt::Options& options) {
            const auto input = mapbox::geojsonvt::geojson::visit(geoJSON, mapbox::geojsonvt::ToFeatureCollection{});
            const auto converted = mapbox::geojsonvt::detail::convert(
                input, (options.tolerance / options.extent) / (1u << options.maxZoom), options.generateId);
            return mapbox::geojsonvt::detail::wrap(
                converted, double(options.buffer) / options.extent, options.lineMetrics);
        }

        State(const GeoJSON& geoJSON, const mapbox::geojsonvt::Options& options_)
            : options(options_),
              features(project(geoJSON, options_)) {}

        TileFeatures getTile(const CanonicalTileID& id) const {
            if (id.z > options.maxZoom) throw std::runtime_error("Requested zoom higher than maxZoom");
            const auto z2 = 1u << id.z;
            const auto x = id.x % z2;
            const auto* current = &features;
            mapbox::geojsonvt::detail::vt_features clipped;
            const double padding = 0.5 * options.buffer / options.extent;
            // Follow the same clipping ancestry as geojson-vt, but generate only the requested
            // child. Direct clipping to the final bounds changes buffered geometry and line metrics.
            for (uint8_t zoom = 0; zoom < id.z; ++zoom) {
                if (current->empty()) return {};
                mapbox::geometry::box<double> bounds{{2, 1}, {-1, 0}};
                for (const auto& feature : *current) {
                    bounds.min.x = std::min(feature.bbox.min.x, bounds.min.x);
                    bounds.min.y = std::min(feature.bbox.min.y, bounds.min.y);
                    bounds.max.x = std::max(feature.bbox.max.x, bounds.max.x);
                    bounds.max.y = std::max(feature.bbox.max.y, bounds.max.y);
                }
                const auto scale = 1u << zoom;
                const auto shift = id.z - zoom;
                const auto px = x >> shift;
                const auto py = id.y >> shift;
                const double halfX = ((x >> (shift - 1)) & 1u) ? 0.5 : 0;
                const double halfY = ((id.y >> (shift - 1)) & 1u) ? 0.5 : 0;
                const double x0 = (px + halfX - padding) / scale;
                const double x1 = (px + halfX + 0.5 + padding) / scale;
                const double y0 = (py + halfY - padding) / scale;
                const double y1 = (py + halfY + 0.5 + padding) / scale;
                // An entirely contained feature set needs no copy or clipping.
                if (bounds.min.x >= x0 && bounds.max.x < x1 && bounds.min.y >= y0 && bounds.max.y < y1) continue;
                const auto left = mapbox::geojsonvt::detail::clip<0>(
                    *current, x0, x1, bounds.min.x, bounds.max.x, options.lineMetrics);
                clipped = mapbox::geojsonvt::detail::clip<1>(
                    left, y0, y1, bounds.min.y, bounds.max.y, options.lineMetrics);
                current = &clipped;
            }
            mapbox::geojsonvt::detail::InternalTile tile{
                *current,
                id.z,
                x,
                id.y,
                options.extent,
                id.z == options.maxZoom ? 0 : (options.tolerance / options.extent) / z2,
                options.lineMetrics};
            return std::move(tile.tile.features);
        }
    };

    void getTile(const CanonicalTileID& id, const std::function<void(TileFeatures)>& fn, bool runSynchronously) final {
        assert(fn);
        if (runSynchronously) {
            fn(impl->getTile(id));
        } else {
            sequencedScheduler->scheduleAndReplyValue(
                util::SimpleIdentity::Empty, [id, state = impl]() -> TileFeatures { return state->getTile(id); }, fn);
        }
    }

    Features getChildren(const std::uint32_t) final { return {}; }
    Features getLeaves(const std::uint32_t, const std::uint32_t, const std::uint32_t) final { return {}; }
    std::uint8_t getClusterExpansionZoom(std::uint32_t) final { return 0; }

    friend GeoJSONData;
    GeoJSONVTData(const GeoJSON& geoJSON,
                  const mapbox::geojsonvt::Options& options,
                  std::shared_ptr<Scheduler> sequencedScheduler_)
        : impl(std::make_shared<const State>(geoJSON, options)),
          sequencedScheduler(std::move(sequencedScheduler_)) {
        assert(sequencedScheduler);
    }

    std::shared_ptr<const State> impl;
    std::shared_ptr<Scheduler> sequencedScheduler;
};

class SuperclusterData final : public GeoJSONData {
    void getTile(const CanonicalTileID& id, const std::function<void(TileFeatures)>& fn, bool) final {
        assert(fn);
        fn(impl.getTile(id.z, id.x, id.y));
    }

    Features getChildren(const std::uint32_t cluster_id) final { return impl.getChildren(cluster_id); }

    Features getLeaves(const std::uint32_t cluster_id, const std::uint32_t limit, const std::uint32_t offset) final {
        return impl.getLeaves(cluster_id, limit, offset);
    }

    std::uint8_t getClusterExpansionZoom(std::uint32_t cluster_id) final {
        return impl.getClusterExpansionZoom(cluster_id);
    }

    friend GeoJSONData;
    SuperclusterData(const Features& features, const mapbox::supercluster::Options& options)
        : impl(features, options) {}
    mapbox::supercluster::Supercluster impl;
};

template <class T>
T evaluateFeature(const mapbox::feature::feature<double>& f,
                  const std::shared_ptr<expression::Expression>& expression,
                  std::optional<T> accumulated = std::nullopt) {
    const expression::EvaluationResult result = expression->evaluate(accumulated, f);
    if (result) {
        std::optional<T> typed = expression::fromExpressionValue<T>(*result);
        if (typed) {
            return std::move(*typed);
        }
    }
    return T();
}

// static
std::shared_ptr<GeoJSONData> GeoJSONData::create(const GeoJSON& geoJSON,
                                                 std::shared_ptr<Scheduler> sequencedScheduler,
                                                 const Immutable<GeoJSONOptions>& options) {
    constexpr double scale = util::EXTENT / util::tileSize_D;
    if (options->cluster && geoJSON.is<Features>() && !geoJSON.get<Features>().empty()) {
        mapbox::supercluster::Options clusterOptions;
        clusterOptions.maxZoom = options->clusterMaxZoom;
        clusterOptions.extent = util::EXTENT;
        clusterOptions.radius = static_cast<uint16_t>(::round(scale * options->clusterRadius));
        clusterOptions.minPoints = options->clusterMinPoints;

        auto feature = std::make_shared<Feature>();
        clusterOptions.map = [feature, options](const PropertyMap& properties) -> PropertyMap {
            PropertyMap ret{};
            if (properties.empty()) return ret;
            for (const auto& p : options->clusterProperties) {
                feature->properties = properties;
                ret[p.first] = evaluateFeature<Value>(*feature, p.second.first);
            }
            return ret;
        };
        clusterOptions.reduce = [feature, options](PropertyMap& toReturn, const PropertyMap& toFill) {
            for (const auto& p : options->clusterProperties) {
                if (!toFill.contains(p.first)) {
                    continue;
                }
                feature->properties = toFill;
                std::optional<Value> accumulated(toReturn[p.first]);
                toReturn[p.first] = evaluateFeature<Value>(*feature, p.second.second, accumulated);
            }
        };
        return std::shared_ptr<GeoJSONData>(new SuperclusterData(geoJSON.get<Features>(), clusterOptions));
    }

    mapbox::geojsonvt::Options vtOptions;
    vtOptions.maxZoom = options->maxzoom;
    vtOptions.extent = util::EXTENT;
    vtOptions.buffer = static_cast<uint16_t>(::round(scale * options->buffer));
    vtOptions.tolerance = scale * options->tolerance;
    vtOptions.lineMetrics = options->lineMetrics;
    return std::shared_ptr<GeoJSONData>(new GeoJSONVTData(geoJSON, vtOptions, std::move(sequencedScheduler)));
}

GeoJSONSource::Impl::Impl(std::string id_, Immutable<GeoJSONOptions> options_)
    : Source::Impl(SourceType::GeoJSON, std::move(id_)),
      options(std::move(options_)) {}

GeoJSONSource::Impl::Impl(const GeoJSONSource::Impl& other, std::shared_ptr<GeoJSONData> data_)
    : Source::Impl(other),
      options(other.options),
      data(std::move(data_)),
      overrideSynchronousUpdate(other.overrideSynchronousUpdate) {}

GeoJSONSource::Impl::~Impl() = default;

Range<uint8_t> GeoJSONSource::Impl::getZoomRange() const {
    return {options->minzoom, options->maxzoom};
}

std::weak_ptr<GeoJSONData> GeoJSONSource::Impl::getData() const {
    return data;
}

std::optional<std::string> GeoJSONSource::Impl::getAttribution() const {
    return {};
}

bool GeoJSONSource::Impl::isUpdateSynchronous() const {
    return options->synchronousUpdate || overrideSynchronousUpdate;
}

void GeoJSONSource::Impl::setOverrideSynchronousUpdate(bool newOverride) const {
    overrideSynchronousUpdate = newOverride;
}

} // namespace style
} // namespace mln
