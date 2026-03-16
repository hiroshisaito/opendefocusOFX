# Development History (HISTORY_DEV.md)

## Project Overview

A project to port OpenDefocus (a Rust-based convolution library) from a Nuke NDK plugin to an OpenFX plugin.

- Project root: `/Volumes/RAID/develop/ofx/oepndefocus_ofx`
- Platform: Rocky Linux 8.10 (x86_64)

## Version Management Rules

The original OpenDefocus version and the ported version are kept in sync.
The ported version receives a `-OFX-v<revision>` suffix.

- Format: `v<original version>-OFX-v<revision>`
- Example: First ported release for OpenDefocus v0.1.10 → `v0.1.10-OFX-v1`
- When the original version is upgraded, the ported version follows and the OFX revision resets to v1
  - Example: Original upgraded to v0.1.11 → `v0.1.11-OFX-v1`
- When only the OFX side is modified for the same original version, the revision is incremented
  - Example: OFX-side bug fix → `v0.1.10-OFX-v2`

Current target: **OpenDefocus v0.1.10** → Ported version **v0.1.10-OFX-v1** (in development)

## Directory Structure

```
oepndefocus_ofx/
├── upstream/
│   ├── opendefocus/    # OpenDefocus original (v0.1.10, Rust/wgpu)
│   └── openfx/         # OFX SDK (include/, Support/)
├── plugin/
│   └── OpenDefocusOFX/ # OFX port target (C++ implementation)
│       ├── src/
│       ├── include/
│       └── cmake/
├── build/               # Build output
├── bundle/
│   └── OpenDefocus.ofx.bundle/
│       └── Contents/
│           └── Linux-x86-64/   # OFX bundle destination
└── references/          # Design documents & background materials
```

## Timeline

### 2026-02-23: Initial Project Setup

#### Background Documentation

The following PDF documents were created and placed in `references/`:

1. **Rocky Linux 8.10 OFX Plugin Development Guide.pdf** — Development environment setup
2. **OpenDefocus OFX Porting Guide.pdf** — Overall porting strategy
3. **NDK to OFX Porting and OpenDefocus.pdf** — Technical background for NDK→OFX port
4. **OFX Plugin Porting Setup.pdf** — Working environment setup

#### Operational Documentation

- **OpenDefocus_OFX_移植セットアップ_2章分離.md** — Primary procedure document with clear separation between Chapter 1 (Nuke original) and Chapter 2 (OFX implementation) to prevent procedure mixing
- **references/README.md** — References usage guide (noting that the Chapter 2 separation version takes priority as the primary procedure)

#### Project Tree Creation

The following directory structure was built:

- `upstream/opendefocus` — OpenDefocus original placed here
- `upstream/openfx` — OFX SDK placed here
- `plugin/OpenDefocusOFX/{src,include,cmake}` — Empty templates for OFX port
- `build/` — Build output (empty)
- `bundle/OpenDefocus.ofx.bundle/Contents/Linux-x86-64/` — OFX bundle structure pre-created

### 2026-02-23: Phase 1 — OFX Skeleton Implementation

- Created OFX plugin skeleton using OFX C++ Support Library
- `OpenDefocusOFX.cpp`: ImageEffect / PluginFactory implementation (passthrough behavior)
- `CMakeLists.txt`: OFX SDK references, automated bundle generation
- Clip definitions: Source (required, RGBA), Depth (optional, RGBA/Alpha), Output (RGBA)
- Parameters: Size (defocus radius), Focus Plane
- Contexts: General (multi-input), Filter (single-input)
- Build succeeded with GCC Toolset 13, confirmed export of OFX entry points (`OfxGetNumberOfPlugins`, `OfxGetPlugin`)

### 2026-02-23: Phase 2 — Rust FFI Bridge Implementation

Connected the OpenDefocus Rust core library to the C++ OFX plugin via extern "C" FFI.

#### New Rust FFI Crate (`rust/opendefocus-ofx-bridge/`)

- `crate-type = ["staticlib"]` to generate `.a` file
- cbindgen for automatic C header generation (`include/opendefocus_ofx_bridge.h`)
- CPU only (`default-features = false, features = ["std", "protobuf-vendored"]`)
- tokio runtime `block_on()` for async → sync bridge

**extern "C" API (9 functions):**

| Function | Role |
|----------|------|
| `od_create` / `od_destroy` | Instance lifecycle management |
| `od_set_size` | Set defocus size |
| `od_set_focus_plane` | Set focal plane |
| `od_set_defocus_mode` | Switch between 2D / Depth mode |
| `od_set_quality` | Rendering quality preset |
| `od_set_resolution` | Set resolution |
| `od_set_aborted` | Rendering abort signal |
| `od_render` | Main rendering (in-place buffer update) |

#### C++ OFX Plugin Updates

- Added `OdHandle rustHandle_` member (opaque pointer / `Box<OdInstance>` ↔ `*mut c_void`)
- Constructor calls `od_create()`, destructor calls `od_destroy()`
- `render()`: OFX image → contiguous f32 buffer → `od_render()` → copy to OFX output
- Depth clip: Supports both RGBA/Alpha, single-channel extraction
- Fallback: passthrough when size ≤ 0 or invalid handle

#### CMake Updates

- `add_custom_command` to execute `cargo build`
- Links Rust staticlib (`libopendefocus_ofx_bridge.a`)
- System libraries: `pthread`, `dl`, `m`

#### Build Results

- `OpenDefocus.ofx`: 15MB (includes Rust core)
- All OFX entry points + 9 FFI functions exported
- Dynamic dependencies: libpthread, libdl, libstdc++, libm, libc (all standard)

#### Issues Resolved During Build

- **protoc not installed**: Resolved using `protobuf-vendored` feature
- **Rust type mismatches (4 cases)**: Fixed naming convention differences in prost-generated code
  - `Option<bool>` → `bool`, `TwoD` → `Twod`, `Quality` enum type, `Option<UVector2>` → `UVector2`

### 2026-02-25: UAT Conducted — NUKE 16.0 / Flame 2026

Tester: Hiroshi. See `UAT_checklist.md` for details.

#### Pre-UAT Fixes

- Changed plugin name from `OpenDefocus` → `OpenDefocusOFX` (to avoid name collision with NUKE NDK version)
- Changed bundle name to `OpenDefocusOFX.ofx.bundle` / `OpenDefocusOFX.ofx`
- Plugin ID (`com.opendefocus.ofx`) unchanged

#### UAT Result Summary (Final)

| Category | Result |
|----------|--------|
| Plugin Loading (4 items) | 3 PASS / 1 N/A |
| Clip Connection (4 items) | 4 PASS |
| Parameter Behavior (6 items) | 6 PASS |
| 2D Mode (3 items) | 2 PASS / 1 DEFERRED |
| Depth Mode (5 items) | 3 PASS / 1 DEFERRED / 1 N/A |
| Rendering Quality (5 items) | 4 PASS / 1 DEFERRED |
| NUKE Version Comparison (4 items) | 1 PASS / 3 DEFERRED |
| Stability (4 items) | 4 PASS |

All FAIL items resolved (fix PASS, N/A, or DEFERRED due to upstream dependency).

#### Issues Detected and Final Resolution

**1. Depth Alpha Input (5.5) — N/A**

- Fix: Changed `depthClip_->getPixelComponents()` → `depth->getPixelComponents()`
- Conclusion: Both Flame/NUKE input Depth as RGBA, so Alpha-only input does not occur in practice. Classified as N/A

**2. Flame Node Name Display (1.1, 1.2) — Fix Complete PASS**

- History: `"Filter"` → `"Filter/OpenDefocusOFX"` → finally changed to `"OpenDefocusOFX"`
- Result: Confirmed correct node name display in both Flame and NUKE

**3. Pixel Drift with NUKE NDK Version (4.3, 5.4, 6.1, 7.1-7.3) — DEFERRED**

- Symptom: ~1px offset difference between NUKE NDK and OFX versions
- Analysis: OFX version operates correctly in OFX standard coordinate system. NDK version may be affected by NUKE's pixel center 0.5 offset
- Barely noticeable at 2K+. Not a release blocker for OFX version
- To be re-verified after upstream coordinate system investigation

**4. Plugin Description (1.4) — N/A**

- `setPluginDescription()` display is host-dependent. OFX-side implementation is correct. No action needed

## Directory Structure (Updated)

```
oepndefocus_ofx/
├── upstream/
│   ├── opendefocus/    # OpenDefocus original (v0.1.10, Rust/wgpu)
│   └── openfx/         # OFX SDK (include/, Support/)
├── rust/
│   └── opendefocus-ofx-bridge/  # Rust FFI crate (staticlib)
│       ├── src/lib.rs           # extern "C" functions
│       ├── build.rs             # cbindgen header generation
│       ├── include/             # Auto-generated C header
│       ├── Cargo.toml
│       └── cbindgen.toml
├── plugin/
│   └── OpenDefocusOFX/
│       ├── src/OpenDefocusOFX.cpp  # OFX plugin main
│       └── CMakeLists.txt
├── build/
├── bundle/
│   └── OpenDefocusOFX.ofx.bundle/
│       └── Contents/Linux-x86-64/OpenDefocusOFX.ofx  # 15MB
├── UAT_checklist.md             # UAT checklist
└── references/
```

### 2026-02-25: Phase 3 — Quality + Bokeh Parameters Added

Extended from Phase 2's Size / FocusPlane minimal configuration to add Quality and Bokeh shape parameters (14 total).

#### Rust FFI Additions (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- Added `OdFilterType` enum (`Simple=0, Disc=1, Blade=2`)
- 12 new FFI functions added (total 21 FFI functions):

| Function | Role |
|----------|------|
| `od_set_samples` | Rendering sample count (when Quality=Custom) |
| `od_set_filter_type` | Filter type (Simple/Disc/Blade) — internally sets FilterMode + bokeh FilterType together |
| `od_set_filter_preview` | Filter preview mode |
| `od_set_filter_resolution` | Bokeh filter resolution |
| `od_set_ring_color` | Bokeh ring color |
| `od_set_inner_color` | Bokeh inner color |
| `od_set_ring_size` | Bokeh ring size |
| `od_set_outer_blur` | Bokeh outer blur |
| `od_set_inner_blur` | Bokeh inner blur |
| `od_set_aspect_ratio` | Bokeh aspect ratio |
| `od_set_blades` | Aperture blade count |
| `od_set_angle` | Aperture blade angle |
| `od_set_curvature` | Aperture blade curvature |

**Filter Type Internal Mapping** (same pattern as Nuke NDK version):
- Simple → `render.filter.mode = Simple`
- Disc → `render.filter.mode = BokehCreator` + `bokeh.filter_type = Disc`
- Blade → `render.filter.mode = BokehCreator` + `bokeh.filter_type = Blade`

#### C++ OFX Plugin Updates (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Controls Page (added to existing page):**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Quality | Choice (Low/Medium/High/Custom) | 0 (Low) | — |
| Samples | Int | 20 | 1–256 |

**Bokeh Page (new):**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Filter Type | Choice (Simple/Disc/Blade) | 0 (Simple) | — |
| Filter Preview | Boolean | false | — |
| Filter Resolution | Int | 256 | 32–1024 |
| Ring Color | Double | 1.0 | 0–1 |
| Inner Color | Double | 0.4 | 0.001–1 |
| Ring Size | Double | 0.1 | 0–1 |
| Outer Blur | Double | 0.1 | 0–1 |
| Inner Blur | Double | 0.05 | 0–1 |
| Aspect Ratio | Double | 1.0 | 0–2 |
| Blades | Int | 5 | 3–16 |
| Angle | Double | 0.0 | -180–180 |
| Curvature | Double | 0.0 | -1–1 |

**Conditional Enable/Disable (`changedParam` + `updateParamVisibility`):**
- Samples: Only enabled when Quality=Custom
- Bokeh parameters: Always enabled (NUKE NDK compliant)

#### Issues Resolved During Build

- **`blades` field type mismatch**: protobuf-generated code uses `u32` but plan assumed `i32`. Fixed FFI function argument to `u32`

#### Build Results

- Rust crate: Build succeeded, C header (`opendefocus_ofx_bridge.h`) correctly generated `OdFilterType` + 12 functions
- C++ OFX: Build succeeded
- All 21 FFI functions + 2 OFX entry points exported

