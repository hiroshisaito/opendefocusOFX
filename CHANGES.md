# Changelog — OpenDefocus OFX

## Unreleased

### Stability

- **FFI panic protection (HIGH #1)**: `ensure_renderer()` and `od_set_use_gpu()` now wrap the wgpu device probe in `catch_unwind`. A panic from broken GPU drivers no longer crosses the FFI boundary into the host.
- **FFI panic protection (HIGH #2)**: All GPU stripes (not just the first) are now wrapped in `catch_unwind` with CPU fallback. Previously only the first GPU stripe was protected; a panic on stripe 2+ would have propagated to the host.
- **FFI panic protection (LOW #1, symmetry)**: The two remaining CPU-renderer creation sites (lazy-init GPU-previously-failed path and stripe-loop CPU fallback) are now also wrapped in `catch_unwind`. CPU adapter creation rarely panics, but the wrap closes the last gap so every renderer-creation path on the bridge is protected.
- **GPU toggle state consistency (LOW #2)**: `od_set_use_gpu()` no longer mutates `inst.settings.render.use_gpu_if_available` before the device probe. The canonical setting is now updated only after a successful probe, so a failed probe leaves the instance in a state consistent with the still-active renderer.

### Build

- **Windows: self-contained bundle**: The Windows `.ofx` now statically links libgcc / libstdc++ / libwinpthread. The bundle can be deployed to NUKE / Flame / Resolve hosts without installing MSYS2 UCRT64 runtimes. Binary size: 9.5 MB → 12.5 MB.
- **Windows toolchain**: Migrated from Strawberry MinGW (GCC 13.2.0) to MSYS2 UCRT64 (GCC 15.2.0) for actively-maintained Windows GNU toolchain support.

### Compatibility

- **Fusion Studio (Linux standalone) load failure resolved (Known Issue #26)**: Two-stage fix.
  - **Stage 1 — OfxSetHost stub.** OFX 1.5 marks this entry point as optional and the C++ Support library does not provide it, but standalone Fusion Studio Linux's loader treats its absence as fatal (`undefined symbol: OfxSetHost`).  A no-op stub returning `kOfxStatOK` resolves the symbol-resolution failure.  The stub gets explicit `default` visibility to override the bundle-wide `-fvisibility=hidden`.
  - **Stage 2 — visibility leak fix.** Stage 1 alone was insufficient: Fusion's loader also rejected the bundle on a second, silent grounds.  The `.ofx` was leaking ~2700 symbols (OFX Support library `OFX::*`, libstdc++ `std::__cxx11::*`, and the Rust FFI bridge's `od_*` exports) because (a) the OFX Support static library was compiled without `-fvisibility=hidden`, and (b) Rust `extern "C"` exports default to public visibility.  Two changes restore parity with plugins Fusion already accepts (smooth.ofx exports only 3 symbols):
    - `CXX_VISIBILITY_PRESET hidden` added to the `OfxSupport` static-library target.
    - A linker version script (`plugin/OpenDefocusOFX/OpenDefocusOFX.exports`) is wired into the Linux link line; only `OfxGetNumberOfPlugins`, `OfxGetPlugin`, and `OfxSetHost` remain in the dynamic symbol table.  Windows PE/COFF only exports `__declspec(dllexport)` symbols by default, so no version script is needed there.
  - **Verified on Linux**: Fusion Studio loads + renders normally; NUKE, Flame, and DaVinci Resolve Studio (Fusion Page + Color Page) are pixel-identical to v5.  macOS and Windows hosts remain to be UAT'd on the respective platforms before the v6 release.
  - **Bundle export profile**: 2725 → 3 dynamic `T` exports; binary size −370 KB from removed export metadata.

### Documentation

- Documented the non-negative RoD origin invariant assumed by `static_cast<int>` truncation (NUKE / Flame only; re-validate when adding Resolve / Fusion).
- Documented the macOS Intel deprecated-path deviation (Intel ships to `MacOS-x86-64` pending universal binary migration in the next major release).
- Removed stale comment about fetchWindow X-axis trimming (no longer applicable after the X-overscan removal in v4).

## v0.1.10-OFX-v5 (2026-04-04)

### Performance Improvements

- **Lazy renderer initialization**: The wgpu GPU renderer is now created on the first render call instead of at node creation. Node creation is significantly faster, especially in projects with many OpenDefocus nodes.

### Bug Fixes

- **eContextFilter clip guard**: `fetchClip()` for `Depth` and `Filter` clips is now guarded by context. In `eContextFilter`, these clips are undefined; the previous unconditional fetch could throw on stricter OFX hosts.
- **Failure diagnostics**: Backend initialization failure (`od_create`), null-handle passthrough, and render failure are now logged to stderr with error codes. The passthrough-on-failure design is unchanged.

### Forward Compatibility

- **Interactive/draft render optimization** (code path only): When an OFX 1.4 host provides `interactiveRenderStatus` or `renderQualityDraft` flags, quality is automatically reduced to Low and samples halved for faster UI feedback. NUKE and Flame currently always report `0` for these flags, so there is no behavior change on those hosts. The code is in place for future host support.

### Notes

- Based on upstream OpenDefocus v0.1.10 (unchanged)
- Tested on: Flame (Linux)
- Platforms: Linux x86_64, macOS x86_64 / arm64, Windows x86_64

---

## v0.1.10-OFX-v4 (2026-03-28)

- P0 stability: per-instance abort, GPU toggle out of render, depth fetch throttling
- Phase E: coordinate fix for catseye/barndoors/astigmatism (NDK parity)
- Thread safety: eRenderUnsafe → eRenderInstanceSafe
- Windows build support (MinGW)
- LTO optimization (binary size -57%)
- OpenGL link fix for Linux (Fusion Studio dlopen compatibility)
- FFI panic protection (catch_unwind on od_create)
- Depth fetch guard in 2D mode, RoI X overscan removal

## v0.1.10-OFX-v3 (2026-03-23)

- macOS support: Intel (x86_64) and Apple Silicon (arm64) builds
- NUKE macOS Focus Point crash mitigation (auto-hide on macOS + NUKE)
- Focus Point XY overlay and depth sampling

## v0.1.10-OFX-v2 (2026-03-17)

- Stripe-based rendering (UHD+ GPU/CPU support)
- Abort callback (coarse, stripe boundary)
- Memory optimization (pre-allocated stripe buffer)
- Flame Filter Type=Image limitation documented

## v0.1.10-OFX-v1 (2026-03-09)

- Initial OFX port of OpenDefocus v0.1.10
- Full parameter parity with NDK version (excluding Camera Mode)
- GPU acceleration via wgpu (Vulkan/Metal)
- Tested on NUKE and Flame (Linux)
