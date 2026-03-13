# OpenDefocus OFX

OpenFX port of [OpenDefocus](https://codeberg.org/gillesvink/opendefocus) — an advanced open-source convolution library for image post-processing.

This project brings the OpenDefocus Rust core to any OFX-compatible host application (NUKE, Flame, DaVinci Resolve, etc.) via an extern "C" FFI bridge.

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
| Host | NUKE only | Any OFX host (NUKE, Flame, Resolve, etc.) |
| Language | Rust + C++ (CXX FFI) | Rust + C++ (extern "C" FFI) |
| Stripe splitting | NUKE host provides stripes | Plugin-internal stripe loop in `od_render()` |
| Camera Mode | Supported (NUKE camera data) | Omitted (host-dependent) |
| GPU acceleration | Vulkan/Metal via wgpu | Same (Vulkan/Metal via wgpu) |
| Build system | `cargo xtask` | CMake + cargo |
| Parameter UI | NUKE knobs | OFX parameter API |

## Known Issues

### OFX-Specific

- **Pixel drift**: A ~1px offset exists between NDK and OFX outputs due to OFX standard coordinate system compliance. This is not a bug.

### Upstream-Originated (DEFERRED)

The following issues originate from the OpenDefocus Rust core and affect both NDK and OFX versions equally. Per porting policy, these are preserved as-is.

| Issue | Detail |
|-------|--------|
| Gamma Correction has no effect | Protobuf-defined but not connected to rendering pipeline |
| Focal Plane Offset has no effect | NDK knob created but not connected to ConvolveSettings |
| Bokeh Noise has no effect | `noise` feature flag not enabled in upstream crate |
| Axial Aberration Type switching has no color change | Protobuf/Rust enum off-by-one mapping |
| CPU/GPU ~1px pixel drift | Minor rendering difference between CPU and GPU backends |
| Size Multiplier bokeh breakdown at large values | Upstream kernel interaction issue |

### Flame: Known Limitations

- **Filter Type = Image is not usable in Flame**: Flame rejects input clips with different resolutions (`Unsupported input resolution mix in node`). This is a Flame platform limitation — commercial plugins (BorisFX Sapphire, Frischluft Lenscare) exhibit the same error. Even if the Filter image is resized to match the Source resolution, the non-square aspect ratio (e.g., 1920x1080) causes the bokeh shape to appear distorted, because the upstream Rust core calculates `filter_aspect_ratio` from the filter image dimensions. Use the built-in filter types (Simple, Disc, Blade) in Flame instead.

For full details, see `references/known_issues.md`.

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
