# UAT Checklist — OpenDefocus OFX v0.1.10-OFX-v1

## Test Environment

| Item | Details |
|------|---------|
| OFX Plugin | `bundle/OpenDefocusOFX.ofx.bundle/Contents/Linux-x86-64/OpenDefocusOFX.ofx` |
| Reference | OpenDefocus Nuke NDK v0.1.10 |
| OS | Rocky Linux 8.10 (x86_64) |
| OFX Host | NUKE 16.0, Flame 2026 |
| Test Date | Feb 25 2026 |
| Tester | Hiroshi |

---

## 1. Plugin Loading

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 1.1 | OFX host recognizes the plugin | PASS | Verified in Flame; also confirmed separated from Filter category in NUKE. |
| 1.2 | Plugin can be added as a node | PASS | |
| 1.3 | No crash when adding plugin | PASS | |
| 1.4 | Plugin description is displayed correctly | N/A | Host-dependent UI behavior. OFX-side implementation (`setPluginDescription`) is correct. No action needed |

## 2. Clip Connection

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 2.1 | Image can be connected to Source input | PASS | RGBA 32-bit float |
| 2.2 | Depth map can be connected to Depth input (optional) | PASS | RGBA or Alpha |
| 2.3 | Result is output to Output | PASS | |
| 2.4 | No error when Depth is not connected | PASS | Should work in 2D mode |

## 3. Parameter Behavior

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 3.1 | Size parameter is displayed in UI | PASS | Default: 10.0, Range: 0–500 |
| 3.2 | Focus Plane parameter is displayed in UI | PASS | Default: 1.0, Range: 0–10000 |
| 3.3 | Size = 0 results in passthrough (input passed directly to output) | PASS | isIdentity behavior confirmed |
| 3.4 | Changing Size changes defocus amount | PASS | |
| 3.5 | Changing Focus Plane changes the focal plane | PASS | Only effective when Depth is connected |
| 3.6 | Parameters support keyframing | PASS | Time-varying animation |

## 4. 2D Mode (Depth Not Connected)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 4.1 | Uniform defocus is applied without Depth connected | PASS | |
| 4.2 | Blur amount increases with Size | PASS | |
| 4.3 | Visually equivalent results to NUKE NDK version in 2D mode | DEFERRED | ~1px pixel drift observed in NUKE NDK version. Upstream coordinate system investigation needed |

> **4.3 Analysis**: A ~1px offset difference was confirmed between OFX and NDK versions. The OFX version operates correctly in the OFX standard coordinate system. The NDK version may be affected by NUKE's pixel center 0.5 offset coordinate system. **Not a blocker for OFX release — to be re-verified after upstream investigation**

## 5. Depth Mode (Depth Connected)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 5.1 | Depth-based defocus is applied when Depth is connected | PASS | |
| 5.2 | Sharp near Focus Plane, blurred at distance | PASS | |
| 5.3 | Changing Focus Plane value moves the focal plane | PASS | |
| 5.4 | Visually equivalent results to NUKE NDK version in Depth mode | DEFERRED | Pixel drift observed in NUKE NDK version. Same root cause as 4.3 |
| 5.5 | Works when Depth is Alpha channel only | N/A | In Flame, RGB and Matte(A) are separate inputs, so Alpha-only Depth input does not occur in practice. No action needed |

> **5.5 Note**: Code has been updated to use `depth->getPixelComponents()` to get the actual image components. However, in both NUKE and Flame workflows, Depth is input as RGBA, so Alpha-only input testing is out of scope.

## 6. Rendering Quality & Accuracy

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 6.1 | No artifacts in output image | DEFERRED | No artifacts in OFX version alone. Pixel drift when compared with NDK version. Same root cause as 4.3 |
| 6.2 | Output image is not black or zero | PASS | Buffer passing error check |
| 6.3 | Alpha channel is processed correctly | PASS | Input Alpha is preserved/processed |
| 6.4 | Rendering completes at high resolution (4K+) | PASS | No memory shortage or crash |
| 6.5 | Works at small resolution (e.g., 256x256) | PASS | Tested at 256x256 |

## 7. Comparison with NUKE Version

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 7.1 | Compare OFX and NUKE version output with same parameters | DEFERRED | Pixel drift caused by upstream coordinate system; to be re-verified after upstream investigation |
| 7.2 | 2D mode comparison | DEFERRED | Same as above |
| 7.3 | Depth mode comparison | DEFERRED | Same as above |
| 7.4 | Visual inspection confirms no major differences | PASS | Drift is minimal and barely noticeable at 2K+. Detectable with overlay/switch comparison tests |

> **7.1–7.3 Analysis**: All same root cause — ~1px pixel drift between NUKE NDK and OFX versions. OFX version operates correctly in OFX standard coordinate system. NDK version drift likely caused by upstream coordinate transformation. **Not a release blocker for OFX version. To be re-verified after upstream investigation**

## 8. Stability

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 8.1 | No crash during continuous multi-frame rendering | PASS | |
| 8.2 | No crash when rapidly changing parameters | PASS | |
| 8.3 | No memory leak when repeatedly adding/removing plugin | PASS | |
| 8.4 | No crash when quitting host application | PASS | od_destroy works correctly |

## 9. Quality Parameter (Phase 3)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 9.1 | Quality parameter is displayed in UI (Low/Medium/High/Custom) | PASS | Default: Low |
| 9.2 | Changing Quality changes rendering result | PASS | |
| 9.3 | Samples parameter is only enabled when Quality=Custom | PASS | |
| 9.4 | Changing Samples value changes rendering result | PASS | When Quality=Custom |