### 2026-02-25: Phase 3 UAT Conducted

Tester: Hiroshi. See `UAT_checklist.md` sections 9–12 for details.

#### UAT Result Summary

| Category | Result |
|----------|--------|
| Quality Parameters (4 items) | 4 PASS |
| Filter Type (6 items) | 4 PASS / 2 FAIL |
| Bokeh Shape (7 items) | 7 PASS |
| Blade-Specific (4 items) | 3 PASS / 1 N/A |

#### Issues Detected and Resolution

**1. Filter Preview Overflow (10.5) — Fix PASS**

- Symptom: With Filter Preview enabled, Bokeh shape fills the entire screen
- Cause: Rust core's `render_preview_bokeh` draws at full buffer size. OFX version was passing full output resolution (2K/4K) buffer
- Fix: Renders Bokeh in a `filter_resolution`-sized buffer (default 256px) and copies to center of output image. Preview size adjustable via Filter Resolution parameter

**2. Grayout Cannot Be Restored (10.6) — Fix PASS**

- Symptom: Switching to Simple with Filter Preview enabled (Disc/Blade) causes Filter Preview to become grayed out and unrestorable
- Cause: `updateParamVisibility()` was graying out Bokeh parameter group when Filter Type=Simple
- Fix: Removed Bokeh parameter grayout to match NUKE NDK original version. Only Quality=Custom Samples retains grayout control

**3. Blade Parameters Grayout When Disc (12.4) — N/A**

- Grayout removed per 10.6 fix. Parameters always enabled (NUKE NDK compliant)

#### Pixel Drift Additional Finding

The original NUKE NDK version has GPU rendering enabled by default, and pixel drift was found to occur when GPU is enabled. The OFX version (CPU only) operates at correct coordinates, so the drift likely originates from NDK version's GPU rendering.

### 2026-02-25: Phase 4 — Defocus General + Advanced Parameters Added

Following Phase 3's Quality + Bokeh, added Defocus general parameters (8) and Advanced parameters (2), totaling 10.

#### Rust FFI Additions (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- Added `OdMath` enum (`Direct=0, OneDividedByZ=1, Real=2`)
- Added `OdResultMode` enum (`Result=0, FocalPlaneSetup=1`)
- 9 new FFI functions added (total 30 FFI functions):

| Function | Role |
|----------|------|
| `od_set_math` | Depth calculation mode — internally sets `use_direct_math` + `circle_of_confusion.math` together |
| `od_set_result_mode` | Rendering result mode (Result/FocalPlaneSetup) |
| `od_set_show_image` | Source image overlay display |
| `od_set_protect` | Focal plane protection range |
| `od_set_max_size` | Maximum defocus radius |
| `od_set_gamma_correction` | Bokeh intensity gamma correction |
| `od_set_farm_quality` | Farm/batch rendering quality preset |
| `od_set_size_multiplier` | Defocus radius multiplier |
| `od_set_focal_plane_offset` | Focal plane offset |

**Math Internal Mapping** (same compound setter pattern as `od_set_filter_type`):
- Direct → `defocus.use_direct_math = true`
- 1÷Z → `defocus.use_direct_math = false` + `circle_of_confusion.math = OneDividedByZ`
- Real → `defocus.use_direct_math = false` + `circle_of_confusion.math = Real`

#### C++ OFX Plugin Updates (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Controls Page (added to existing page):**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Mode | Choice (2D/Depth) | 0 (2D) | — |
| Math | Choice (Direct/1÷Z/Real) | 1 (1/Z) | — |
| Render Result | Choice (Result/Focal Plane Setup) | 0 (Result) | — |
| Show Image | Boolean | false | — |
| Protect | Double | 0.0 | 0–10000 |
| Maximum Size | Double | 10.0 | 0–500 |
| Gamma Correction | Double | 1.0 | 0.2–5.0 |
| Farm Quality | Choice (Low/Medium/High/Custom) | 2 (High) | — |

**Advanced Page (new):**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Size Multiplier | Double | 1.0 | 0–2 |
| Focal Plane Offset | Double | 0.0 | -5–5 |

**Mode Parameter Logic Change:**
- Previous: Automatically determined by Depth clip connection status
- Phase 4: Explicit selection via Mode parameter (2D/Depth)
  - Mode=2D: Always 2D (regardless of Depth connection)
  - Mode=Depth + Depth connected: DEPTH mode
  - Mode=Depth + Depth not connected: Falls back to 2D (no error)

**Conditional Enable/Disable (`changedParam` + `updateParamVisibility`):**
- Math, RenderResult, Protect, MaxSize, FocalPlaneOffset: Only enabled when Mode=Depth
- ShowImage: Only enabled when Mode=Depth AND RenderResult=FocalPlaneSetup
- GammaCorrection, FarmQuality, SizeMultiplier: Always enabled

#### Build Results

- Rust crate: Build succeeded, C header correctly generated `OdMath` + `OdResultMode` + 9 functions
- C++ OFX: Build succeeded
- All 30 FFI functions + 2 OFX entry points exported

### 2026-02-26: Phase 4 UAT Conducted

Tester: Hiroshi. See `UAT_checklist.md` sections 13–15 for details.

#### UAT Result Summary

| Category | Result |
|----------|--------|
| Defocus General Parameters (13 items) | 11 PASS / 1 DEFERRED (13.12) |
| Conditional Enable/Disable (5 items) | 5 PASS |
| Advanced Parameters (5 items) | 4 PASS / 1 DEFERRED (15.5) |

#### Issues Detected and Resolution

**1. Gamma Correction Has No Effect (13.12) — DEFERRED (Upstream Unimplemented)**

- Symptom: No visual change when adjusting value in both 2D/Depth modes
- Investigation: Upstream Rust core's `gamma_correction` field is a dead field in protobuf definition only
  - NDK version also does not call `create_knob_with_value()` — no UI knob created
  - Not included in `ConvolveSettings` structure — not passed to rendering pipeline at all
  - Note: `catseye.gamma` / `barndoors.gamma` / `astigmatism.gamma` are separate fields that function correctly (for non-uniform effects)
- Not an OFX issue. DEFERRED until upstream connects to pipeline

**2. Focal Plane Offset Has No Effect (15.5) — DEFERRED (Upstream Unimplemented)**

- Symptom: No visual change when adjusting value in Mode=Depth. NDK version has same symptom (confirmed by user report)
- Investigation: Upstream Rust core's `focal_plane_offset` field has protobuf definition and NDK knob created but not connected to `ConvolveSettings`
  - NDK version creates knob in Advanced tab (lib.rs line 621-625)
  - However, value is not passed to rendering pipeline — changing it has no effect
- Not an OFX issue. DEFERRED until upstream connects to pipeline

### 2026-02-26: Phase 5 — Bokeh Noise Parameters Added

Following Phase 4's Defocus General + Advanced, added Bokeh Noise parameters (3).

#### Rust FFI Additions (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- 3 new FFI functions added (total 34 FFI functions):

| Function | Role |
|----------|------|
| `od_set_noise_size` | Bokeh noise size — sets `inst.settings.bokeh.noise.size` |
| `od_set_noise_intensity` | Bokeh noise intensity — sets `inst.settings.bokeh.noise.intensity` |
| `od_set_noise_seed` | Bokeh noise seed — sets `inst.settings.bokeh.noise.seed` (u32) |

**Data Flow**: Bokeh Noise is stored in `Settings.bokeh.noise`, not `ConvolveSettings`. Used during filter generation via `bokeh_creator::Renderer::render_to_array(settings.bokeh, ...)` (same path as Bokeh shape parameters).

#### C++ OFX Plugin Updates (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Bokeh Page (added to existing page, after Curvature):**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Noise Size | Double | 0.1 | 0–1 |
| Noise Intensity | Double | 0.25 | 0–1 |
| Noise Seed | Int | 0 | 0–10000 |

- Conditional enable/disable: None (Bokeh parameters always enabled, NUKE NDK compliant)
- Total OFX parameters: 29

#### Build Results

- Rust crate: Build succeeded, 3 functions correctly generated in C header
- C++ OFX: Build succeeded
- All 34 FFI functions + 2 OFX entry points exported

### 2026-02-26: Phase 5 UAT Conducted

Tester: Hiroshi. See `UAT_checklist.md` section 16 for details.

#### UAT Result Summary

| Category | Result |
|----------|--------|
| Bokeh Noise Parameters (8 items) | 4 PASS / 4 DEFERRED |

#### Issues Detected and Resolution

**1. Noise Parameters Have No Effect (16.2/16.4/16.6/16.8) — DEFERRED (Upstream Feature Flag Disabled)**

- Symptom: No visual change when adjusting Noise Size/Intensity/Seed with Disc/Blade. Not reflected in Filter Preview either. NDK version has same symptom
- Investigation: bokeh_creator crate (v0.1.17) contains a complete Noise implementation
  - `Renderer::apply_noise()` generates and applies Fbm Simplex noise (`#[cfg(feature = "noise")]`)
  - `Renderer::render_pixel()` calls `apply_noise()` when noise.intensity > 0 and noise.size > 0
- Cause: Upstream `opendefocus` crate depends on `bokeh-creator = { default-features = false, features = ["image"] }`
  - `"noise"` feature not included, so `#[cfg(not(feature = "noise"))]` stub function is compiled
  - Stub is `fn apply_noise(&self, _, _, bokeh: f32) -> f32 { bokeh }` — returns value unchanged
- Not an OFX issue. Will work automatically once upstream enables the `noise` feature

### 2026-02-26: Phase 6 — Non-Uniform Effects: Catseye + Barndoors

Following Phase 5's Bokeh Noise, added Catseye (7) and Barndoors (9) parameters from Non-uniform effects, totaling 16.

#### Rust FFI Additions (`rust/opendefocus-ofx-bridge/src/lib.rs`)

- 16 new FFI functions added (total 50 FFI functions):

**Catseye (7 functions):**

| Function | Role |
|----------|------|
| `od_set_catseye_enable` | Catseye enable/disable — `non_uniform.catseye.enable` |
| `od_set_catseye_amount` | Catseye intensity — `non_uniform.catseye.amount` |
| `od_set_catseye_inverse` | Catseye inversion — `non_uniform.catseye.inverse` |
| `od_set_catseye_inverse_foreground` | Catseye foreground inversion — `non_uniform.catseye.inverse_foreground` |
| `od_set_catseye_gamma` | Catseye gamma — `non_uniform.catseye.gamma` |
| `od_set_catseye_softness` | Catseye softness — `non_uniform.catseye.softness` |
| `od_set_catseye_dimension_based` | Catseye screen-size relative — `non_uniform.catseye.relative_to_screen` |

**Barndoors (9 functions):**

| Function | Role |
|----------|------|
| `od_set_barndoors_enable` | Barndoors enable/disable — `non_uniform.barndoors.enable` |
| `od_set_barndoors_amount` | Barndoors intensity — `non_uniform.barndoors.amount` |
| `od_set_barndoors_inverse` | Barndoors inversion — `non_uniform.barndoors.inverse` |
| `od_set_barndoors_inverse_foreground` | Barndoors foreground inversion — `non_uniform.barndoors.inverse_foreground` |
| `od_set_barndoors_gamma` | Barndoors gamma — `non_uniform.barndoors.gamma` |
| `od_set_barndoors_top` | Barndoors top position — `non_uniform.barndoors.top` |
| `od_set_barndoors_bottom` | Barndoors bottom position — `non_uniform.barndoors.bottom` |
| `od_set_barndoors_left` | Barndoors left position — `non_uniform.barndoors.left` |
| `od_set_barndoors_right` | Barndoors right position — `non_uniform.barndoors.right` |

**Data Flow**: Non-uniform settings are stored in `Settings.non_uniform`. `settings_to_convolve_settings()` automatically maps `NonUniformFlags` / `GlobalFlags` bitmasks and individual values to `ConvolveSettings`. FFI setters simply set values directly in `inst.settings.non_uniform.catseye.*` / `inst.settings.non_uniform.barndoors.*`, and the Rust side automatically computes flags.

