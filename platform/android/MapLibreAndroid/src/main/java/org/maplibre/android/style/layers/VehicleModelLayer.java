package org.maplibre.android.style.layers;

import android.graphics.Bitmap;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/**
 * Vehicles drawn as 3D models inside the map's own frames, moved along tracks sampled ahead of time: they follow every
 * gesture exactly, the 3D buildings hide them, and the map draws only while one on screen moves.
 * <p>
 * Hand it the models file once, then a new set of vehicles and their samples whenever they change (a few times a
 * minute). Nothing has to run per frame; ask {@link #wantsFrame(double)} from a frame clock to wake a map at rest when
 * a standing vehicle sets off.
 * </p>
 */
public class VehicleModelLayer extends Layer {

  /**
   * Creates a VehicleModelLayer.
   *
   * @param nativePtr pointer used by core
   */
  @Keep
  public VehicleModelLayer(long nativePtr) {
    super(nativePtr);
  }

  /**
   * Creates a VehicleModelLayer.
   *
   * @param layerId the id of the layer
   */
  public VehicleModelLayer(@NonNull String layerId) {
    super();
    initialize(layerId);
  }

  @Keep
  protected native void initialize(String layerId);

  /**
   * The models: the app's model file (`MCVM`, version 1), meshes in metres, +X left, +Y up, +Z ahead.
   */
  public void setModels(@NonNull byte[] data) {
    checkThread();
    nativeSetModels(data);
  }

  /**
   * The vehicles and their samples. Vehicle {@code i} has {@code sampleCount} samples [longitude, latitude, bearing]
   * from {@code startMs} (milliseconds since the epoch), {@code stepMs} apart, at
   * {@code tracks[i * sampleCount * 3]}.
   *
   * @param kinds      the model each is drawn as (its index in the models file)
   * @param tints      the colour its livery is painted in, 0xRRGGBBAA
   * @param opacities  0 to 1; a faded vehicle is drawn without its inner faces showing
   * @param labelKeys  the key of its line-name pill ({@link #setLabelImage}), or the empty string for none
   * @param partStarts for a model of several parts (an articulated tram): the first of its parts' tracks in
   *                   {@code partTracks}, each laid out like a vehicle's; -1 to stand its parts straight along its own
   *                   track
   */
  public void setVehicles(double startMs, double stepMs, int sampleCount, @NonNull String[] ids, @NonNull byte[] kinds,
                          @NonNull int[] tints, @NonNull float[] opacities, @NonNull String[] labelKeys,
                          @NonNull double[] tracks, @NonNull int[] partStarts, @Nullable double[] partTracks) {
    checkThread();
    nativeSetVehicles(startMs, stepMs, sampleCount, ids, kinds, tints, opacities, labelKeys, tracks, partStarts,
      partTracks);
  }

  /**
   * The image of a line-name pill, drawn flat to the screen over the roofs of the vehicles that name it.
   *
   * @param pixelRatio the bitmap's pixels per point
   */
  public void setLabelImage(@NonNull String key, @NonNull Bitmap bitmap, float pixelRatio) {
    checkThread();
    nativeSetLabelImage(key, bitmap, pixelRatio);
  }

  /**
   * The zooms over which the models fade in; below {@code from} the layer draws nothing.
   */
  public void setZoomRange(float from, float to) {
    checkThread();
    nativeSetZoomRange(from, to);
  }

  /**
   * The dark map's light and lamps.
   */
  public void setDark(boolean dark) {
    checkThread();
    nativeSetDark(dark);
  }

  /**
   * The vehicle drawn under a point of the map view, in screen points, as the last frame drew it: its pill first,
   * then its body, the nearest first; null when none is within {@code slop} points.
   */
  @Nullable
  public String vehicleAt(float x, float y, float slop) {
    checkThread();
    return nativeVehicleAt(x, y, slop);
  }

  /**
   * Whether a vehicle on screen starts to move by {@code nowMs} (milliseconds since the epoch), so the map must draw
   * again although nothing else asked it to.
   */
  public boolean wantsFrame(double nowMs) {
    return nativeWantsFrame(nowMs);
  }

  /**
   * What the layer drew last: drawing (0 or 1), zoom, vehicles, parts, frames per second, mean update time in ms.
   */
  @NonNull
  public double[] getStats() {
    return nativeGetStats();
  }

  @Keep
  private native void nativeSetModels(byte[] data);

  @Keep
  private native void nativeSetVehicles(double startMs, double stepMs, int sampleCount, String[] ids, byte[] kinds,
                                        int[] tints, float[] opacities, String[] labelKeys, double[] tracks,
                                        int[] partStarts, double[] partTracks);

  @Keep
  private native void nativeSetLabelImage(String key, Bitmap bitmap, float pixelRatio);

  @Keep
  private native void nativeSetZoomRange(float from, float to);

  @Keep
  private native void nativeSetDark(boolean dark);

  @Keep
  private native String nativeVehicleAt(float x, float y, float slop);

  @Keep
  private native boolean nativeWantsFrame(double nowMs);

  @Keep
  private native double[] nativeGetStats();

  @Override
  @Keep
  protected native void finalize() throws Throwable;
}
