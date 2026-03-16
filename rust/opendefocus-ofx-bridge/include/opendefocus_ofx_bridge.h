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
  CAMERA = 2,
} OdDefocusMode;

/**
 * Filter type for bokeh shape.
 */
typedef enum OdFilterType {
  SIMPLE = 0,
  DISC = 1,
  BLADE = 2,
  IMAGE = 3,
} OdFilterType;

/**
 * Depth math interpretation mode.
 */
typedef enum OdMath {
  DIRECT = 0,
  ONE_DIVIDED_BY_Z = 1,
  REAL = 2,
} OdMath;

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
  ABORTED = 5,
} OdResult;

/**
 * Render result output mode.
 */
typedef enum OdResultMode {
  RESULT = 0,
  FOCAL_PLANE_SETUP = 1,
} OdResultMode;

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
 * Query whether the renderer is using GPU acceleration.
 *
 * Returns `true` if GPU is active, `false` if CPU fallback is in use.
 * Returns `false` if the handle is null.
 */
bool od_is_gpu_active(OdHandle handle);

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
 * Set the render center point (image center, required for non-uniform effects).
 */
enum OdResult od_set_center(OdHandle handle, float x, float y);

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
 * Set the depth math mode.
 *
 * Maps to two internal fields:
 * - Direct: use_direct_math = true
 * - OneDividedByZ: use_direct_math = false + circle_of_confusion.math = OneDividedByZ
 * - Real: use_direct_math = false + circle_of_confusion.math = Real
 */
enum OdResult od_set_math(OdHandle handle, enum OdMath math);

/**
 * Set the render result mode (Result or FocalPlaneSetup).
 */
enum OdResult od_set_result_mode(OdHandle handle, enum OdResultMode mode);

/**
 * Set whether to show the source image overlay (FocalPlaneSetup mode).
 */
enum OdResult od_set_show_image(OdHandle handle, bool show);

/**
 * Set the focal plane protection range.
 */
enum OdResult od_set_protect(OdHandle handle, float protect);

/**
 * Set the maximum defocus radius.
 */
enum OdResult od_set_max_size(OdHandle handle, float max_size);

/**
 * Set the gamma correction for bokeh intensities.
 */
enum OdResult od_set_gamma_correction(OdHandle handle, float gamma);

/**
 * Set the farm/batch render quality preset.
 */
enum OdResult od_set_farm_quality(OdHandle handle, enum OdQuality quality);

/**
 * Set the size multiplier applied to all defocus radii.
 */
enum OdResult od_set_size_multiplier(OdHandle handle, float multiplier);

/**
 * Set the focal plane offset.
 */
enum OdResult od_set_focal_plane_offset(OdHandle handle, float offset);

/**
 * Set bokeh noise size.
 */
enum OdResult od_set_noise_size(OdHandle handle, float size);

/**
 * Set bokeh noise intensity.
 */
enum OdResult od_set_noise_intensity(OdHandle handle, float value);

/**
 * Set bokeh noise seed.
 */
enum OdResult od_set_noise_seed(OdHandle handle, uint32_t seed);

/**
 * Set catseye enable flag.
 */
enum OdResult od_set_catseye_enable(OdHandle handle, bool enable);

/**
 * Set catseye amount.
 */
enum OdResult od_set_catseye_amount(OdHandle handle, float amount);

/**
 * Set catseye inverse flag.
 */
enum OdResult od_set_catseye_inverse(OdHandle handle, bool inverse);

/**
 * Set catseye inverse foreground flag.
 */
enum OdResult od_set_catseye_inverse_foreground(OdHandle handle, bool inv_fg);

/**
 * Set catseye gamma.
 */
enum OdResult od_set_catseye_gamma(OdHandle handle, float gamma);

/**
 * Set catseye softness.
 */
enum OdResult od_set_catseye_softness(OdHandle handle, float softness);

/**
 * Set catseye dimension based (relative to screen) flag.
 */
enum OdResult od_set_catseye_dimension_based(OdHandle handle, bool dim_based);

/**
 * Set barndoors enable flag.
 */
enum OdResult od_set_barndoors_enable(OdHandle handle, bool enable);

/**
 * Set barndoors amount.
 */
enum OdResult od_set_barndoors_amount(OdHandle handle, float amount);

/**
 * Set barndoors inverse flag.
 */
enum OdResult od_set_barndoors_inverse(OdHandle handle, bool inverse);

/**
 * Set barndoors inverse foreground flag.
 */
enum OdResult od_set_barndoors_inverse_foreground(OdHandle handle, bool inv_fg);

/**
 * Set barndoors gamma.
 */
enum OdResult od_set_barndoors_gamma(OdHandle handle, float gamma);

/**
 * Set barndoors top position.
 */
enum OdResult od_set_barndoors_top(OdHandle handle, float top);

/**
 * Set barndoors bottom position.
 */
enum OdResult od_set_barndoors_bottom(OdHandle handle, float bottom);

/**
 * Set barndoors left position.
 */
enum OdResult od_set_barndoors_left(OdHandle handle, float left);

/**
 * Set barndoors right position.
 */
enum OdResult od_set_barndoors_right(OdHandle handle, float right);

/**
 * Set astigmatism enable flag.
 */
enum OdResult od_set_astigmatism_enable(OdHandle handle, bool enable);

/**
 * Set astigmatism amount.
 */
enum OdResult od_set_astigmatism_amount(OdHandle handle, float amount);

/**
 * Set astigmatism gamma.
 */
enum OdResult od_set_astigmatism_gamma(OdHandle handle, float gamma);

/**
 * Set axial aberration enable flag.
 */
enum OdResult od_set_axial_aberration_enable(OdHandle handle, bool enable);

/**
 * Set axial aberration amount.
 */
enum OdResult od_set_axial_aberration_amount(OdHandle handle, float amount);

/**
 * Set axial aberration color type (0=RED_BLUE, 1=BLUE_YELLOW, 2=GREEN_PURPLE).
 */
enum OdResult od_set_axial_aberration_type(OdHandle handle, int32_t color_type);

/**
 * Set the global inverse foreground filter shape flag.
 */
enum OdResult od_set_inverse_foreground(OdHandle handle, bool inverse);

/**
 * Set GPU usage preference and recreate the renderer if the mode changed.
 *
 * When `use_gpu` transitions from true to false (or vice versa), the internal
 * renderer is destroyed and recreated with the new preference.
 * Also resets `gpu_failed` flag when re-enabling GPU.
 */
enum OdResult od_set_use_gpu(OdHandle handle, bool use_gpu);

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
 * `filter_data` may be NULL if filter_type is not Image.
 * When provided, must contain `filter_height * filter_width * filter_channels` floats.
 *
 * `full_region` and `render_region` are [x1, y1, x2, y2] arrays.
 *
 * Internally splits the image into horizontal stripes (NDK parity) to keep
 * wgpu storage buffers under the 128 MB default limit and improve CPU cache
 * utilization.  Each stripe is rendered via `render_stripe()` independently.
 */
enum OdResult od_render(OdHandle handle,
                        float *image_data,
                        uint32_t image_width,
                        uint32_t image_height,
                        uint32_t image_channels,
                        const float *depth_data,
                        uint32_t depth_width,
                        uint32_t depth_height,
                        const float *filter_data,
                        uint32_t filter_width,
                        uint32_t filter_height,
                        uint32_t filter_channels,
                        const int32_t *full_region,
                        const int32_t *render_region,
                        bool (*abort_check_fn)(void*),
                        void *abort_user_data);

#endif  /* OPENDEFOCUS_OFX_BRIDGE_H */
