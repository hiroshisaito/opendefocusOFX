// OpenDefocus OFX Plugin
// v0.1.10-OFX-v1
//
// Porting of OpenDefocus (Nuke NDK) to OpenFX standard.
// This plugin calls into the OpenDefocus Rust core via extern "C" FFI bridge.
//
// Development version is displayed in the UI via kParamDevVersion parameter.

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsInteract.h"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <cmath>

extern "C" {
#include "opendefocus_ofx_bridge.h"
}

#include <cstring>
#include <vector>
#include <memory>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const char* kPluginName       = "OpenDefocusOFX";
static const char* kPluginGrouping   = "OpenDefocusOFX";
static const char* kPluginIdentifier = "com.opendefocus.ofx";
static const int   kPluginVersionMajor = 0;
static const int   kPluginVersionMinor = 1;

// Development version string — update on each dev build
static const char* kDevVersion = "v0.1.10-OFX-v2-dev (Phase C: Abort Callback)";
static const char* kParamDevVersion = "devVersion";

static const char* kClipSource = kOfxImageEffectSimpleSourceClipName;
static const char* kClipDepth  = "Depth";
static const char* kClipFilter = "Filter";
static const char* kClipOutput = kOfxImageEffectOutputClipName;

// Parameter names — Controls
static const char* kParamUseGpu     = "useGpu";
static const char* kParamSize       = "size";
static const char* kParamFocusPlane = "focusPlane";
static const char* kParamQuality    = "quality";
static const char* kParamSamples    = "samples";

// Parameter names — Defocus / Advanced
static const char* kParamMode              = "mode";
static const char* kParamMath              = "math";
static const char* kParamRenderResult      = "renderResult";
static const char* kParamShowImage         = "showImage";
static const char* kParamProtect           = "protect";
static const char* kParamMaxSize           = "maxSize";
static const char* kParamGammaCorrection   = "gammaCorrection";
static const char* kParamFarmQuality       = "farmQuality";
static const char* kParamSizeMultiplier    = "sizeMultiplier";
static const char* kParamFocalPlaneOffset  = "focalPlaneOffset";

// Parameter names — Bokeh
static const char* kParamFilterType       = "filterType";
static const char* kParamFilterPreview    = "filterPreview";
static const char* kParamFilterResolution = "filterResolution";
static const char* kParamRingColor        = "ringColor";
static const char* kParamInnerColor       = "innerColor";
static const char* kParamRingSize         = "ringSize";
static const char* kParamOuterBlur        = "outerBlur";
static const char* kParamInnerBlur        = "innerBlur";
static const char* kParamAspectRatio      = "aspectRatio";
static const char* kParamBlades           = "blades";
static const char* kParamAngle            = "angle";
static const char* kParamCurvature        = "curvature";
static const char* kParamNoiseSize        = "noiseSize";
static const char* kParamNoiseIntensity   = "noiseIntensity";
static const char* kParamNoiseSeed        = "noiseSeed";

// Parameter names — Non-Uniform: Catseye
static const char* kParamCatseyeEnable             = "catseyeEnable";
static const char* kParamCatseyeAmount             = "catseyeAmount";
static const char* kParamCatseyeInverse            = "catseyeInverse";
static const char* kParamCatseyeInverseForeground  = "catseyeInverseForeground";
static const char* kParamCatseyeGamma              = "catseyeGamma";
static const char* kParamCatseyeSoftness           = "catseyeSoftness";
static const char* kParamCatseyeDimensionBased     = "catseyeDimensionBased";

// Parameter names — Non-Uniform: Barndoors
static const char* kParamBarndoorsEnable             = "barndoorsEnable";
static const char* kParamBarndoorsAmount             = "barndoorsAmount";
static const char* kParamBarndoorsInverse            = "barndoorsInverse";
static const char* kParamBarndoorsInverseForeground  = "barndoorsInverseForeground";
static const char* kParamBarndoorsGamma              = "barndoorsGamma";
static const char* kParamBarndoorsTop                = "barndoorsTop";
static const char* kParamBarndoorsBottom             = "barndoorsBottom";
static const char* kParamBarndoorsLeft               = "barndoorsLeft";
static const char* kParamBarndoorsRight              = "barndoorsRight";

// Parameter names — Non-Uniform: Astigmatism
static const char* kParamAstigmatismEnable  = "astigmatismEnable";
static const char* kParamAstigmatismAmount  = "astigmatismAmount";
static const char* kParamAstigmatismGamma   = "astigmatismGamma";

// Parameter names — Non-Uniform: Axial Aberration
static const char* kParamAxialAberrationEnable = "axialAberrationEnable";
static const char* kParamAxialAberrationAmount = "axialAberrationAmount";
static const char* kParamAxialAberrationType   = "axialAberrationType";

// Parameter names — Non-Uniform: Global
static const char* kParamInverseForeground = "inverseForeground";

// Parameter names — Focus Point
static const char* kParamUseFocusPoint = "useFocusPoint";
static const char* kParamFocusPointXY  = "focusPointXY";

// ---------------------------------------------------------------------------
// Overlay: OpenGL crosshair for Focus Point XY Picker
// ---------------------------------------------------------------------------
// Crosshair size in screen pixels
static const double kCrosshairScreenPx = 100.0;

class OpenDefocusOverlay : public OFX::OverlayInteract {
protected:
    enum StateEnum { eInActive, ePoised, ePicked };
    StateEnum state_;
    OFX::Double2DParam* position_;
    OFX::BooleanParam*  useFocusPoint_;

public:
    OpenDefocusOverlay(OfxInteractHandle handle, OFX::ImageEffect* effect)
        : OFX::OverlayInteract(handle)
        , state_(eInActive)
    {
        position_      = effect->fetchDouble2DParam("focusPointXY");
        useFocusPoint_ = effect->fetchBooleanParam("useFocusPoint");
    }

    OfxPointD getPos(double time) const {
        OfxPointD p;
        position_->getValueAtTime(time, p.x, p.y);
        return p;
    }

    bool draw(const OFX::DrawArgs& args) override {
        bool enabled = false;
        useFocusPoint_->getValueAtTime(args.time, enabled);
        if (!enabled) return false;

        OfxPointD pos = getPos(args.time);

        OfxRGBColourF col;
        switch (state_) {
        case eInActive: col.r = 1.0f; col.g = 1.0f; col.b = 1.0f; break;
        case ePoised:   col.r = 0.0f; col.g = 1.0f; col.b = 0.0f; break;
        case ePicked:   col.r = 1.0f; col.g = 1.0f; col.b = 0.0f; break;
        }

        float dx = static_cast<float>(kCrosshairScreenPx / args.pixelScale.x);
        float dy = static_cast<float>(kCrosshairScreenPx / args.pixelScale.y);

        glPushMatrix();
        glColor3f(col.r, col.g, col.b);
        glTranslated(pos.x, pos.y, 0);
        // Cross lines
        glBegin(GL_LINES);
        glVertex2f(-dx, 0); glVertex2f(dx, 0);
        glVertex2f(0, -dy); glVertex2f(0, dy);
        glEnd();
        // Small square
        glBegin(GL_LINE_LOOP);
        glVertex2f(-dx * 0.3f, -dy * 0.3f);
        glVertex2f(-dx * 0.3f,  dy * 0.3f);
        glVertex2f( dx * 0.3f,  dy * 0.3f);
        glVertex2f( dx * 0.3f, -dy * 0.3f);
        glEnd();
        glPopMatrix();
        return true;
    }

    bool penMotion(const OFX::PenArgs& args) override {
        bool enabled = false;
        useFocusPoint_->getValueAtTime(args.time, enabled);
        if (!enabled) return false;

        float dx = static_cast<float>(kCrosshairScreenPx / args.pixelScale.x);
        float dy = static_cast<float>(kCrosshairScreenPx / args.pixelScale.y);
        OfxPointD pos = getPos(args.time);

        switch (state_) {
        case eInActive:
        case ePoised: {
            StateEnum newState;
            double relX = args.penPosition.x - pos.x;
            double relY = args.penPosition.y - pos.y;
            if (std::abs(relX) < dx && std::abs(relY) < dy)
                newState = ePoised;
            else
                newState = eInActive;
            if (state_ != newState) {
                state_ = newState;
                _effect->redrawOverlays();
            }
            break;
        }
        case ePicked:
            position_->setValue(args.penPosition.x, args.penPosition.y);
            _effect->redrawOverlays();
            break;
        }
        return state_ != eInActive;
    }

    bool penDown(const OFX::PenArgs& args) override {
        bool enabled = false;
        useFocusPoint_->getValueAtTime(args.time, enabled);
        if (!enabled) return false;

        penMotion(args);
        if (state_ == ePoised) {
            state_ = ePicked;
            position_->setValue(args.penPosition.x, args.penPosition.y);
            _effect->redrawOverlays();
        }
        return state_ == ePicked;
    }

    bool penUp(const OFX::PenArgs& args) override {
        if (state_ == ePicked) {
            state_ = ePoised;
            penMotion(args);
            _effect->redrawOverlays();
            return true;
        }
        return false;
    }
};

class OpenDefocusOverlayDescriptor
    : public OFX::DefaultEffectOverlayDescriptor<OpenDefocusOverlayDescriptor, OpenDefocusOverlay> {};