## 10. Bokeh Parameters — Filter Type (Phase 3)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 10.1 | Filter Type parameter is displayed in UI (Simple/Disc/Blade) | PASS | Default: Simple |
| 10.2 | Filter Type=Simple applies conventional defocus | PASS | |
| 10.3 | Filter Type=Disc applies circular Bokeh | PASS | |
| 10.4 | Filter Type=Blade applies polygonal Bokeh | PASS | |
| 10.5 | Filter Preview=true outputs filter shape preview | PASS | For Disc/Blade. Fix: Renders Bokeh in a small buffer of filter_resolution size, then copies to center of output image. Confirmed that preview size changes with Filter Size. |
| 10.6 | Filter Type=Simple disables (grays out) Bokeh Shape parameters | PASS | Fix: Grayout removed to match NUKE NDK version. Bokeh parameters are always enabled. Confirmed toggle works at all times. |

> **10.5 Analysis**: Rust core's `render_preview_bokeh` draws Bokeh at full buffer size. OFX version was passing full output resolution (2K/4K) buffer. Fix: Renders Bokeh in a `filter_resolution`-sized buffer (default 256px) and copies to center of output image. Preview size adjustable via Filter Resolution parameter.

> **10.6 Analysis**: When switching to Simple with Filter Preview enabled, Filter Preview becomes grayed out and cannot be restored. Original NUKE NDK version does not gray out. Fix: Removed Bokeh parameter grayout to match NUKE NDK behavior. Only Quality=Custom Samples retains grayout control.

## 11. Bokeh Parameters — Bokeh Shape (Phase 3)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 11.1 | Changing Ring Color changes Bokeh ring brightness | PASS | For Disc/Blade |
| 11.2 | Changing Inner Color changes Bokeh center brightness | PASS | For Disc/Blade |
| 11.3 | Changing Ring Size changes Bokeh ring width | PASS | For Disc/Blade |
| 11.4 | Changing Outer Blur changes Bokeh outer softness | PASS | For Disc/Blade |
| 11.5 | Changing Inner Blur changes Bokeh inner softness | PASS | For Disc/Blade |
| 11.6 | Changing Aspect Ratio changes Bokeh shape aspect ratio | PASS | For Disc/Blade |
| 11.7 | Changing Filter Resolution renders correctly | PASS | For Disc/Blade |

## 12. Bokeh Parameters — Blade-Specific (Phase 3)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 12.1 | Changing Blades value changes polygon vertex count | PASS | For Blade, Range: 3–16 |
| 12.2 | Changing Angle changes Bokeh shape rotation angle | PASS | For Blade |
| 12.3 | Changing Curvature changes Bokeh edge curvature | PASS | For Blade |
| 12.4 | Filter Type=Disc disables (grays out) Blades/Angle/Curvature | N/A | Grayout removed per 10.6 fix (NUKE NDK compliant). Parameters always enabled |

## 13. Defocus General Parameters (Phase 4)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 13.1 | Mode parameter is displayed in UI (2D/Depth) | PASS | Default: 2D |
| 13.2 | Mode=2D applies uniform defocus equivalent to no Depth connection | PASS | Regardless of Depth connection |
| 13.3 | Mode=Depth + Depth connected applies depth-based defocus | PASS | |
| 13.4 | Mode=Depth + Depth not connected falls back to 2D | PASS | No error |
| 13.5 | Math parameter is displayed in UI (Direct/1÷Z/Real) | PASS | Default: 1/Z |
| 13.6 | Changing Math changes rendering result | PASS | When Mode=Depth |
| 13.7 | Render Result parameter is displayed in UI (Result/Focal Plane Setup) | PASS | Default: Result |
| 13.8 | Render Result=Focal Plane Setup outputs focal plane visualization | PASS | When Mode=Depth |
| 13.9 | Show Image=true overlays source image | PASS | When RenderResult=Focal Plane Setup |
| 13.10 | Changing Protect changes focal plane protection range | PASS | When Mode=Depth |
| 13.11 | Changing Maximum Size limits maximum defocus radius | PASS | When Mode=Depth |
| 13.12 | Changing Gamma Correction changes Bokeh brightness balance | DEFERRED | Upstream unimplemented — protobuf definition only, not connected to rendering pipeline. NDK version also has no knob created. Not an OFX issue |
| 13.13 | Farm Quality parameter is displayed in UI (Low/Medium/High/Custom) | PASS | Default: High |

> **13.12 Analysis**: Upstream Rust core investigation: `gamma_correction` is defined in protobuf (`opendefocus.proto` line 68) as `required float gamma_correction = 110 [default = 1.0]`, but (1) `create_knob_with_value()` is not called in NDK version so no UI knob is created, (2) not included in `ConvolveSettings` structure so not passed to rendering pipeline. Completely dead field. Note: `catseye.gamma` / `barndoors.gamma` / `astigmatism.gamma` are separate fields that function correctly (for non-uniform effects).

## 14. Defocus — Conditional Enable/Disable (Phase 4)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 14.1 | Math/RenderResult/Protect/MaxSize/FocalPlaneOffset are grayed out when Mode=2D | PASS | |
| 14.2 | Above parameters are enabled when Mode=Depth | PASS | |
| 14.3 | ShowImage is grayed out when Mode=Depth + RenderResult=Result | PASS | |
| 14.4 | ShowImage is enabled when Mode=Depth + RenderResult=Focal Plane Setup | PASS | |
| 14.5 | GammaCorrection, FarmQuality, SizeMultiplier are always enabled regardless of Mode | PASS | |

