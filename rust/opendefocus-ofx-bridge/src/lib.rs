//! OpenDefocus OFX Bridge
//!
//! Thin extern "C" wrapper around the OpenDefocus core library,
//! callable from the C++ OFX plugin.

use std::ffi::c_void;

use ndarray::{Array2, Array3, ArrayViewMut3};
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
    Camera = 2,
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
    Image = 3,
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
    gpu_failed: bool,
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
    settings.render.use_gpu_if_available = true; // GPU preferred (Phase 1: wgpu independent device)

    let renderer =
        match runtime.block_on(opendefocus::OpenDefocusRenderer::new(true, &mut settings)) {
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
        gpu_failed: false,
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

/// Query whether the renderer is using GPU acceleration.
///
/// Returns `true` if GPU is active, `false` if CPU fallback is in use.
/// Returns `false` if the handle is null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_is_gpu_active(handle: OdHandle) -> bool {
    match unsafe { get_instance(handle) } {
        Some(inst) => inst.renderer.is_gpu() && !inst.gpu_failed,
        None => false,
    }
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
                // Camera uses Depth internally; camera-specific data (CameraOp) is not yet available in OFX
                OdDefocusMode::Camera => datamodel::defocus::DefocusMode::Depth,
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

/// Set the render center point (image center, required for non-uniform effects).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_center(
    handle: OdHandle,
    x: f32,
    y: f32,
) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.render.center = datamodel::Vector2 { x, y };
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
                OdFilterType::Image => {
                    inst.settings.render.filter.set_mode(FilterMode::Image);
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
// Non-uniform: Astigmatism
// ---------------------------------------------------------------------------

/// Set astigmatism enable flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_astigmatism_enable(handle: OdHandle, enable: bool) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.astigmatism.enable = enable;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set astigmatism amount.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_astigmatism_amount(handle: OdHandle, amount: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.astigmatism.amount = amount;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set astigmatism gamma.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_astigmatism_gamma(handle: OdHandle, gamma: f32) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.astigmatism.gamma = gamma;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Non-uniform: Axial Aberration
// ---------------------------------------------------------------------------

/// Set axial aberration enable flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_axial_aberration_enable(
    handle: OdHandle,
    enable: bool,
) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.axial_aberration.enable = enable;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set axial aberration amount.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_axial_aberration_amount(
    handle: OdHandle,
    amount: f32,
) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.axial_aberration.amount = amount;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

/// Set axial aberration color type (0=RED_BLUE, 1=BLUE_YELLOW, 2=GREEN_PURPLE).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_axial_aberration_type(
    handle: OdHandle,
    color_type: i32,
) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings
                .non_uniform
                .axial_aberration
                .set_color_type(
                    datamodel::non_uniform::AxialAberrationType::try_from(color_type)
                        .unwrap_or(datamodel::non_uniform::AxialAberrationType::RedBlue),
                );
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// Non-uniform: Inverse Foreground (global)
// ---------------------------------------------------------------------------

/// Set the global inverse foreground filter shape flag.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_inverse_foreground(
    handle: OdHandle,
    inverse: bool,
) -> OdResult {
    match unsafe { get_instance(handle) } {
        Some(inst) => {
            inst.settings.non_uniform.inverse_foreground = inverse;
            OdResult::Ok
        }
        None => OdResult::ErrorNullPointer,
    }
}

// ---------------------------------------------------------------------------
// GPU control
// ---------------------------------------------------------------------------

