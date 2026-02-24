// OpenDefocus OFX Plugin
// v0.1.10-OFX-v1
//
// Porting of OpenDefocus (Nuke NDK) to OpenFX standard.
// This plugin calls into the OpenDefocus Rust core via extern "C" FFI bridge.

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

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

static const char* kClipSource = kOfxImageEffectSimpleSourceClipName;
static const char* kClipDepth  = "Depth";
static const char* kClipOutput = kOfxImageEffectOutputClipName;

// Parameter names — Controls
static const char* kParamSize       = "size";
static const char* kParamFocusPlane = "focusPlane";
static const char* kParamQuality    = "quality";
static const char* kParamSamples    = "samples";

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

// ---------------------------------------------------------------------------
// Plugin: main ImageEffect implementation
// ---------------------------------------------------------------------------
class OpenDefocusPlugin : public OFX::ImageEffect {
public:
    explicit OpenDefocusPlugin(OfxImageEffectHandle handle)
        : OFX::ImageEffect(handle)
        , rustHandle_(nullptr)
    {
        srcClip_   = fetchClip(kClipSource);
        depthClip_ = fetchClip(kClipDepth);
        dstClip_   = fetchClip(kClipOutput);

        sizeParam_       = fetchDoubleParam(kParamSize);
        focusPlaneParam_ = fetchDoubleParam(kParamFocusPlane);
        qualityParam_    = fetchChoiceParam(kParamQuality);
        samplesParam_    = fetchIntParam(kParamSamples);

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

    bool isIdentity(const OFX::IsIdentityArguments& args,
                    OFX::Clip*& identityClip,
                    double& identityTime) override;

    void changedParam(const OFX::InstanceChangedArgs& args,
                      const std::string& paramName) override;

private:
    void updateParamVisibility();

    OFX::Clip* srcClip_   = nullptr;
    OFX::Clip* depthClip_ = nullptr;
    OFX::Clip* dstClip_   = nullptr;

    // Controls
    OFX::DoubleParam* sizeParam_       = nullptr;
    OFX::DoubleParam* focusPlaneParam_ = nullptr;
    OFX::ChoiceParam* qualityParam_    = nullptr;
    OFX::IntParam*    samplesParam_    = nullptr;

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

    OdHandle rustHandle_;
};

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

    // Configure Rust settings
    od_set_size(rustHandle_, static_cast<float>(size));
    od_set_focus_plane(rustHandle_, static_cast<float>(focusPlane));
    od_set_resolution(rustHandle_, static_cast<uint32_t>(width),
                      static_cast<uint32_t>(height));
    od_set_quality(rustHandle_, static_cast<OdQuality>(quality));
    od_set_samples(rustHandle_, static_cast<int32_t>(samples));
    od_set_filter_type(rustHandle_, static_cast<OdFilterType>(filterType));
    od_set_filter_preview(rustHandle_, filterPreview);
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