## 15. Advanced Parameters (Phase 4)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 15.1 | Advanced page is displayed in UI | PASS | Separate page from Controls and Bokeh |
| 15.2 | Size Multiplier parameter is displayed in UI | PASS | Default: 1.0, Range: 0–2 |
| 15.3 | Changing Size Multiplier scales defocus size proportionally | PASS | |
| 15.4 | Focal Plane Offset parameter is displayed in UI | PASS | Default: 0.0, Range: -5–5 |
| 15.5 | Changing Focal Plane Offset offsets the focal plane | DEFERRED | Upstream unimplemented — protobuf definition and NDK knob created but not connected to `ConvolveSettings`, so not reflected in rendering. NDK version exhibits same behavior (confirmed by user report). Not an OFX issue |

> **15.5 Analysis**: Upstream Rust core investigation: `focal_plane_offset` is defined in protobuf (`opendefocus.proto` line 63) as `required float focal_plane_offset = 80 [default = 0.0]`. NDK version creates the knob in the Advanced tab via `create_knob_with_value()` (lib.rs line 621-625). However, it is not included in `ConvolveSettings` structure and not passed to the rendering pipeline at all. Changing the value has no effect — this is an upstream bug/unimplemented feature. NDK version has the same symptom (confirmed by user report).

## 16. Bokeh Noise Parameters (Phase 5)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 16.1 | Noise Size parameter is displayed on Bokeh page | PASS | Default: 0.1, Range: 0–1 |
| 16.2 | Changing Noise Size changes Bokeh noise size | DEFERRED | Upstream `noise` feature flag is disabled. Implementation exists in bokeh_creator crate but compiled out via `default-features = false` + `features = ["image"]`. NDK version also has no effect under same conditions |
| 16.3 | Noise Intensity parameter is displayed on Bokeh page | PASS | Default: 0.25, Range: 0–1 |
| 16.4 | Changing Noise Intensity changes Bokeh noise intensity | DEFERRED | Same root cause as 16.2 — upstream `noise` feature disabled |
| 16.5 | Noise Seed parameter is displayed on Bokeh page | PASS | Default: 0, Range: 0–10000 |
| 16.6 | Changing Noise Seed changes noise pattern | DEFERRED | Same root cause as 16.2 — upstream `noise` feature disabled |
| 16.7 | Noise parameters are always enabled regardless of Filter Type (no grayout) | PASS | NUKE NDK compliant |
| 16.8 | Noise effects are reflected in Filter Preview when enabled | DEFERRED | Same root cause as 16.2 — upstream `noise` feature disabled |

> **16.2/16.4/16.6/16.8 Analysis**: Upstream Rust core investigation: bokeh_creator crate (v0.1.17) contains a complete Noise implementation (`Renderer::apply_noise()` generates and applies Fbm Simplex noise). However, the upstream `opendefocus` crate depends on `bokeh-creator = { default-features = false, features = ["image"] }`, excluding the `"noise"` feature. This causes the `#[cfg(not(feature = "noise"))]` stub function (returns value unchanged) to be compiled. NDK version operates under the same conditions with no effect. Not an OFX issue — will work automatically once upstream enables the `noise` feature.

## 17. Non-Uniform: Catseye Parameters (Phase 6)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 17.1 | Non-Uniform page is displayed in UI | PASS | NUKE: "Non-Uniform" tab, Flame: displayed in "Page 3" |
| 17.2 | Catseye Enable parameter is displayed in UI | PASS | Default: false |
| 17.3 | Catseye sub-parameters (6) are grayed out when Catseye Enable=false | PASS | Amount, Inverse, InverseForeground, Gamma, Softness, DimensionBased |
| 17.4 | Catseye sub-parameters (6) are enabled when Catseye Enable=true | PASS | |
| 17.5 | Catseye Amount parameter is displayed in UI | PASS | Default: 0.5, Range: 0–2 |
| 17.6 | Changing Catseye Amount changes Catseye effect intensity | PASS | When Enable=true |
| 17.7 | Catseye Inverse parameter is displayed in UI | PASS | Default: false |
| 17.8 | Catseye Inverse Foreground parameter is displayed in UI | PASS | Default: true |
| 17.9 | Catseye Gamma parameter is displayed in UI | PASS | Default: 1.0, Range: 0.2–4.0 |
| 17.10 | Changing Catseye Gamma changes Catseye falloff curve | PASS | When Enable=true |
| 17.11 | Catseye Softness parameter is displayed in UI | PASS | Default: 0.2, Range: 0.01–1.0 |
| 17.12 | Changing Catseye Softness changes Catseye transition width | PASS | When Enable=true |
| 17.13 | Catseye Dimension Based parameter is displayed in UI | PASS | Default: false |

## 18. Non-Uniform: Barndoors Parameters (Phase 6)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 18.1 | Barndoors Enable parameter is displayed in UI | PASS | Default: false |
| 18.2 | Barndoors sub-parameters (8) are grayed out when Barndoors Enable=false | PASS | Amount, Inverse, InverseForeground, Gamma, Top, Bottom, Left, Right |
| 18.3 | Barndoors sub-parameters (8) are enabled when Barndoors Enable=true | PASS | |
| 18.4 | Barndoors Amount parameter is displayed in UI | PASS | Default: 0.5, Range: 0–2 |
| 18.5 | Changing Barndoors Amount changes Barndoors effect intensity | PASS | When Enable=true |
| 18.6 | Barndoors Inverse parameter is displayed in UI | PASS | Default: false |
| 18.7 | Barndoors Inverse Foreground parameter is displayed in UI | PASS | Default: true |
| 18.8 | Barndoors Gamma parameter is displayed in UI | PASS | Default: 1.0, Range: 0.2–4.0 |
| 18.9 | Changing Barndoors Gamma changes Barndoors falloff curve | PASS | When Enable=true |
| 18.10 | Barndoors Top parameter is displayed in UI | PASS | Default: 100.0, Range: 0–100 |
| 18.11 | Barndoors Bottom parameter is displayed in UI | PASS | Default: 100.0, Range: 0–100 |
| 18.12 | Barndoors Left parameter is displayed in UI | PASS | Default: 100.0, Range: 0–100 |
| 18.13 | Barndoors Right parameter is displayed in UI | PASS | Default: 100.0, Range: 0–100 |
| 18.14 | Changing Barndoors Top/Bottom/Left/Right changes edge defocus range | PASS | When Enable=true |