#### C++ OFX Plugin Updates (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Non-Uniform Page (new) — Catseye Section:**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Catseye Enable | Boolean | false | — |
| Catseye Amount | Double | 0.5 | 0–2 |
| Catseye Inverse | Boolean | false | — |
| Catseye Inverse Foreground | Boolean | true | — |
| Catseye Gamma | Double | 1.0 | 0.2–4.0 |
| Catseye Softness | Double | 0.2 | 0.01–1.0 |
| Catseye Dimension Based | Boolean | false | — |

**Non-Uniform Page — Barndoors Section:**

| Parameter | OFX Type | Default | Range |
|-----------|----------|---------|-------|
| Barndoors Enable | Boolean | false | — |
| Barndoors Amount | Double | 0.5 | 0–2 |
| Barndoors Inverse | Boolean | false | — |
| Barndoors Inverse Foreground | Boolean | true | — |
| Barndoors Gamma | Double | 1.0 | 0.2–4.0 |
| Barndoors Top | Double | 100.0 | 0–100 |
| Barndoors Bottom | Double | 100.0 | 0–100 |
| Barndoors Left | Double | 100.0 | 0–100 |
| Barndoors Right | Double | 100.0 | 0–100 |

**Conditional Enable/Disable (`changedParam` + `updateParamVisibility`):**
- Catseye: 6 parameters other than Enable are only enabled when `CatseyeEnable = true`
- Barndoors: 8 parameters other than Enable are only enabled when `BarndoorsEnable = true`

- Total OFX parameters: 45 (29 + 16)

#### Build Results

- Rust crate: Build succeeded, 16 functions correctly generated in C header
- C++ OFX: Build succeeded
- All 50 FFI functions + 2 OFX entry points exported

### 2026-02-26: OFX Page/Tab Display Issue — Resolved

At the start of Phase 6 UAT, a problem was discovered where pages/tabs were not displayed correctly in host UIs. Multiple approaches were attempted, many of which failed.

#### Symptom

NDK version has 4 tabs: Controls / Bokeh / Non-Uniform / Advanced. In the OFX version, some pages were not displayed, or parameters appeared on wrong pages. **Even in NUKE, the Advanced page disappeared** — not a Flame-specific issue but an OFX-level problem.

#### Attempted Fixes and Results

