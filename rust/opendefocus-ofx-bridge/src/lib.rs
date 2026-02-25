//! OpenDefocus OFX Bridge
//!
//! Thin extern "C" wrapper around the OpenDefocus core library,
//! callable from the C++ OFX plugin.

use std::ffi::c_void;

use ndarray::{Array2, ArrayViewMut3};
use opendefocus::datamodel::{self, bokeh_creator, circle_of_confusion, render::FilterMode};

// ---------------------------------------------------------------------------
// C-facing types
// ---------------------------------------------------------------------------

/// Opaque handle to an OpenDefocus instance.
pub type OdHandle = *mut c_void;

/// Result codes returned to C++.
#[repr(C)]
pub enum OdResult {
    Ok = 0,
    ErrorNullPointer = 1,
    ErrorInvalidHandle = 2,
    ErrorRenderFailed = 3,
    ErrorInitFailed = 4,
}

/// Defocus operating mode.
#[repr(C)]
pub enum OdDefocusMode {
    TwoD = 0,
    Depth = 1,
}

/// Render quality preset.
#[repr(C)]
pub enum OdQuality {
    Low = 0,
    Medium = 1,
    High = 2,
    Custom = 3,
}

/// Filter type for bokeh shape.
#[repr(C)]
pub enum OdFilterType {
    Simple = 0,
    Disc = 1,
    Blade = 2,
}

/// Depth math interpretation mode.
#[repr(C)]
pub enum OdMath {
    Direct = 0,
    OneDividedByZ = 1,
    Real = 2,
}

/// Render result output mode.
#[repr(C)]
pub enum OdResultMode {
    Result = 0,
    FocalPlaneSetup = 1,
}

// ---------------------------------------------------------------------------
// Internal instance (not exposed to C)
// ---------------------------------------------------------------------------

struct OdInstance {
    settings: datamodel::Settings,
    renderer: opendefocus::OpenDefocusRenderer,
    runtime: tokio::runtime::Runtime,
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/// Create a new OpenDefocus instance.
///
/// Writes the opaque handle to `*handle_out` on success.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_create(handle_out: *mut OdHandle) -> OdResult {
    if handle_out.is_null() {
        return OdResult::ErrorNullPointer;
    }

    // Initialize logging (once, ignore errors on subsequent calls)
    let _ = env_logger::try_init();

    let runtime = match tokio::runtime::Builder::new_multi_thread()
        .enable_time()
        .build()
    {
        Ok(rt) => rt,
        Err(e) => {
            log::error!("Failed to create tokio runtime: {e}");
            return OdResult::ErrorInitFailed;
        }
    };

    let mut settings = datamodel::Settings::default();
    settings.render.use_gpu_if_available = false; // CPU-only

    let renderer =
        match runtime.block_on(opendefocus::OpenDefocusRenderer::new(false, &mut settings)) {
            Ok(r) => r,
            Err(e) => {
                log::error!("Failed to create OpenDefocus renderer: {e}");
                return OdResult::ErrorInitFailed;
            }
        };

    let instance = Box::new(OdInstance {
        settings,
        renderer,
        runtime,
    });

    unsafe {
        *handle_out = Box::into_raw(instance) as *mut c_void;
    }

    OdResult::Ok
}

/// Destroy an OpenDefocus instance and free all resources.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_destroy(handle: OdHandle) -> OdResult {
    if handle.is_null() {
        return OdResult::ErrorNullPointer;
    }
    unsafe {
        let _ = Box::from_raw(handle as *mut OdInstance);
    }
    OdResult::Ok
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

/// Helper: recover a mutable reference to OdInstance from the opaque handle.
unsafe fn get_instance(handle: OdHandle) -> Option<&'static mut OdInstance> {
    if handle.is_null() {
        return None;
    }
    unsafe { Some(&mut *(handle as *mut OdInstance)) }
}

