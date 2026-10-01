#pragma once

#include "MLNStyleLayer_Private.h"

#include <mln/layermanager/vehicle_model_layer_factory.hpp>

namespace mln {

class VehicleModelStyleLayerPeerFactory : public LayerPeerFactory, public mln::VehicleModelLayerFactory {
    // LayerPeerFactory overrides.
    LayerFactory* getCoreLayerFactory() final { return this; }
    virtual MLNStyleLayer* createPeer(style::Layer*) final;
};

} // namespace mln
