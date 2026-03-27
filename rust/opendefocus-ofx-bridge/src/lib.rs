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
    Aborted = 5,
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
    /// Lazy-initialized renderer.  `None` until the first render or
    /// explicit `od_set_use_gpu()` call.  This keeps `od_create()` fast
    /// (no wgpu device probe at node-creation time).
    renderer: Option<opendefocus::OpenDefocusRenderer>,
    runtime: tokio::runtime::Runtime,
    gpu_failed: bool,
    /// Per-instance abort flag.  Avoids cross-instance interference when
    /// multiple instances render in parallel (eRenderInstanceSafe).
    aborted: std::sync::Arc<std::sync::atomic::AtomicBool>,
}

impl OdInstance {
    /// Ensure the renderer is initialised, creating it on demand.
    /// Returns a mutable reference to the renderer.
    fn ensure_renderer(&mut self) -> Result<&mut opendefocus::OpenDefocusRenderer, OdResult> {
        if self.renderer.is_none() {
            log::info!("Lazy-initializing renderer (GPU={})", self.settings.render.use_gpu_if_available);
            match self.runtime.block_on(opendefocus::OpenDefocusRenderer::new(
                self.settings.render.use_gpu_if_available,
                &mut self.settings,
            )) {
                Ok(r) => {
                    log::info!("Renderer created: {}", if r.is_gpu() { "GPU" } else { "CPU" });
                    self.renderer = Some(r);
                }
                Err(e) => {
                    log::error!("Failed to create renderer: {e}");
                    return Err(OdResult::ErrorInitFailed);
                }
            }
        }
        Ok(self.renderer.as_mut().unwrap())
    }
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

    // Initialize logging (once, ignore errors on subsequent calls).
    // Default to "info" level so OFX hosts see log output without
    // requiring RUST_LOG to be set in the process environment.
    let _ = env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or("info")
    ).try_init();

    // Wrap initialization in catch_unwind to prevent panics from
    // propagating across the FFI boundary and crashing the host.
    //
    // Lazy init: only create the tokio runtime and default settings here.
    // The renderer (wgpu device probe) is deferred to the first render
    // or od_set_use_gpu() call, keeping node creation fast.
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let runtime = match tokio::runtime::Builder::new_current_thread()
            .enable_time()
            .build()
        {
            Ok(rt) => rt,
            Err(e) => {
                log::error!("Failed to create tokio runtime: {e}");
                return Err(OdResult::ErrorInitFailed);
            }
        };

        let mut settings = datamodel::Settings::default();
        settings.render.use_gpu_if_available = true;

        Ok(Box::new(OdInstance {
            settings,
            renderer: None, // lazy — created on first render
            runtime,
            gpu_failed: false,
            aborted: std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false)),
        }))
    }));

    match result {
        Ok(Ok(instance)) => {
            unsafe {
                *handle_out = Box::into_raw(instance) as *mut c_void;
            }
            OdResult::Ok
        }
        Ok(Err(e)) => e,
        Err(_panic) => {
            log::error!("od_create: caught panic during initialization");
            OdResult::ErrorInitFailed
        }
    }
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
        Some(inst) => inst.renderer.as_ref().map_or(false, |r| r.is_gpu()) && !inst.gpu_failed,
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

    let currently_gpu = inst.renderer.as_ref().map_or(false, |r| r.is_gpu()) && !inst.gpu_failed;
    let need_recreate = inst.renderer.is_none() // not yet initialised
        || use_gpu != currently_gpu
        || (use_gpu && inst.gpu_failed)      // re-enable GPU after previous failure
        || (!use_gpu && inst.renderer.as_ref().map_or(false, |r| r.is_gpu())); // force CPU

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
            if use_gpu {
                inst.gpu_failed = false; // reset failure flag on GPU re-enable
            }
            log::info!(
                "Renderer recreated: {}",
                if new_renderer.is_gpu() { "GPU" } else { "CPU" }
            );
            inst.renderer = Some(new_renderer);
            OdResult::Ok
        }
        Err(e) => {
            log::error!("Failed to recreate renderer: {e}");
            // Keep old renderer as fallback (if any)
            OdResult::ErrorRenderFailed
        }
    }
}

// ---------------------------------------------------------------------------
// Abort
// ---------------------------------------------------------------------------