// ---------------------------------------------------------------------------
// Plugin: main ImageEffect implementation
// ---------------------------------------------------------------------------
class OpenDefocusPlugin : public OFX::ImageEffect {
public:
    explicit OpenDefocusPlugin(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle)
        , rustHandle_(nullptr)
    {
        srcClip_    = fetchClip(kClipSource);
        depthClip_  = fetchClip(kClipDepth);
        filterClip_ = fetchClip(kClipFilter);
        dstClip_    = fetchClip(kClipOutput);

        useGpuParam_     = fetchBooleanParam(kParamUseGpu);
        sizeParam_       = fetchDoubleParam(kParamSize);
        focusPlaneParam_ = fetchDoubleParam(kParamFocusPlane);
        qualityParam_    = fetchChoiceParam(kParamQuality);
        samplesParam_    = fetchIntParam(kParamSamples);

        modeParam_             = fetchChoiceParam(kParamMode);
        mathParam_             = fetchChoiceParam(kParamMath);
        renderResultParam_     = fetchChoiceParam(kParamRenderResult);
        showImageParam_        = fetchBooleanParam(kParamShowImage);
        protectParam_          = fetchDoubleParam(kParamProtect);
        maxSizeParam_          = fetchDoubleParam(kParamMaxSize);
        gammaCorrectionParam_  = fetchDoubleParam(kParamGammaCorrection);
        farmQualityParam_      = fetchChoiceParam(kParamFarmQuality);
        sizeMultiplierParam_   = fetchDoubleParam(kParamSizeMultiplier);
        focalPlaneOffsetParam_ = fetchDoubleParam(kParamFocalPlaneOffset);

        filterTypeParam_       = fetchChoiceParam(kParamFilterType);
        filterPreviewParam_    = fetchBooleanParam(kParamFilterPreview);
        filterResolutionParam_ = fetchIntParam(kParamFilterResolution);
        ringColorParam_        = fetchDoubleParam(kParamRingColor);
        innerColorParam_       = fetchDoubleParam(kParamInnerColor);
        ringSizeParam_         = fetchDoubleParam(kParamRingSize);
        outerBlurParam_        = fetchDoubleParam(kParamOuterBlur);
        innerBlurParam_        = fetchDoubleParam(kParamInnerBlur);
        aspectRatioParam_      = fetchDoubleParam(kParamAspectRatio);
        bladesParam_           = fetchIntParam(kParamBlades);
        angleParam_            = fetchDoubleParam(kParamAngle);
        curvatureParam_        = fetchDoubleParam(kParamCurvature);
        noiseSizeParam_        = fetchDoubleParam(kParamNoiseSize);
        noiseIntensityParam_   = fetchDoubleParam(kParamNoiseIntensity);
        noiseSeedParam_        = fetchIntParam(kParamNoiseSeed);

        catseyeEnableParam_            = fetchBooleanParam(kParamCatseyeEnable);
        catseyeAmountParam_            = fetchDoubleParam(kParamCatseyeAmount);
        catseyeInverseParam_           = fetchBooleanParam(kParamCatseyeInverse);
        catseyeInverseForegroundParam_ = fetchBooleanParam(kParamCatseyeInverseForeground);
        catseyeGammaParam_             = fetchDoubleParam(kParamCatseyeGamma);
        catseyeSoftnessParam_          = fetchDoubleParam(kParamCatseyeSoftness);
        catseyeDimensionBasedParam_    = fetchBooleanParam(kParamCatseyeDimensionBased);

        barndoorsEnableParam_            = fetchBooleanParam(kParamBarndoorsEnable);
        barndoorsAmountParam_            = fetchDoubleParam(kParamBarndoorsAmount);
        barndoorsInverseParam_           = fetchBooleanParam(kParamBarndoorsInverse);
        barndoorsInverseForegroundParam_ = fetchBooleanParam(kParamBarndoorsInverseForeground);
        barndoorsGammaParam_             = fetchDoubleParam(kParamBarndoorsGamma);
        barndoorsTopParam_               = fetchDoubleParam(kParamBarndoorsTop);
        barndoorsBottomParam_            = fetchDoubleParam(kParamBarndoorsBottom);
        barndoorsLeftParam_              = fetchDoubleParam(kParamBarndoorsLeft);
        barndoorsRightParam_             = fetchDoubleParam(kParamBarndoorsRight);

        astigmatismEnableParam_ = fetchBooleanParam(kParamAstigmatismEnable);
        astigmatismAmountParam_ = fetchDoubleParam(kParamAstigmatismAmount);
        astigmatismGammaParam_  = fetchDoubleParam(kParamAstigmatismGamma);

        axialAberrationEnableParam_ = fetchBooleanParam(kParamAxialAberrationEnable);
        axialAberrationAmountParam_ = fetchDoubleParam(kParamAxialAberrationAmount);
        axialAberrationTypeParam_   = fetchChoiceParam(kParamAxialAberrationType);

        inverseForegroundParam_ = fetchBooleanParam(kParamInverseForeground);

        useFocusPointParam_ = fetchBooleanParam(kParamUseFocusPoint);
        focusPointXYParam_  = fetchDouble2DParam(kParamFocusPointXY);

        // Initialize Rust backend
        OdResult res = od_create(&rustHandle_);
        if (res != OK) {
            rustHandle_ = nullptr;
        }

        updateParamVisibility();
    }

    ~OpenDefocusPlugin() override {
        if (rustHandle_) {
            od_destroy(rustHandle_);
            rustHandle_ = nullptr;
        }
    }

    void render(const OFX::RenderArguments& args) override;

    void copySourceToBuffer(OFX::Image* src,
                            std::vector<float>& buffer,
                            int bufWidth, int bufHeight,
                            const OfxRectI& fetchWindow);

    bool isIdentity(const OFX::IsIdentityArguments& args,
                    OFX::Clip*& identityClip,
                    double& identityTime) override;

    void changedParam(const OFX::InstanceChangedArgs& args,
                      const std::string& paramName) override;

    void getRegionsOfInterest(const OFX::RegionsOfInterestArguments& args,
                              OFX::RegionOfInterestSetter& rois) override;

    void getClipPreferences(OFX::ClipPreferencesSetter& clipPreferences) override;

    bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments& args,
                               OfxRectD& rod) override;

private:
    void updateParamVisibility();

    OFX::Clip* srcClip_   = nullptr;
    OFX::Clip* depthClip_  = nullptr;
    OFX::Clip* filterClip_ = nullptr;
    OFX::Clip* dstClip_    = nullptr;

    // Controls
    OFX::BooleanParam* useGpuParam_    = nullptr;
    OFX::DoubleParam* sizeParam_       = nullptr;
    OFX::DoubleParam* focusPlaneParam_ = nullptr;
    OFX::ChoiceParam* qualityParam_    = nullptr;
    OFX::IntParam*    samplesParam_    = nullptr;

    // Defocus / Advanced
    OFX::ChoiceParam*   modeParam_             = nullptr;
    OFX::ChoiceParam*   mathParam_             = nullptr;
    OFX::ChoiceParam*   renderResultParam_     = nullptr;
    OFX::BooleanParam*  showImageParam_        = nullptr;
    OFX::DoubleParam*   protectParam_          = nullptr;
    OFX::DoubleParam*   maxSizeParam_          = nullptr;
    OFX::DoubleParam*   gammaCorrectionParam_  = nullptr;
    OFX::ChoiceParam*   farmQualityParam_      = nullptr;
    OFX::DoubleParam*   sizeMultiplierParam_   = nullptr;
    OFX::DoubleParam*   focalPlaneOffsetParam_ = nullptr;

    // Bokeh
    OFX::ChoiceParam*   filterTypeParam_       = nullptr;
    OFX::BooleanParam*  filterPreviewParam_    = nullptr;
    OFX::IntParam*      filterResolutionParam_ = nullptr;
    OFX::DoubleParam*   ringColorParam_        = nullptr;
    OFX::DoubleParam*   innerColorParam_       = nullptr;
    OFX::DoubleParam*   ringSizeParam_         = nullptr;
    OFX::DoubleParam*   outerBlurParam_        = nullptr;
    OFX::DoubleParam*   innerBlurParam_        = nullptr;
    OFX::DoubleParam*   aspectRatioParam_      = nullptr;
    OFX::IntParam*      bladesParam_           = nullptr;
    OFX::DoubleParam*   angleParam_            = nullptr;
    OFX::DoubleParam*   curvatureParam_        = nullptr;
    OFX::DoubleParam*   noiseSizeParam_        = nullptr;
    OFX::DoubleParam*   noiseIntensityParam_   = nullptr;
    OFX::IntParam*      noiseSeedParam_        = nullptr;

    // Non-Uniform: Catseye
    OFX::BooleanParam*  catseyeEnableParam_            = nullptr;
    OFX::DoubleParam*   catseyeAmountParam_            = nullptr;
    OFX::BooleanParam*  catseyeInverseParam_           = nullptr;
    OFX::BooleanParam*  catseyeInverseForegroundParam_ = nullptr;
    OFX::DoubleParam*   catseyeGammaParam_             = nullptr;
    OFX::DoubleParam*   catseyeSoftnessParam_          = nullptr;
    OFX::BooleanParam*  catseyeDimensionBasedParam_    = nullptr;

    // Non-Uniform: Barndoors
    OFX::BooleanParam*  barndoorsEnableParam_            = nullptr;
    OFX::DoubleParam*   barndoorsAmountParam_            = nullptr;
    OFX::BooleanParam*  barndoorsInverseParam_           = nullptr;
    OFX::BooleanParam*  barndoorsInverseForegroundParam_ = nullptr;
    OFX::DoubleParam*   barndoorsGammaParam_             = nullptr;
    OFX::DoubleParam*   barndoorsTopParam_               = nullptr;
    OFX::DoubleParam*   barndoorsBottomParam_            = nullptr;
    OFX::DoubleParam*   barndoorsLeftParam_              = nullptr;
    OFX::DoubleParam*   barndoorsRightParam_             = nullptr;

    // Non-Uniform: Astigmatism
    OFX::BooleanParam*  astigmatismEnableParam_ = nullptr;
    OFX::DoubleParam*   astigmatismAmountParam_ = nullptr;
    OFX::DoubleParam*   astigmatismGammaParam_  = nullptr;

    // Non-Uniform: Axial Aberration
    OFX::BooleanParam*  axialAberrationEnableParam_ = nullptr;
    OFX::DoubleParam*   axialAberrationAmountParam_ = nullptr;
    OFX::ChoiceParam*   axialAberrationTypeParam_   = nullptr;

    // Non-Uniform: Global
    OFX::BooleanParam*  inverseForegroundParam_ = nullptr;

    // Focus Point
    OFX::BooleanParam*  useFocusPointParam_ = nullptr;
    OFX::Double2DParam* focusPointXYParam_  = nullptr;

    OdHandle rustHandle_;
};