| # | Approach | Flame Result | NUKE Result |
|---|----------|-------------|-------------|
| 1 | Consolidate page definitions at top (4 PageParam) | Only Controls and Bokeh shown | Advanced disappeared |
| 2 | Add `setPageParamOrder()` | Crash | — |
| 3 | Merge into 2 pages using GroupParam | Works but only 2 pages | — |
| 4 | Remove hyphens from page names | No change | — |
| 5 | PageParam + GroupParam mixed (groups at top, incorrect definition order) | 3 pages, content misaligned | — |
| 6 | PageParam only (all GroupParam removed) | 3 pages displayed correctly | Boolean parameters disappeared |
| 7 | PageParam + GroupParam mixed (inline definition) | Only Controls and Bokeh | — |
| 8 | PageParam + GroupParam mixed (groups at top, correct definition order) | Only Controls and Bokeh | — |
| 9 | GroupParam only (all PageParam removed) | Generic names Page1 / Page2 | — |
| 10 | 1 PageParam + 4 GroupParam (page→addChild for Groups to Page) | Controls / Page2 / Page3 | Expandable sections, not tabs |
| 11 | 4 PageParam only (no GroupParam, spec-compliant: no Group added to Page) | Failed | Failed |
| 12 | 4 PageParam only (no GroupParam, no setParent, plan approved) | Only Controls + Bokeh | No pages/tabs, all parameters in single panel |
| 13 | 4 PageParam + 4 GroupParam (Method A: dual registration via page→addChild + setParent, Group not addChild'd to Page) | Failed | Failed |
| 14 | Host branching (v3 report Method B): Flame=PageParam only, NUKE=GroupParam+kFnOfxParamPropGroupIsTab | Only Controls+Bokeh (2/4 pages) | **All 4 tabs displayed correctly** ✅ |
| 15 | GroupParam only (common across all hosts, no PageParam, no GroupIsTab) | Failed | — |
| 16 | #14 + `setPageParamOrder()` added (all 4 pages) | **Plugin load error** (crash) | — |
| 17 | #14 + Parameter definition order matches page definition order (Advanced moved to end) | Only Controls+Bokeh (no change) | — |
| 18 | Common GroupParam + Page defined before each section + addChild(*param) dual registration | 3 pages shown (Controls, Bokeh, NonUniform), parameters shifted by 1 page | NUKE OK |
| 19 | #18 with all addChild(*param) removed, setParent(*grp) only + page→addChild(*grp) | 3 pages, parameters shifted by 1 page (same as #18) | NUKE OK |
| 20 | Common GroupParam + sub-group split + Flame page names "Page N" + GroupIsTab removed | Page1/2/3 displayed ✅ | 6 tabs (GroupParam promoted to tabs) |
| 21 | Host Branching: GroupParam only for Flame, NUKE uses Page→addChild(*param) flat approach | — | 1 page (all parameters in single flat list) |
| 22 | **Topological Branching**: Common GroupParam + topology branching. NUKE: 4 non-nested groups → 4 tabs. Flame: flat sub-groups → column split. Sub-groups (BokehNoise/Catseye/Barndoors) are Flame-only; for NUKE, parameters flow directly into parent group. defineGroupParam definition order matches display order | **Page1/2/3/4 displayed correctly** ✅ | **Controls/Bokeh/Non-Uniform/Advanced 4 tabs displayed correctly** ✅ |

**Approach #14 Details:**
- Detects Flame/NUKE via `OFX::getImageEffectHostDescription()->hostName`
- NUKE: Sets `OfxParamPropGroupIsTab = 1` on GroupParam (NUKE proprietary extension property) → displays as Knob Tab
- Flame: PageParam only, no GroupParam/setParent
- **NUKE issue completely resolved**. Flame still shows only 2 pages

**Approach #14 Additional Diagnostic Tests:**

| Test | Result |
|------|--------|
| Host property query | `maxPages=0`, `pageRowCount=0`, `pageColumnCount=0`, `maxParameters=-1` |
| kOfxParamPropPageChild dump | Confirmed all 4 pages have correct children properties |
| Page definition order change (NonUniform→Advanced→Controls→Bokeh) | First 2 pages (NonUniform, Advanced) tabs shown, but contents are Controls+Bokeh parameters |

#### Established Facts

**NUKE:**
- NUKE is a hierarchical layout host. It completely ignores PageParam (`kOfxParamHostPropMaxPages = 0`)
- To generate Knob Tabs in NUKE, GroupParam + `OfxParamPropGroupIsTab = 1` is required (NUKE proprietary extension, not OFX standard)
- **Approach #14 achieved all 4 NUKE tabs** — `kFnOfxParamPropGroupIsTab` was the key

**Flame:**
- Flame reports `kOfxParamHostPropMaxPages = 0`, `pageRowCount = 0`, `pageColumnCount = 0` — does not set OFX standard page properties
- [Autodesk Community Forum](https://forums.autodesk.com/t5/flame-forum/openfx-plugin-development-resources/td-p/12268117) official information: **"Flame does not support the Pages and lists all visible Params one after the other in the tabs after the 'Plugin' tab. For Params that are inside a group, the group name is shown at the top of the column and empty space in the column is added at the bottom to not show params that are not part of the group in that column."**
- Flame officially does not support OFX PageParam. The 2 tabs shown are Flame's partial, non-standard behavior
- `kOfxParamPropPageChild` is correctly set for each page, but Flame does not process this correctly
- Page definition order change test confirmed: Flame always shows only the first 2 page tabs, and parameters are slot-assigned by definition order rather than `addChild`
- **Flame recognizes GroupParam** — group names are displayed at column tops per specification

**OFX Specification:**
- OFX spec (`ofxParam.h` L544-548): "Group parameters cannot be added to a page"
- Host layout methods are mutually exclusive: paged layout (PageParam) vs hierarchical layout (GroupParam)
- `kOfxParamPropPageChild` valid values: "the names of any parameter that is not a group or page" (`ofxParam.h` L565)
- `OfxParamPropGroupIsTab` does not exist in OFX SDK standard (NUKE proprietary extension)

#### Fixes Applied in Parallel (also reflected in rolled-back configurations)

| Fix | Details | Rationale |
|-----|---------|-----------|
| Depth clip context guard | Protected `fetchClip(kClipDepth)` with `getContext() == eContextGeneral` | Depth clip doesn't exist in Filter context, causing exception |
| updateParamVisibility complete rewrite | Faithfully ported original `consts.rs` `KnobChanged::new(enabled, visible)` pattern | Reproduced original's 2-axis control (enabled + visible) with OFX `setEnabled` + `setIsSecret` |
| changedParam trigger additions | Added FarmQuality, Math | Samples display toggle, Math-dependent parameter enabled update leaks |
| Samples setIsSecret | `setEnabled` → `setIsSecret(!samplesVisible)` | Original uses visible control (hide), not enabled control (grayout) |
| Filter context hide | Set `setIsSecret(true)` for 8 parameters: Mode, Math, RenderResult, etc. | These parameters are meaningless in Filter context without Depth clip |
| Added Camera to Mode | 2D / Depth / Camera choices | Per original. Camera maps to Depth on Rust side (protobuf has no Camera) |
| Added Image to FilterType | Simple / Disc / Blade / Image choices | Per original |

#### Binary Analysis Discovery

Analyzed binaries of OFX plugins that correctly display 4+ pages in Flame using the `strings` command:

**`out_of_focus.ofx` / `depth_of_field.ofx`** (correctly functioning plugins):
- `OfxParamTypePage` — Uses PageParam
- `OfxParamPropPageChild` — Uses addChild
- `OfxPluginPropParamPageOrder` — **Explicitly specifies page order**
- `OfxParamPropParent` — Uses setParent
- `N3OFX10GroupParamE` — Uses GroupParam
- `OfxParamPropGroupIsTab` — **Not used**

**`FlaresOFX.ofx`** (reference):
- GroupParam + setParent only (no PageParam)

**Important Discovery**: `kOfxPluginPropParamPageOrder` (`ofxParam.h:558`) is a standard OFX property. Correctly functioning Flame plugins use this to explicitly specify page order. Settable via `desc.setPageParamOrder(page)` in the Support Library.

**Approach #16 Crash Cause (unidentified)**: `setPageParamOrder` was added directly to the #14 host-branching configuration, but caused plugin load error in Flame. `out_of_focus.ofx` uses a common PageParam + GroupParam + setPageParamOrder configuration without `GroupIsTab`, so the combination of host branching + GroupIsTab may be the problem.

#### Flame Auto-Pagination Specification Revealed (#17-#19 Investigation Results)

Flame's OFX UI construction was found to operate with the following specification:

1. **Completely ignores PageParam** — Tab names and tab structure do not depend on OFX Page definitions
2. **Generates columns from GroupParam** — Each group is displayed as a new column
3. **Vertical parameter limit per column: ~12-14** — Overflow pushes to next column
4. **2-3 columns per tab** — Groups that don't fit are automatically pushed to the next tab
5. **Tab names auto-generated from group names** — First group's name becomes tab name

**Cause of Misalignment**: Bokeh (15 items) and NonUniform (16 items) exceeded the column limit, causing overflow parameters to cascade into the next tab

**Solution**: Split large groups into sub-groups of 10 or fewer
- Bokeh (15) → BokehShape (12) + BokehNoise (3)
- NonUniform (16) → Catseye (7) + Barndoors (9)

#### Final Configuration (Approach #22: Topological Branching)

**Design Pattern**: Groups are defined commonly across all hosts, with only parent-child relationships (topology) branched per host.

**Page Definitions (common across all hosts):**
- 4 PageParam: Controls / Bokeh / NonUniform / Advanced
- Flame: Labels set to "Page 1" through "Page 4"
- NUKE: Labels set to "Controls" / "Bokeh" / "Non-Uniform" / "Advanced"

**Group Definitions (defineGroupParam call order = UI display order):**
1. ControlsGroup — Common across all hosts
2. BokehGroup — Common across all hosts
3. BokehNoiseGroup — **Flame only** (`isFlame ? define... : nullptr`)
4. NonUniformGroup — **NUKE only** (`isFlame ? nullptr : define...`)
5. CatseyeGroup — **Flame only**
6. BarndoorsGroup — **Flame only**
7. AdvancedGroup — Common across all hosts (**must be defined last**)

**Topology (parent-child relationships) Branching:**
- **NUKE**: 4 main groups (Controls, Bokeh, NonUniform, Advanced) `addChild` to each page → 4 tabs. Sub-groups are not generated, so no nested menus. BokehNoise/Catseye/Barndoors parameters directly `setParent` to parent groups (bokehGrp/nonUniformGrp)
- **Flame**: All groups (including sub-groups) flatly `addChild` to each page → arranged as independent columns, distributed across Page 1/2/3/4 by Flame's auto-pagination

**Parameter Membership:**
- Controls (12) → `controlsGrp`
- Bokeh first half (12) → `bokehGrp`
- Bokeh Noise (3) → `bokehNoiseGrp` (Flame) / `bokehGrp` (NUKE) — fallback branching
- Catseye (7) → `catseyeGrp` (Flame) / `nonUniformGrp` (NUKE) — fallback branching
- Barndoors (9) → `barndoorsGrp` (Flame) / `nonUniformGrp` (NUKE) — fallback branching
- Advanced (2) → `advancedGrp`

**Key Insights:**
- `defineGroupParam` / `definePageParam` call order directly maps to UI display order
- `kFnOfxParamPropGroupIsTab` (NUKE proprietary extension) is NOT needed — NUKE naturally generates tabs from PageParam + GroupParam combination
- Flame sub-groups are used for column splitting, but in NUKE they become nested menus, so they are not generated for NUKE

#### Status: Both NUKE and Flame Resolved ✅

- **NUKE**: Controls / Bokeh / Non-Uniform / Advanced 4 tabs displayed correctly, no nested menus ✅
- **Flame**: Page 1 / Page 2 / Page 3 / Page 4 displayed correctly ✅
- 22 approaches attempted

### 2026-02-27: Phase 8 — GPU Rendering Support (wgpu)

Following Phase 7's Non-Uniform completion, enabled wgpu-based GPU rendering. Upstream's `WgpuRunner` creates its own Vulkan device (independent from OFX host's GPU context).

#### Rust FFI Changes (`rust/opendefocus-ofx-bridge/`)

**Cargo.toml:**
- Added `"wgpu"` to `opendefocus` features: `features = ["std", "protobuf-vendored", "wgpu"]`

**src/lib.rs:**
- Enabled GPU in `od_create()`:
  - `settings.render.use_gpu_if_available = true`
  - `OpenDefocusRenderer::new(true, &mut settings)` — prefer_gpu = true
- 1 new FFI function added (total 59 FFI functions):

| Function | Role |
|----------|------|
| `od_is_gpu_active` | Query GPU usage status — returns `inst.renderer.is_gpu()` |

**Operating Principle:**
- `SharedRunner::init(true)` attempts `WgpuRunner::new()`
- Success → GPU rendering (Vulkan backend)
- Failure → Automatically falls back to `CpuRunner` (no crash)
- `od_render()` call interface unchanged (passes CPU memory pointers, wgpu internally manages GPU upload/download)

#### C++ OFX Plugin Updates (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

- **Dev version display**: Added `kDevVersion` constant (`"v0.1.10-OFX-v1 (Phase 8: GPU)"`) and read-only String parameter `devVersion` at top of Controls page. `setEnabled(false)` for grayout (non-editable)
- **No changes to C++ plugin rendering flow** — GPU is fully managed on the Rust side, no C++ code changes needed

#### Build Results

- Rust crate: Build succeeded including wgpu dependency (+3-5min for first build)
- C++ OFX: Build succeeded, no linker errors
- Bundle size: ~35MB (includes wgpu/Vulkan dependencies)
- All FFI functions including `od_is_gpu_active` export confirmed

### 2026-02-27: Phase 8 UAT Conducted

Tester: Hiroshi. See `UAT_checklist.md` section 24 for details.

#### UAT Result Summary

| Category | Result |
|----------|--------|
| GPU Rendering (15 items) | 13 PASS / 2 N/A |

#### Issues Detected and Resolution

**1. Filter Preview Black Screen (24.9) — Fix PASS**

- Symptom: Black output when Filter Preview ON in Depth mode
- Cause: Filter Preview path passes `depth_data = nullptr` to `od_render`, but `od_set_defocus_mode` is called after the preview path (within the normal rendering path), so the previous rendering's defocus_mode remains. Executing preview in Depth mode state causes Rust-side `validate()` to return `DepthNotFound` error, failing the render
- Fix: Force `od_set_defocus_mode(rustHandle_, TWO_D)` immediately before Filter Preview path. Preview only draws bokeh shape and doesn't need depth, so TWO_D is correct
- Note: Latent bug regardless of GPU presence (undetected in Phase 3 UAT because 2D mode was tested)

**2. CPU Version Comparison (24.4/24.5) — N/A**

- No CPU/GPU toggle available for strict comparison. Visual comparison with previous version shows near-identical results

**3. CPU Fallback (24.15) — N/A**

- GPU environment only (Linux RTX A4000). No GPU-less environment available for testing

#### Performance

Performance significantly improved with GPU support. Quality High mode, which was previously difficult to test, now runs comfortably.

### 2026-02-28: Phase 9 — Filter Type: Image (Custom Bokeh Image Input)

Following Phase 8's GPU rendering, added Filter Type = Image support. Users can connect any Bokeh image to the Filter clip for use as a custom kernel.

#### Rust FFI Changes (`rust/opendefocus-ofx-bridge/src/lib.rs`)

**`od_render()` Signature Extension:**
- Added `filter_data: *const f32`, `filter_width: u32`, `filter_height: u32`, `filter_channels: u32` parameters
- When filter_data is non-NULL and width/height/channels > 0, constructs `Array3<f32>`
- Same pattern as NDK's `render.rs:62-73`: `Array3::from_shape_vec((height, width, channels), slice.to_vec())`
- Changed 5th argument of `render_stripe()` from `None` → `filter`

**`OdFilterType` enum Update:**
- `Image = 3` was already defined
- `od_set_filter_type()`'s `OdFilterType::Image` arm sets `FilterMode::Image`

**`ndarray` import Update:**
- Added `Array3`: `use ndarray::{Array2, Array3, ArrayViewMut3};`

#### C++ OFX Plugin Updates (`plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`)

**Filter Clip Added:**
- Added `kClipFilter = "Filter"` constant
- `describeInContext()`: Defined as optional RGBA clip (General context only)
- Constructor: `filterClip_ = fetchClip(kClipFilter)`
- Member variable: `OFX::Clip* filterClip_ = nullptr`

**"Image" Added to Filter Type Choice:**
- `param->appendOption("Image")` — index 3 = Image

**render() Changes:**
- When `filterType == 3` (Image) and Filter clip connected:
  - Fetch image from Filter clip (`filterClip_->fetchImage(args.time)`)
  - Copy to RGBA float buffer (same pattern as Depth)
  - Pass filter parameters to `od_render()`
- Otherwise: Pass `nullptr, 0, 0, 0` to `od_render()`

**Filter Preview Condition Change:**
- Before: `filterPreview && filterType >= 1`
- After: `filterPreview && filterType >= 1 && filterType <= 2`
- Reason: In Image mode, user-specified image is used, so bokeh_creator preview is meaningless

**Dev Version Update:**
- `kDevVersion = "v0.1.10-OFX-v1 (Phase 9: Filter Image)"`

#### Data Flow

```
OFX Filter Clip → fetchImage() → copy to float buffer
  ↓
od_render(..., filter_data, filter_w, filter_h, filter_ch, ...)
  ↓
Array3::from_shape_vec((height, width, channels), filter.to_vec())
  ↓
render_stripe(..., filter: Some(Array3<f32>))
  ↓
prepare_filter_image() → resize + mipmap
  ↓
render_convolve() uses as kernel
```

#### Phase 9 Initial UAT Results

| Category | Result |
|----------|--------|
| Filter Image (9 items) | 2 PASS / 7 FAIL |

**FAIL Cause**: "Image" option was not added to the Filter Type Choice parameter (`appendOption("Image")` omission). Retest planned after fix.

### 2026-02-28: GPU Stabilization — 4K Crash Fix

#### Problem

Rendering 4K UHD (3840x2160) footage crashes NUKE/Flame (abort).

#### Root Cause

wgpu validation error → panic → cannot unwind across `extern "C"` boundary → abort:

```
wgpu error: Validation Error
Caused by:
  In Device::create_bind_group, label = 'Convolve Bind Group'
    Buffer binding 3 range 165888000 exceeds `max_*_buffer_binding_size` limit 134217728
```

- 4K RGBA32F staging buffer size: 165,888,000 bytes (≈158MB)
- wgpu default `max_*_buffer_binding_size` limit: 134,217,728 bytes (128MB)
- wgpu handles validation errors with `panic!` (not error return)
- `od_render` is an `extern "C" fn` so panic cannot unwind → `panic_cannot_unwind` → **abort (entire host process crashes)**

#### Fix Details

**1. `std::panic::catch_unwind` for Panic Capture** (`lib.rs`):
- Wrapped `render_stripe()` call with `std::panic::catch_unwind(AssertUnwindSafe(...))`
- Catches panic → returns `OdResult::ErrorRenderFailed` (avoids abort)
- Sets `gpu_failed = true` → CPU fallback on next render

**2. `[profile.release] panic = "unwind"` Explicit** (`Cargo.toml`):
- Explicitly specified in release profile to ensure `catch_unwind` works

**3. Runtime CPU Fallback** (`lib.rs`):
- Added `gpu_failed: bool` flag to `OdInstance`
- Flag set on GPU render failure (error or panic)
- On next `od_render()` call, if `gpu_failed && renderer.is_gpu()`, recreates CPU renderer
- `od_is_gpu_active()` returns `false` after fallback

#### GPU Stabilization Initial UAT Results

| Category | Result |
|----------|--------|
| GPU Stabilization (9 items) | 2 PASS / 3 FAIL / 4 not yet |

**FAIL Cause**: Tested with build before `catch_unwind` implementation. 4K crashed without fallback triggering. Retest planned with `catch_unwind` build.

### 2026-02-28: GPU Stabilization — Immediate CPU Retry

#### Problem

`catch_unwind` panic capture succeeded, but output after GPU failure has no blur applied.

#### Cause

GPU failure → `ErrorRenderFailed` returned → C++ side copies source buffer directly to output → `gpu_failed = true` is set, but OFX host does not re-render the same frame, so CPU fallback is not triggered.

#### Fix Details

When GPU failure is detected within `od_render()`, immediately creates CPU renderer and retries within the same call:

1. `catch_unwind` detects GPU failure → `gpu_failed_now` flag
2. Immediately creates CPU renderer with `OpenDefocusRenderer::new(false, ...)`
3. Reconstructs `ArrayViewMut3` / `Array2` / `Array3` from raw pointers (consumed by panic)
4. CPU `render_stripe()` retry
5. Success → returns `OdResult::Ok` (appears as success to host)

**Key Point**: `render_specs.clone()` passes to initial GPU attempt, preserving `render_specs` for retry. Confirmed that `RenderSpecs` implements `Clone`.

### 2026-02-28: Phase 10 — Render Scale Correction + RoI Extension + Use GPU Parameter

Following Phase 9 + GPU stabilization, implemented 3 features needed for a commercial OFX plugin.

#### 1. Render Scale Correction

Fixed over-application of pixel-space parameters in proxy mode (1/2, 1/4 resolution).

**Multiply spatial parameters by `args.renderScale.x` in render():**
- `size *= renderScale` — Blur radius
- `maxSize *= renderScale` — Maximum blur radius
- `protect *= renderScale` — Focal plane protection range

**Parameters not requiring scaling**: `focusPlane`, `sizeMultiplier`, `ringColor`, `quality`, etc. (normalized values/ratios/non-spatial values)

#### 2. getRegionsOfInterest (RoI Extension)

Requests wider input image by blur radius to prevent edge clipping.

**Added `getRegionsOfInterest()` override:**
- Effective blur radius = `max(size, maxSize) * sizeMultiplier` (Depth mode) or `size * sizeMultiplier` (2D mode)
- Margin = `ceil(effectiveRadius) + 1.0` (canonical coordinates)
- Extends RoI for Source / Depth clips. Filter clip does not need extension

**Changed render() buffer processing to srcBounds-based:**
- Allocate `imageBuffer` at `src->getBounds()` size (may be larger than renderWindow due to RoI extension)
- `fullRegion` = srcBounds size, `renderRegion` = renderWindow relative to srcBounds
- Result copy also maps with offset consideration
- Depth buffer also changed to `depth->getBounds()` based

#### 3. Use GPU Parameter (CPU/GPU Toggle)

Added manual toggle corresponding to NDK version's `use_gpu_if_available`.

**Rust FFI Addition (`od_set_use_gpu`):**
- `use_gpu` parameter specifies GPU/CPU
- Recreates renderer on mode change (`OpenDefocusRenderer::new(use_gpu, ...)`)
- Resets `gpu_failed` flag when re-enabling GPU
- Early return when no change (avoids per-frame call cost)

**C++ OFX Plugin:**
- Added "Use GPU" Boolean parameter to Controls tab (default: true)
- Calls `od_set_use_gpu()` at start of render()

**Coexistence with GPU Auto-Fallback:**
- `Use GPU = false`: Manual CPU mode. `od_set_use_gpu(false)` switches to CPU renderer
- `Use GPU = true`: Attempts GPU. Auto CPU fallback at 4K → re-checking Use GPU to true restores GPU (`gpu_failed` reset)

#### Changed Files

| File | Changes |
|------|---------|
| `plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp` | Added `getRegionsOfInterest()`, renderScale correction, srcBounds-based buffer processing, Use GPU parameter |
| `rust/opendefocus-ofx-bridge/src/lib.rs` | Added `od_set_use_gpu()` FFI function |

#### Dev Version Update
- `kDevVersion = "v0.1.10-OFX-v1 (Phase 10: RenderScale + RoI + UseGPU)"`

### 2026-02-28: Code Review — Thread Safety Fix

Code review conducted by debug team. Evaluated for safety and correctness against OFX specification and coding standards.

#### Critical Fix: Thread Safety Declaration

**Problem**: Declared `setRenderThreadSafety(eRenderInstanceSafe)`, but `render()` performs parameter setting → rendering on `rustHandle_` (Rust-side internal state), creating race condition risk for parallel `render()` calls on the same instance. Particularly, concurrent `od_set_use_gpu()` renderer recreation could cause fatal wgpu-level crashes.

**Fix**: Changed `eRenderInstanceSafe` → `eRenderUnsafe`. Host (NUKE/Flame) will either serialize render() calls or create separate instances per thread.

#### Review Evaluation Points (Confirmed as Correct Implementation)

- **Row-padding aware buffer copy**: `getPixelAddress()` with per-row `memcpy` — accurate implementation handling OFX host row-end padding
- **Coordinate system mapping**: `rw.x1 - srcBounds.x1` for OFX world coordinates to 0-based buffer-local coordinate conversion
- **RoI extension math**: `max(size, maxSize) * sizeMultiplier` + `renderScale` correction — mathematically correct

#### Minor Improvement Notes (Future Work)

- Handling cases where Depth/Filter clips have different resolutions (scales) from Source (e.g., Depth at full resolution in NUKE while Source is proxied). Currently using common `args.renderScale.x` which works for 99% of cases. Will consider per-clip scale ratio check if misalignment is reported

### 2026-02-28: Phase 10 UAT — Proxy Mode Crash Fix

#### Crash Symptom

Crash (abort) in NUKE proxy mode (Render Scale correction test). Stack trace: `panic_cannot_unwind` → `od_render` → abort.

#### Root Cause

In proxy mode or viewer pan, NUKE's `src->getBounds()` can be smaller than / offset from `renderWindow`. `rw.x1 - srcBounds.x1` becomes negative, causing overflow when converting to `usize` on Rust side → panic. This panic occurs at slice construction stage (outside `catch_unwind`) so it cannot be caught → abort.

#### Fix Details

**1. `getRegionsOfInterest` — Missing renderScale Application Fix:**
- Multiply `size` / `maxSize` by `args.renderScale.x` before margin calculation
- RoI extension width now correctly scales in proxy mode

**2. `render()` — Complete Rewrite to fetchWindow + intersection Method:**

Old method (srcBounds-based):
- Allocate buffer at `srcBounds` size → `renderRegion = rw - srcBounds` → crash with negative values

New method (fetchWindow + intersection):
- **fetchWindow** = `renderWindow` + blur margin to calculate required area
- **Zero-initialized buffer** allocated at fetchWindow size (out-of-bounds is black/transparent)
- **intersection** copies only the overlap between `srcBounds` and `fetchWindow`
- `renderRegion = rw - fetchWindow` → **mathematically guaranteed non-negative** (fetchWindow ⊇ rw)
- Depth buffer also uses fetchWindow + intersection method
- Optical center computed from `RegionOfDefinition` and mapped to fetchWindow local coordinates

### 2026-03-01: Edge Black Border Fix — Clamp to Edge Padding

#### Symptom

Black area proportional to blur size appears at left and bottom edges of image. Reproduces in both 2D/Depth modes with Filter Disc/Image. Does not occur in NDK version.

#### Root Cause

Previous proxy mode crash fix used `0.0f` (black/transparent) Zero-Padding for areas outside `srcBounds` within fetchWindow. Convolution processing picks up these black pixels, causing edges to darken.

NDK version does not have this problem because the NUKE engine automatically performs Clamp to Edge (edge pixel repeat). OFX hosts do not perform this processing, so the plugin must implement it.

Left/bottom edges are most visible because NUKE's coordinate origin is bottom-left (0,0), so fetchWindow extends into negative coordinates on the left/bottom where Zero-Padding directly affects.

#### Fix Details

**Changed Zero-Padding → Clamp to Edge:**

Source Buffer (RGBA):
- Y direction: `clampY = clamp(y, srcBounds.y1, srcBounds.y2 - 1)` repeats edge rows
- X direction: Split into 3 regions
  - Left margin: Repeat leftmost pixel
  - Center: `memcpy` for fast copy
  - Right margin: Repeat rightmost pixel

Depth Buffer (single channel):
- Same Clamp to Edge logic applied
- Also resolves "unnatural focus changes" caused by Depth suddenly becoming 0 at edges

**validX Clamp Safety Hardening (debug team review feedback):**
- In extreme pan-out when `srcBounds` does not intersect with `fetchWindow` at all, `validX2 < fetchWindow.x1` causing right margin `dstX` to go negative → buffer underrun
- Fix: Clamp `validX1` / `validX2` within fetchWindow range
  - `validX1 = min(max(fetchWindow.x1, srcBounds.x1), fetchWindow.x2)`
  - `validX2 = max(min(fetchWindow.x2, srcBounds.x2), fetchWindow.x1)`
- Applied to both Source and Depth

### 2026-03-01: Phase 9 + Phase 10 Final UAT Complete

Tester: Hiroshi. See `UAT_checklist.md` sections 25–29 for details.

#### UAT Result Summary

| Category | Result |
|----------|--------|
| Filter Type: Image (9 items, Section 25) | 9 PASS |
| GPU Stabilization (9 items, Section 26) | 8 PASS / 1 unknown (26.8) |
| Render Scale + RoI (18 items, Section 27) | 15 PASS / 2 FAIL / 1 untested (27.9 Flame) |
| Use GPU Parameter (10 items, Section 28) | 7 PASS / 3 FAIL |
| Thread Safety (5 items, Section 29) | 5 PASS |

#### FAIL Items and Classification

**27.6 — Size Multiplier Bokeh Breakdown:**
- Symptom: Increasing Size Multiplier causes bokeh breakdown and gray areas
- NDK version has similar symptoms. No issue when doubling bokeh via Size/MaxSize
- Classification: Upstream sizeMultiplier processing issue. DEFERRED

**27.8 — Filter Preview Overflow:**
- Symptom: Filter Preview overflows to fill entire screen regardless of proxy mode
- Not a Render Scale correction test but a Filter Preview issue itself
- Classification: Filter Preview size control issue. Needs investigation

**28.4 / 28.5 — Use GPU Toggle Log Not Output:**
- Symptom: CPU/GPU toggle works correctly but no log output in NUKE/Flame console
- Classification: `log::info!` output destination may not be connected to host console. No functional impact

**28.6 — CPU/GPU Pixel Drift:**
- Symptom: ~1px offset between CPU and GPU. Similar behavior to NDK version
- Classification: Caused by upstream CPU/wgpu implementation difference. DEFERRED

#### Key Insights

- **Pixel drift root cause confirmed**: ~1px offset between CPU and GPU occurs same as NDK version. Not an OFX wrapper issue but inherent in upstream Rust core (CPU vs wgpu implementation difference or coordinate system interpretation)
- **Clamp to Edge fully operational**: Edge black border issue completely resolved. Stable operation across all combinations of Crop/AdjBBox intentional BBox reduction, extreme panning, and proxy mode
- **Thread safety**: Flipbook, Write node batch, Flame batch all PASS. `eRenderUnsafe` declaration functions correctly

### Current Status — v0.1.10-OFX-v1 Port Complete

- **Phase 1 (OFX Skeleton)**: Complete, UAT complete
- **Phase 2 (FFI Bridge)**: Complete, UAT complete
- **Phase 3 (Quality + Bokeh Parameters)**: Complete, UAT complete
- **Phase 4 (Defocus General + Advanced)**: Complete, UAT complete
- **Phase 5 (Bokeh Noise)**: Complete, UAT complete
- **Phase 6 (Non-Uniform: Catseye + Barndoors)**: Complete, UAT complete
- **Phase 7 (Non-Uniform: Astigmatism + Axial Aberration + InverseForeground)**: Complete, UAT complete
- **Phase 8 (GPU Rendering)**: Complete, UAT complete
- **Phase 9 (Filter Type: Image)**: Complete, UAT complete
- **GPU Stabilization (4K Crash Fix + Immediate CPU Retry)**: Complete, UAT complete
- **Phase 10 (RenderScale + RoI + UseGPU)**: Complete, UAT complete
- **Code Review (Thread Safety + Edge Processing)**: Complete, UAT complete

**OFX porting mission complete.** All DEFERRED items are upstream-caused. Zero OFX-side regressions.

### Known Constraints (All Upstream-Caused)

| Constraint | Reason | Future Action |
|------------|--------|---------------|
| Tiling disabled | `setSupportsTiles(false)` | Tile splitting needed for ultra-high resolution (8K+) |
| 4K+ GPU rendering | wgpu `max_buffer_binding_size` (128MB) exceeded → immediate CPU fallback | Upstream device limits extension or buffer splitting |
| CPU/GPU pixel drift | ~1px difference. Caused by upstream CPU/wgpu implementation difference | Re-verify after upstream coordinate system unification |
| Size Multiplier bokeh breakdown | Upstream sizeMultiplier processing issue. NDK version has similar symptoms | Upstream investigation |
| Gamma Correction has no effect | Upstream dead field | Re-verify after upstream pipeline connection |
| Focal Plane Offset has no effect | Upstream unimplemented (ConvolveSettings not connected) | Re-verify after upstream pipeline connection |
| Bokeh Noise has no effect | Upstream bokeh_creator `noise` feature disabled | Re-verify after upstream feature enablement |
| Camera mode | protobuf has no Camera | Re-verify after upstream Camera addition |
| Axial Aberration Type switch has no color change | Upstream enum off-by-one bug | Re-verify after upstream fix |

### OFX-Side Unresolved (Requires Fix)

| Item | Details | Priority |
|------|---------|----------|
| Filter Preview overflow (27.8) | Preview ignores filter size and fills entire screen | Medium |
| Use GPU log not output (28.4/28.5) | `log::info!` does not reach host console | Low (no functional impact) |

### 2026-03-01: Upstream Investigation — Porting Exclusion Decisions

Investigated feature differences with upstream NDK version and decided the following as intentional omissions (not to be ported).

#### Parameters Excluded from Porting

| Parameter | Reason |
|-----------|--------|
| **CameraMaxSize / UseCameraFocal / WorldUnit** | Camera Mode requires different camera data acquisition methods per host application, making full support impossible. Omitted as NUKE-dependent feature. Alternatives like metadata reading exceed the scope of original NDK version porting |
| **DeviceName** | Read-only GPU device name display. Low priority |
| **UseCustomStripeHeight / CustomStripeHeight** | Performance tuning. Low priority |
| **Donate / Documentation** | UI buttons. Not needed for OFX plugin |

#### Not Needed for Porting (NDK-Specific)

| Parameter | Reason |
|-----------|--------|
| **Channels / DepthChannel** | NUKE NDK channel specification. Already replaced by Clip architecture in OFX |

### 2026-03-01: Phase 11 — Focus Point XY Picker

Implemented the OFX equivalent of NDK version's `FocusPointUtility`. Users specify an XY coordinate on screen, and the Depth value at that position is sampled and applied as focus distance.

#### Design Philosophy (Read-Only in Render)

XY pickers in OFX are known to easily cause crashes in other plugins, so the safest design paradigm was adopted:

- **No `setValue()` in render()** — UI updates from worker threads are prohibited by OFX specification
- **No custom Interact** — Delegates to host standard widget (`setUseHostOverlayHandle`)
- **`focusPlane` is overridden as a local variable** and passed to Rust (UI value unchanged)

#### Behavioral Differences from NDK Version

| Item | NDK | OFX |
|------|-----|-----|
| Sampling trigger | `knob_changed()` (main thread) | `render()` (worker thread) |
| Focus Plane update | Directly updates knob value | Local variable override (knob value unchanged) |
| depth == 0 | Skip | Skip (same behavior) |

#### C++ OFX Plugin Changes (`OpenDefocusOFX.cpp`)

**UI Parameters Added (Controls group, after Focus Plane):**

| Parameter | OFX Type | Default |
|-----------|----------|---------|
| Use Focus Point | Boolean | false |
| Focus Point XY | Double2D (`eDoubleTypeXYAbsolute`, `eCoordinatesCanonical`) | (0, 0) |

**render() Sampling Logic (after depthBuffer construction):**
1. Executes when all 3 conditions met: `useFocusPoint` && `useDepth` && `!depthBuffer.empty()`
2. Canonical → pixel coordinate conversion (`renderScale` applied)
3. fetchWindow boundary check (prevents buffer out-of-bounds access)
4. Gets depth value from `depthBuffer[idx]`
5. Only overrides `focusPlane` local variable when depth != 0
6. Moved `od_set_focus_plane()` from parameter setting block (L402) → after sampling (after depthBuffer completion)

**updateParamVisibility():**
- `useFocusPoint`: Only enabled when Mode=Depth
- `focusPointXY`: Only enabled when Mode=Depth AND Use Focus Point=true

**No Rust FFI Changes** — No new FFI functions needed. Uses existing `od_set_focus_plane()`.

#### Issues Resolved During Build

**Plugin Load Error — `setUseHostOverlayHandle` Exception:**

- Symptom: Constructor failure in both NUKE/Flame (`Constructor for OFXcom.opendefocus.ofx_v0 failed`)
- Cause: OFX Support Library's `setUseHostOverlayHandle()` directly calls `propSetInt(kOfxParamPropUseHostOverlayHandle, ...)` **without try-catch protection**. When host doesn't support this property, exception is thrown → entire `describeInContext()` fails → plugin description invalidated
- Contrast: Same OFX 1.2 property `setDefaultCoordinateSystem()` IS try-catch protected in Support Library (ofxsParams.cpp L449-461)
- Fix: Protected with `try { param->setUseHostOverlayHandle(true); } catch (...) {}`. Falls back to numeric input only on hosts without overlay support

#### Phase 11 UAT — Crosshair Display Issue Trial Records

Phase 11's render() sampling logic worked correctly, but the crosshair display was not visible in either NUKE or Flame. The following is a record of trials and failure patterns.

**Trial 1: `setUseHostOverlayHandle(true)` Only (No OverlayInteract)**

- Hypothesis: `eDoubleTypeXYAbsolute` + `setUseHostOverlayHandle(true)` would make the host draw a standard crosshair widget
- Result: **FAIL** — Plugin load error (above exception issue). No crosshair display even after adding try-catch
- Lesson: **`setUseHostOverlayHandle` alone does not draw crosshairs. OverlayInteract registration is required**

**Trial 2: Dummy OverlayInteract (draw only returns false)**

- Hypothesis: Registering an OverlayInteract would make the host automatically draw crosshairs
- Implementation: Empty overlay with only `draw()` returning `false`. Registered in `describe()` using `DefaultEffectOverlayDescriptor` CRTP pattern
- Result: **FAIL** — No crosshair display. Additional side effects:
  - **NUKE**: Use Focus Point ON → XY default (0,0) samples wrong depth value → defocus stops working
  - **Flame**: Even empty overlay causes OpenGL context switching → severe performance degradation
- Lesson: **OFX does not auto-draw crosshairs. All SDK samples (Tester, Basic, MultiBundle, ChoiceParams) draw with OpenGL. Dummy overlays cause severe Flame performance issues**

**Trial 3: Custom OpenGL Drawing (Based on SDK PositionInteract Pattern) — Initial Test**

- Implementation: Complete OpenGL drawing based on SDK's `PositionInteract` (Tester.cpp:30-178):
  - `draw()`: Crosshair + small square, state-dependent color changes (white/green/yellow)
  - `penMotion()`: Hit test + drag
  - `penDown()` / `penUp()`: Pick start/end
  - Returns `false` when `useFocusPoint` is OFF to avoid drawing/event consumption (Flame countermeasure)
- Removed `setUseHostOverlayHandle` try-catch block (not needed with custom drawing)
- Added OpenGL header (`GL/gl.h`)
- Crosshair size: 5 screen pixels, default position: (0, 0)
- Result: **Superficially FAIL** — User reported "crosshair not visible"

**Debugging — Staged Isolation:**

1st debug build: Added `fprintf(stderr, ...)` in `draw()` → Confirmed in NUKE terminal that `draw()` was NOT being called
- At this point, judged as overlay registration level issue

2nd debug build: Added debug output at 3 locations:
1. `describe()` — `mainEntry` pointer value and registration completion
2. `OpenDefocusOverlay` constructor — Instance creation
3. `draw()` — Call occurrence, `enabled` state, coordinates, pixelScale

Result (after clearing OFX cache):
```
[OpenDefocus] describe(): mainEntry=0x7f02a475ded0
[OpenDefocus] describe(): overlay registered OK
[OpenDefocus] OverlayInteract constructor called
[OpenDefocus] OverlayInteract constructor OK
[OpenDefocus] draw() called, enabled=0   ← Use Focus Point OFF
[OpenDefocus] draw() called, enabled=1   ← Use Focus Point ON
[OpenDefocus] draw() pos=(0.00, 0.00) pixelScale=(2.0000, 2.0000)
```

**Root Cause Identified:**

- Registration, instance creation, and draw dispatch were **all working correctly**
- The reason `draw()` wasn't called in the 1st test was **OFX cache** — the pre-cache-clear build (dummy overlay) was being loaded
- When `enabled=1`, OpenGL drawing code was also executing, but **crosshair was invisible for 2 reasons**:
  1. **Default position (0, 0)** — Drawn at canonical coordinate origin (bottom-left corner of image), while user was viewing near image center
  2. **Crosshair size 5px** — ~5px width at pixelScale=2.0. Existed as a tiny dot at bottom-left corner but was too small to see

**Lessons:**
- **OFX Cache Trap**: Hosts cache plugin describe results. Overlay registration changes are not reflected without cache clearing. Always clear cache before testing
- **Staged Debug Output**: Rather than guessing fixes, `fprintf(stderr, ...)` at each stage (registration → instance creation → draw call → draw parameters) is the most reliable approach
- **Default Coordinates and Visibility**: XY parameter initial value (0, 0) is the bottom-left corner of the image, invisible to users viewing near the center. Set default values considering size

**Rejected Hypotheses:**

- ~~"Re-registration in `describeInContext()` is needed"~~ — All 5 SDK samples register only in `describe()`. Confirmed in Support Library source (ofxsImageEffect.cpp L2619, L2638) that `describe()` and `describeInContext()` share the same handle, so one registration is sufficient
- ~~"OpenGL linking issue"~~ — Confirmed with `nm -D`. All OpenGL symbols (`glBegin`, `glVertex2f`, etc.) exist as `U` (undefined). Normal for `.so` dynamic linking (resolved at runtime from host process's `libGL.so`). SDK sample CMake also links `opengl::opengl`, but for OFX plugins this is resolved by the host

#### Fix Details (Crosshair Display)

- Crosshair size: 5px → 20px → **100px** (sufficient visibility even at UHD)
- Default position: (0, 0) → **(25, 25)** (entire crosshair fits within image, doesn't overlap with bottom-left corner boundary)
- Removed `setUseHostOverlayHandle` try-catch block (not needed with custom OpenGL drawing)
- Removed all debug output (`fprintf`, `#include <cstdio>`)

#### Fix Details (Focus Plane Knob Update)

UAT reported "Focus Plane knob value does not change". The current implementation only overrode a local variable in render() without reflecting to the UI, causing these problems:
- Toggling Use Focus Point ON/OFF changes focus
- Changing input image resolution shifts focus position
- Cannot set keyframes

**Design Change — From render() Sampling to changedParam() Sampling:**

OFX specification investigation confirmed that `fetchImage()` + `setValue()` within `changedParam()` (`kOfxActionInstanceChanged`) is completely safe per OFX spec:
- `changedParam()` runs on the main thread
- `fetchImage()` is explicitly documented within `kOfxActionInstanceChanged` (ofxThreadSafety.rst)
- `beginEditBlock`/`endEditBlock` for undo/redo support

Implementation:
- In `changedParam()`, detects `focusPointXY` change → samples via `depthClip_->fetchImage()` → directly updates Focus Plane knob via `focusPlaneParam_->setValue()`
- Removed sampling logic in `render()` ("4b. Focus Point XY Picker" block)
- `render()` now uses value from `focusPlaneParam_->getValueAtTime()` as-is (already updated by changedParam)

Achieves nearly identical flow to NDK version's `focus_point_knobchanged()` (lib.rs:1033-1058).

#### Dev Version Update
- `kDevVersion = "v0.1.10-OFX-v1 (Phase 11: Focus Point)"`

### Stripe Height (TIER 3) Investigation Results

Based on upstream investigation, decided to defer OFX implementation:

- `use_custom_stripe_height` / `custom_stripe_height` are stored in `NukeSpecificSettings` (lib.rs:149-151). Not in `Settings.render`
- In NDK, the `stripe_height()` method controls the value returned to NUKE, and **the NUKE engine performs the stripe splitting**
- To achieve equivalent functionality in OFX, a stripe loop must be self-implemented in C++ `render()`, managing convolution margins (overlapping regions) between stripes
- Current GPU panic CPU fallback operates correctly at 4K+, so immediate need is absent

### 2026-03-02: Phase 11 UAT Complete

All 19 items PASS (NUKE 16.0 / Flame 2026).

**Known Constraints (Accepted):**
- Crosshair drag responsiveness in Flame is slower than NUKE (overhead from `fetchImage` within `changedParam`). Significantly improved from previous performance degradation issue

---

## Performance Optimization (Branch: `feature/stripe-rendering`)

### 2026-03-02: Performance Optimization Investigation

Based on UAT feedback, investigated three performance issues and created `OPTIMIZATION_REPORT.md`.

#### Issue Summary

| # | Issue | Root Cause | Severity |
|---|---|---|---|
| 1 | UHD GPU rendering fails | wgpu storage buffer limit 128MB exceeded (UHD: 158MB) | High |
| 2 | Extreme slowdown with CPU fallback at UHD Quality High | Single-pass full-frame processing (500MB+ working set) | High |
| 3 | Flame crosshair drag responsiveness | `fetchImage()` full evaluation overhead on each call | Medium |

#### Root Cause Analysis

The OFX bridge passes the entire image as a single buffer to `render_stripe()`, while the NDK version leverages NUKE's stripe splitting (64–256px). The upstream `ChunkHandler` (4096×4096 chunks) exists but chunk sizes still produce 335MB storage buffers, exceeding the GPU buffer limit.

#### Solution Strategy

- **Phase 1**: Implement stripe-based rendering within Rust FFI bridge `od_render()` (solves Issues 1 & 2 simultaneously)
- **Phase 2**: Depth image caching (copy to `std::vector<float>` on `penDown`, read from cache during drag)
- **Phase 3 (Future)**: Thread safety analysis → upgrade to `eRenderInstanceSafe`

#### Branch Operations

- Created branch `feature/stripe-rendering`, pushed to remote
- Plugin names have `_stripe` postfix for parallel loading and comparison with main:
  - Plugin name: `OpenDefocusOFX_stripe`
  - Identifier: `com.opendefocus.ofx.stripe`
  - Bundle: `OpenDefocusOFX_stripe.ofx.bundle/`
- **Must revert to original names when merging to main**

### 2026-03-02: Phase 1 — Stripe-Based Rendering Implementation

#### Changed Files

`rust/opendefocus-ofx-bridge/src/lib.rs` only (no upstream changes)

#### Implementation Details

**Added `get_stripe_height()` helper function:**

Same logic as NDK `stripe_height()` (lib.rs:458-473):

| Mode | Stripe Height |
|---|---|
| CPU | 64 px |
| GPU Low | 256 px |
| GPU Medium | 128 px |
| GPU High / Custom | 64 px |
| FocalPlaneSetup | 32 px |

**Converted `od_render()` to stripe loop:**

1. Snapshot entire source image (`source_image`) before stripe loop
2. For each stripe, build fresh buffer (`stripe_buf`) from `source_image`
3. Build per-stripe `RenderSpecs` (`full_region.y = y_in` to preserve global coordinates)
4. After `render_stripe()`, copy only render_region from `stripe_buf` back to `image_data`
5. Abort check between stripes
6. First GPU stripe only: `catch_unwind` for panic protection, CPU fallback on failure

#### First UAT Results (Stripe Boundary Seam Issue)

Horizontal stripe boundary "seams" observed across all modes and Quality levels.

**Root cause:** All stripes shared the same `image_data` buffer. After rendering stripe N, its render_region contained rendered (blurred) pixels. Stripe N+1's padding area overlapped with stripe N's render_region, reading already-blurred pixels as source. In NDK, Nuke provides a fresh buffer (original source) for each stripe call, preventing this issue.

**Fix:** Snapshot source image before stripe loop. Each stripe copies fresh data from the snapshot into an independent buffer, renders into it, then copies only the render_region back to the output buffer. This achieves identical behavior to NDK.

#### First UAT Summary

| Status | Count | Notes |
|---|---|---|
| PASS | 3 | UHD GPU success (32.3), UHD CPU speed improvement (32.4), extreme bokeh size (32.15) |
| FAIL | 12 | Stripe boundary seams (32.1–32.12) → fixed, Flame performance degradation (32.18) |
| NOTYET | 2 | Proxy mode (32.13), multi-frame (32.17) |
| ??? | 1 | Abort (32.14) |

#### Flame Performance Degradation (32.18) Analysis

Code diff is limited to stripe splitting in `od_render()` only — no changes to plugin load/initialization. Given the report of degradation "from plugin load", the most likely cause is **simultaneous loading of main (`OpenDefocusOFX.ofx`) and stripe (`OpenDefocusOFX_stripe.ofx`) plugins, with two wgpu devices competing for GPU resources**. Retest with stripe version only is planned.

#### GPU Fallback (32.16) Analysis

Stripe splitting keeps each stripe's buffer under 128MB, so GPU rendering succeeds even at 10K+ (this is the intended improvement). The 12K "Asked for too-large image input" error is a NUKE host-side buffer limit. Test item expectations need updating.

#### Second UAT Results

- **Flame performance degradation (32.18)**: Removed main bundle, tested with stripe version only → performance issue resolved. Confirmed cause was two wgpu devices competing for GPU resources.
- **Stripe boundary seams**: Still present after source_image snapshot fix. Report: "bokeh at the bottom of seams appears larger", "seams occur at equal intervals".

#### Second Seam Fix: Insufficient Padding

**Root Cause Analysis:**

Detailed investigation of upstream kernel revealed that `get_padding()` returns `ceil(max_size)` (exactly the convolution radius), which provides zero margin for boundary sampling due to:

1. **`bilinear_depth_based`** samples at `base_coords + Vec2::ONE` (+1 pixel) → requires +1 pixel beyond convolution radius
2. **`skip_overlap`** processes `process_region.y - 1` (one extra row) → processes 1 row above render_region
3. These cause outermost ring samples from render_region edge pixels to hit **ClampToEdge** at stripe buffer boundary → subtly different convolution results vs full-image render → periodic seams

In NDK, Nuke host provides additional internal margin beyond the plugin's reported padding, preventing this issue. OFX has no such mechanism.

**Fix:** Changed padding calculation in `lib.rs:1134`:
```rust
// Before:
let padding = inst.settings.defocus.get_padding() as i32;
// After:
let padding = inst.settings.defocus.get_padding() as i32 + 4;
```
+4 breakdown: +1 bilinear interpolation, +1 skip_overlap y-1, +2 safety margin (Nuke host equivalent)

#### Third Seam Fix: render_region Expansion

Detailed analysis of NDK C++ code (`opendefocus.cpp:177-178`) revealed that NDK sets `render_region = full_region.expand(2)`, causing the kernel's `skip_overlap()` to process ALL buffer pixels including padding. The OFX bridge had set `render_region = output area only`, causing padding pixels to be skipped by `skip_overlap()`.

**Fix:** Expanded `stripe_specs.render_region` by 2px on each side beyond `full_region`.

#### Fourth Fix: Zero-Based full_region

Noted that the C++ main branch passes `fullRegion = [0, 0, bufWidth, bufHeight]` (zero-based), while the stripe version used `full_region.y = y_in` (global coordinates). Changed to zero-based coordinates `[0, 0, W, stripe_h_in]`.

#### Build System Issue Discovery and Resolution

UAT after fixes 2–4 had reported "seams still present", but **the CMake `add_custom_command` lacked `DEPENDS` on Rust source files**, so `make` never recompiled the Rust code. All tests were executed against the initial implementation binary — none of the fixes were ever deployed.

**Discovery:** Debug logging (file output to `/tmp/stripe_debug.log`) produced no output, prompting a timestamp check on build artifacts. Both `libopendefocus_ofx_bridge.a` and the bundled `.ofx` binary had timestamps of March 2 04:38 (initial build time) — unchanged across all subsequent builds.

**Workaround:** Force recompilation by deleting the static library before building:
```bash
rm -f rust/opendefocus-ofx-bridge/target/release/libopendefocus_ofx_bridge.a
make -j$(nproc)
```

**Result:** UAT with the binary containing all fixes (source_image snapshot, padding +4, render_region expand(2), zero-based coordinates) confirmed **no seams in NUKE**.

#### Debug Log Removal and CMake DEPENDS Fix

- Removed debug logging at 4 locations (file output to `/tmp/stripe_debug.log`)
- Removed `use std::io::Write` import
- CMake: Changed from `add_custom_command(OUTPUT) + add_custom_target(DEPENDS)` to `add_custom_target(rust_bridge ALL ...)`. Since cargo handles incremental compilation internally, running cargo every build is idempotent. This fundamentally eliminates missed Rust source change detection.

#### Position-Dependent Effect Seam Fix (Global Coordinates)

**Problem:** Horizontal seams appeared when astigmatism, catseye, or barndoors were enabled.

**Cause:** Each stripe's `full_region.y` was always set to `0`, causing `get_real_coordinates()` to treat all stripes as the top of the image. Position-dependent effects vary bokeh shape based on screen coordinates, so stripes beyond the first used incorrect coordinates.

**Fix:** Changed to use global coordinates matching upstream ChunkHandler convention:
- `full_region = {x: fr[0], y: y_in, z: fr[2], w: y_in_end}` — absolute Y coordinates
- `render_region = {x: rr[0]-2, y: y_out-2, z: rr[2]+2, w: y_out_end+2}` — absolute Y coordinates + expand(2)

**Verification:** The engine's image/depth slicing in `engine.rs` converts global to local via `chunk.full_region.y - self.render_specs.full_region.y`, so buffer access is unaffected. `center` (optical center) uses the global value directly from `settings.render.center` and is stripe-independent.

#### 4K-DCP Vertical Boundary Issue

**Problem:** A vertical boundary line appears on the right side of the image when rendering 4K-DCP (4096×2160) with Size=160.

**Root Cause Analysis:** The C++ fetchWindow expanded the renderWindow by margin (ceil(effectiveRadius)+1) in both X and Y directions. For 4K-DCP + Size=160, bufWidth = 4096 + 2×161 = 4418, exceeding the upstream ChunkHandler limit (4096) and triggering a horizontal split. The chunk boundary produces a visible vertical seam.

**First Fix Attempt (v2):** Removed X-axis margin from fetchWindow expansion. Only Y-axis margin is added (Rust stripe code handles Y padding). X-axis edge handling was expected to be provided equivalently by wgpu texture `ClampToEdge` sampler.

**Result:** Boundary line position shifted but was not fully eliminated.

**Second Investigation (v3):** Debug logging added to capture runtime geometry values:
```
rw=[-12,-12,4108,2172] fetchWindow=[-12,-173,4108,2333] buf=4120x2506 margin=161
```
Even with X margin removed, NUKE's renderWindow itself includes overscan (-12 to 4108 = width 4120px). bufWidth=4120 > 4096, so ChunkHandler splitting persisted. v3 testing revealed both vertical and horizontal boundaries — the right chunk underwent independent stripe processing, producing horizontal seams as well.

**Root Cause:** The upstream ChunkHandler processes chunks in-place sequentially. Chunk N's blurred output contaminates chunk N+1's padding region, causing "double blur" seams at chunk boundaries. OFX stripe splitting solves this with `source_image` snapshot, but ChunkHandler internals cannot be modified (upstream code).

**Final Fix (v4):**
1. **Cap fetchWindow X width to 4096** — Symmetrically trim overscan to fundamentally prevent ChunkHandler horizontal splitting.
   ```cpp
   if (bufWidth > 4096) {
       int excess = bufWidth - 4096;
       fetchWindow.x1 += excess / 2;
       fetchWindow.x2 -= (excess - excess / 2);
       bufWidth = 4096;
   }
   ```
2. **Clamp renderRegion to buffer bounds** — Handle cases where rw extends beyond fetchWindow after trimming.
3. **Edge-replicate overscan in output copy** — For trimmed overscan regions, replicate buffer edge pixels using ClampToEdge approach for both left and right overscan zones.
4. **Remove debug logging** — Removed `/tmp/ofx_render_debug.log` output.

### 2026-03-13: Flame Filter Image Resolution Mix Error Investigation

**Issue:** Flame reports `Unsupported input resolution mix in node` when Filter Type = Image is set and the Filter clip (e.g., 256x256 bokeh image) has a different resolution from the Source clip (e.g., 1920x1080). NUKE handles the same configuration without error.

**Investigation:** Three OFX-level fixes were attempted:

1. `getClipPreferences()` override — Output format declaration → No effect
2. `getRegionOfDefinition()` override — Source-only RoD → No effect
3. Host capability checks for `setClipBitDepth` / `setPixelAspectRatio` → No effect

**Conclusion: Flame Platform Limitation.** Commercial OFX defocus plugins (BorisFX Sapphire, Frischluft Lenscare) exhibit the same error in Flame. Flame validates input clip resolutions at the graph level before OFX actions are called, ignoring `setSupportsMultiResolution(true)`. This is not fixable via OFX API.

**Code retained:** `getClipPreferences()` and `getRegionOfDefinition()` overrides remain as they are correct OFX practice and benefit other hosts.

**Flame workaround:** Users must resize Filter images to match Source resolution before connecting.

**Known sub-issues with same-resolution filter in Flame:**

1. **Filter shape aspect ratio distortion (upstream-caused):** When Filter image is resized to match Source resolution (e.g., 1920×1080), the non-square aspect ratio causes bokeh shape distortion. Root cause: upstream Rust core calculates `filter_aspect_ratio = filter_resolution.x / filter_resolution.y` (`opendefocus-datastructure/src/lib.rs:265`), so non-square filter images produce distorted bokeh. Frischluft Lenscare does not exhibit this distortion (custom filter processing). Not fixable without upstream changes. DEFERRED.
2. **"No filter provided" error:** Occurs when the Filter clip input is temporarily disconnected. Normal behavior — `filterClip_->isConnected()` returns false, so no filter data is passed to Rust. Not an issue.

**Total judgment: Filter Type = Image is not usable in Flame.** Use built-in filter types (Simple, Disc, Blade) instead.

### 2026-03-13: Stripe-Based Rendering UAT Complete

Tester: Hiroshi. See `UAT_checklist_ja.md` section 32 for details.

#### UAT Result Summary

| Category | Result |
|----------|--------|
| Stripe-Based Rendering (18 items, Section 32) | 14 PASS / 3 DEFERRED / 1 N/A |

#### Results by Item

| # | Item | Result | Notes |
|---|------|--------|-------|
| 32.1 | HD GPU rendering | PASS | Output matches NDK |
| 32.2 | HD CPU rendering | PASS | UseGPU=false |
| 32.3 | UHD GPU rendering | PASS | Previously failed due to wgpu 128MB limit |
| 32.4 | UHD CPU rendering | PASS | No hang or extreme slowdown |
| 32.5 | Quality=Low (256px stripe) | PASS | No seams |
| 32.6 | Quality=Medium (128px stripe) | PASS | No seams |
| 32.7 | Quality=High (64px stripe) | PASS | No seams |
| 32.8 | Mode=2D (no Depth) | PASS | |
| 32.9 | Depth + FocalPlaneSetup (32px stripe) | PASS | |
| 32.10 | Filter Type=Image | DEFERRED | Flame Filter Image limitation (upstream) |
| 32.11 | Catseye stripe boundary | PASS | No seams with position-dependent effect |
| 32.12 | Barndoors stripe boundary | PASS | No seams |
| 32.12a | Astigmatism stripe boundary | PASS | No seams (global coordinates fix verified) |
| 32.13 | Proxy mode (1/2, 1/4) | PASS | renderScale correctly applied |
| 32.14 | Abort between stripes | DEFERRED | OFX abort() unimplemented (Known Issue #17). Phase 2 planned |
| 32.15 | Extreme bokeh size (500+) | PASS | Large padding, no crash |
| 32.16 | GPU fallback in stripe loop | N/A | GPU succeeds even at 10K+ — cannot trigger fallback. 12K hits NUKE host buffer limit |
| 32.17 | Multi-frame rendering | PASS | Flipbook/Write stable |
| 32.18 | Flame stripe rendering | DEFERRED | Flame Filter Image limitation |

#### Key Results

- **UHD GPU rendering now works** — stripe splitting keeps each buffer under 128MB
- **All position-dependent effects (catseye, barndoors, astigmatism)** render seamlessly across stripe boundaries — global coordinates fix verified
- **GPU fallback (32.16) reclassified as N/A** — stripe splitting resolves the root cause (buffer size), so GPU failure cannot be triggered even at 10K+. The 12K "Asked for too-large image input" error is a NUKE host-side limit, not a plugin issue
- **DEFERRED items (32.10, 32.18)** are both caused by the known Flame Filter Image platform limitation, not stripe-specific issues
- **32.14 reclassified as DEFERRED** — OFX abort() is unimplemented (Known Issue #17). Phase 2 planned with callback-based abort propagation

### 2026-03-16: Phase B — OFX Bug Fixes

#### B1: Filter Preview stripe artifact and proxy scaling (27.8)

- **Symptom 1**: Filter Resolution 256+ produced step artifacts at stripe boundaries
- **Root cause**: Preview rendering went through stripe loop. `render_preview_bokeh()` renders bokeh to the full array view, but each stripe only sees its portion, producing truncated shapes
- **Fix**: `get_stripe_height()` returns `image_height` when `filter.preview == true`, bypassing stripe splitting. Preview buffers are small (max 1024×1024), no memory concern
- **Symptom 2**: Proxy mode (1:2) doubled preview size
- **Root cause**: `fRes = filterResolution` was not scaled by `renderScale`
- **Fix**: `fRes = filterResolution * renderScale`
- **UAT 27.8**: PASS

#### B2: env_logger default filter (28.4, 28.5, 26.8)

- **Symptom**: `log::info!` messages not reaching host console (NUKE/Flame)
- **Root cause**: `env_logger::try_init()` defaults to showing nothing unless `RUST_LOG` env var is set. OFX hosts do not set this
- **Fix**: `env_logger::Builder::from_env(Env::default().default_filter_or("info")).try_init()`
- **UAT 28.4/28.5**: PASS — "Renderer recreated: CPU/GPU" confirmed in stderr
- **UAT 26.8**: PASS — GPU status visible via log output

#### Flame GPU/CPU switch crash report

- Flame crash during GPU/CPU mode switching test
- Investigation: crash dump showed all OpenDefocusOFX threads in normal wait state (tokio park). No SIGSEGV/SIGABRT
- Terminal log showed `^CApplication exited abnormally` — Ctrl+C misoperation confirmed
- Retest with proper procedure: no crash. Not a plugin issue

### 2026-03-16: Phase C — Abort Callback Implementation

Implemented coarse abort (stripe boundary) using `user_data` callback pattern.

#### Design (based on 8 rounds of code review)

- `od_render()` signature extended with `abort_check_fn` + `abort_user_data`
- `OdResult::Aborted = 5` added to FFI enum
- C++ `abortCheckThunk()` bridges OFX `abort()` API via static thunk + `this` as user_data
- On abort: `copySourceToBuffer()` helper re-populates imageBuffer with pristine source (overscan-safe, clamp-to-edge)
- Preview render passes `nullptr` (no abort needed)

#### Key files

- `rust/opendefocus-ofx-bridge/src/lib.rs`: OdResult::Aborted, od_render signature, stripe loop abort check
- `plugin/OpenDefocusOFX/src/OpenDefocusOFX.cpp`: abortCheckThunk, copySourceToBuffer helper, abort fallback

#### UAT Results (32.14, 32.14a-c)

| # | Item | Result | Notes |
|---|------|--------|-------|
| 32.14 | Abort reflected between stripes | PASS | NUKE: confirmed. Flame: N/A (host blocks UI during render) |
| 32.14a | Output after abort is pristine source | PASS | NUKE confirmed |
| 32.14b | No crash/hang after abort | PASS | NUKE confirmed |
| 32.14c | Filter Preview normal operation | PASS | nullptr callback works correctly |

#### Granularity note

Phase 1 (coarse): abort checked at stripe boundaries only. NDK polls every 10ms via async task (finer granularity). Flame does not benefit from abort due to UI blocking during render.

### Current Status

- **Phase 1–11 (OFX Port)**: Complete, UAT complete (master branch)
- **Performance Optimization Phase 1 (Stripe Rendering)**: Complete, merged to master
- **Code Review Phase**: Flame comment correction, README host/renderScale documentation completed
- **Phase B (OFX Bug Fixes)**: Filter Preview (27.8), env_logger (28.4/28.5/26.8) — all PASS
- **Phase C (Abort Callback)**: Coarse abort implemented, UAT complete (32.14/32.14a-c all PASS), merged to master
- **Flame Filter Image**: Resolution mix error and aspect ratio distortion — DEFERRED (platform/upstream limitation)

### 2026-03-17: Phase D — Stripe Performance Optimization

#### D1: Stripe buffer pre-allocation (Known Issue #20 mitigation)

- **Problem**: Each stripe allocated a new `Vec<f32>` via `to_vec()`, causing ~266MB of heap allocation/copy per 4K frame
- **Fix**: Pre-allocate one stripe buffer before the loop, reuse with `copy_from_slice()` per stripe
- Heap allocations reduced from 34/frame to 1/frame (4K High quality)
- GPU retry path also reuses the same buffer

#### D2: Stripe height increase

- CPU: 64 → 256 (34 → 9 stripes at 4K, 75% reduction)
- GPU High: 64 → 128 (34 → 17 stripes)
- GPU Medium: 128 → 256 (17 → 9 stripes)
- GPU Low, FocalPlaneSetup, Preview: unchanged

#### D3: bufWidth 4096 cap removal (Known Issue #21 — FIXED)

- **Problem**: Resolutions > 4096px wide showed horizontal edge-fold artifacts. Left/right edges were cropped and edge pixels replicated, causing visible stripe patterns after defocus convolution. Severity increased with resolution
- **Discovery**: Found during Phase D UAT with 5K+ test patterns. Pre-existed since the cap was added (before stripe rendering), but was missed in earlier UAT due to insufficient high-resolution testing
- **Root cause**: `bufWidth` was capped to 4096 in OpenDefocusOFX.cpp to prevent wgpu 128MB buffer overflow. After stripe-based rendering was introduced, this cap became unnecessary — each stripe's buffer is `width × stripe_h × 4ch × 4bytes`, well under 128MB even at 8K+
- **Fix**: Removed the `bufWidth` 4096 cap and associated `fetchWindow` trim logic
- **Verification**: 5K+ and extreme resolution test patterns confirmed artifact-free output
- **Reference samples**: `references/checker_testpattern_defocus_C.png` (before), `references/checker_testpattern_defocus_C_fixed.png` (after)

#### Known Issue #22: Catseye/Barndoors/Astigmatism NDK parity (IDENTIFIED)

- **Symptom**: Position-dependent effects produce slightly different (weaker) results than NDK
- **Root cause**: Coordinate system mismatch between `center` and `full_region`
  - NDK: both are box-local (same coordinate system). center = `format_center - box.y`, full_region.y = `stripe.y - box.y`
  - OFX: center is fetchWindow-local (`rod_center - fetchWindow.y1`), full_region is buffer-local (0 origin)
  - Upstream kernel divides by center: `(real_position - center) / center` — coordinate mismatch produces different normalized distance
- **Decision**: Not fixed in Phase D. Requires coordinate system alignment across stripe loop, overscan, and proxy — all position-dependent UAT must be re-run. Planned for future phase
- **Reference samples**: `references/checker_testpattern_defocus_catseye_NDK.png` vs `_OFX.png`, `_barndoor_NDK.png` vs `_OFX.png`

#### Known Issue #23: Vertical seam at resolutions > 4096px (DEFERRED — Upstream)

- **Symptom**: Vertical boundary line visible at X≈4096 when image width exceeds 4096px. Same artifact occurs in NDK
- **Root cause**: Upstream `ChunkHandler` (`chunks.rs:19`) hardcodes `limit: 4096`. When stripe `full_region` width exceeds 4096, it splits horizontally into chunks. Chunk boundary produces a visible seam due to imperfect padding overlap
- **OFX fixable**: No. ChunkHandler is inside the upstream `opendefocus` crate, inaccessible from OFX bridge
- **Classification**: Upstream bug. Per porting policy, not fixed on OFX side. Upstream Issue report candidate
- **Reference samples**: `references/checker_testpattern_seam.png` (OFX), `references/checker_testpattern_seam_NDK.png` (NDK)

### Current Status

- **Phase 1–11 (OFX Port)**: Complete, UAT complete (master branch)
- **Performance Optimization Phase 1 (Stripe Rendering)**: Complete, merged to master
- **Code Review Phase**: Flame comment correction, README host/renderScale documentation completed
- **Phase B (OFX Bug Fixes)**: Filter Preview (27.8), env_logger (28.4/28.5/26.8) — all PASS
- **Phase C (Abort Callback)**: Coarse abort implemented, UAT complete, merged to master
- **Phase D (Stripe Perf Optimize)**: Buffer pre-allocation, stripe height increase, bufWidth cap removal — in progress on `feature/stripe-perf-optimize`
- **Flame Filter Image**: Resolution mix error and aspect ratio distortion — DEFERRED (platform/upstream limitation)

### OFX-Side Unresolved

Known Issue #20 (memory copy overhead) partially mitigated by Phase D but not fully resolved — upstream API requires owned buffers.

### Next Steps

1. Phase D UAT completion and merge to master
2. Release: v0.1.10-OFX-v2 (Phase B + C + D)
3. Upstream feedback: pixel drift, enum off-by-one, unwired parameters (gamma, focal_plane_offset, noise), catseye enable check missing (#18), axial aberration flag misreference (#19)
4. Depth image caching for Flame drag responsiveness improvement
