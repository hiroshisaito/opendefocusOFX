# Changelog — OpenDefocus OFX

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