// Copy source pixels into imageBuffer with clamp-to-edge padding.
// Used both for the initial source copy and for abort fallback
// (re-populating imageBuffer with pristine source to avoid partial frames).
void OpenDefocusPlugin::copySourceToBuffer(
    OFX::Image* src,
    std::vector<float>& buffer,
    int bufWidth, int bufHeight,
    const OfxRectI& fetchWindow)
{
    OfxRectI srcBounds = src->getBounds();

    if (srcBounds.x1 < srcBounds.x2 && srcBounds.y1 < srcBounds.y2) {
        int validX1 = std::min(std::max(fetchWindow.x1, srcBounds.x1), fetchWindow.x2);
        int validX2 = std::max(std::min(fetchWindow.x2, srcBounds.x2), fetchWindow.x1);
        int validWidth = validX2 - validX1;

        for (int y = fetchWindow.y1; y < fetchWindow.y2; ++y) {
            int clampY = std::max(srcBounds.y1, std::min(y, srcBounds.y2 - 1));

            int bufY = y - fetchWindow.y1;
            float* dstRow = buffer.data() + static_cast<size_t>(bufY) * bufWidth * 4;

            // Centre: memcpy the valid intersection
            const float* srcRowCenter = (validWidth > 0)
                ? static_cast<const float*>(src->getPixelAddress(validX1, clampY))
                : nullptr;
            if (srcRowCenter && validWidth > 0) {
                int dstX = validX1 - fetchWindow.x1;
                std::memcpy(dstRow + dstX * 4, srcRowCenter,
                            validWidth * 4 * sizeof(float));
            }

            // Left margin: replicate leftmost source pixel
            if (fetchWindow.x1 < validX1) {
                const float* leftPx = static_cast<const float*>(
                    src->getPixelAddress(srcBounds.x1, clampY));
                if (leftPx) {
                    for (int x = fetchWindow.x1; x < validX1; ++x) {
                        int dstX = x - fetchWindow.x1;
                        dstRow[dstX * 4 + 0] = leftPx[0];
                        dstRow[dstX * 4 + 1] = leftPx[1];
                        dstRow[dstX * 4 + 2] = leftPx[2];
                        dstRow[dstX * 4 + 3] = leftPx[3];
                    }
                }
            }

            // Right margin: replicate rightmost source pixel
            if (validX2 < fetchWindow.x2) {
                const float* rightPx = static_cast<const float*>(
                    src->getPixelAddress(srcBounds.x2 - 1, clampY));
                if (rightPx) {
                    for (int x = validX2; x < fetchWindow.x2; ++x) {
                        int dstX = x - fetchWindow.x1;
                        dstRow[dstX * 4 + 0] = rightPx[0];
                        dstRow[dstX * 4 + 1] = rightPx[1];
                        dstRow[dstX * 4 + 2] = rightPx[2];
                        dstRow[dstX * 4 + 3] = rightPx[3];
                    }
                }
            }
        }
    }
}

// Abort callback thunk: casts user_data back to OFX::ImageEffect*
// and queries the host whether rendering should be cancelled.
// Called from the Rust stripe loop between stripes (coarse abort).
extern "C" {
static bool abortCheckThunk(void* user_data) {
    return static_cast<OFX::ImageEffect*>(user_data)->abort();
}
}

