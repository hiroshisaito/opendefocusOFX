#ifndef OPENDEFOCUS_OFX_BRIDGE_H
#define OPENDEFOCUS_OFX_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Defocus operating mode.
 */
typedef enum OdDefocusMode {
  TWO_D = 0,
  DEPTH = 1,
} OdDefocusMode;

/**
 * Filter type for bokeh shape.
 */
typedef enum OdFilterType {
  SIMPLE = 0,
  DISC = 1,
  BLADE = 2,
} OdFilterType;

/**
 * Render quality preset.
 */
typedef enum OdQuality {
  LOW = 0,
  MEDIUM = 1,
  HIGH = 2,
  CUSTOM = 3,
} OdQuality;

/**
 * Result codes returned to C++.
 */
typedef enum OdResult {
  OK = 0,
  ERROR_NULL_POINTER = 1,
  ERROR_INVALID_HANDLE = 2,
  ERROR_RENDER_FAILED = 3,
  ERROR_INIT_FAILED = 4,
} OdResult;

/**
 * Opaque handle to an OpenDefocus instance.
 */
typedef void *OdHandle;

/**
 * Create a new OpenDefocus instance.
 *
 * Writes the opaque handle to `*handle_out` on success.
 */
enum OdResult od_create(OdHandle *handle_out);

/**
 * Destroy an OpenDefocus instance and free all resources.
 */
enum OdResult od_destroy(OdHandle handle);

/**
 * Set the defocus size (radius in pixels).
 */
enum OdResult od_set_size(OdHandle handle, float size);

/**
 * Set the focal plane distance.
 */
enum OdResult od_set_focus_plane(OdHandle handle, float focal_plane);

/**
 * Set the defocus mode (2D or Depth).
 */
enum OdResult od_set_defocus_mode(OdHandle handle, enum OdDefocusMode mode);

/**
 * Set the render quality preset.
 */
enum OdResult od_set_quality(OdHandle handle, enum OdQuality quality);

/**
 * Set the image resolution (must be called before render).
 */
enum OdResult od_set_resolution(OdHandle handle, uint32_t width, uint32_t height);

/**
 * Set the render sample count (used when Quality = Custom).
 */
enum OdResult od_set_samples(OdHandle handle, int32_t samples);

/**
 * Set the filter type (Simple / Disc / Blade).
 *
 * This maps to two internal settings:
 * - `render.filter.mode`: Simple or BokehCreator
 * - `bokeh.filter_type`: Disc or Blade (when BokehCreator)
 */
enum OdResult od_set_filter_type(OdHandle handle, enum OdFilterType filter_type);

/**
 * Set filter preview mode.
 */
enum OdResult od_set_filter_preview(OdHandle handle, bool preview);

/**
 * Set filter resolution (for bokeh creator).
 */
enum OdResult od_set_filter_resolution(OdHandle handle, uint32_t resolution);

/**
 * Set bokeh ring color.
 */
enum OdResult od_set_ring_color(OdHandle handle, float value);

/**
 * Set bokeh inner color.
 */
enum OdResult od_set_inner_color(OdHandle handle, float value);

/**
 * Set bokeh ring size.
 */
enum OdResult od_set_ring_size(OdHandle handle, float value);

/**
 * Set bokeh outer blur.
 */
enum OdResult od_set_outer_blur(OdHandle handle, float value);

/**
 * Set bokeh inner blur.
 */
enum OdResult od_set_inner_blur(OdHandle handle, float value);

/**
 * Set bokeh aspect ratio.
 */
enum OdResult od_set_aspect_ratio(OdHandle handle, float value);

/**
 * Set number of aperture blades (Blade filter type).
 */
enum OdResult od_set_blades(OdHandle handle, uint32_t blades);

/**
 * Set aperture blade angle in degrees.
 */
enum OdResult od_set_angle(OdHandle handle, float angle);

/**
 * Set aperture blade curvature.
 */
enum OdResult od_set_curvature(OdHandle handle, float curvature);

/**
 * Signal or clear the abort flag for rendering.
 */
enum OdResult od_set_aborted(OdHandle _handle, bool aborted);

/**
 * Main render function.
 *
 * Processes image data in-place. The `image_data` buffer must contain
 * `image_height * image_width * image_channels` floats in row-major,
 * channel-interleaved (chunky) order.
 *
 * `depth_data` may be NULL if defocus_mode is TwoD.
 *
 * `full_region` and `render_region` are [x1, y1, x2, y2] arrays.
 */
enum OdResult od_render(OdHandle handle,
                        float *image_data,
                        uint32_t image_width,
                        uint32_t image_height,
                        uint32_t image_channels,
                        const float *depth_data,
                        uint32_t depth_width,
                        uint32_t depth_height,
                        const int32_t *full_region,
                        const int32_t *render_region);

#endif  /* OPENDEFOCUS_OFX_BRIDGE_H */