## 20. Non-Uniform: Astigmatism Parameters (Phase 7)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 20.1 | Astigmatism Enable parameter is displayed in UI | PASS | Default: false |
| 20.2 | Astigmatism sub-parameters (2) are grayed out when Astigmatism Enable=false | PASS | Amount, Gamma |
| 20.3 | Astigmatism sub-parameters (2) are enabled when Astigmatism Enable=true | PASS | |
| 20.4 | Astigmatism Amount parameter is displayed in UI | PASS | Default: 0.5, Range: 0–1 |
| 20.5 | Changing Astigmatism Amount changes Astigmatism effect intensity | PASS | When Enable=true |
| 20.6 | Astigmatism Gamma parameter is displayed in UI | PASS | Default: 1.0, Range: 0.2–4.0 |
| 20.7 | Changing Astigmatism Gamma changes Astigmatism falloff curve | PASS | When Enable=true |

## 21. Non-Uniform: Axial Aberration Parameters (Phase 7)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 21.1 | Axial Aberration Enable parameter is displayed in UI | PASS | Default: false |
| 21.2 | Axial Aberration sub-parameters (2) are grayed out when Axial Aberration Enable=false | PASS | Amount, Type |
| 21.3 | Axial Aberration sub-parameters (2) are enabled when Axial Aberration Enable=true | PASS | |
| 21.4 | Axial Aberration Amount parameter is displayed in UI | PASS | Default: 0.5, Range: -1–1 |
| 21.5 | Changing Axial Aberration Amount changes chromatic aberration effect intensity | PASS | When Enable=true |
| 21.6 | Axial Aberration Type parameter is displayed in UI (Red/Blue, Blue/Yellow, Green/Purple) | PASS | Default: Red/Blue |
| 21.7 | Changing Axial Aberration Type changes chromatic aberration color combination | DEFERRED | Upstream bug — off-by-one mismatch between protobuf enum (0,1,2) and internal Rust enum (1,2,3) causes all types to fall back to RedBlue. NDK version has same symptom |

> **21.7 Analysis**: Upstream Rust core investigation: protobuf defines `AxialAberrationType` as `RED_BLUE=0, BLUE_YELLOW=1, GREEN_PURPLE=2`. However, the internal `AxialAberrationType` enum is `#[repr(u32)]` with `RedBlue=1, BlueYellow=2, GreenPurple=3` (`opendefocus-shared/src/internal_settings.rs` lines 66-74). The conversion match statement (lines 184-189) expects `1=>RedBlue, 2=>BlueYellow, 3=>GreenPurple`, so values `0, 1, 2` from protobuf all fall through to the default branch `_ => RedBlue`. The rendering functions themselves (`axial_aberration.rs`) correctly implement all 3 colors, but the correct branches are never reached. NDK version uses the same Rust core and exhibits the same symptom. Not an OFX issue.

## 22. Non-Uniform: Inverse Foreground Parameter (Phase 7)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 22.1 | Inverse Foreground parameter is displayed on Non-Uniform page | PASS | Default: true |
| 22.2 | Inverse Foreground is always enabled (no grayout) | PASS | Global flag that does not require Enable |

## 24. GPU Rendering (Phase 8)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 24.1 | Plugin loads normally (GPU-enabled build) | PASS | Both Flame and NUKE |
| 24.2 | Rendering completes in 2D mode | PASS | No crash or hang |
| 24.3 | Rendering completes in Depth mode | PASS | No crash or hang |
| 24.4 | 2D mode output matches CPU version | FAIL | Visual comparison with pre-GPU rendering results. No CPU mode toggle available. Previous version comparison shows near-identical results |
| 24.5 | Depth mode output matches CPU version | FAIL | Visual comparison with pre-GPU rendering results. No CPU mode toggle available. Previous version comparison shows near-identical results |
| 24.6 | Filter Type: Simple renders correctly | PASS | |
| 24.7 | Filter Type: Disc renders correctly | PASS | |
| 24.8 | Filter Type: Blade renders correctly | PASS | |
| 24.9 | Filter Preview displays correctly | PASS | Confirmed display in both 2D/Depth modes. Preview size changes correctly with Size parameter. |
| 24.10 | Catseye Enable=true renders correctly | PASS | No black output |
| 24.11 | Barndoors Enable=true renders correctly | PASS | No black output |
| 24.12 | Astigmatism Enable=true renders correctly | PASS | |
| 24.13 | Axial Aberration Enable=true renders correctly | PASS | |
| 24.14 | Real-time update when changing parameters | PASS | Major parameters like Size, Focus Plane |
| 24.15 | CPU fallback on GPU-unsupported environment | FAIL | Only testable with GPU-less environment. GPU environment (Linux) only. |