    // Filter Preview: render bokeh shape at filter_resolution, centered in output
    if (filterPreview && filterType >= 1) {
        int fRes = filterResolution;
        if (fRes < 32) fRes = 32;
        if (fRes > 1024) fRes = 1024;

        // Render bokeh into a small buffer
        std::vector<float> previewBuf(static_cast<size_t>(fRes) * fRes * 4, 0.0f);
        int32_t pFullRegion[4]   = { 0, 0, fRes, fRes };
        int32_t pRenderRegion[4] = { 0, 0, fRes, fRes };

        od_set_resolution(rustHandle_, static_cast<uint32_t>(fRes),
                          static_cast<uint32_t>(fRes));
        od_set_aborted(rustHandle_, false);

        od_render(rustHandle_, previewBuf.data(),
                  static_cast<uint32_t>(fRes), static_cast<uint32_t>(fRes),
                  4, nullptr, 0, 0, pFullRegion, pRenderRegion);

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

    // Copy source pixels into contiguous buffer (OFX images may have row padding)
    std::vector<float> imageBuffer(static_cast<size_t>(width) * height * 4);
    for (int y = rw.y1; y < rw.y2; ++y) {
        const float* srcRow = static_cast<const float*>(src->getPixelAddress(rw.x1, y));
        float* bufRow = imageBuffer.data() + static_cast<size_t>(y - rw.y1) * width * 4;
        if (srcRow) {
            std::memcpy(bufRow, srcRow, width * 4 * sizeof(float));
        }
    }

    // Copy depth if available
    const float* depthPtr = nullptr;
    uint32_t depthW = 0, depthH = 0;
    std::vector<float> depthBuffer;

    if (depth) {
        depthBuffer.resize(static_cast<size_t>(width) * height);
        OFX::PixelComponentEnum depthComp = depth->getPixelComponents();
        int depthStride = (depthComp == OFX::ePixelComponentRGBA) ? 4 : 1;

        for (int y = rw.y1; y < rw.y2; ++y) {
            const float* depthRow = static_cast<const float*>(
                depth->getPixelAddress(rw.x1, y));
            float* bufRow = depthBuffer.data() + static_cast<size_t>(y - rw.y1) * width;
            if (depthRow) {
                for (int x = 0; x < width; ++x) {
                    bufRow[x] = depthRow[x * depthStride];
                }
            }
        }
        depthPtr = depthBuffer.data();
        depthW = static_cast<uint32_t>(width);
        depthH = static_cast<uint32_t>(height);
        od_set_defocus_mode(rustHandle_, DEPTH);
    } else {
        od_set_defocus_mode(rustHandle_, TWO_D);
    }

    // Regions
    int32_t fullRegion[4]   = { 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
    int32_t renderRegion[4] = { 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };

    // Clear abort and render
    od_set_aborted(rustHandle_, false);

    OdResult res = od_render(
        rustHandle_,
        imageBuffer.data(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        4, // RGBA
        depthPtr,
        depthW,
        depthH,
        fullRegion,
        renderRegion);

    // Copy result to OFX output (even on error, to avoid black frames)
    for (int y = rw.y1; y < rw.y2; ++y) {
        float* dstRow = static_cast<float*>(dst->getPixelAddress(rw.x1, y));
        const float* bufRow = imageBuffer.data() + static_cast<size_t>(y - rw.y1) * width * 4;
        if (dstRow) {
            std::memcpy(dstRow, bufRow, width * 4 * sizeof(float));
        }
    }

    if (res != OK) {
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

void OpenDefocusPlugin::changedParam(const OFX::InstanceChangedArgs& /*args*/,
                                      const std::string& paramName) {
    if (paramName == kParamQuality || paramName == kParamFilterType) {
        updateParamVisibility();
    }
}

void OpenDefocusPlugin::updateParamVisibility() {
    // Samples: only enabled when Quality = Custom
    int quality = 0;
    qualityParam_->getValue(quality);
    bool isCustom = (quality == 3); // Custom
    samplesParam_->setEnabled(isCustom);

    // Bokeh parameters: always enabled (matching NUKE NDK original behavior).
    // Parameters have no visual effect when Filter Type = Simple,
    // but remain accessible so users can configure before switching type.
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
        "Port of OpenDefocus to OpenFX. v0.1.10-OFX-v1");

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

    // Thread safety: instance-safe
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
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

    // Output clip
    OFX::ClipDescriptor* dstClip = desc.defineClip(kClipOutput);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->setSupportsTiles(false);

    // --- Parameters ---

    OFX::PageParamDescriptor* page = desc.definePageParam("Controls");

    // Size (defocus radius in pixels)
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSize);
        param->setLabels("Size", "Size", "Size");
        param->setHint("Defocus radius in pixels");
        param->setDefault(10.0);
        param->setRange(0.0, 500.0);
        param->setDisplayRange(0.0, 100.0);
        param->setDoubleType(OFX::eDoubleTypePlain);
        if (page) page->addChild(*param);
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
        if (page) page->addChild(*param);
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
        if (page) page->addChild(*param);
    }

    // Samples (visible when Quality = Custom)
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamSamples);
        param->setLabels("Samples", "Samples", "Samples");
        param->setHint("Number of samples (used with Custom quality)");
        param->setDefault(20);
        param->setRange(1, 256);
        param->setDisplayRange(1, 128);
        if (page) page->addChild(*param);
    }

    // --- Bokeh Page ---
    OFX::PageParamDescriptor* bokehPage = desc.definePageParam("Bokeh");

    // Filter Type
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFilterType);
        param->setLabels("Filter Type", "Filter Type", "Filter Type");
        param->setHint("Bokeh filter shape");
        param->appendOption("Simple");
        param->appendOption("Disc");
        param->appendOption("Blade");
        param->setDefault(0);
        if (bokehPage) bokehPage->addChild(*param);
    }

    // Filter Preview
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamFilterPreview);
        param->setLabels("Filter Preview", "Filter Preview", "Filter Preview");
        param->setHint("Preview the bokeh filter shape instead of the defocus result");
        param->setDefault(false);
        if (bokehPage) bokehPage->addChild(*param);
    }

    // Filter Resolution
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamFilterResolution);
        param->setLabels("Filter Resolution", "Filter Resolution", "Filter Resolution");
        param->setHint("Resolution of the generated bokeh filter");
        param->setDefault(256);
        param->setRange(32, 1024);
        param->setDisplayRange(32, 512);
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
    }

    // Blades
    {
        OFX::IntParamDescriptor* param = desc.defineIntParam(kParamBlades);
        param->setLabels("Blades", "Blades", "Blades");
        param->setHint("Number of aperture blades");
        param->setDefault(5);
        param->setRange(3, 16);
        param->setDisplayRange(3, 16);
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
        if (bokehPage) bokehPage->addChild(*param);
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