void OpenDefocusPlugin::render(const OFX::RenderArguments& args) {
    // Validate pixel format
    if (dstClip_->getPixelDepth() != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
    if (dstClip_->getPixelComponents() != OFX::ePixelComponentRGBA) {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> src(
        (srcClip_ && srcClip_->isConnected())
            ? srcClip_->fetchImage(args.time) : nullptr);
    std::unique_ptr<OFX::Image> depth(
        (depthClip_ && depthClip_->isConnected())
            ? depthClip_->fetchImage(args.time) : nullptr);

    if (!dst || !src) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // Render window
    OfxRectI rw = args.renderWindow;
    int width  = rw.x2 - rw.x1;
    int height = rw.y2 - rw.y1;

    if (width <= 0 || height <= 0) {
        return;
    }

    // Get OFX parameters
    double size = 10.0, focusPlane = 1.0;
    sizeParam_->getValueAtTime(args.time, size);
    focusPlaneParam_->getValueAtTime(args.time, focusPlane);

    // Fallback: pass-through if no Rust handle or size is zero
    if (!rustHandle_ || size <= 0.0) {
        for (int y = rw.y1; y < rw.y2; ++y) {
            const float* srcRow = static_cast<const float*>(src->getPixelAddress(rw.x1, y));
            float* dstRow = static_cast<float*>(dst->getPixelAddress(rw.x1, y));
            if (srcRow && dstRow) {
                std::memcpy(dstRow, srcRow, width * 4 * sizeof(float));
            }
        }
        return;
    }

    // Get additional parameters
    int quality = 0;
    qualityParam_->getValueAtTime(args.time, quality);
    int samples = 20;
    samplesParam_->getValueAtTime(args.time, samples);
    int filterType = 0;
    filterTypeParam_->getValueAtTime(args.time, filterType);
    bool filterPreview = false;
    filterPreviewParam_->getValueAtTime(args.time, filterPreview);
    int filterResolution = 256;
    filterResolutionParam_->getValueAtTime(args.time, filterResolution);
    double ringColor = 1.0, innerColor = 0.4, ringSize = 0.1;
    double outerBlur = 0.1, innerBlur = 0.05, aspectRatio = 1.0;
    ringColorParam_->getValueAtTime(args.time, ringColor);
    innerColorParam_->getValueAtTime(args.time, innerColor);
    ringSizeParam_->getValueAtTime(args.time, ringSize);
    outerBlurParam_->getValueAtTime(args.time, outerBlur);
    innerBlurParam_->getValueAtTime(args.time, innerBlur);
    aspectRatioParam_->getValueAtTime(args.time, aspectRatio);
    int blades = 5;
    bladesParam_->getValueAtTime(args.time, blades);
    double angle = 0.0, curvature = 0.0;
    angleParam_->getValueAtTime(args.time, angle);
    curvatureParam_->getValueAtTime(args.time, curvature);
    double noiseSize = 0.1, noiseIntensity = 0.25;
    noiseSizeParam_->getValueAtTime(args.time, noiseSize);
    noiseIntensityParam_->getValueAtTime(args.time, noiseIntensity);
    int noiseSeed = 0;
    noiseSeedParam_->getValueAtTime(args.time, noiseSeed);

    int mode = 0;
    modeParam_->getValueAtTime(args.time, mode);
    int math = 0;
    mathParam_->getValueAtTime(args.time, math);
    int renderResult = 0;
    renderResultParam_->getValueAtTime(args.time, renderResult);
    bool showImage = false;
    showImageParam_->getValueAtTime(args.time, showImage);
    double protect = 0.0;
    protectParam_->getValueAtTime(args.time, protect);
    double maxSize = 10.0;
    maxSizeParam_->getValueAtTime(args.time, maxSize);
    double gammaCorrection = 1.0;
    gammaCorrectionParam_->getValueAtTime(args.time, gammaCorrection);
    int farmQuality = 2;
    farmQualityParam_->getValueAtTime(args.time, farmQuality);
    double sizeMultiplier = 1.0;
    sizeMultiplierParam_->getValueAtTime(args.time, sizeMultiplier);
    double focalPlaneOffset = 0.0;
    focalPlaneOffsetParam_->getValueAtTime(args.time, focalPlaneOffset);

    // Apply render scale to spatial parameters (proxy mode support)
    double renderScale = args.renderScale.x;
    size     *= renderScale;
    maxSize  *= renderScale;
    protect  *= renderScale;

    // GPU mode toggle (recreates renderer if changed)
    bool useGpu = true;
    useGpuParam_->getValueAtTime(args.time, useGpu);
    od_set_use_gpu(rustHandle_, useGpu);

    // Configure Rust settings
    od_set_size(rustHandle_, static_cast<float>(size));
    // Note: od_set_focus_plane is called later after Focus Point XY sampling
    // Note: resolution and center are set later using srcBounds (after RoI expansion)
    od_set_quality(rustHandle_, static_cast<OdQuality>(quality));
    od_set_samples(rustHandle_, static_cast<int32_t>(samples));
    od_set_filter_type(rustHandle_, static_cast<OdFilterType>(filterType));
    // Filter Preview only applies to Disc (1) and Blade (2).
    // Image (3) uses user-provided filter — must not enter preview path in Rust engine.
    od_set_filter_preview(rustHandle_, filterPreview && filterType >= 1 && filterType <= 2);
    od_set_filter_resolution(rustHandle_, static_cast<uint32_t>(filterResolution));
    od_set_ring_color(rustHandle_, static_cast<float>(ringColor));
    od_set_inner_color(rustHandle_, static_cast<float>(innerColor));
    od_set_ring_size(rustHandle_, static_cast<float>(ringSize));
    od_set_outer_blur(rustHandle_, static_cast<float>(outerBlur));
    od_set_inner_blur(rustHandle_, static_cast<float>(innerBlur));
    od_set_aspect_ratio(rustHandle_, static_cast<float>(aspectRatio));
    od_set_blades(rustHandle_, static_cast<uint32_t>(blades));
    od_set_angle(rustHandle_, static_cast<float>(angle));
    od_set_curvature(rustHandle_, static_cast<float>(curvature));
    od_set_noise_size(rustHandle_, static_cast<float>(noiseSize));
    od_set_noise_intensity(rustHandle_, static_cast<float>(noiseIntensity));
    od_set_noise_seed(rustHandle_, static_cast<uint32_t>(noiseSeed));
    od_set_math(rustHandle_, static_cast<OdMath>(math));
    od_set_result_mode(rustHandle_, static_cast<OdResultMode>(renderResult));
    od_set_show_image(rustHandle_, showImage);
    od_set_protect(rustHandle_, static_cast<float>(protect));
    od_set_max_size(rustHandle_, static_cast<float>(maxSize));
    od_set_gamma_correction(rustHandle_, static_cast<float>(gammaCorrection));
    od_set_farm_quality(rustHandle_, static_cast<OdQuality>(farmQuality));
    od_set_size_multiplier(rustHandle_, static_cast<float>(sizeMultiplier));
    od_set_focal_plane_offset(rustHandle_, static_cast<float>(focalPlaneOffset));

    // Non-Uniform: Catseye
    bool catseyeEnable = false;
    catseyeEnableParam_->getValueAtTime(args.time, catseyeEnable);
    double catseyeAmount = 0.5;
    catseyeAmountParam_->getValueAtTime(args.time, catseyeAmount);
    bool catseyeInverse = false;
    catseyeInverseParam_->getValueAtTime(args.time, catseyeInverse);
    bool catseyeInverseForeground = true;
    catseyeInverseForegroundParam_->getValueAtTime(args.time, catseyeInverseForeground);
    double catseyeGamma = 1.0;
    catseyeGammaParam_->getValueAtTime(args.time, catseyeGamma);
    double catseyeSoftness = 0.2;
    catseyeSoftnessParam_->getValueAtTime(args.time, catseyeSoftness);
    bool catseyeDimensionBased = false;
    catseyeDimensionBasedParam_->getValueAtTime(args.time, catseyeDimensionBased);

    od_set_catseye_enable(rustHandle_, catseyeEnable);
    od_set_catseye_amount(rustHandle_, static_cast<float>(catseyeAmount));
    od_set_catseye_inverse(rustHandle_, catseyeInverse);
    od_set_catseye_inverse_foreground(rustHandle_, catseyeInverseForeground);
    od_set_catseye_gamma(rustHandle_, static_cast<float>(catseyeGamma));
    od_set_catseye_softness(rustHandle_, static_cast<float>(catseyeSoftness));
    od_set_catseye_dimension_based(rustHandle_, catseyeDimensionBased);

    // Non-Uniform: Barndoors
    bool barndoorsEnable = false;
    barndoorsEnableParam_->getValueAtTime(args.time, barndoorsEnable);
    double barndoorsAmount = 0.5;
    barndoorsAmountParam_->getValueAtTime(args.time, barndoorsAmount);
    bool barndoorsInverse = false;
    barndoorsInverseParam_->getValueAtTime(args.time, barndoorsInverse);
    bool barndoorsInverseForeground = true;
    barndoorsInverseForegroundParam_->getValueAtTime(args.time, barndoorsInverseForeground);
    double barndoorsGamma = 1.0;
    barndoorsGammaParam_->getValueAtTime(args.time, barndoorsGamma);
    double barndoorsTop = 100.0;
    barndoorsTopParam_->getValueAtTime(args.time, barndoorsTop);
    double barndoorsBottom = 100.0;
    barndoorsBottomParam_->getValueAtTime(args.time, barndoorsBottom);
    double barndoorsLeft = 100.0;
    barndoorsLeftParam_->getValueAtTime(args.time, barndoorsLeft);
    double barndoorsRight = 100.0;
    barndoorsRightParam_->getValueAtTime(args.time, barndoorsRight);

    od_set_barndoors_enable(rustHandle_, barndoorsEnable);
    od_set_barndoors_amount(rustHandle_, static_cast<float>(barndoorsAmount));
    od_set_barndoors_inverse(rustHandle_, barndoorsInverse);
    od_set_barndoors_inverse_foreground(rustHandle_, barndoorsInverseForeground);
    od_set_barndoors_gamma(rustHandle_, static_cast<float>(barndoorsGamma));
    od_set_barndoors_top(rustHandle_, static_cast<float>(barndoorsTop));
    od_set_barndoors_bottom(rustHandle_, static_cast<float>(barndoorsBottom));
    od_set_barndoors_left(rustHandle_, static_cast<float>(barndoorsLeft));
    od_set_barndoors_right(rustHandle_, static_cast<float>(barndoorsRight));

    // Non-Uniform: Astigmatism
    bool astigmatismEnable = false;
    astigmatismEnableParam_->getValueAtTime(args.time, astigmatismEnable);
    double astigmatismAmount = 0.5;
    astigmatismAmountParam_->getValueAtTime(args.time, astigmatismAmount);
    double astigmatismGamma = 1.0;
    astigmatismGammaParam_->getValueAtTime(args.time, astigmatismGamma);

    od_set_astigmatism_enable(rustHandle_, astigmatismEnable);
    od_set_astigmatism_amount(rustHandle_, static_cast<float>(astigmatismAmount));
    od_set_astigmatism_gamma(rustHandle_, static_cast<float>(astigmatismGamma));

    // Non-Uniform: Axial Aberration
    bool axialAberrationEnable = false;
    axialAberrationEnableParam_->getValueAtTime(args.time, axialAberrationEnable);
    double axialAberrationAmount = 0.5;
    axialAberrationAmountParam_->getValueAtTime(args.time, axialAberrationAmount);
    int axialAberrationType = 0;
    axialAberrationTypeParam_->getValueAtTime(args.time, axialAberrationType);

    od_set_axial_aberration_enable(rustHandle_, axialAberrationEnable);
    od_set_axial_aberration_amount(rustHandle_, static_cast<float>(axialAberrationAmount));
    od_set_axial_aberration_type(rustHandle_, static_cast<int32_t>(axialAberrationType));

    // Non-Uniform: Global Inverse Foreground
    bool inverseForeground = true;
    inverseForegroundParam_->getValueAtTime(args.time, inverseForeground);
    od_set_inverse_foreground(rustHandle_, inverseForeground);

    // Filter Preview: render bokeh shape at filter_resolution, centered in output
    // Only for Disc (1) and Blade (2) — Image (3) uses user-provided filter, no bokeh preview
    if (filterPreview && filterType >= 1 && filterType <= 2) {
        int fRes = static_cast<int>(filterResolution * renderScale);
        if (fRes < 32) fRes = 32;
        if (fRes > 1024) fRes = 1024;

        // Render bokeh into a small buffer
        std::vector<float> previewBuf(static_cast<size_t>(fRes) * fRes * 4, 0.0f);
        int32_t pFullRegion[4]   = { 0, 0, fRes, fRes };
        int32_t pRenderRegion[4] = { 0, 0, fRes, fRes };

        // Force 2D mode for preview (avoids DepthNotFound validation error
        // when current mode is Depth and no depth buffer is provided)
        od_set_defocus_mode(rustHandle_, TWO_D);
        od_set_resolution(rustHandle_, static_cast<uint32_t>(fRes),
                          static_cast<uint32_t>(fRes));
        od_set_aborted(rustHandle_, false);

        od_render(rustHandle_, previewBuf.data(),
                  static_cast<uint32_t>(fRes), static_cast<uint32_t>(fRes),
                  4, nullptr, 0, 0,
                  nullptr, 0, 0, 0,
                  pFullRegion, pRenderRegion,
                  nullptr, nullptr);

        // Clear output to black
        for (int y = rw.y1; y < rw.y2; ++y) {
            float* dstRow = static_cast<float*>(dst->getPixelAddress(rw.x1, y));
            if (dstRow) {
                std::memset(dstRow, 0, width * 4 * sizeof(float));
            }
        }

        // Center-copy the preview into the output
        int offsetX = (width  - fRes) / 2;
        int offsetY = (height - fRes) / 2;

        for (int py = 0; py < fRes; ++py) {
            int outY = py + offsetY;
            if (outY < 0 || outY >= height) continue;

            float* dstRow = static_cast<float*>(
                dst->getPixelAddress(rw.x1, rw.y1 + outY));
            if (!dstRow) continue;

            int copyStart = std::max(0, offsetX);
            int srcStart  = std::max(0, -offsetX);
            int copyEnd   = std::min(width, offsetX + fRes);
            int copyLen   = copyEnd - copyStart;
            if (copyLen <= 0) continue;

            const float* previewRow = previewBuf.data()
                + static_cast<size_t>(py) * fRes * 4
                + srcStart * 4;
            std::memcpy(dstRow + copyStart * 4, previewRow,
                        copyLen * 4 * sizeof(float));
        }
        return;
    }

    // ====================================================================
    // Robust buffer construction using fetchWindow + intersection
    // In proxy mode or with viewer panning, srcBounds can be smaller than
    // or offset from renderWindow. Using fetchWindow (renderWindow + margin)
    // as the buffer basis guarantees renderRegion offsets are always non-negative.
    // ====================================================================

    // 1. Calculate fetchWindow = renderWindow expanded by blur margin
    double effectiveRadius = (mode == 1) ? std::max(size, maxSize) : size;
    effectiveRadius *= sizeMultiplier; // size/maxSize already have renderScale applied
    int margin = static_cast<int>(std::ceil(effectiveRadius) + 1.0);

    OfxRectI fetchWindow = rw;
    // Only expand Y axis — Rust stripe code uses Y margin for stripe padding.
    // X axis is NOT expanded: wgpu texture ClampToEdge provides equivalent
    // edge behavior.
    fetchWindow.y1 -= margin;
    fetchWindow.y2 += margin;

    // Cap buffer width to prevent the upstream ChunkHandler (limit=4096)
    // from splitting horizontally.  The ChunkHandler processes chunks
    // in-place, so chunk N+1 reads chunk N's blurred output in the overlap
    // zone, causing visible vertical seams at the chunk boundary.
    // OFX hosts may provide renderWindow with overscan (e.g. -12..4108 for
    // 4K-DCP), pushing bufWidth past 4096.  Trim the overscan symmetrically;
    // the ClampToEdge copy loop already handles edge pixels.
    int bufWidth  = fetchWindow.x2 - fetchWindow.x1;
    if (bufWidth > 4096) {
        int excess = bufWidth - 4096;
        int trimLeft = excess / 2;
        int trimRight = excess - trimLeft;
        fetchWindow.x1 += trimLeft;
        fetchWindow.x2 -= trimRight;
        bufWidth = 4096;
    }

    int bufHeight = fetchWindow.y2 - fetchWindow.y1;

    if (bufWidth <= 0 || bufHeight <= 0) return;

    // 2. Allocate buffer for source image
    std::vector<float> imageBuffer(static_cast<size_t>(bufWidth) * bufHeight * 4, 0.0f);

    // 3. Copy source pixels with Clamp to Edge padding
    //    NDK version benefits from NUKE's automatic edge clamping; OFX must do it manually.
    //    Without this, convolution picks up black (0.0f) pixels beyond image edges,
    //    causing dark borders on left/bottom edges.
    copySourceToBuffer(src.get(), imageBuffer, bufWidth, bufHeight, fetchWindow);

    // 4. Depth buffer with Clamp to Edge padding
    const float* depthPtr = nullptr;
    uint32_t depthW = 0, depthH = 0;
    std::vector<float> depthBuffer;

    bool useDepth = (mode == 1) && depth;
    if (useDepth) {
        depthBuffer.resize(static_cast<size_t>(bufWidth) * bufHeight, 0.0f);
        OfxRectI depthBounds = depth->getBounds();

        if (depthBounds.x1 < depthBounds.x2 && depthBounds.y1 < depthBounds.y2) {
            OFX::PixelComponentEnum depthComp = depth->getPixelComponents();
            int depthStride = (depthComp == OFX::ePixelComponentRGBA) ? 4 : 1;

            int dValidX1 = std::min(std::max(fetchWindow.x1, depthBounds.x1), fetchWindow.x2);
            int dValidX2 = std::max(std::min(fetchWindow.x2, depthBounds.x2), fetchWindow.x1);
            int dValidWidth = dValidX2 - dValidX1;

            for (int y = fetchWindow.y1; y < fetchWindow.y2; ++y) {
                int clampY = std::max(depthBounds.y1, std::min(y, depthBounds.y2 - 1));

                int bufY = y - fetchWindow.y1;
                float* dRow = depthBuffer.data() + static_cast<size_t>(bufY) * bufWidth;

                // Centre
                const float* depthRowCenter = (dValidWidth > 0)
                    ? static_cast<const float*>(depth->getPixelAddress(dValidX1, clampY))
                    : nullptr;
                if (depthRowCenter && dValidWidth > 0) {
                    int dstX = dValidX1 - fetchWindow.x1;
                    for (int x = 0; x < dValidWidth; ++x) {
                        dRow[dstX + x] = depthRowCenter[x * depthStride];
                    }
                }

                // Left margin
                if (fetchWindow.x1 < dValidX1) {
                    const float* leftPx = static_cast<const float*>(
                        depth->getPixelAddress(depthBounds.x1, clampY));
                    if (leftPx) {
                        float leftVal = leftPx[0];
                        for (int x = fetchWindow.x1; x < dValidX1; ++x) {
                            dRow[x - fetchWindow.x1] = leftVal;
                        }
                    }
                }

                // Right margin
                if (dValidX2 < fetchWindow.x2) {
                    const float* rightPx = static_cast<const float*>(
                        depth->getPixelAddress(depthBounds.x2 - 1, clampY));
                    if (rightPx) {
                        float rightVal = rightPx[0];
                        for (int x = dValidX2; x < fetchWindow.x2; ++x) {
                            dRow[x - fetchWindow.x1] = rightVal;
                        }
                    }
                }
            }
        }
        depthPtr = depthBuffer.data();
        depthW = static_cast<uint32_t>(bufWidth);
        depthH = static_cast<uint32_t>(bufHeight);
    }

    // Focus Plane is set via changedParam() when Focus Point XY is updated
    od_set_focus_plane(rustHandle_, static_cast<float>(focusPlane));
    od_set_defocus_mode(rustHandle_, useDepth ? DEPTH : TWO_D);

    // 5. Filter image (Phase 9 — unchanged, uses its own bounds)
    const float* filterPtr = nullptr;
    uint32_t filterW = 0, filterH = 0, filterCh = 0;
    std::vector<float> filterBuffer;

    bool useFilter = (filterType == 3) && filterClip_ && filterClip_->isConnected();
    if (useFilter) {
        std::unique_ptr<OFX::Image> filterImg(filterClip_->fetchImage(args.time));
        if (filterImg.get()) {
            OfxRectI fBounds = filterImg->getBounds();
            int fWidth  = fBounds.x2 - fBounds.x1;
            int fHeight = fBounds.y2 - fBounds.y1;

            if (fWidth > 0 && fHeight > 0) {
                filterBuffer.resize(static_cast<size_t>(fWidth) * fHeight * 4);
                for (int y = fBounds.y1; y < fBounds.y2; ++y) {
                    const float* fRow = static_cast<const float*>(
                        filterImg->getPixelAddress(fBounds.x1, y));
                    float* bufRow = filterBuffer.data()
                        + static_cast<size_t>(y - fBounds.y1) * fWidth * 4;
                    if (fRow) {
                        std::memcpy(bufRow, fRow, fWidth * 4 * sizeof(float));
                    }
                }
                filterPtr = filterBuffer.data();
                filterW = static_cast<uint32_t>(fWidth);
                filterH = static_cast<uint32_t>(fHeight);
                filterCh = 4;
            }
        }
    }

    // 6. Geometry: use Region of Definition for optical center
    OfxRectD rod = srcClip_->getRegionOfDefinition(args.time);
    int rodW = static_cast<int>((rod.x2 - rod.x1) * renderScale);
    int rodH = static_cast<int>((rod.y2 - rod.y1) * renderScale);
    if (rodW <= 0) rodW = bufWidth;
    if (rodH <= 0) rodH = bufHeight;

    od_set_resolution(rustHandle_, static_cast<uint32_t>(rodW),
                      static_cast<uint32_t>(rodH));

    // Optical center in fetchWindow-local coordinates
    double centerWorldX = (rod.x1 + rod.x2) / 2.0 * renderScale;
    double centerWorldY = (rod.y1 + rod.y2) / 2.0 * renderScale;
    od_set_center(rustHandle_,
                  static_cast<float>(centerWorldX - fetchWindow.x1),
                  static_cast<float>(centerWorldY - fetchWindow.y1));

    // renderRegion in buffer-local coordinates, clamped to buffer bounds.
    // fetchWindow X may have been trimmed to cap bufWidth ≤ 4096, so rw
    // can extend beyond fetchWindow — clamp to [0, bufWidth/bufHeight].
    int32_t fullRegion[4] = {
        0, 0,
        static_cast<int32_t>(bufWidth),
        static_cast<int32_t>(bufHeight)
    };
    int32_t renderRegion[4] = {
        static_cast<int32_t>(std::max(0, rw.x1 - fetchWindow.x1)),
        static_cast<int32_t>(std::max(0, rw.y1 - fetchWindow.y1)),
        static_cast<int32_t>(std::min(bufWidth,  rw.x2 - fetchWindow.x1)),
        static_cast<int32_t>(std::min(bufHeight, rw.y2 - fetchWindow.y1))
    };

    od_set_aborted(rustHandle_, false);

    OdResult res = od_render(
        rustHandle_,
        imageBuffer.data(),
        static_cast<uint32_t>(bufWidth),
        static_cast<uint32_t>(bufHeight),
        4, // RGBA
        depthPtr,
        depthW,
        depthH,
        filterPtr,
        filterW,
        filterH,
        filterCh,
        fullRegion,
        renderRegion,
        abortCheckThunk,
        static_cast<void*>(this));

    // On abort: re-populate imageBuffer with pristine source so that
    // the dst copy below writes clean (unprocessed) source pixels
    // instead of a partially rendered frame.
    if (res == ABORTED) {
        copySourceToBuffer(src.get(), imageBuffer, bufWidth, bufHeight, fetchWindow);
    }

    // Copy result to OFX output (fetchWindow-local → world coords).
    // fetchWindow X may have been trimmed to cap bufWidth, so rw can extend
    // beyond fetchWindow.  Copy the intersection; replicate edge pixels for
    // the overscan zones that fall outside the rendered buffer.
    int copyX1 = std::max(rw.x1, fetchWindow.x1);
    int copyX2 = std::min(rw.x2, fetchWindow.x2);
    int copyWidth = std::max(0, copyX2 - copyX1);

    for (int y = rw.y1; y < rw.y2; ++y) {
        int bufY = y - fetchWindow.y1;

        // Centre: memcpy rendered data
        if (copyWidth > 0) {
            float* dstRow = static_cast<float*>(dst->getPixelAddress(copyX1, y));
            int bufX = copyX1 - fetchWindow.x1;
            const float* bufRow = imageBuffer.data()
                + (static_cast<size_t>(bufY) * bufWidth + bufX) * 4;
            if (dstRow) {
                std::memcpy(dstRow, bufRow, copyWidth * 4 * sizeof(float));
            }
        }

        // Left overscan: replicate leftmost buffer pixel
        if (rw.x1 < copyX1) {
            const float* edgePx = imageBuffer.data()
                + static_cast<size_t>(bufY) * bufWidth * 4;
            for (int x = rw.x1; x < copyX1; ++x) {
                float* px = static_cast<float*>(dst->getPixelAddress(x, y));
                if (px) std::memcpy(px, edgePx, 4 * sizeof(float));
            }
        }

        // Right overscan: replicate rightmost buffer pixel
        if (copyX2 < rw.x2) {
            const float* edgePx = imageBuffer.data()
                + (static_cast<size_t>(bufY) * bufWidth + bufWidth - 1) * 4;
            for (int x = copyX2; x < rw.x2; ++x) {
                float* px = static_cast<float*>(dst->getPixelAddress(x, y));
                if (px) std::memcpy(px, edgePx, 4 * sizeof(float));
            }
        }
    }

    if (res != OK && res != ABORTED) {
        // Log warning but don't throw — the buffer still has valid source data
    }
}

bool OpenDefocusPlugin::isIdentity(const OFX::IsIdentityArguments& args,
                                    OFX::Clip*& identityClip,
                                    double& /*identityTime*/) {
    double size = 0.0;
    sizeParam_->getValueAtTime(args.time, size);
    if (size <= 0.0) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}

void OpenDefocusPlugin::changedParam(const OFX::InstanceChangedArgs& args,
                                      const std::string& paramName) {
    if (paramName == kParamQuality || paramName == kParamFilterType
        || paramName == kParamMode || paramName == kParamRenderResult
        || paramName == kParamCatseyeEnable || paramName == kParamBarndoorsEnable
        || paramName == kParamAstigmatismEnable || paramName == kParamAxialAberrationEnable
        || paramName == kParamUseFocusPoint) {
        updateParamVisibility();
    }

    // Focus Point XY Picker — sample depth and update Focus Plane knob
    // NDK parity: focus_point_knobchanged() (lib.rs:1033-1058)
    if (paramName == kParamFocusPointXY) {
        bool useFocusPoint = false;
        useFocusPointParam_->getValueAtTime(args.time, useFocusPoint);
        int mode = 0;
        modeParam_->getValueAtTime(args.time, mode);
        bool useDepth = (mode == 1);

        if (useFocusPoint && useDepth && depthClip_ && depthClip_->isConnected()) {
            std::unique_ptr<OFX::Image> depthImg(depthClip_->fetchImage(args.time));
            if (depthImg) {
                double focusX = 0.0, focusY = 0.0;
                focusPointXYParam_->getValueAtTime(args.time, focusX, focusY);

                // Canonical → pixel coordinates (renderScale-aware)
                double renderScale = args.renderScale.x;
                int px = static_cast<int>(std::round(focusX * renderScale));
                int py = static_cast<int>(std::round(focusY * renderScale));

                OfxRectI bounds = depthImg->getBounds();
                if (px >= bounds.x1 && px < bounds.x2 &&
                    py >= bounds.y1 && py < bounds.y2)
                {
                    const float* pix = reinterpret_cast<const float*>(
                        depthImg->getPixelAddress(px, py));
                    if (pix) {
                        float sampledDepth = pix[0];
                        // NDK parity: skip if depth == 0 (lib.rs:1050)
                        if (sampledDepth != 0.0f) {
                            beginEditBlock("Focus Point Sample");
                            focusPlaneParam_->setValue(static_cast<double>(sampledDepth));
                            endEditBlock();
                        }
                    }
                }
            }
        }
    }
}

void OpenDefocusPlugin::getRegionsOfInterest(
    const OFX::RegionsOfInterestArguments& args,
    OFX::RegionOfInterestSetter& rois)
{
    double size = 10.0;
    sizeParam_->getValueAtTime(args.time, size);
    double sizeMultiplier = 1.0;
    sizeMultiplierParam_->getValueAtTime(args.time, sizeMultiplier);
    double maxSize = 10.0;
    maxSizeParam_->getValueAtTime(args.time, maxSize);
    int mode = 0;
    modeParam_->getValueAtTime(args.time, mode);

    // Apply render scale to spatial parameters for margin calculation
    double renderScale = args.renderScale.x;
    size *= renderScale;
    maxSize *= renderScale;

    // Effective blur radius: Depth mode uses max(size, maxSize), 2D uses size
    double effectiveRadius = (mode == 1) ? std::max(size, maxSize) : size;
    effectiveRadius *= sizeMultiplier;

    // Margin in canonical coordinates
    double margin = std::ceil(effectiveRadius) + 1.0;

    OfxRectD srcRoI = args.regionOfInterest;
    srcRoI.x1 -= margin;
    srcRoI.y1 -= margin;
    srcRoI.x2 += margin;
    srcRoI.y2 += margin;

    if (srcClip_)   rois.setRegionOfInterest(*srcClip_, srcRoI);
    if (depthClip_) rois.setRegionOfInterest(*depthClip_, srcRoI);
    // Filter clip: bokeh shape image, no expansion needed
}

void OpenDefocusPlugin::getClipPreferences(
    OFX::ClipPreferencesSetter& clipPreferences)
{
    // Explicitly declare output format to match Source clip.
    // Standard OFX best practice for multi-input plugins.
    // Note: does NOT resolve Flame's "Unsupported input resolution mix"
    // error — Flame validates clip resolutions at graph level before
    // calling getClipPreferences.  See Known Issue #12.
    clipPreferences.setClipComponents(*dstClip_, srcClip_->getPixelComponents());

    // setClipBitDepth / setPixelAspectRatio require host capability flags;
    // calling without support throws PropertyUnknownToHost, which would
    // abort the entire getClipPreferences action.
    OFX::ImageEffectHostDescription* hostDesc = OFX::getImageEffectHostDescription();
    if (hostDesc && hostDesc->supportsMultipleClipDepths) {
        clipPreferences.setClipBitDepth(*dstClip_, srcClip_->getPixelDepth());
    }
    if (hostDesc && hostDesc->supportsMultipleClipPARs) {
        clipPreferences.setPixelAspectRatio(*dstClip_, srcClip_->getPixelAspectRatio());
    }
}

bool OpenDefocusPlugin::getRegionOfDefinition(
    const OFX::RegionOfDefinitionArguments& args, OfxRectD& rod)
{
    // Output RoD = Source clip's RoD only.
    // Without this override the default is the union of all input RoDs,
    // which can cause unexpected output dimensions with multi-resolution
    // inputs.  Note: does NOT resolve Flame's "Unsupported input
    // resolution mix" error — Flame validates at graph level before
    // calling getRegionOfDefinition.  See Known Issue #12.
    if (srcClip_ && srcClip_->isConnected()) {
        rod = srcClip_->getRegionOfDefinition(args.time);
        return true;
    }
    return false;
}

void OpenDefocusPlugin::updateParamVisibility() {
    // Samples: only enabled when Quality = Custom
    int quality = 0;
    qualityParam_->getValue(quality);
    bool isCustom = (quality == 3); // Custom
    samplesParam_->setEnabled(isCustom);

    // Mode-dependent parameters: only enabled when Mode = Depth
    int mode = 0;
    modeParam_->getValue(mode);
    bool isDepth = (mode == 1);
    mathParam_->setEnabled(isDepth);
    renderResultParam_->setEnabled(isDepth);
    protectParam_->setEnabled(isDepth);
    maxSizeParam_->setEnabled(isDepth);
    focalPlaneOffsetParam_->setEnabled(isDepth);

    // Focus Point: enabled only in Depth mode; XY enabled only when toggle is on
    bool useFocusPoint = false;
    useFocusPointParam_->getValue(useFocusPoint);
    useFocusPointParam_->setEnabled(isDepth);
    focusPointXYParam_->setEnabled(isDepth && useFocusPoint);

    // ShowImage: only enabled when Mode = Depth AND RenderResult = FocalPlaneSetup
    int renderResult = 0;
    renderResultParam_->getValue(renderResult);
    bool isFocalPlaneSetup = isDepth && (renderResult == 1);
    showImageParam_->setEnabled(isFocalPlaneSetup);

    // GammaCorrection, FarmQuality, SizeMultiplier: always enabled
    // Bokeh parameters: always enabled (matching NUKE NDK original behavior)

    // Catseye: sub-params enabled only when CatseyeEnable = true
    bool catseyeOn = false;
    catseyeEnableParam_->getValue(catseyeOn);
    catseyeAmountParam_->setEnabled(catseyeOn);
    catseyeInverseParam_->setEnabled(catseyeOn);
    catseyeInverseForegroundParam_->setEnabled(catseyeOn);
    catseyeGammaParam_->setEnabled(catseyeOn);
    catseyeSoftnessParam_->setEnabled(catseyeOn);
    catseyeDimensionBasedParam_->setEnabled(catseyeOn);

    // Barndoors: sub-params enabled only when BarndoorsEnable = true
    bool barndoorsOn = false;
    barndoorsEnableParam_->getValue(barndoorsOn);
    barndoorsAmountParam_->setEnabled(barndoorsOn);
    barndoorsInverseParam_->setEnabled(barndoorsOn);
    barndoorsInverseForegroundParam_->setEnabled(barndoorsOn);
    barndoorsGammaParam_->setEnabled(barndoorsOn);
    barndoorsTopParam_->setEnabled(barndoorsOn);
    barndoorsBottomParam_->setEnabled(barndoorsOn);
    barndoorsLeftParam_->setEnabled(barndoorsOn);
    barndoorsRightParam_->setEnabled(barndoorsOn);

    // Astigmatism: sub-params enabled only when AstigmatismEnable = true
    bool astigmatismOn = false;
    astigmatismEnableParam_->getValue(astigmatismOn);
    astigmatismAmountParam_->setEnabled(astigmatismOn);
    astigmatismGammaParam_->setEnabled(astigmatismOn);

    // Axial Aberration: sub-params enabled only when AxialAberrationEnable = true
    bool axialOn = false;
    axialAberrationEnableParam_->getValue(axialOn);
    axialAberrationAmountParam_->setEnabled(axialOn);
    axialAberrationTypeParam_->setEnabled(axialOn);
}

// ---------------------------------------------------------------------------
// Factory: plugin description and registration
// ---------------------------------------------------------------------------
class OpenDefocusPluginFactory
    : public OFX::PluginFactoryHelper<OpenDefocusPluginFactory> {
public:
    OpenDefocusPluginFactory(const std::string& id, unsigned int vMaj,
                             unsigned int vMin)
        : OFX::PluginFactoryHelper<OpenDefocusPluginFactory>(id, vMaj, vMin)
    {}

    void describe(OFX::ImageEffectDescriptor& desc) override;
    void describeInContext(OFX::ImageEffectDescriptor& desc,
                           OFX::ContextEnum context) override;
    OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                      OFX::ContextEnum context) override;
};

void OpenDefocusPluginFactory::describe(OFX::ImageEffectDescriptor& desc) {
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(
        "Physically-based depth-of-field / defocus effect. "
        "Port of OpenDefocus to OpenFX.");

    // 32-bit float only (DoF requires precision)
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    // General context: allows Source + Depth + Output
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedContext(OFX::eContextFilter);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false); // Full image per render call (no tiling yet)
    desc.setTemporalClipAccess(false);

    // Thread safety: unsafe — rustHandle_ holds mutable state (params + renderer),
    // so concurrent render() on the same instance would cause race conditions.
    // Host will serialize render calls or create separate instances per thread.
    desc.setRenderThreadSafety(OFX::eRenderUnsafe);

    // OpenGL overlay for Focus Point XY crosshair
    desc.setOverlayInteractDescriptor(new OpenDefocusOverlayDescriptor);
}