/// Set GPU usage preference and recreate the renderer if the mode changed.
///
/// When `use_gpu` transitions from true to false (or vice versa), the internal
/// renderer is destroyed and recreated with the new preference.
/// Also resets `gpu_failed` flag when re-enabling GPU.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_use_gpu(handle: OdHandle, use_gpu: bool) -> OdResult {
    let inst = match unsafe { get_instance(handle) } {
        Some(i) => i,
        None => return OdResult::ErrorNullPointer,
    };

    let currently_gpu = inst.renderer.is_gpu() && !inst.gpu_failed;
    let need_recreate = use_gpu != currently_gpu
        || (use_gpu && inst.gpu_failed)      // re-enable GPU after previous failure
        || (!use_gpu && inst.renderer.is_gpu()); // force CPU even if GPU is active

    if !need_recreate {
        inst.settings.render.use_gpu_if_available = use_gpu;
        return OdResult::Ok;
    }

    inst.settings.render.use_gpu_if_available = use_gpu;

    let mut new_settings = inst.settings.clone();
    match inst.runtime.block_on(
        opendefocus::OpenDefocusRenderer::new(use_gpu, &mut new_settings),
    ) {
        Ok(new_renderer) => {
            inst.renderer = new_renderer;
            if use_gpu {
                inst.gpu_failed = false; // reset failure flag on GPU re-enable
            }
            log::info!(
                "Renderer recreated: {}",
                if inst.renderer.is_gpu() { "GPU" } else { "CPU" }
            );
            OdResult::Ok
        }
        Err(e) => {
            log::error!("Failed to recreate renderer: {e}");
            // Keep old renderer as fallback
            OdResult::ErrorRenderFailed
        }
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
/// `filter_data` may be NULL if filter_type is not Image.
/// When provided, must contain `filter_height * filter_width * filter_channels` floats.
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
    filter_data: *const f32,
    filter_width: u32,
    filter_height: u32,
    filter_channels: u32,
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

    // Build filter Array3 if provided (same pattern as render.rs:62-73)
    let filter = if !filter_data.is_null() && filter_width > 0 && filter_height > 0 && filter_channels > 0 {
        let filter_count = (filter_height as usize) * (filter_width as usize) * (filter_channels as usize);
        let filter_slice = unsafe { std::slice::from_raw_parts(filter_data, filter_count) };
        match Array3::from_shape_vec(
            (filter_height as usize, filter_width as usize, filter_channels as usize),
            filter_slice.to_vec(),
        ) {
            Ok(arr) => Some(arr),
            Err(e) => {
                log::error!("Failed to create filter array: {e}");
                return OdResult::ErrorRenderFailed;
            }
        }
    } else {
        None
    };

    // Clear abort flag
    opendefocus::abort::set_aborted(false);

    // If GPU previously failed, recreate as CPU-only before attempting render
    if inst.gpu_failed && inst.renderer.is_gpu() {
        log::info!("GPU previously failed, recreating renderer as CPU-only");
        let mut cpu_settings = inst.settings.clone();
        cpu_settings.render.use_gpu_if_available = false;
        match inst.runtime.block_on(opendefocus::OpenDefocusRenderer::new(false, &mut cpu_settings)) {
            Ok(cpu_renderer) => {
                inst.renderer = cpu_renderer;
                log::info!("CPU renderer created successfully");
            }
            Err(e) => {
                log::error!("Failed to create CPU fallback renderer: {e}");
                return OdResult::ErrorRenderFailed;
            }
        }
    }

    // Call render via block_on, wrapped in catch_unwind to prevent wgpu panics
    // from crossing the extern "C" boundary (which would abort the host process).
    // wgpu panics on validation errors (e.g. buffer exceeding max_*_buffer_binding_size
    // for 4K+ images) instead of returning errors.
    let render_result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        inst.runtime.block_on(inst.renderer.render_stripe(
            render_specs.clone(),
            inst.settings.clone(),
            image,
            depth,
            filter,
        ))
    }));

    // Helper to extract panic message
    let panic_msg = |info: Box<dyn std::any::Any + Send>| -> String {
        if let Some(s) = info.downcast_ref::<&str>() {
            s.to_string()
        } else if let Some(s) = info.downcast_ref::<String>() {
            s.clone()
        } else {
            "unknown panic".to_string()
        }
    };

    // Check if GPU failed and we should retry with CPU immediately
    let gpu_failed_now = match &render_result {
        Ok(Ok(())) => false,
        Ok(Err(_)) => inst.renderer.is_gpu() && !inst.gpu_failed,
        Err(_) => inst.renderer.is_gpu() && !inst.gpu_failed,
    };

    if gpu_failed_now {
        // Log the GPU failure
        match render_result {
            Ok(Err(e)) => log::error!("GPU render failed: {e} — retrying with CPU immediately"),
            Err(info) => log::error!("GPU render panicked: {} — retrying with CPU immediately", panic_msg(info)),
            _ => {}
        }
        inst.gpu_failed = true;

        // Create CPU renderer
        let mut cpu_settings = inst.settings.clone();
        cpu_settings.render.use_gpu_if_available = false;
        match inst.runtime.block_on(opendefocus::OpenDefocusRenderer::new(false, &mut cpu_settings)) {
            Ok(cpu_renderer) => {
                inst.renderer = cpu_renderer;
                log::info!("CPU fallback renderer created, retrying render");
            }
            Err(e) => {
                log::error!("Failed to create CPU fallback renderer: {e}");
                return OdResult::ErrorRenderFailed;
            }
        }

        // Rebuild array views from raw pointers (originals were consumed by the failed render)
        let image_retry = match ArrayViewMut3::from_shape(
            (image_height as usize, image_width as usize, image_channels as usize),
            unsafe { std::slice::from_raw_parts_mut(image_data, pixel_count) },
        ) {
            Ok(v) => v,
            Err(e) => {
                log::error!("Failed to recreate image array for CPU retry: {e}");
                return OdResult::ErrorRenderFailed;
            }
        };

        let depth_retry = if !depth_data.is_null() && depth_width > 0 && depth_height > 0 {
            let depth_count = (depth_height as usize) * (depth_width as usize);
            let depth_slice = unsafe { std::slice::from_raw_parts(depth_data, depth_count) };
            Array2::from_shape_vec(
                (depth_height as usize, depth_width as usize),
                depth_slice.to_vec(),
            ).ok()
        } else {
            None
        };

        let filter_retry = if !filter_data.is_null() && filter_width > 0 && filter_height > 0 && filter_channels > 0 {
            let filter_count = (filter_height as usize) * (filter_width as usize) * (filter_channels as usize);
            let filter_slice = unsafe { std::slice::from_raw_parts(filter_data, filter_count) };
            Array3::from_shape_vec(
                (filter_height as usize, filter_width as usize, filter_channels as usize),
                filter_slice.to_vec(),
            ).ok()
        } else {
            None
        };

        opendefocus::abort::set_aborted(false);

        // CPU retry (no catch_unwind needed — CPU renderer won't panic on buffer limits)
        match inst.runtime.block_on(inst.renderer.render_stripe(
            render_specs,
            inst.settings.clone(),
            image_retry,
            depth_retry,
            filter_retry,
        )) {
            Ok(()) => {
                log::info!("CPU fallback render succeeded");
                return OdResult::Ok;
            }
            Err(e) => {
                log::error!("CPU fallback render also failed: {e}");
                return OdResult::ErrorRenderFailed;
            }
        }
    }

    // Normal result handling (no GPU failure, or already on CPU)
    match render_result {
        Ok(Ok(())) => OdResult::Ok,
        Ok(Err(e)) => {
            log::error!("Render failed: {e}");
            OdResult::ErrorRenderFailed
        }
        Err(info) => {
            log::error!("Render panicked: {}", panic_msg(info));
            OdResult::ErrorRenderFailed
        }
    }
}