## 25. Filter Type: Image — Custom Bokeh Image Input (Phase 9)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 25.1 | Filter clip is displayed in NUKE / Flame | PASS | Recognized as optional input |
| 25.2 | Filter Type = Image: rendering completes with image connected to Filter clip | PASS | No crash or hang |
| 25.3 | Rendering result reflects the connected Bokeh image shape | PASS | Visual comparison with Disc/Blade |
| 25.4 | Behavior when Filter clip is not connected with Filter Type = Image | PASS | Error handling or fallback. No bokeh applied -> NUKE console output: [2026-02-27T21:49:01Z ERROR opendefocus_ofx_bridge] Render failed: No filter provided but 'image' selected as filter |
| 25.5 | Normal operation with Filter Type = Simple/Disc/Blade when Filter clip is not used | PASS | No impact on existing behavior |
| 25.6 | Filter Preview is disabled (skipped) when Filter Type = Image | PASS | Only Disc/Blade support Preview |
| 25.7 | 2D mode + Filter Type = Image renders correctly | PASS | |
| 25.8 | Depth mode + Filter Type = Image renders correctly | PASS | |
| 25.9 | Dev version display updated to "Phase 9: Filter Image" | PASS | Controls tab header. Tested with latest version v0.1.10-OFX-v1 (Phase 10: RenderScale + RoI + UseGPU) |

## 26. GPU Stabilization — 4K Crash Fix & Runtime CPU Fallback (Phase 8 Supplement)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 26.1 | Rendering completes normally with HD (1920x1080) footage | PASS | No crash or hang |
| 26.2 | Rendering completes normally with 2K (2048x1080) footage | PASS | No crash or hang |
| 26.3 | Rendering completes normally with 4K UHD (3840x2160) footage | PASS | Auto CPU fallback on GPU failure. CPU switchover confirmed |
| 26.4 | Rendering completes normally with 4K DCI (4096x2160) footage | PASS | Auto CPU fallback on GPU failure. CPU switchover confirmed |
| 26.5 | Automatic CPU fallback on GPU rendering failure | PASS | Log should show "GPU render failed" → "recreating renderer as CPU-only". Confirmed in NUKE console |
| 26.6 | Continuous rendering works normally after CPU fallback | PASS | Stable operation with parameter changes and frame scrubbing |
| 26.7 | Output image is normal after CPU fallback | PASS | No black output or artifacts |
| 26.8 | od_is_gpu_active returns false after fallback | PASS | B2 fix: env_logger default filter set to info. "Renderer recreated: CPU/GPU" log output confirmed |
| 26.9 | GPU continues to be used at resolutions where GPU succeeds (HD/2K) | PASS | No unnecessary CPU fallback |

## 27. Render Scale Correction + RoI Extension (Phase 10)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 27.1 | Defocus is applied correctly at full resolution | PASS | No regression in existing behavior |
| 27.2 | 1/2 proxy mode defocus appearance matches full resolution | PASS | Blur amount is neither excessive nor insufficient |
| 27.3 | 1/4 proxy mode defocus appearance matches full resolution | PASS | |
| 27.4 | No clipping (black border) at image edges with large Size (50+) | PASS | RoI extension functions correctly |
| 27.5 | Image edges render correctly in Depth mode + large Max Size | PASS | RoI extended by max(size, maxSize) |
| 27.6 | Image edges render correctly with large Size Multiplier | FAIL | Bokeh breaks down or partial gray areas appear. Original NDK version has similar symptoms (not exactly the same). No symptoms when doubling bokeh via Size/MaxSize |
| 27.7 | Filter Type = Image + RoI extension renders correctly | PASS | Combined with Filter clip |
| 27.8 | Filter Preview displays correctly in proxy mode | PASS | Fixed: bypass stripe splitting for preview (get_stripe_height returns image_height). Proxy mode scaling fixed (fRes scaled by renderScale). Retest passed |
| 27.9 | Flame renders correctly when switching proxy mode | | Same behavior as NUKE |
| 27.10 | No crash and correct rendering when panning viewer | PASS | Handles srcBounds and renderWindow offset |
| 27.11 | No crash when zooming in (partial display) viewer | PASS | When renderWindow covers only part of image |
| 27.12 | Stable operation with proxy mode + viewer pan combination | PASS | Most prone to bounds misalignment |
| 27.13 | No black area proportional to blur size at left/bottom edges of image | PASS | Clamp to Edge fix verification. Both 2D/Depth modes |
| 27.14 | No black area at right/top edges of image | PASS | Confirmed on all 4 edges |
| 27.15 | NDK and OFX version edge processing matches | PASS | Compare edge pixel blur appearance |
| 27.16 | No unnatural focus changes near edges in Depth mode | PASS | Depth Clamp to Edge functions correctly |
| 27.17 | No crash when panning image completely off-screen | PASS | Extreme case where fetchWindow and srcBounds do not intersect |
| 27.18 | No black border with Crop/AdjBBox intentionally reducing BBox | PASS | Clamp to Edge verification when srcBounds is smaller than image |

## 28. Use GPU Parameter — CPU/GPU Toggle (Phase 10)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 28.1 | Use GPU parameter is displayed on Controls tab | PASS | Default: true (checkbox) |
| 28.2 | Rendering completes normally with Use GPU = true (GPU) | PASS | HD/2K resolution |
| 28.3 | Rendering completes normally with Use GPU = false (CPU) | PASS | HD/2K resolution |
| 28.4 | Renderer is recreated as CPU when switching Use GPU true → false | PASS | Fix: env_logger default filter set to info. Log output confirmed. Flame GPU/CPU switch stable (initial crash report was Ctrl+C misoperation) |
| 28.5 | Renderer is recreated as GPU when switching Use GPU false → true | PASS | Fix: env_logger default filter set to info. Log output confirmed. Retest passed |
| 28.6 | CPU mode and GPU mode output results match | FAIL | ~1px pixel drift observed — similar behavior to original NDK version (NUKE/Flame) |
| 28.7 | 4K rendering completes normally in CPU mode | PASS | No GPU memory limitation impact |
| 28.8 | GPU recovers when setting Use GPU = true after auto fallback | PASS | gpu_failed flag is reset |
| 28.9 | No crash when toggling Use GPU continuously | PASS | Stability check |
| 28.10 | Use GPU parameter works correctly in Flame | PASS | Same behavior as NUKE |

