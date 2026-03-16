# OpenDefocus OFX

**Version: v0.1.10-OFX-v1**

OpenFX port of [OpenDefocus](https://codeberg.org/gillesvink/opendefocus) — an advanced open-source convolution library for image post-processing.

This project brings the OpenDefocus Rust core to OFX-compatible host applications via an extern "C" FFI bridge. Tested and supported on **NUKE** and **Flame**. Other OFX hosts may work but are untested.

## Porting Policy

- The OFX port aims to be a **faithful reproduction of the original NUKE NDK version**.
- Upstream bugs or unimplemented features are preserved as-is — the OFX side does not independently fix or extend upstream behavior.
- Parameter behavior follows the NUKE NDK version as the reference (e.g., grayout behavior, default values).
- Upstream-originated UAT failures are classified as DEFERRED.

### Intentional Omissions

The following NDK features are intentionally omitted from the OFX version:

| Feature | Reason |
|---------|--------|
| Camera Mode (CameraMaxSize, UseCameraFocal, WorldUnit) | Host-specific camera data access; full OFX support is infeasible |
| DeviceName | Read-only GPU device name display; low priority |
| UseCustomStripeHeight / CustomStripeHeight | Performance tuning; low priority |
| Donate / Documentation buttons | UI-only; not applicable to OFX |

### Stripe-Based Rendering (OFX-specific)

In the NDK version, NUKE's host engine splits the image into horizontal stripes and calls the plugin's `render()` per stripe. OFX hosts do not guarantee this behavior, so the OFX version implements its own stripe splitting inside `od_render()`. This is the only significant architectural divergence from the NDK version.

## Build

### Prerequisites

- **CMake** 3.20+
- **C++17** compiler (GCC 8+ / Clang 7+)
- **Rust** stable (1.92+) and the nightly toolchain listed in `upstream/opendefocus/crates/spirv-cli-build/rust-toolchain.toml`
- **OpenFX SDK** — included as a git submodule (`upstream/openfx/`)
- **OpenDefocus** — included as a git submodule (`upstream/opendefocus/`)

### Dependencies

| Component | Location | License |
|-----------|----------|---------|
| OpenDefocus (Rust core) | `upstream/opendefocus/` | EUPL-1.2 |
| OpenFX SDK | `upstream/openfx/` | BSD 3-Clause |
| OFX FFI bridge (Rust) | `rust/opendefocus-ofx-bridge/` | EUPL-1.2 |
| OFX plugin (C++) | `plugin/OpenDefocusOFX/` | EUPL-1.2 |

### Build Steps

```bash
# Ensure Rust toolchain is in PATH
export PATH="$HOME/.cargo/bin:$PATH"

# Initialize submodules
git submodule update --init --recursive

# Create build directory and build
mkdir -p build && cd build
cmake ../plugin/OpenDefocusOFX -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The built plugin is automatically copied to the bundle directory:
```
bundle/OpenDefocusOFX.ofx.bundle/Contents/Linux-x86-64/OpenDefocusOFX.ofx
```

### Installation

Copy the bundle to your OFX plugin path:

```bash
# System-wide
sudo cp -r bundle/OpenDefocusOFX.ofx.bundle /usr/OFX/Plugins/

# Or set OFX_PLUGIN_PATH
export OFX_PLUGIN_PATH=/path/to/bundle/parent
```

## OFX vs NDK Differences

| Aspect | NDK Version | OFX Version |
|--------|------------|-------------|
| Host | NUKE only | Tested: NUKE, Flame (other OFX hosts may work but are untested) |
| Language | Rust + C++ (CXX FFI) | Rust + C++ (extern "C" FFI) |
| Stripe splitting | NUKE host provides stripes | Plugin-internal stripe loop in `od_render()` |
| Camera Mode | Supported (NUKE camera data) | Omitted (host-dependent) |
| GPU acceleration | Vulkan/Metal via wgpu | Same (Vulkan/Metal via wgpu) |
| Build system | `cargo xtask` | CMake + cargo |
| Parameter UI | NUKE knobs | OFX parameter API |

### renderScale

The plugin uses `renderScale.x` only (assumes uniform scaling). NUKE and Flame always provide `renderScale.x == renderScale.y`. Hosts that use non-uniform renderScale (non-square pixels) may produce distorted output.

## Known Issues

### OFX-Specific

| # | Issue | Status | Detail |
|---|-------|--------|--------|
| 5 | NDK/OFX ~1px pixel drift | DEFERRED | OFX standard coordinate system compliance; imperceptible at 2K+ |
| 8 | Stripe boundary seam | FIXED | Fixed with source_image snapshot + global coordinates in stripe RenderSpecs |
| 9 | Filter Preview buffer overflow / stripe artifact / proxy scaling | FIXED | Buffer size calculation corrected. Stripe splitting bypass for preview rendering. Proxy mode renderScale applied to filter resolution |
| 10 | Bokeh parameter grayout not restoring | FIXED | Visibility logic corrected |
| 11 | Filter Preview black in Depth mode | FIXED | State initialization corrected |

### Upstream-Originated (DEFERRED)

The following issues originate from the OpenDefocus Rust core and affect both NDK and OFX versions equally. Per porting policy, these are preserved as-is.

| # | Issue | Detail |
|---|-------|--------|
| 1 | Gamma Correction has no effect | Protobuf-defined but not connected to rendering pipeline. Per-axis gamma (Catseye/Barndoors/Astigmatism) works correctly |
| 2 | Focal Plane Offset has no effect | NDK knob created but not connected to ConvolveSettings. Same symptom confirmed in NDK |
| 3 | Bokeh Noise has no effect | `noise` feature flag not enabled in upstream `opendefocus` crate. `apply_noise()` implementation exists in `bokeh-creator` but is stubbed out |
| 4 | Axial Aberration Type switching has no color change | Protobuf enum (0,1,2) vs Rust internal enum (1,2,3) off-by-one. All protobuf values fall through to `RedBlue`. Rendering function itself supports all 3 colors |
| 6 | CPU/GPU ~1px pixel drift | Minor rendering difference between CPU and GPU backends. Same behavior in NDK |
| 7 | Size Multiplier bokeh breakdown at large values | Bokeh collapses or grey regions appear at large Size Multiplier values. Normal when equivalent size is set via Size/MaxSize parameters. Similar symptom in NDK |
| 18 | Catseye applied when disabled (Barndoor interaction) | `calculate_catseye()` in the kernel is called unconditionally without checking `CATSEYE_ENABLED` flag. When Barndoor Enable=on triggers the non-uniform path, catseye effects are applied even with Catseye Enable=off. Barndoors and Astigmatism correctly check their enable flags |
| 19 | Axial Aberration enable flag checks wrong bitflag | `get_axial_aberration_settings()` checks `BARNDOORS_ENABLED` instead of the correct flag (copy-paste error in `internal_settings.rs`) |

### Architecture

| # | Issue | Status | Detail |
|---|-------|--------|--------|
| 14 | UHD GPU failure (wgpu 128MB limit) | FIXED | Resolved by stripe-based rendering |
| 15 | UHD CPU extreme slowdown | FIXED | Resolved by stripe-based rendering |
| 16 | Flame crosshair drag responsiveness | IDENTIFIED | OFX API requires `fetchImage()` which triggers full node tree re-evaluation. NDK version accesses NUKE's scanline cache directly at zero cost. Fundamental OFX API limitation; current performance is acceptable (UAT 30.19 PASS) |
| 17 | Render abort not implemented | IDENTIFIED | OFX version does not call the host `abort()` API during rendering. User cancellation during rendering is not reflected. NDK version polls `node.aborted()` every 10ms via a background task. Phase 2 implementation planned using callback-based abort propagation |

### Flame: Known Limitations

| # | Issue | Status | Detail |
|---|-------|--------|--------|
| 12 | Filter Type = Image resolution mismatch error | DEFERRED | Flame validates input clip resolutions at graph connection level before OFX actions are called, ignoring `setSupportsMultiResolution(true)`. Commercial plugins (BorisFX Sapphire, Frischluft Lenscare) exhibit the same error. Not fixable via OFX API |
| 13 | Filter Type = Image aspect ratio distortion | DEFERRED | Flame requires Filter image to match Source resolution, but upstream Rust core calculates `filter_aspect_ratio` from filter image dimensions (`filter_resolution.x / filter_resolution.y`). Non-square filter images produce distorted bokeh. Frischluft Lenscare avoids this with custom filter processing |

**Filter Type = Image is not usable in Flame.** Use the built-in filter types (Simple, Disc, Blade) instead.

## Project Structure

```
.
├── plugin/OpenDefocusOFX/     # OFX plugin (C++)
│   ├── CMakeLists.txt
│   └── src/OpenDefocusOFX.cpp
├── rust/opendefocus-ofx-bridge/  # Rust FFI bridge
│   ├── src/lib.rs
│   └── include/opendefocus_ofx_bridge.h
├── upstream/
│   ├── opendefocus/           # OpenDefocus core (git submodule)
│   └── openfx/                # OpenFX SDK (git submodule)
├── bundle/                    # Built OFX bundle output
├── references/                # Design documents, investigation notes, known issues
├── README.md                  # Upstream OpenDefocus README
├── README_OFX.md              # This file
├── LICENSE.md                 # License information
├── HISTORY_DEV_en.md          # Development history (English)
└── UAT_checklist_en.md        # UAT test checklist (English)
```

## Original Project & Acknowledgments

This project is a derivative work of **OpenDefocus** by Gilles Vink.

- **Repository**: https://codeberg.org/gillesvink/opendefocus
- **Documentation**: https://opendefocus.codeberg.page
- **License**: European Union Public Licence v. 1.2 (EUPL-1.2)

OpenDefocus is an advanced open-source convolution library featuring GPU-accelerated rendering (Vulkan/Metal), position-dependent bokeh effects (catseye, barndoors, astigmatism, axial aberration), and depth-based defocus. The entire rendering kernel is written in pure Rust and runs on both GPU and CPU using the same source code via the Rust-GPU SPIR-V compiler.

We gratefully acknowledge the work of Gilles Vink and all contributors to the OpenDefocus project.
