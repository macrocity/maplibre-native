#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#import "MLNFoundation.h"
#import "MLNStyleLayer.h"

NS_ASSUME_NONNULL_BEGIN

/**
 A style layer that draws vehicles as 3D models inside the map's own frames, moved along tracks sampled ahead of time:
 they follow every gesture exactly, the 3D buildings hide them, and the map draws only while one on screen moves.

 Hand it the models file once, then a new set of vehicles and their samples whenever they change (a few times a
 minute). Nothing has to run per frame; ask ``wantsFrameAtTime:`` from a frame clock to wake a map at rest when a
 standing vehicle sets off.
 */
MLN_EXPORT
@interface MLNVehicleModelStyleLayer : MLNStyleLayer

- (instancetype)initWithIdentifier:(NSString *)identifier;

/** The models: the app's model file (`MCVM`, version 1), meshes in metres, +X left, +Y up, +Z ahead. */
- (void)setModelsData:(NSData *)data;

/**
 The vehicles and their samples. Vehicle `i` has `sampleCount` samples `[longitude, latitude, bearing]` from `startMs`
 (milliseconds since the epoch), `stepMs` apart, at `tracks[i · sampleCount · 3]`.

 @param kinds The model each is drawn as (its index in the models file).
 @param tints The colour its livery is painted in, `0xRRGGBBAA`.
 @param opacities 0 to 1; a faded vehicle is drawn without its inner faces showing.
 @param labelKeys The key of its line-name pill (``setLabelImage:pixelRatio:forKey:``), or the empty string for none.
 @param partStarts For a model of several parts (an articulated tram): the first of its parts' tracks in `partTracks`,
   each laid out like a vehicle's; -1 to stand its parts straight along its own track.
 */
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
                    partTracks:(nullable const double *)partTracks
                partTrackCount:(NSUInteger)partTrackCount;

/** The image of a line-name pill, drawn flat to the screen over the roofs of the vehicles that name it. */
- (void)setLabelImage:(CGImageRef)image pixelRatio:(CGFloat)pixelRatio forKey:(NSString *)key;

/** The zooms over which the models fade in; below `from` the layer draws nothing. */
- (void)setZoomFrom:(double)from to:(double)to;

/** The dark map's light and lamps. */
@property (nonatomic, getter=isDark) BOOL dark;

/** The vehicle drawn under a point of the map view, as the last frame drew it: its pill first, then its body. */
- (nullable NSString *)vehicleIdentifierAtPoint:(CGPoint)point slop:(CGFloat)slop;

/** Whether a vehicle on screen starts to move by `nowMs` (milliseconds since the epoch), so the map must draw. */
- (BOOL)wantsFrameAtTime:(double)nowMs;

/** What the layer drew last: `drawing`, `zoom`, `vehicles`, `parts`, `framesPerSecond`, `meanUpdateMs`. */
@property (nonatomic, readonly) NSDictionary<NSString *, NSNumber *> *statistics;

@end

NS_ASSUME_NONNULL_END