## 29. Thread Safety (Multi-Frame Rendering)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 29.1 | NUKE Flipbook rendering (continuous frames) completes normally | PASS | 24-frame duration / HD footage with Focus Plane animation keyframes, rendered at both full and proxy resolution |
| 29.2 | NUKE Write node batch rendering completes normally | PASS | 24-frame duration / HD footage with Focus Plane animation keyframes, EXR render via Write node completed |
| 29.3 | No parameter cross-contamination between frames during multi-frame rendering | PASS | 24-frame duration / HD footage with Focus Plane animation keyframes, EXR render via Write node completed |
| 29.4 | No crash when changing parameters in UI during background rendering | PASS | Knob manipulation during NUKE rendering |
| 29.5 | Flame batch rendering completes normally | PASS | Executed in Flame render queue |

## 30. Focus Point XY Picker (Phase 11)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 30.1 | Plugin loads normally in NUKE / Flame | PASS | No constructor error |
| 30.2 | "Use Focus Point" and "Focus Point XY" are displayed on Controls tab | PASS | |
| 30.3 | Mode=2D → Use Focus Point is grayed out | PASS | |
| 30.4 | Mode=Depth + Use Focus Point OFF → Focus Point XY is grayed out | PASS | |
| 30.5 | Mode=Depth + Use Focus Point ON → Crosshair (cross + small square) is displayed in viewer | PASS | Custom OpenGL drawing (common across all hosts) |
| 30.6 | Dragging XY → Focus follows depth value | PASS | Depth connection required |
| 30.7 | Dragging XY off-screen → No crash | PASS | Boundary check |
| 30.8 | Clicking on Depth == 0 area → Focus Plane does not change | PASS | NDK compliant: depth==0 skip |
| 30.9 | Sampling works correctly in proxy mode (1/2, 1/4) | PASS | renderScale applied |
| 30.10 | Tested on both NUKE / Flame | PASS | Same behavior confirmed on both NUKE and Flame |
| 30.11 | Crosshair color changes from white to green on mouse hover (Poised state) | PASS | OpenGL state transition |
| 30.12 | Crosshair turns yellow during click (Picked state) | PASS | Drag feedback |
| 30.13 | Use Focus Point OFF → Crosshair is hidden and mouse events are not consumed | PASS | Flame performance degradation prevention |
| 30.14 | Flame performance (viewer interaction responsiveness within acceptable range) | PASS | Re-verification of previous degradation issue. Significantly improved from before. However, crosshair drag response feels heavier than NUKE. |
| 30.15 | Dragging crosshair → Focus Plane knob value is updated | PASS | Sampled in changedParam |
| 30.16 | Use Focus Point ON/OFF toggle → Focus Plane value is preserved (focus does not change) | PASS | Knob value persisted via setValue |
| 30.17 | Keyframes can be set on Focus Plane (with sampled value reflected) | PASS | Feature parity with NDK version |
| 30.18 | Crosshair size 100px provides sufficient visibility (HD / UHD) | PASS | Enlarged from 20px to 100px |
| 30.19 | Crosshair drag responsiveness in Flame | PASS | Impact check for fetchImage in changedParam. Still noticeably slower compared to NUKE, but significantly improved from previous iteration. |

## 32. Stripe-Based Rendering (Performance Optimization Phase 1)

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 32.1 | HD (1920×1080) GPU rendering produces equivalent output to existing results | PASS | Compare with NDK original, or cross-compare Quality levels (Low/Medium/High) for identical output |
| 32.2 | HD (1920×1080) CPU rendering produces equivalent output to existing results | PASS | Use GPU=false. Compare with NDK original, or cross-compare Quality levels |
| 32.3 | UHD (3840×2160) GPU rendering succeeds | PASS | Previously failed due to wgpu storage buffer limit exceeded. UHD GPU rendering now possible. |
| 32.4 | UHD (3840×2160) CPU rendering completes at practical speed | PASS | No hang or extreme delay |
| 32.5 | Quality=Low (stripe 256px) renders correctly | PASS | No seams at stripe boundaries |
| 32.6 | Quality=Medium (stripe 128px) renders correctly | PASS | No seams at stripe boundaries |
| 32.7 | Quality=High (stripe 64px) renders correctly | PASS | No seams at stripe boundaries |
| 32.8 | Mode=2D (no Depth) stripe splitting works correctly | PASS | |
| 32.9 | Mode=Depth + RenderResult=Focal Plane Setup (stripe 32px) renders correctly | PASS | |
| 32.10 | Filter Type=Image: filter applied consistently across stripes | DEFERRED | Flame Filter Image limitation (upstream) and aspect ratio distortion. Not a stripe-specific issue |
| 32.11 | Catseye Enable=true: continuous stripe boundaries (no seams) | PASS | Non-uniform bokeh stripe boundary verification |
| 32.12 | Barndoors Enable=true: continuous stripe boundaries (no seams) | PASS | Non-uniform bokeh stripe boundary verification |
| 32.12a | Astigmatism Enable=true: continuous stripe boundaries (no seams) | PASS | Non-uniform bokeh stripe boundary verification. Global coordinate fix verified |
| 32.13 | Proxy mode (1/2, 1/4) renderScale correctly applied | PASS | Combination with stripe splitting |
| 32.14 | Abort during rendering is reflected between stripes | PASS | NUKE: abort callback works correctly. Flame: UI is blocked during rendering, abort() never returns true — host limitation (N/A) |
| 32.14a | Output after abort is unprocessed source image | PASS | NUKE: confirmed. Flame: N/A due to 32.14 host limitation |
| 32.14b | No crash or hang after abort | PASS | NUKE: confirmed. Flame: N/A due to 32.14 host limitation |
| 32.14c | Filter Preview still works after abort implementation | PASS | Preview path uses nullptr callback — normal operation confirmed |
| 32.15 | No crash with extreme bokeh size (Size=500+) with large padding | PASS | Rendering takes time but completes without crash. Padding > stripe height case |
| 32.16 | CPU fallback on GPU failure works correctly within stripe loop | N/A | Stripe splitting resolves root cause (buffer size), GPU succeeds even at 10K+. Cannot trigger fallback. 12K hits NUKE host buffer limit ("Asked for too-large image input") |
| 32.17 | Multi-frame rendering (Flipbook/Write) with stripe splitting operates stably | PASS | No crash or data cross-contamination between frames |
| 32.18 | Stripe-based rendering works correctly in Flame | DEFERRED | Flame Filter Image platform limitation. Non-Filter-Image rendering works correctly |

