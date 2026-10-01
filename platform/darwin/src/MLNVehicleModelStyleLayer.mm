#import "MLNVehicleModelStyleLayer.h"
#import "MLNVehicleModelStyleLayer_Private.h"

#import "MLNStyleLayer_Private.h"

#include <mln/style/layers/vehicle_model_layer.hpp>
#include <mln/util/image+MLNAdditions.hpp>

@interface MLNVehicleModelStyleLayer ()
@property (nonatomic, readonly) mln::style::VehicleModelLayer *rawLayer;
@end

@implementation MLNVehicleModelStyleLayer

- (instancetype)initWithIdentifier:(NSString *)identifier {
  auto layer = std::make_unique<mln::style::VehicleModelLayer>(identifier.UTF8String);
  return self = [super initWithPendingLayer:std::move(layer)];
}

- (mln::style::VehicleModelLayer *)rawLayer {
  return (mln::style::VehicleModelLayer *)super.rawLayer;
}

- (void)setModelsData:(NSData *)data {
  MLNAssertStyleLayerIsValid();
  const auto *bytes = static_cast<const uint8_t *>(data.bytes);
  self.rawLayer->setModels(std::make_shared<const std::vector<uint8_t>>(bytes, bytes + data.length));
}

- (void)setVehiclesWithStartMs:(double)startMs
                        stepMs:(double)stepMs
                   sampleCount:(NSUInteger)sampleCount
                   identifiers:(NSArray<NSString *> *)identifiers
                         kinds:(const uint8_t *)kinds
                         tints:(const uint32_t *)tints
                     opacities:(const float *)opacities
                     labelKeys:(NSArray<NSString *> *)labelKeys
                        tracks:(const double *)tracks
                    trackCount:(NSUInteger)trackCount
                    partStarts:(const int32_t *)partStarts
                    partTracks:(const double *)partTracks
                partTrackCount:(NSUInteger)partTrackCount {
  MLNAssertStyleLayerIsValid();
  auto samples = std::make_shared<mln::style::VehicleModelSamples>();
  samples->startMs = startMs;
  samples->stepMs = MAX(1.0, stepMs);
  samples->sampleCount = static_cast<uint32_t>(sampleCount);
  const NSUInteger count = MIN(identifiers.count, labelKeys.count);
  if (sampleCount == 0 || trackCount < count * sampleCount * 3) return;
  samples->vehicles.reserve(count);
  for (NSUInteger i = 0; i < count; i++) {
    mln::style::VehicleModelSamples::Vehicle vehicle;
    vehicle.id = identifiers[i].UTF8String;
    vehicle.kind = kinds[i];
    const uint32_t rgba = tints[i];
    vehicle.tint = mln::Color(((rgba >> 24) & 0xff) / 255.0f, ((rgba >> 16) & 0xff) / 255.0f,
                              ((rgba >> 8) & 0xff) / 255.0f, (rgba & 0xff) / 255.0f);
    vehicle.opacity = opacities[i];
    vehicle.labelKey = labelKeys[i].UTF8String;
    vehicle.track = static_cast<uint32_t>(i);
    vehicle.parts = partStarts[i];
    samples->vehicles.push_back(std::move(vehicle));
  }
  samples->tracks.assign(tracks, tracks + count * sampleCount * 3);
  if (partTracks && partTrackCount > 0) samples->partTracks.assign(partTracks, partTracks + partTrackCount);
  self.rawLayer->setVehicles(std::move(samples));
}

- (void)setLabelImage:(CGImageRef)image pixelRatio:(CGFloat)pixelRatio forKey:(NSString *)key {
  MLNAssertStyleLayerIsValid();
  if (!image) return;
  self.rawLayer->setLabelImage(key.UTF8String, MLNPremultipliedImageFromCGImage(image), static_cast<float>(pixelRatio));
}

- (void)setZoomFrom:(double)from to:(double)to {
  MLNAssertStyleLayerIsValid();
  self.rawLayer->setZoomRange(static_cast<float>(from), static_cast<float>(to));
}

- (void)setDark:(BOOL)dark {
  MLNAssertStyleLayerIsValid();
  self.rawLayer->setDark(dark);
}

- (BOOL)isDark {
  MLNAssertStyleLayerIsValid();
  return self.rawLayer->getDark();
}

- (NSString *)vehicleIdentifierAtPoint:(CGPoint)point slop:(CGFloat)slop {
  MLNAssertStyleLayerIsValid();
  const auto found = self.rawLayer->vehicleAt({point.x, point.y}, slop);
  return found ? @(found->c_str()) : nil;
}

- (BOOL)wantsFrameAtTime:(double)nowMs {
  MLNAssertStyleLayerIsValid();
  return self.rawLayer->wantsFrame(nowMs);
}

- (NSDictionary<NSString *, NSNumber *> *)statistics {
  MLNAssertStyleLayerIsValid();
  const auto stats = self.rawLayer->getStats();
  return @{
    @"drawing" : @(stats.drawing),
    @"zoom" : @(stats.zoom),
    @"vehicles" : @(stats.vehicles),
    @"parts" : @(stats.parts),
    @"framesPerSecond" : @(stats.framesPerSecond),
    @"meanUpdateMs" : @(stats.meanUpdateMs),
  };
}

@end

namespace mln {

MLNStyleLayer *VehicleModelStyleLayerPeerFactory::createPeer(style::Layer *rawLayer) {
  return [[MLNVehicleModelStyleLayer alloc] initWithRawLayer:rawLayer];
}

} // namespace mln