void OpenDefocusPluginFactory::describeInContext(
    OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context) {
    // --- Clips ---

    // Source clip (required)
    OFX::ClipDescriptor* srcClip = desc.defineClip(kClipSource);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(false);

    // Depth clip (optional) — General context only
    if (context == OFX::eContextGeneral) {
        OFX::ClipDescriptor* depthClip = desc.defineClip(kClipDepth);
        depthClip->addSupportedComponent(OFX::ePixelComponentRGBA);
        depthClip->addSupportedComponent(OFX::ePixelComponentAlpha);
        depthClip->setOptional(true);
        depthClip->setIsMask(false);
        depthClip->setTemporalClipAccess(false);
        depthClip->setSupportsTiles(false);
    }

    // Filter clip (optional) — custom bokeh image for Filter Type = Image
    if (context == OFX::eContextGeneral) {
        OFX::ClipDescriptor* filterClip = desc.defineClip(kClipFilter);
        filterClip->addSupportedComponent(OFX::ePixelComponentRGBA);
        filterClip->setOptional(true);
        filterClip->setIsMask(false);
        filterClip->setTemporalClipAccess(false);
        filterClip->setSupportsTiles(false);
    }

    // Output clip
    OFX::ClipDescriptor* dstClip = desc.defineClip(kClipOutput);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->setSupportsTiles(false);

    // --- Parameters ---
    // Approach #22: Topological Branching
    // - Groups defined for ALL hosts; topology (parent-child) varies by host
    // - NUKE: nested groups (sub-groups inside parent groups) → 4 tabs
    // - Flame: flat groups (all groups at root level) → column packing

    std::string hostName = OFX::getImageEffectHostDescription()->hostName;
    bool isFlame = (hostName.find("Autodesk") != std::string::npos ||
                    hostName.find("Flame")    != std::string::npos ||
                    hostName.find("flame")    != std::string::npos);

    // ==========================================================
    // 1. Pages (Tabs) の定義
    // ==========================================================
    OFX::PageParamDescriptor* controlsPage   = desc.definePageParam("Controls");
    OFX::PageParamDescriptor* bokehPage      = desc.definePageParam("Bokeh");
    OFX::PageParamDescriptor* nonUniformPage = desc.definePageParam("NonUniform");
    OFX::PageParamDescriptor* advancedPage   = desc.definePageParam("Advanced");

    if (controlsPage)   controlsPage->setLabels(isFlame ? "Page 1" : "Controls", isFlame ? "Page 1" : "Controls", isFlame ? "Page 1" : "Controls");
    if (bokehPage)      bokehPage->setLabels(isFlame ? "Page 2" : "Bokeh", isFlame ? "Page 2" : "Bokeh", isFlame ? "Page 2" : "Bokeh");
    if (nonUniformPage) nonUniformPage->setLabels(isFlame ? "Page 3" : "Non-Uniform", isFlame ? "Page 3" : "Non-Uniform", isFlame ? "Page 3" : "Non-Uniform");
    if (advancedPage)   advancedPage->setLabels(isFlame ? "Page 4" : "Advanced", isFlame ? "Page 4" : "Advanced", isFlame ? "Page 4" : "Advanced");

    // ==========================================================
    // 2. Groups (階層の器) の定義 — 定義順 = UI表示順
    // ==========================================================
    // Controls (1番目)
    OFX::GroupParamDescriptor* controlsGrp   = desc.defineGroupParam("ControlsGroup");
    // Bokeh (2番目)
    OFX::GroupParamDescriptor* bokehGrp      = desc.defineGroupParam("BokehGroup");
    // Flame専用: Bokeh Noise サブグループ
    OFX::GroupParamDescriptor* bokehNoiseGrp = isFlame ? desc.defineGroupParam("BokehNoiseGroup") : nullptr;
    // NUKE専用: Non-Uniform 親グループ (3番目)
    OFX::GroupParamDescriptor* nonUniformGrp = isFlame ? nullptr : desc.defineGroupParam("NonUniformGroup");
    // Flame専用: Catseye / Barndoors サブグループ
    OFX::GroupParamDescriptor* catseyeGrp    = isFlame ? desc.defineGroupParam("CatseyeGroup") : nullptr;
    OFX::GroupParamDescriptor* barndoorsGrp  = isFlame ? desc.defineGroupParam("BarndoorsGroup") : nullptr;
    // Flame専用: Astigmatism / Axial Aberration サブグループ
    OFX::GroupParamDescriptor* astigmatismGrp      = isFlame ? desc.defineGroupParam("AstigmatismGroup") : nullptr;
    OFX::GroupParamDescriptor* axialAberrationGrp  = isFlame ? desc.defineGroupParam("AxialAberrationGroup") : nullptr;
    // Advanced (最後)
    OFX::GroupParamDescriptor* advancedGrp   = desc.defineGroupParam("AdvancedGroup");

    if (controlsGrp)   { controlsGrp->setLabels("Controls", "Controls", "Controls"); controlsGrp->setOpen(true); }
    if (bokehGrp)      { bokehGrp->setLabels("Bokeh", "Bokeh", "Bokeh"); bokehGrp->setOpen(true); }
    if (bokehNoiseGrp) { bokehNoiseGrp->setLabels("Bokeh Noise", "Bokeh Noise", "Bokeh Noise"); bokehNoiseGrp->setOpen(true); }
    if (nonUniformGrp) { nonUniformGrp->setLabels("Non-Uniform", "Non-Uniform", "Non-Uniform"); nonUniformGrp->setOpen(true); }
    if (catseyeGrp)    { catseyeGrp->setLabels("Catseye", "Catseye", "Catseye"); catseyeGrp->setOpen(true); }
    if (barndoorsGrp)  { barndoorsGrp->setLabels("Barndoors", "Barndoors", "Barndoors"); barndoorsGrp->setOpen(true); }
    if (astigmatismGrp)     { astigmatismGrp->setLabels("Astigmatism", "Astigmatism", "Astigmatism"); astigmatismGrp->setOpen(true); }
    if (axialAberrationGrp) { axialAberrationGrp->setLabels("Axial Aberration", "Axial Aberration", "Axial Aberration"); axialAberrationGrp->setOpen(true); }
    if (advancedGrp)   { advancedGrp->setLabels("Advanced", "Advanced", "Advanced"); advancedGrp->setOpen(true); }

    // ==========================================================
    // 3. Topology (親子関係のルーティング)
    // ==========================================================
    if (!isFlame) {
        // 【NUKE / Resolve】: 入れ子なし！ 4つのメイングループをそのままタブに入れるだけ
        if (controlsPage && controlsGrp)     controlsPage->addChild(*controlsGrp);
        if (bokehPage && bokehGrp)           bokehPage->addChild(*bokehGrp);
        if (nonUniformPage && nonUniformGrp) nonUniformPage->addChild(*nonUniformGrp);
        if (advancedPage && advancedGrp)     advancedPage->addChild(*advancedGrp);
    } else {
        // 【Flame】: すべてのグループを独立した列として並べる
        if (controlsPage && controlsGrp)     controlsPage->addChild(*controlsGrp);
        if (bokehPage && bokehGrp)           bokehPage->addChild(*bokehGrp);
        if (bokehPage && bokehNoiseGrp)      bokehPage->addChild(*bokehNoiseGrp);
        if (nonUniformPage && catseyeGrp)    nonUniformPage->addChild(*catseyeGrp);
        if (nonUniformPage && barndoorsGrp)       nonUniformPage->addChild(*barndoorsGrp);
        if (nonUniformPage && astigmatismGrp)     nonUniformPage->addChild(*astigmatismGrp);
        if (nonUniformPage && axialAberrationGrp) nonUniformPage->addChild(*axialAberrationGrp);
        if (advancedPage && advancedGrp)     advancedPage->addChild(*advancedGrp);
    }

    // ==========================================================
    // 4. パラメータの定義
    // ==========================================================

    // Dev Version (read-only label)
    {
        OFX::StringParamDescriptor* param = desc.defineStringParam(kParamDevVersion);
        param->setLabels("Version", "Version", "Version");
        param->setDefault(kDevVersion);
        param->setStringType(OFX::eStringTypeSingleLine);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Use GPU
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseGpu);
        param->setLabels("Use GPU", "Use GPU", "Use GPU");
        param->setHint("Enable GPU acceleration (wgpu). Disable to force CPU rendering.");
        param->setDefault(true);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Size (defocus radius in pixels)
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSize);
        param->setLabels("Size", "Size", "Size");
        param->setHint("Defocus radius in pixels");
        param->setDefault(10.0);
        param->setRange(0.0, 500.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Focus Plane
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFocusPlane);
        param->setLabels("Focus Plane", "Focus Plane", "Focus Plane");
        param->setHint("Depth value at which the image is in focus");
        param->setDefault(1.0);
        param->setRange(0.0, 10000.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Use Focus Point (toggle)
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseFocusPoint);
        param->setLabels("Use Focus Point", "Use Focus Point", "Use Focus Point");
        param->setHint("Sample depth at XY position to set focus distance. "
            "Don't animate this - use Focus Plane for animation.");
        param->setDefault(false);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Focus Point XY (host provides crosshair overlay widget)
    {
        OFX::Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamFocusPointXY);
        param->setLabels("Focus Point XY", "Focus Point XY", "Focus Point XY");
        param->setHint("Click in the viewer to sample depth at this position.");
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesCanonical);
        param->setDefault(25.0, 25.0);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Quality
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamQuality);
        param->setLabels("Quality", "Quality", "Quality");
        param->setHint("Render quality preset");
        param->appendOption("Low");
        param->appendOption("Medium");
        param->appendOption("High");
        param->appendOption("Custom");
        param->setDefault(0);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Samples (visible when Quality = Custom)
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamSamples);
        param->setLabels("Samples", "Samples", "Samples");
        param->setHint("Number of samples (used with Custom quality)");
        param->setDefault(20);
        param->setRange(1, 256);
        param->setDisplayRange(1, 128);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Mode (2D / Depth)
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMode);
        param->setLabels("Mode", "Mode", "Mode");
        param->setHint("Defocus operating mode");
        param->appendOption("2D");
        param->appendOption("Depth");
        param->setDefault(0);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Math (Direct / 1÷Z / Real)
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMath);
        param->setLabels("Math", "Math", "Math");
        param->setHint("Depth math interpretation mode");
        param->appendOption("Direct");
        param->appendOption("1/Z");
        param->appendOption("Real");
        param->setDefault(1);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Render Result
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamRenderResult);
        param->setLabels("Render Result", "Render Result", "Render Result");
        param->setHint("Render output mode");
        param->appendOption("Result");
        param->appendOption("Focal Plane Setup");
        param->setDefault(0);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Show Image
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamShowImage);
        param->setLabels("Show Image", "Show Image", "Show Image");
        param->setHint("Overlay source image in Focal Plane Setup mode");
        param->setDefault(false);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Protect
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamProtect);
        param->setLabels("Protect", "Protect", "Protect");
        param->setHint("Focal plane protection range");
        param->setDefault(0.0);
        param->setRange(0.0, 10000.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Maximum Size
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamMaxSize);
        param->setLabels("Maximum Size", "Maximum Size", "Maximum Size");
        param->setHint("Maximum defocus radius");
        param->setDefault(10.0);
        param->setRange(0.0, 500.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Gamma Correction
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGammaCorrection);
        param->setLabels("Gamma Correction", "Gamma Correction", "Gamma Correction");
        param->setHint("Gamma correction for bokeh intensities");
        param->setDefault(1.0);
        param->setRange(0.2, 5.0);
        param->setDisplayRange(0.2, 5.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // Farm Quality
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFarmQuality);
        param->setLabels("Farm Quality", "Farm Quality", "Farm Quality");
        param->setHint("Quality preset for farm/batch rendering");
        param->appendOption("Low");
        param->appendOption("Medium");
        param->appendOption("High");
        param->appendOption("Custom");
        param->setDefault(2);
        if (controlsGrp) param->setParent(*controlsGrp);
    }

    // --- Bokeh params (12) ---
    // Filter Type
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFilterType);
        param->setLabels("Filter Type", "Filter Type", "Filter Type");
        param->setHint("Bokeh filter shape");
        param->appendOption("Simple");
        param->appendOption("Disc");
        param->appendOption("Blade");
        param->appendOption("Image");
        param->setDefault(0);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Filter Preview
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamFilterPreview);
        param->setLabels("Filter Preview", "Filter Preview", "Filter Preview");
        param->setHint("Preview the bokeh filter shape instead of the defocus result");
        param->setDefault(false);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Filter Resolution
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamFilterResolution);
        param->setLabels("Filter Resolution", "Filter Resolution", "Filter Resolution");
        param->setHint("Resolution of the generated bokeh filter");
        param->setDefault(256);
        param->setRange(32, 1024);
        param->setDisplayRange(32, 512);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Ring Color
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRingColor);
        param->setLabels("Ring Color", "Ring Color", "Ring Color");
        param->setHint("Brightness of the bokeh ring edge");
        param->setDefault(1.0);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Inner Color
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamInnerColor);
        param->setLabels("Inner Color", "Inner Color", "Inner Color");
        param->setHint("Brightness of the bokeh center");
        param->setDefault(0.4);
        param->setRange(0.001, 1.0);
        param->setDisplayRange(0.001, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Ring Size
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRingSize);
        param->setLabels("Ring Size", "Ring Size", "Ring Size");
        param->setHint("Width of the bokeh ring");
        param->setDefault(0.1);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Outer Blur
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamOuterBlur);
        param->setLabels("Outer Blur", "Outer Blur", "Outer Blur");
        param->setHint("Softness of the outer edge of the bokeh");
        param->setDefault(0.1);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Inner Blur
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamInnerBlur);
        param->setLabels("Inner Blur", "Inner Blur", "Inner Blur");
        param->setHint("Softness of the inner edge of the bokeh");
        param->setDefault(0.05);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Aspect Ratio
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAspectRatio);
        param->setLabels("Aspect Ratio", "Aspect Ratio", "Aspect Ratio");
        param->setHint("Aspect ratio of the bokeh shape");
        param->setDefault(1.0);
        param->setRange(0.0, 2.0);
        param->setDisplayRange(0.0, 2.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Blades
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBlades);
        param->setLabels("Blades", "Blades", "Blades");
        param->setHint("Number of aperture blades");
        param->setDefault(5);
        param->setRange(3, 16);
        param->setDisplayRange(3, 16);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Angle
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAngle);
        param->setLabels("Angle", "Angle", "Angle");
        param->setHint("Rotation angle of the aperture blades in degrees");
        param->setDefault(0.0);
        param->setRange(-180.0, 180.0);
        param->setDisplayRange(-180.0, 180.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Curvature
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCurvature);
        param->setLabels("Curvature", "Curvature", "Curvature");
        param->setHint("Curvature of the aperture blade edges");
        param->setDefault(0.0);
        param->setRange(-1.0, 1.0);
        param->setDisplayRange(-1.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehGrp) param->setParent(*bokehGrp);
    }

    // --- Bokeh Noise params (3) ---
    // Noise Size
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamNoiseSize);
        param->setLabels("Noise Size", "Noise Size", "Noise Size");
        param->setHint("Size of the bokeh noise pattern");
        param->setDefault(0.1);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehNoiseGrp) param->setParent(*bokehNoiseGrp);
        else if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Noise Intensity
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamNoiseIntensity);
        param->setLabels("Noise Intensity", "Noise Intensity", "Noise Intensity");
        param->setHint("Intensity of the bokeh noise pattern");
        param->setDefault(0.25);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (bokehNoiseGrp) param->setParent(*bokehNoiseGrp);
        else if (bokehGrp) param->setParent(*bokehGrp);
    }

    // Noise Seed
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamNoiseSeed);
        param->setLabels("Noise Seed", "Noise Seed", "Noise Seed");
        param->setHint("Random seed for the bokeh noise pattern");
        param->setDefault(0);
        param->setRange(0, 10000);
        param->setDisplayRange(0, 1000);
        if (bokehNoiseGrp) param->setParent(*bokehNoiseGrp);
        else if (bokehGrp) param->setParent(*bokehGrp);
    }

    // --- Catseye params (7) ---
    // Catseye Enable
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCatseyeEnable);
        param->setLabels("Catseye Enable", "Catseye Enable", "Catseye Enable");
        param->setHint("Enable catseye (optical vignetting) effect");
        param->setDefault(false);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Catseye Amount
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCatseyeAmount);
        param->setLabels("Catseye Amount", "Catseye Amount", "Catseye Amount");
        param->setHint("Strength of the catseye effect");
        param->setDefault(0.5);
        param->setRange(0.0, 2.0);
        param->setDisplayRange(0.0, 2.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Catseye Inverse
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCatseyeInverse);
        param->setLabels("Catseye Inverse", "Catseye Inverse", "Catseye Inverse");
        param->setHint("Invert the catseye gradient direction");
        param->setDefault(false);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Catseye Inverse Foreground
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCatseyeInverseForeground);
        param->setLabels("Catseye Inverse Foreground", "Catseye Inverse Foreground", "Catseye Inverse Foreground");
        param->setHint("Apply catseye inverse to foreground defocus");
        param->setDefault(true);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Catseye Gamma
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCatseyeGamma);
        param->setLabels("Catseye Gamma", "Catseye Gamma", "Catseye Gamma");
        param->setHint("Gamma curve for the catseye falloff");
        param->setDefault(1.0);
        param->setRange(0.2, 4.0);
        param->setDisplayRange(0.2, 4.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Catseye Softness
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCatseyeSoftness);
        param->setLabels("Catseye Softness", "Catseye Softness", "Catseye Softness");
        param->setHint("Softness of the catseye transition");
        param->setDefault(0.2);
        param->setRange(0.01, 1.0);
        param->setDisplayRange(0.01, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Catseye Dimension Based
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCatseyeDimensionBased);
        param->setLabels("Catseye Dimension Based", "Catseye Dimension Based", "Catseye Dimension Based");
        param->setHint("Use screen dimensions for catseye calculation");
        param->setDefault(false);
        if (catseyeGrp) param->setParent(*catseyeGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // --- Barndoors params (9) ---
    // Barndoors Enable
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBarndoorsEnable);
        param->setLabels("Barndoors Enable", "Barndoors Enable", "Barndoors Enable");
        param->setHint("Enable barndoors effect for edge defocus control");
        param->setDefault(false);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Amount
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarndoorsAmount);
        param->setLabels("Barndoors Amount", "Barndoors Amount", "Barndoors Amount");
        param->setHint("Strength of the barndoors effect");
        param->setDefault(0.5);
        param->setRange(0.0, 2.0);
        param->setDisplayRange(0.0, 2.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Inverse
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBarndoorsInverse);
        param->setLabels("Barndoors Inverse", "Barndoors Inverse", "Barndoors Inverse");
        param->setHint("Invert the barndoors gradient direction");
        param->setDefault(false);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Inverse Foreground
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBarndoorsInverseForeground);
        param->setLabels("Barndoors Inverse Foreground", "Barndoors Inverse Foreground", "Barndoors Inverse Foreground");
        param->setHint("Apply barndoors inverse to foreground defocus");
        param->setDefault(true);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Gamma
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarndoorsGamma);
        param->setLabels("Barndoors Gamma", "Barndoors Gamma", "Barndoors Gamma");
        param->setHint("Gamma curve for the barndoors falloff");
        param->setDefault(1.0);
        param->setRange(0.2, 4.0);
        param->setDisplayRange(0.2, 4.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Top
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarndoorsTop);
        param->setLabels("Barndoors Top", "Barndoors Top", "Barndoors Top");
        param->setHint("Top barndoor position (percentage)");
        param->setDefault(100.0);
        param->setRange(0.0, 100.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Bottom
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarndoorsBottom);
        param->setLabels("Barndoors Bottom", "Barndoors Bottom", "Barndoors Bottom");
        param->setHint("Bottom barndoor position (percentage)");
        param->setDefault(100.0);
        param->setRange(0.0, 100.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Left
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarndoorsLeft);
        param->setLabels("Barndoors Left", "Barndoors Left", "Barndoors Left");
        param->setHint("Left barndoor position (percentage)");
        param->setDefault(100.0);
        param->setRange(0.0, 100.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Barndoors Right
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarndoorsRight);
        param->setLabels("Barndoors Right", "Barndoors Right", "Barndoors Right");
        param->setHint("Right barndoor position (percentage)");
        param->setDefault(100.0);
        param->setRange(0.0, 100.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (barndoorsGrp) param->setParent(*barndoorsGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // --- Non-Uniform: Astigmatism params (3) ---
    // Astigmatism Enable
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAstigmatismEnable);
        param->setLabels("Astigmatism Enable", "Astigmatism Enable", "Astigmatism Enable");
        param->setHint("Enable astigmatism effect");
        param->setDefault(false);
        if (astigmatismGrp) param->setParent(*astigmatismGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Astigmatism Amount
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAstigmatismAmount);
        param->setLabels("Astigmatism Amount", "Astigmatism Amount", "Astigmatism Amount");
        param->setHint("Amount of the astigmatism stretching effect");
        param->setDefault(0.5);
        param->setRange(0.0, 1.0);
        param->setDisplayRange(0.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (astigmatismGrp) param->setParent(*astigmatismGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Astigmatism Gamma
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAstigmatismGamma);
        param->setLabels("Astigmatism Gamma", "Astigmatism Gamma", "Astigmatism Gamma");
        param->setHint("Gamma correction for the astigmatism falloff");
        param->setDefault(1.0);
        param->setRange(0.2, 4.0);
        param->setDisplayRange(0.2, 4.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (astigmatismGrp) param->setParent(*astigmatismGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // --- Non-Uniform: Axial Aberration params (3) ---
    // Axial Aberration Enable
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAxialAberrationEnable);
        param->setLabels("Axial Aberration Enable", "Axial Aberration Enable", "Axial Aberration Enable");
        param->setHint("Enable axial chromatic aberration effect");
        param->setDefault(false);
        if (axialAberrationGrp) param->setParent(*axialAberrationGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Axial Aberration Amount
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAxialAberrationAmount);
        param->setLabels("Axial Aberration Amount", "Axial Aberration Amount", "Axial Aberration Amount");
        param->setHint("Amount of axial aberration (negative for reverse)");
        param->setDefault(0.5);
        param->setRange(-1.0, 1.0);
        param->setDisplayRange(-1.0, 1.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (axialAberrationGrp) param->setParent(*axialAberrationGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // Axial Aberration Type
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamAxialAberrationType);
        param->setLabels("Axial Aberration Type", "Axial Aberration Type", "Axial Aberration Type");
        param->setHint("Color type for chromatic aberration");
        param->appendOption("Red/Blue");
        param->appendOption("Blue/Yellow");
        param->appendOption("Green/Purple");
        param->setDefault(0);
        if (axialAberrationGrp) param->setParent(*axialAberrationGrp);
        else if (nonUniformGrp) param->setParent(*nonUniformGrp);
    }

    // --- Non-Uniform: Global Inverse Foreground (1) ---
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInverseForeground);
        param->setLabels("Inverse Foreground", "Inverse Foreground", "Inverse Foreground");
        param->setHint("Invert non-uniform effects for foreground filter shape");
        param->setDefault(true);
        if (nonUniformGrp) param->setParent(*nonUniformGrp);
        else if (catseyeGrp) param->setParent(*catseyeGrp);
    }

    // --- Advanced params (2) ---
    // Size Multiplier
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSizeMultiplier);
        param->setLabels("Size Multiplier", "Size Multiplier", "Size Multiplier");
        param->setHint("Multiplier applied to all defocus radii");
        param->setDefault(1.0);
        param->setRange(0.0, 2.0);
        param->setDisplayRange(0.0, 2.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (advancedGrp) param->setParent(*advancedGrp);
    }

    // Focal Plane Offset
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFocalPlaneOffset);
        param->setLabels("Focal Plane Offset", "Focal Plane Offset", "Focal Plane Offset");
        param->setHint("Offset applied to focal plane distance");
        param->setDefault(0.0);
        param->setRange(-5.0, 5.0);
        param->setDisplayRange(-5.0, 5.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (advancedGrp) param->setParent(*advancedGrp);
    }
}

OFX::ImageEffect* OpenDefocusPluginFactory::createInstance(
    OfxImageEffectHandle handle, OFX::ContextEnum /*context*/) {
    return new OpenDefocusPlugin(handle);
}

// ---------------------------------------------------------------------------
// Plugin registration (OFX entry points)
// ---------------------------------------------------------------------------
static OpenDefocusPluginFactory gPluginFactory(kPluginIdentifier,
                                                kPluginVersionMajor,
                                                kPluginVersionMinor);

namespace OFX { namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray& ids) {
    ids.push_back(&gPluginFactory);
}

}} // namespace OFX::Plugin