### 33. Phase D: Stripe Performance Optimization

| # | Test Item | Result | Notes |
|---|-----------|--------|-------|
| 33.1 | HD (1920×1080) GPU rendering produces identical output to pre-optimization | PASS | NDK comparison also performed |
| 33.2 | HD (1920×1080) CPU rendering produces identical output to pre-optimization | PASS | NDK comparison also performed |
| 33.3 | UHD (3840×2160) GPU rendering completes successfully | PASS | No wgpu buffer limit issues |
| 33.4 | Quality=Low/Medium/High all render correctly | PASS | Stripe height changes verified |
| 33.5 | Catseye/Barndoors/Astigmatism: no seams at stripe boundaries | PASS | Upstream ChunkHandler seam discovered at >4096px — unrelated to Phase D, recorded as Known Issue #23 |
| 33.6 | Filter Preview operates normally | PASS | Preview stripe height unchanged (full height) |
| 33.7 | Depth mode + Focal Plane Setup renders correctly | PASS | FocalPlaneSetup stripe height unchanged (32) |
| 33.8 | Proxy mode (1/2, 1/4) operates correctly | PASS | Rendering and Filter Preview confirmed |
| 33.9 | Abort works correctly between stripes | PASS | Phase C abort callback compatible |
| 33.10 | Flame GUI responsiveness is improved | PASS | Noticeable improvement, still slower than NUKE |
| 33.11 | Multi-frame rendering (Flipbook/Write) stable | PASS | HD Depth mode, Focal Plane animation, 120 frames — no crash or render error |

## 31. Known Constraints (Out of Scope for This Version)

The following are not implemented in v0.1.10-OFX-v1 and are out of test scope:

- Tiling (`setSupportsTiles(false)`)
- Camera mode (Camera input clip + camera_data parameter group)
- Custom Stripe Height (NUKE NDK-specific stripe splitting mechanism. Requires render() architecture changes for OFX)

---

## FAIL Item Summary (Phase 2 UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 1.1 | Flame node name display | Grouping settings | PASS — Changed to `"OpenDefocusOFX"` |
| 1.4 | Description text display | Host-dependent | N/A — No action needed |
| 4.3 | 2D mode NDK comparison | Pixel drift | DEFERRED — To be re-verified after upstream investigation |
| 5.4 | Depth mode NDK comparison | Pixel drift | DEFERRED — To be re-verified after upstream investigation |
| 5.5 | Depth Alpha input | Not needed in practice | N/A — Both Flame/NUKE input Depth as RGBA |
| 6.1 | Artifacts | Pixel drift | DEFERRED — To be re-verified after upstream investigation |
| 7.1–7.3 | NDK comparison | Pixel drift | DEFERRED — To be re-verified after upstream investigation |

## FAIL Item Summary (Phase 3 UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 10.5 | Filter Preview overflow | Rendering buffer size | PASS — Fixed to center placement at filter_resolution size. Retest passed |
| 10.6 | Grayout cannot be restored | Parameter visibility | PASS — Grayout removed (NUKE NDK compliant). Retest passed |

## FAIL Item Summary (Phase 4 UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 13.12 | Gamma Correction has no effect | Upstream dead field | DEFERRED — Protobuf definition only. NDK version also has no knob created and not connected to pipeline |
| 15.5 | Focal Plane Offset has no effect | Upstream unimplemented | DEFERRED — NDK version has knob created but not connected to pipeline. NDK version has same symptom |

## FAIL Item Summary (Phase 5 UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 16.2 | Noise Size has no effect | Upstream `noise` feature disabled | DEFERRED — Implementation exists in bokeh_creator but compiled out by feature flag. NDK version also affected |
| 16.4 | Noise Intensity has no effect | Same as above | DEFERRED |
| 16.6 | Noise Seed has no effect | Same as above | DEFERRED |
| 16.8 | Noise not reflected in Filter Preview | Same as above | DEFERRED |

## FAIL Item Summary (Phase 7 UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 21.7 | Axial Aberration Type switch has no color change | Upstream enum off-by-one bug | DEFERRED — Off-by-one between protobuf (0,1,2) and internal Rust enum (1,2,3) causes all types to fall back to RedBlue. NDK version has same symptom |