/// Set the defocus size (radius in pixels).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_size(handle: OdHandle, size: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.circle_of_confusion.size = size;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the focal plane distance.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_focus_plane(handle: OdHandle, focal_plane: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.circle_of_confusion.focal_plane = focal_plane;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the defocus mode (2D or Depth).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_defocus_mode(handle: OdHandle, mode: OdDefocusMode) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            let dm = match mode {
                OdDefocusMode::TwoD => datamodel::defocus::DefocusMode::Twod,
                OdDefocusMode::Depth => datamodel::defocus::DefocusMode::Depth,
            };
            inst.settings.defocus.set_defocus_mode(dm);
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the render quality preset.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_quality(handle: OdHandle, quality: OdQuality) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            let q = match quality {
                OdQuality::Low => datamodel::render::Quality::Low,
                OdQuality::Medium => datamodel::render::Quality::Medium,
                OdQuality::High => datamodel::render::Quality::High,
                OdQuality::Custom => datamodel::render::Quality::Custom,
            };
            inst.settings.render.set_quality(q);
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the image resolution (must be called before render).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_resolution(
    handle: OdHandle,
    width: u32,
    height: u32,
) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.render.resolution = datamodel::UVector2 {
                x: width,
                y: height,
            };
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the render sample count (used when Quality = Custom).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_samples(handle: OdHandle, samples: i32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.render.samples = samples;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the filter type (Simple / Disc / Blade).
///
/// This maps to two internal settings:
/// - `render.filter.mode`: Simple or BokehCreator
/// - `bokeh.filter_type`: Disc or Blade (when BokehCreator)
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_filter_type(handle: OdHandle, filter_type: OdFilterType) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            match filter_type {
                OdFilterType::Simple => {
                    inst.settings.render.filter.set_mode(FilterMode::Simple);
                }
                OdFilterType::Disc => {
                    inst.settings.render.filter.set_mode(FilterMode::BokehCreator);
                    inst.settings.bokeh.set_filter_type(bokeh_creator::FilterType::Disc);
                }
                OdFilterType::Blade => {
                    inst.settings.render.filter.set_mode(FilterMode::BokehCreator);
                    inst.settings.bokeh.set_filter_type(bokeh_creator::FilterType::Blade);
                }
            }
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set filter preview mode.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_filter_preview(handle: OdHandle, preview: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.render.filter.preview = preview;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set filter resolution (for bokeh creator).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_filter_resolution(handle: OdHandle, resolution: u32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.render.filter.resolution = resolution;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh ring color.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_ring_color(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.ring_color = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh inner color.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_inner_color(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.inner_color = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh ring size.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_ring_size(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.ring_size = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh outer blur.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_outer_blur(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.outer_blur = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh inner blur.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_inner_blur(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.inner_blur = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh aspect ratio.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_aspect_ratio(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.aspect_ratio = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set number of aperture blades (Blade filter type).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_blades(handle: OdHandle, blades: u32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.blades = blades;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set aperture blade angle in degrees.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_angle(handle: OdHandle, angle: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.angle = angle;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set aperture blade curvature.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_curvature(handle: OdHandle, curvature: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.curvature = curvature;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Defocus / Advanced settings
// ---------------------------------------------------------------------------

/// Set the depth math mode.
///
/// Maps to two internal fields:
/// - Direct: use_direct_math = true
/// - OneDividedByZ: use_direct_math = false + circle_of_confusion.math = OneDividedByZ
/// - Real: use_direct_math = false + circle_of_confusion.math = Real
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_math(handle: OdHandle, math: OdMath) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            match math {
                OdMath::Direct => {
                    inst.settings.defocus.use_direct_math = true;
                }
                OdMath::OneDividedByZ => {
                    inst.settings.defocus.use_direct_math = false;
                    inst.settings
                        .defocus
                        .circle_of_confusion
                        .set_math(circle_of_confusion::Math::OneDividedByZ);
                }
                OdMath::Real => {
                    inst.settings.defocus.use_direct_math = false;
                    inst.settings
                        .defocus
                        .circle_of_confusion
                        .set_math(circle_of_confusion::Math::Real);
                }
            }
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the render result mode (Result or FocalPlaneSetup).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_result_mode(handle: OdHandle, mode: OdResultMode) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            let rm = match mode {
                OdResultMode::Result => datamodel::render::ResultMode::Result,
                OdResultMode::FocalPlaneSetup => datamodel::render::ResultMode::FocalPlaneSetup,
            };
            inst.settings.render.set_result_mode(rm);
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set whether to show the source image overlay (FocalPlaneSetup mode).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_show_image(handle: OdHandle, show: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.show_image = show;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the focal plane protection range.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_protect(handle: OdHandle, protect: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.circle_of_confusion.protect = protect;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the maximum defocus radius.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_max_size(handle: OdHandle, max_size: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.circle_of_confusion.max_size = max_size;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the gamma correction for bokeh intensities.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_gamma_correction(handle: OdHandle, gamma: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.gamma_correction = gamma;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the farm/batch render quality preset.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_farm_quality(handle: OdHandle, quality: OdQuality) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            let q = match quality {
                OdQuality::Low => datamodel::render::Quality::Low,
                OdQuality::Medium => datamodel::render::Quality::Medium,
                OdQuality::High => datamodel::render::Quality::High,
                OdQuality::Custom => datamodel::render::Quality::Custom,
            };
            inst.settings.render.set_farm_quality(q);
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the size multiplier applied to all defocus radii.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_size_multiplier(handle: OdHandle, multiplier: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.size_multiplier = multiplier;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set the focal plane offset.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_focal_plane_offset(handle: OdHandle, offset: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.defocus.focal_plane_offset = offset;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Bokeh Noise
// ---------------------------------------------------------------------------

/// Set bokeh noise size.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_noise_size(handle: OdHandle, size: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.noise.size = size;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh noise intensity.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_noise_intensity(handle: OdHandle, value: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.noise.intensity = value;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set bokeh noise seed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_noise_seed(handle: OdHandle, seed: u32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.bokeh.noise.seed = seed;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Non-uniform: Catseye
// ---------------------------------------------------------------------------

/// Set catseye enable flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_enable(handle: OdHandle, enable: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.enable = enable;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set catseye amount.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_amount(handle: OdHandle, amount: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.amount = amount;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set catseye inverse flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_inverse(handle: OdHandle, inverse: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.inverse = inverse;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set catseye inverse foreground flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_inverse_foreground(handle: OdHandle, inv_fg: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.inverse_foreground = inv_fg;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set catseye gamma.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_gamma(handle: OdHandle, gamma: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.gamma = gamma;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set catseye softness.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_softness(handle: OdHandle, softness: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.softness = softness;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set catseye dimension based (relative to screen) flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_catseye_dimension_based(handle: OdHandle, dim_based: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.catseye.relative_to_screen = dim_based;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Non-uniform: Barndoors
// ---------------------------------------------------------------------------

/// Set barndoors enable flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_enable(handle: OdHandle, enable: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.enable = enable;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors amount.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_amount(handle: OdHandle, amount: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.amount = amount;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors inverse flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_inverse(handle: OdHandle, inverse: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.inverse = inverse;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors inverse foreground flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_inverse_foreground(handle: OdHandle, inv_fg: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.inverse_foreground = inv_fg;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors gamma.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_gamma(handle: OdHandle, gamma: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.gamma = gamma;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors top position.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_top(handle: OdHandle, top: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.top = top;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors bottom position.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_bottom(handle: OdHandle, bottom: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.bottom = bottom;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors left position.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_left(handle: OdHandle, left: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.left = left;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set barndoors right position.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_barndoors_right(handle: OdHandle, right: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.barndoors.right = right;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Abort
// ---------------------------------------------------------------------------

/// Signal or clear the abort flag for rendering.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_aborted(_handle: OdHandle, aborted: bool) -> OdResult {
    opendefocus::abort::set_aborted(aborted);
    OdResult::Ok
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

/// Main render function.
///
/// Processes image data in-place. The `image_data` buffer must contain
/// `image_height * image_width * image_channels` floats in row-major,
/// channel-interleaved (chunky) order.
///
/// `depth_data` may be NULL if defocus_mode is TwoD.
///
/// `full_region` and `render_region` are [x1, y1, x2, y2] arrays.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_render(
    handle: OdHandle,
    image_data: *mut f32,
    image_width: u32,
    image_height: u32,
    image_channels: u32,
    depth_data: *const f32,
    depth_width: u32,
    depth_height: u32,
    full_region: *const i32,
    render_region: *const i32,
) -> OdResult {
    // Validate handle
    let inst = match unsafe { get_instance(handle) } {
        Some(i) => i,
        None => return OdResult::ErrorNullPointer,
    };

    // Validate image pointer
    if image_data.is_null() {
        return OdResult::ErrorNullPointer;
    }

    let pixel_count = (image_height as usize) * (image_width as usize) * (image_channels as usize);
    let image_slice = unsafe { std::slice::from_raw_parts_mut(image_data, pixel_count) };

    // Wrap in ndarray (same pattern as opendefocus-nuke/src/render.rs:36-43)
    let image = match ArrayViewMut3::from_shape(
        (
            image_height as usize,
            image_width as usize,
            image_channels as usize,
        ),
        image_slice,
    ) {
        Ok(v) => v,
        Err(e) => {
            log::error!("Failed to create image array view: {e}");
            return OdResult::ErrorRenderFailed;
        }
    };

    // Build depth Array2 if provided (same pattern as render.rs:49-56)
    let depth = if !depth_data.is_null() && depth_width > 0 && depth_height > 0 {
        let depth_count = (depth_height as usize) * (depth_width as usize);
        let depth_slice = unsafe { std::slice::from_raw_parts(depth_data, depth_count) };
        match Array2::from_shape_vec(
            (depth_height as usize, depth_width as usize),
            depth_slice.to_vec(),
        ) {
            Ok(arr) => Some(arr),
            Err(e) => {
                log::error!("Failed to create depth array: {e}");
                return OdResult::ErrorRenderFailed;
            }
        }
    } else {
        None
    };

    // Build RenderSpecs from region arrays
    let fr = if full_region.is_null() {
        [0i32, 0, image_width as i32, image_height as i32]
    } else {
        unsafe {
            [
                *full_region,
                *full_region.add(1),
                *full_region.add(2),
                *full_region.add(3),
            ]
        }
    };

    let rr = if render_region.is_null() {
        fr
    } else {
        unsafe {
            [
                *render_region,
                *render_region.add(1),
                *render_region.add(2),
                *render_region.add(3),
            ]
        }
    };

    let render_specs = datamodel::render::RenderSpecs {
        full_region: datamodel::IVector4 {
            x: fr[0],
            y: fr[1],
            z: fr[2],
            w: fr[3],
        },
        render_region: datamodel::IVector4 {
            x: rr[0],
            y: rr[1],
            z: rr[2],
            w: rr[3],
        },
    };

    // Clear abort flag
    opendefocus::abort::set_aborted(false);

    // Call render via block_on (same pattern as opendefocus-nuke/src/lib.rs:555-568)
    let result = inst.runtime.block_on(inst.renderer.render_stripe(
        render_specs,
        inst.settings.clone(),
        image,
        depth,
        None, // no custom filter image for now
    ));

    match result {
        Ok(()) => OdResult::Ok,
        Err(e) => {
            log::error!("Render failed: {e}");
            OdResult::ErrorRenderFailed
        }
    }
}