/// Signal or clear the abort flag for this instance's rendering.
///
/// Sets the per-instance flag.  The upstream global abort flag is
/// synchronised at render boundaries (cleared on render start, set
/// on abort, cleared on render end).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn od_set_aborted(handle: OdHandle, aborted: bool) -> OdResult {
    let inst = match unsafe { get_instance(handle) } {
        Some(i) => i,
        None => {
            // Fallback: set global flag when handle is invalid
            opendefocus::abort::set_aborted(aborted);
            return OdResult::ErrorNullPointer;
        }
    };
    inst.aborted.store(aborted, std::sync::atomic::Ordering::SeqCst);
    // Also propagate to upstream global flag so that internal kernel loops
    // (which check the global flag) honour the abort request.
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
///
/// Internally splits the image into horizontal stripes (NDK parity) to keep
/// wgpu storage buffers under the 128 MB default limit and improve CPU cache
/// utilization.  Each stripe is rendered via `render_stripe()` independently.
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
    abort_check_fn: Option<unsafe extern "C" fn(*mut c_void) -> bool>,
    abort_user_data: *mut c_void,
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

    // Build RenderSpecs from region arrays (needed for stripe calculation)
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

    // Build filter Array3 if provided (shared across all stripes, cloned per stripe)
    let filter_template = if !filter_data.is_null() && filter_width > 0 && filter_height > 0 && filter_channels > 0 {
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

    // Clear both per-instance and global abort flags at render start.
    // This ensures a previous abort from this or another instance does
    // not leak into the current render.
    inst.aborted.store(false, std::sync::atomic::Ordering::SeqCst);
    opendefocus::abort::set_aborted(false);

    // Lazy-init renderer on first render (or after destroy).
    if inst.renderer.is_none() {
        if let Err(e) = inst.ensure_renderer() {
            return e;
        }
    }

    // If GPU previously failed, recreate as CPU-only before attempting render
    if inst.gpu_failed && inst.renderer.as_ref().map_or(false, |r| r.is_gpu()) {
        log::info!("GPU previously failed, recreating renderer as CPU-only");
        let mut cpu_settings = inst.settings.clone();
        cpu_settings.render.use_gpu_if_available = false;
        match inst.runtime.block_on(opendefocus::OpenDefocusRenderer::new(false, &mut cpu_settings)) {
            Ok(cpu_renderer) => {
                inst.renderer = Some(cpu_renderer);
                log::info!("CPU renderer created successfully");
            }
            Err(e) => {
                log::error!("Failed to create CPU fallback renderer: {e}");
                return OdResult::ErrorRenderFailed;
            }
        }
    }

    // --- Stripe-based rendering (NDK parity) ---
    //
    // Split the image into horizontal stripes to:
    // 1. Keep wgpu storage buffers under 128 MB (enables UHD+ GPU rendering)
    // 2. Improve CPU cache utilization (each stripe fits in L3 cache)
    // 3. Allow abort checks between stripes for responsive cancellation
    //
    // Stripe height mirrors NDK stripe_height() (lib.rs:458-473).
    //
    // IMPORTANT: In NDK, Nuke provides a fresh source buffer per stripe call.
    // In OFX, we share a single image_data buffer across all stripes.
    // After rendering stripe N, its render_region contains rendered (blurred)
    // pixels. Stripe N+1's padding overlaps with stripe N's render_region,
    // so it would read already-blurred pixels as source — causing visible seams.
    //
    // Fix: Clone the source image before the loop. For each stripe, copy fresh
    // source data into a temporary buffer, render into it, then copy only the
    // render_region back to the output image_data.

    let is_gpu = inst.renderer.as_ref().map_or(false, |r| r.is_gpu()) && !inst.gpu_failed;
    let stripe_h = get_stripe_height(&inst.settings, is_gpu, image_height) as i32;
    // Add extra padding beyond the convolution radius to prevent edge-clamp
    // artifacts at stripe boundaries.  The upstream kernel's bilinear sampling
    // (bilinear_depth_based) accesses base_coords + 1, and skip_overlap
    // processes one extra row (process_region.y - 1).  Without the extra
    // margin, the outermost ring samples from render_region pixels would hit
    // ClampToEdge at the stripe buffer boundary, producing subtly different
    // results compared to a full-image render — visible as periodic seams.
    // In the NDK flow, NUKE's host engine provides its own additional margin
    // beyond the plugin's reported padding; we replicate that here.
    let padding = inst.settings.defocus.get_padding() as i32 + 4;

    let has_depth = !depth_data.is_null() && depth_width > 0 && depth_height > 0;
    let img_w = image_width as usize;
    let img_ch = image_channels as usize;
    let dep_w = depth_width as usize;

    // Snapshot of the original source image (read-only reference for all stripes)
    let total_img_pixels = (image_height as usize) * img_w * img_ch;
    let source_image = unsafe { std::slice::from_raw_parts(image_data, total_img_pixels) }.to_vec();

    // Buffer Y origin: fr[1] is the RoD-based Y of the buffer's first row.
    // All y_in/y_out values are RoD-based, so subtract buf_y_origin to get
    // buffer-local row indices for memory access.
    let buf_y_origin = fr[1];

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

    // Pre-allocate stripe buffer to avoid per-stripe heap allocation.
    // Max input height = stripe_h + padding*2 (clamped to image bounds).
    let max_stripe_h_in = (stripe_h + padding * 2).min(fr[3] - fr[1]) as usize;
    let max_stripe_pixels = max_stripe_h_in * img_w * img_ch;
    let mut stripe_buf = vec![0.0f32; max_stripe_pixels];

    let mut is_first_gpu_stripe = is_gpu;
    let mut y_out = rr[1]; // render_region.y

    while y_out < rr[3] {
        // Abort check between stripes (coarse — Phase 1)
        // Check per-instance flag (may have been set by upstream kernel via global flag)
        if inst.aborted.load(std::sync::atomic::Ordering::SeqCst)
            || opendefocus::abort::get_aborted()
        {
            // Clear global flag so it doesn't affect other instances
            opendefocus::abort::set_aborted(false);
            return OdResult::Aborted;
        }
        // Check via host callback (if provided)
        if let Some(check_fn) = abort_check_fn {
            if unsafe { check_fn(abort_user_data) } {
                inst.aborted.store(true, std::sync::atomic::Ordering::SeqCst);
                opendefocus::abort::set_aborted(true); // propagate to upstream internals
                return OdResult::Aborted;
            }
        }

        let y_out_end = (y_out + stripe_h).min(rr[3]);

        // Input range = output range + padding (clamped to buffer bounds)
        let y_in = (y_out - padding).max(fr[1]);
        let y_in_end = (y_out_end + padding).min(fr[3]);
        let stripe_h_in = (y_in_end - y_in) as usize;

        // Copy fresh source pixels into the pre-allocated stripe buffer.
        // This ensures each stripe always sees un-rendered source pixels,
        // even in padding areas that overlap with previously rendered stripes.
        // y_in is RoD-based; subtract buf_y_origin to get buffer-local row index.
        let buf_y_in = (y_in - buf_y_origin) as usize;
        let img_offset = buf_y_in * img_w * img_ch;
        let img_count = stripe_h_in * img_w * img_ch;
        stripe_buf[..img_count].copy_from_slice(&source_image[img_offset..img_offset + img_count]);

        // Build stripe RenderSpecs with global coordinates.
        // full_region.y must encode the stripe's absolute Y offset so that
        // get_real_coordinates() returns correct screen-space positions for
        // position-dependent effects (astigmatism, catseye, barndoors).
        let stripe_specs = datamodel::render::RenderSpecs {
            full_region: datamodel::IVector4 {
                x: fr[0],
                y: y_in,
                z: fr[2],
                w: y_in_end,
            },
            render_region: datamodel::IVector4 {
                x: rr[0] - 2,
                y: y_out - 2,
                z: rr[2] + 2,
                w: y_out_end + 2,
            },
        };

        let stripe_image = match ArrayViewMut3::from_shape(
            (stripe_h_in, img_w, img_ch),
            &mut stripe_buf[..img_count],
        ) {
            Ok(v) => v,
            Err(e) => {
                log::error!("Failed to create stripe image view (y={}): {e}", y_out);
                return OdResult::ErrorRenderFailed;
            }
        };

        // Build stripe depth Array2 from raw pointer
        let stripe_depth = if has_depth {
            let d_offset = buf_y_in * dep_w;
            let d_count = stripe_h_in * dep_w;
            let d_slice = unsafe { std::slice::from_raw_parts(depth_data.add(d_offset), d_count) };
            match Array2::from_shape_vec(
                (stripe_h_in, dep_w),
                d_slice.to_vec(),
            ) {
                Ok(arr) => Some(arr),
                Err(e) => {
                    log::error!("Failed to create stripe depth array (y={}): {e}", y_out);
                    return OdResult::ErrorRenderFailed;
                }
            }
        } else {
            None
        };

        // Helper: copy only the render_region from stripe_buf back to image_data
        let copy_render_region = |buf: &[f32]| {
            let y_render_start = (y_out - y_in) as usize;
            let y_render_end = (y_out_end - y_in) as usize;
            let render_src_offset = y_render_start * img_w * img_ch;
            let render_count = (y_render_end - y_render_start) * img_w * img_ch;
            // y_out is RoD-based; subtract buf_y_origin for buffer-local offset
            let buf_y_out = (y_out - buf_y_origin) as usize;
            let out_offset = buf_y_out * img_w * img_ch;
            unsafe {
                std::ptr::copy_nonoverlapping(
                    buf.as_ptr().add(render_src_offset),
                    image_data.add(out_offset),
                    render_count,
                );
            }
        };

        // First GPU stripe: wrap in catch_unwind for wgpu panic protection.
        // If GPU fails, switch to CPU and retry this stripe + all remaining.
        if is_first_gpu_stripe {
            is_first_gpu_stripe = false;

            let render_result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                inst.runtime.block_on(inst.renderer.as_mut().unwrap().render_stripe(
                    stripe_specs.clone(),
                    inst.settings.clone(),
                    stripe_image,
                    stripe_depth,
                    filter_template.clone(),
                ))
            }));

            let gpu_failed_now = match &render_result {
                Ok(Ok(())) => false,
                Ok(Err(_)) => true,
                Err(_) => true,
            };

            if gpu_failed_now {
                // Log the GPU failure
                match render_result {
                    Ok(Err(e)) => log::error!("GPU stripe render failed: {e} — switching to CPU"),
                    Err(info) => log::error!("GPU stripe render panicked: {} — switching to CPU", panic_msg(info)),
                    _ => {}
                }
                inst.gpu_failed = true;

                // Create CPU renderer
                let mut cpu_settings = inst.settings.clone();
                cpu_settings.render.use_gpu_if_available = false;
                match inst.runtime.block_on(opendefocus::OpenDefocusRenderer::new(false, &mut cpu_settings)) {
                    Ok(cpu_renderer) => {
                        inst.renderer = Some(cpu_renderer);
                        log::info!("CPU fallback renderer created");
                    }
                    Err(e) => {
                        log::error!("Failed to create CPU fallback renderer: {e}");
                        return OdResult::ErrorRenderFailed;
                    }
                }

                // Retry this stripe with CPU (re-copy source — originals consumed by GPU attempt)
                stripe_buf[..img_count].copy_from_slice(&source_image[img_offset..img_offset + img_count]);
                let retry_image = match ArrayViewMut3::from_shape(
                    (stripe_h_in, img_w, img_ch),
                    &mut stripe_buf[..img_count],
                ) {
                    Ok(v) => v,
                    Err(e) => {
                        log::error!("Failed to recreate stripe image for CPU retry: {e}");
                        return OdResult::ErrorRenderFailed;
                    }
                };

                let retry_depth = if has_depth {
                    let d_offset = buf_y_in * dep_w;
                    let d_count = stripe_h_in * dep_w;
                    let d_slice = unsafe { std::slice::from_raw_parts(depth_data.add(d_offset), d_count) };
                    Array2::from_shape_vec((stripe_h_in, dep_w), d_slice.to_vec()).ok()
                } else {
                    None
                };

                opendefocus::abort::set_aborted(false);

                match inst.runtime.block_on(inst.renderer.as_mut().unwrap().render_stripe(
                    stripe_specs,
                    inst.settings.clone(),
                    retry_image,
                    retry_depth,
                    filter_template.clone(),
                )) {
                    Ok(()) => {
                        log::info!("CPU fallback stripe render succeeded");
                        copy_render_region(&stripe_buf);
                    }
                    Err(e) => {
                        log::error!("CPU fallback stripe render also failed: {e}");
                        return OdResult::ErrorRenderFailed;
                    }
                }
            } else {
                // GPU success — copy render_region to output
                copy_render_region(&stripe_buf);
            }
        } else {
            // Subsequent stripes or already on CPU — no catch_unwind needed
            match inst.runtime.block_on(inst.renderer.as_mut().unwrap().render_stripe(
                stripe_specs,
                inst.settings.clone(),
                stripe_image,
                stripe_depth,
                filter_template.clone(),
            )) {
                Ok(()) => {
                    copy_render_region(&stripe_buf);
                }
                Err(e) => {
                    log::error!("Stripe render failed (y={}): {e}", y_out);
                    return OdResult::ErrorRenderFailed;
                }
            }
        }

        y_out = y_out_end;
    }

    // Clear global abort flag on render completion so it doesn't leak
    // into subsequent renders of other instances.
    opendefocus::abort::set_aborted(false);

    OdResult::Ok
}

/// Determine stripe height based on quality and GPU mode.
/// Mirrors NDK `stripe_height()` (opendefocus-nuke/src/lib.rs:458-473).
fn get_stripe_height(settings: &datamodel::Settings, is_gpu: bool, image_height: u32) -> u32 {
    // Filter preview renders bokeh shape directly into the buffer via
    // bokeh_creator::Renderer::render_to_array().  This is NOT a convolution,
    // so stripe splitting would produce visible seams at stripe boundaries.
    // Preview buffers are small (max 1024×1024), so no memory concern.
    if settings.render.filter.preview {
        return image_height;
    }
    if settings.render.result_mode() == datamodel::render::ResultMode::FocalPlaneSetup {
        return 32;
    }
    if !is_gpu {
        return 256; // CPU has no buffer limit; fewer stripes = less copy overhead
    }
    match settings.render.get_quality() {
        datamodel::render::Quality::Low => 256,
        datamodel::render::Quality::Medium => 256,  // 128 → 256
        _ => 128, // High, Custom: 64 → 128 (safe within wgpu 128MB limit)
    }
}