## FAIL Item Summary (Phase 8 UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 24.4 | 2D mode comparison with CPU version | Test environment constraint | N/A — No CPU/GPU toggle. Visual comparison with previous version shows near-identical results |
| 24.5 | Depth mode comparison with CPU version | Test environment constraint | N/A — Same as above |
| 24.9 | Filter Preview black screen | Depth mode validate error | PASS — Fixed by setting od_set_defocus_mode(TWO_D) before preview. Retest passed |
| 24.15 | CPU fallback on GPU-unsupported environment | Test environment constraint | N/A — GPU environment (Linux) only. Cannot verify |

## FAIL Item Summary (Stripe-Based Rendering UAT)

| # | Item | Category | Status |
|---|------|----------|--------|
| 32.10 | Filter Type=Image stripe consistency | Flame platform limitation | DEFERRED — Flame Filter Image resolution mix error and upstream aspect ratio distortion. Not stripe-specific |
| 32.16 | GPU fallback in stripe loop | Test expectation update | N/A — Stripe splitting resolves buffer size issue, GPU succeeds even at 10K+. 12K error is NUKE host buffer limit |
| 32.18 | Flame stripe rendering | Flame platform limitation | DEFERRED — Flame Filter Image limitation. Non-Filter-Image rendering works correctly |

### Status Legend

| Status | Meaning |
|--------|---------|
| PASS | Test passed |
| FAIL | Test failed — fix required |
| RETEST | Fixed — awaiting retest |
| DEFERRED | To be re-verified later — pending external factors such as upstream investigation |
| N/A | No action needed — host-dependent specification or does not occur in practice |

### Pixel Drift Issue

A ~1px pixel offset was confirmed between NUKE NDK and OFX versions. The OFX version complies with the OFX standard coordinate system and operates correctly.

**Additional Finding**: The original NUKE NDK version has GPU rendering enabled by default. Whether the pixel drift is caused by GPU rendering cannot be determined until a CPU/GPU toggle is implemented for comparison (the ~1px difference is too small for visual confirmation). Not a release blocker for the OFX version.


---

## Verdict

### Phase 2 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 25 2026 |
| Verdicted By | Hiroshi |
| Notes | No FAIL items (resolved as fix PASS / N/A / DEFERRED). Pixel drift is non-blocking |

### Phase 3 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 25 2026 |
| Verdicted By | Hiroshi |
| Notes | Quality + Bokeh parameter verification complete. 10.5, 10.6 passed after fix. No FAIL items |

### Phase 4 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 26 2026 |
| Verdicted By | Hiroshi |
| Notes | Defocus general + Advanced parameter verification complete. 13.12 (Gamma Correction), 15.5 (Focal Plane Offset) are dead fields/unimplemented in upstream Rust core. Not blockers for OFX version. Classified as DEFERRED. No OFX-side FAIL items |

### Phase 5 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 26 2026 |
| Verdicted By | Hiroshi |
| Notes | Bokeh Noise parameter verification complete. 16.2/16.4/16.6/16.8 (Noise has no effect) caused by upstream bokeh_creator `noise` feature flag being disabled. Implementation exists but compiled out. NDK version also has no effect under same conditions. Not a blocker for OFX version. Classified as DEFERRED. No OFX-side FAIL items |

### Phase 6 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 27 2026 |
| Verdicted By | Hiroshi |
| Notes | Catseye + Barndoors parameter verification complete. Fixed black output bug caused by missing od_set_center call. All 27 items PASS. No OFX-side FAIL items |

### Phase 7 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 27 2026 |
| Verdicted By | Hiroshi |
| Notes | Astigmatism + Axial Aberration + Inverse Foreground verification complete. 15 PASS / 1 DEFERRED. 21.7 (Axial Aberration Type) is upstream enum off-by-one bug. No OFX-side FAIL items |

### Phase 8 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Feb 27 2026 |
| Verdicted By | Hiroshi |
| Notes | GPU rendering (wgpu) verification complete. 13 PASS / 2 N/A (test environment constraint). 24.9 (Filter Preview black screen) caused by validate error in Depth mode, passed after fix. Dev version display feature also confirmed. Performance significantly improved with GPU support, Quality High mode now runs comfortably |

### Phase 9 + Phase 10 UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Mar 1 2026 |
| Verdicted By | Hiroshi |
| Notes | Filter Image (9 PASS), GPU Stabilization (8 PASS / 1 unknown), RenderScale+RoI (15 PASS / 2 FAIL / 1 untested), Use GPU (7 PASS / 3 FAIL), Thread Safety (5 PASS). FAIL: 27.6 SizeMultiplier bokeh breakdown (DEFERRED/upstream), 27.8 Filter Preview overflow (needs investigation), 28.4/28.5 log not output (low priority), 28.6 CPU/GPU pixel drift (DEFERRED/upstream). OFX porting mission complete |

### Stripe-Based Rendering UAT

| Item | Result |
|------|--------|
| Overall Verdict | PASS |
| Verdict Date | Mar 13 2026 |
| Verdicted By | Hiroshi |
| Notes | Stripe-based rendering (14 PASS / 3 DEFERRED / 1 N/A). UHD GPU rendering now works. All position-dependent effects (catseye, barndoors, astigmatism) seamless across stripe boundaries. DEFERRED: 32.10/32.18 are Flame Filter Image platform limitation, not stripe-specific. 32.14 abort unimplemented (Known Issue #17, Phase 2). N/A: 32.16 GPU fallback cannot be triggered (stripe splitting resolves root cause) |
