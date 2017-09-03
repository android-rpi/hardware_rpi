//#define LOG_NDEBUG 0
#undef LOG_TAG
#define LOG_TAG "Hwc2Device"
#include <log/log.h>
#include <utils/Trace.h>

#include <sstream>

#include "Hwc2Device.h"
#include "hwc2_rpi.h"

using namespace std::chrono_literals;

static bool operator==(const hwc_color_t& lhs, const hwc_color_t& rhs) {
    return lhs.r == rhs.r &&
            lhs.g == rhs.g &&
            lhs.b == rhs.b &&
            lhs.a == rhs.a;
}

static bool operator==(const hwc_rect_t& lhs, const hwc_rect_t& rhs) {
    return lhs.left == rhs.left &&
            lhs.top == rhs.top &&
            lhs.right == rhs.right &&
            lhs.bottom == rhs.bottom;
}

static bool operator==(const hwc_frect_t& lhs, const hwc_frect_t& rhs) {
    return lhs.left == rhs.left &&
            lhs.top == rhs.top &&
            lhs.right == rhs.right &&
            lhs.bottom == rhs.bottom;
}

template <typename T>
static inline bool operator!=(const T& lhs, const T& rhs)
{
    return !(lhs == rhs);
}

template <typename PFN, typename T>
static hwc2_function_pointer_t asFP(T function)
{
    static_assert(std::is_same<PFN, T>::value, "Incompatible function pointer");
    return reinterpret_cast<hwc2_function_pointer_t>(function);
}

using namespace HWC2;

namespace android {

void Hwc2Device::DisplayContentsDeleter::operator()(
        hwc_display_contents_1_t* contents)
{
    if (contents != nullptr) {
        for (size_t l = 0; l < contents->numHwLayers; ++l) {
            auto& layer = contents->hwLayers[l];
            std::free(const_cast<hwc_rect_t*>(layer.visibleRegionScreen.rects));
        }
    }
    std::free(contents);
}


Hwc2Device::Hwc2Device(hwc_context *context)
  : mHwc1Device(context),
    mPendingVsyncs()
{
	ALOGV("Hwc2Device()");
    common.close = CloseDevice;
    getCapabilities = GetCapabilities;
    getFunction = GetFunction;
    populatePrimary();
}

Hwc2Device::~Hwc2Device() {
}

int Hwc2Device::CloseDevice(hw_device_t *device) {
    delete getDevice((hwc2_device *)device);
    return 0;
}

void Hwc2Device::GetCapabilities(struct hwc2_device */*device*/, uint32_t *outCount,
        int32_t */*hwc2_capability_t outCapabilities*/) {
    *outCount = 0;
}

hwc2_function_pointer_t Hwc2Device::GetFunction(struct hwc2_device *device,
        int32_t /*hwc2_function_descriptor_t*/ intDesc) {
    if (!device) {
        ALOGE("NULL hwc2_device");
	    return nullptr;
	}
    auto descriptor = static_cast<HWC2::FunctionDescriptor>(intDesc);
    switch (descriptor) {
        // Device functions
        case FunctionDescriptor::CreateVirtualDisplay:
            return asFP<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(
                    CreateVirtualDisplay);
        case FunctionDescriptor::DestroyVirtualDisplay:
            return asFP<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(
                    DestroyVirtualDisplay);
        case FunctionDescriptor::Dump:
            return asFP<HWC2_PFN_DUMP>(Dump);
        case FunctionDescriptor::GetMaxVirtualDisplayCount:
            return asFP<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(
                    GetMaxVirtualDisplayCount);
        case FunctionDescriptor::RegisterCallback:
            return asFP<HWC2_PFN_REGISTER_CALLBACK>(RegisterCallback);

        // Display functions
        case FunctionDescriptor::AcceptDisplayChanges:
            return asFP<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(
                    displayHook<decltype(&Display::acceptChanges),
                    &Display::acceptChanges>);
        case FunctionDescriptor::CreateLayer:
            return asFP<HWC2_PFN_CREATE_LAYER>(
                    displayHook<decltype(&Display::createLayer),
                    &Display::createLayer, hwc2_layer_t*>);
        case FunctionDescriptor::DestroyLayer:
            return asFP<HWC2_PFN_DESTROY_LAYER>(
                    displayHook<decltype(&Display::destroyLayer),
                    &Display::destroyLayer, hwc2_layer_t>);
        case FunctionDescriptor::GetActiveConfig:
            return asFP<HWC2_PFN_GET_ACTIVE_CONFIG>(
                    displayHook<decltype(&Display::getActiveConfig),
                    &Display::getActiveConfig, hwc2_config_t*>);
        case FunctionDescriptor::GetChangedCompositionTypes:
            return asFP<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(
                    displayHook<decltype(&Display::getChangedCompositionTypes),
                    &Display::getChangedCompositionTypes, uint32_t*,
                    hwc2_layer_t*, int32_t*>);
        case FunctionDescriptor::GetColorModes:
            return asFP<HWC2_PFN_GET_COLOR_MODES>(
                    displayHook<decltype(&Display::getColorModes),
                    &Display::getColorModes, uint32_t*, int32_t*>);
        case FunctionDescriptor::GetDisplayAttribute:
            return asFP<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(
                    getDisplayAttributeHook);
        case FunctionDescriptor::GetDisplayConfigs:
            return asFP<HWC2_PFN_GET_DISPLAY_CONFIGS>(
                    displayHook<decltype(&Display::getConfigs),
                    &Display::getConfigs, uint32_t*, hwc2_config_t*>);
        case FunctionDescriptor::GetDisplayName:
            return asFP<HWC2_PFN_GET_DISPLAY_NAME>(
                    displayHook<decltype(&Display::getName),
                    &Display::getName, uint32_t*, char*>);
        case FunctionDescriptor::GetDisplayRequests:
            return asFP<HWC2_PFN_GET_DISPLAY_REQUESTS>(
                    displayHook<decltype(&Display::getRequests),
                    &Display::getRequests, int32_t*, uint32_t*, hwc2_layer_t*,
                    int32_t*>);
        case FunctionDescriptor::GetDisplayType:
            return asFP<HWC2_PFN_GET_DISPLAY_TYPE>(
                    displayHook<decltype(&Display::getType),
                    &Display::getType, int32_t*>);
        case FunctionDescriptor::GetDozeSupport:
            return asFP<HWC2_PFN_GET_DOZE_SUPPORT>(
                    displayHook<decltype(&Display::getDozeSupport),
                    &Display::getDozeSupport, int32_t*>);
        case FunctionDescriptor::GetHdrCapabilities:
            return asFP<HWC2_PFN_GET_HDR_CAPABILITIES>(
                    displayHook<decltype(&Display::getHdrCapabilities),
                    &Display::getHdrCapabilities, uint32_t*, int32_t*, float*,
                    float*, float*>);
        case FunctionDescriptor::GetReleaseFences:
            return asFP<HWC2_PFN_GET_RELEASE_FENCES>(
                    displayHook<decltype(&Display::getReleaseFences),
                    &Display::getReleaseFences, uint32_t*, hwc2_layer_t*,
                    int32_t*>);
        case FunctionDescriptor::PresentDisplay:
            return asFP<HWC2_PFN_PRESENT_DISPLAY>(
                    displayHook<decltype(&Display::presentDisplay),
                    &Display::presentDisplay, int32_t*>);
        case FunctionDescriptor::SetActiveConfig:
            return asFP<HWC2_PFN_SET_ACTIVE_CONFIG>(
                    displayHook<decltype(&Display::setActiveConfig),
                    &Display::setActiveConfig, hwc2_config_t>);
        case FunctionDescriptor::SetClientTarget:
            return asFP<HWC2_PFN_SET_CLIENT_TARGET>(
                    displayHook<decltype(&Display::setClientTarget),
                    &Display::setClientTarget, buffer_handle_t, int32_t,
                    int32_t, hwc_region_t>);
        case FunctionDescriptor::SetColorMode:
            return asFP<HWC2_PFN_SET_COLOR_MODE>(setColorModeHook);
        case FunctionDescriptor::SetColorTransform:
            return asFP<HWC2_PFN_SET_COLOR_TRANSFORM>(setColorTransformHook);
        case FunctionDescriptor::SetOutputBuffer:
            return asFP<HWC2_PFN_SET_OUTPUT_BUFFER>(
                    displayHook<decltype(&Display::setOutputBuffer),
                    &Display::setOutputBuffer, buffer_handle_t, int32_t>);
        case FunctionDescriptor::SetPowerMode:
            return asFP<HWC2_PFN_SET_POWER_MODE>(setPowerModeHook);
        case FunctionDescriptor::SetVsyncEnabled:
            return asFP<HWC2_PFN_SET_VSYNC_ENABLED>(setVsyncEnabledHook);
        case FunctionDescriptor::ValidateDisplay:
            return asFP<HWC2_PFN_VALIDATE_DISPLAY>(
                    displayHook<decltype(&Display::validateDisplay),
                    &Display::validateDisplay, uint32_t*, uint32_t*>);
        case FunctionDescriptor::GetClientTargetSupport:
            return asFP<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(
                    displayHook<decltype(&Display::getClientTargetSupport),
                    &Display::getClientTargetSupport, uint32_t, uint32_t,
                                                      int32_t, int32_t>);

        // Layer functions
        case FunctionDescriptor::SetCursorPosition:
            return asFP<HWC2_PFN_SET_CURSOR_POSITION>(
                    layerHook<decltype(&Layer::setCursorPosition),
                    &Layer::setCursorPosition, int32_t, int32_t>);
        case FunctionDescriptor::SetLayerBuffer:
            return asFP<HWC2_PFN_SET_LAYER_BUFFER>(
                    layerHook<decltype(&Layer::setBuffer), &Layer::setBuffer,
                    buffer_handle_t, int32_t>);
        case FunctionDescriptor::SetLayerSurfaceDamage:
            return asFP<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(
                    layerHook<decltype(&Layer::setSurfaceDamage),
                    &Layer::setSurfaceDamage, hwc_region_t>);

        // Layer state functions
        case FunctionDescriptor::SetLayerBlendMode:
            return asFP<HWC2_PFN_SET_LAYER_BLEND_MODE>(
                    setLayerBlendModeHook);
        case FunctionDescriptor::SetLayerColor:
            return asFP<HWC2_PFN_SET_LAYER_COLOR>(
                    layerHook<decltype(&Layer::setColor), &Layer::setColor,
                    hwc_color_t>);
        case FunctionDescriptor::SetLayerCompositionType:
            return asFP<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(
                    setLayerCompositionTypeHook);
        case FunctionDescriptor::SetLayerDataspace:
            return asFP<HWC2_PFN_SET_LAYER_DATASPACE>(setLayerDataspaceHook);
        case FunctionDescriptor::SetLayerDisplayFrame:
            return asFP<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(
                    layerHook<decltype(&Layer::setDisplayFrame),
                    &Layer::setDisplayFrame, hwc_rect_t>);
        case FunctionDescriptor::SetLayerPlaneAlpha:
            return asFP<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(
                    layerHook<decltype(&Layer::setPlaneAlpha),
                    &Layer::setPlaneAlpha, float>);
        case FunctionDescriptor::SetLayerSidebandStream:
            return asFP<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(
                    layerHook<decltype(&Layer::setSidebandStream),
                    &Layer::setSidebandStream, const native_handle_t*>);
        case FunctionDescriptor::SetLayerSourceCrop:
            return asFP<HWC2_PFN_SET_LAYER_SOURCE_CROP>(
                    layerHook<decltype(&Layer::setSourceCrop),
                    &Layer::setSourceCrop, hwc_frect_t>);
        case FunctionDescriptor::SetLayerTransform:
            return asFP<HWC2_PFN_SET_LAYER_TRANSFORM>(setLayerTransformHook);
        case FunctionDescriptor::SetLayerVisibleRegion:
            return asFP<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(
                    layerHook<decltype(&Layer::setVisibleRegion),
                    &Layer::setVisibleRegion, hwc_region_t>);
        case FunctionDescriptor::SetLayerZOrder:
            return asFP<HWC2_PFN_SET_LAYER_Z_ORDER>(setLayerZOrderHook);

        default:
            ALOGE("GetFunction: Unknown function descriptor: %d (%s)",
                    static_cast<int32_t>(descriptor),
                    to_string(descriptor).c_str());
            return nullptr;
    }
}

// Device functions

void Hwc2Device::dump(uint32_t* outSize, char* outBuffer)
{
    if (outBuffer != nullptr) {
        auto copiedBytes = mDumpString.copy(outBuffer, *outSize);
        *outSize = static_cast<uint32_t>(copiedBytes);
        return;
    }

    std::stringstream output;

    output << "-- Hwc2Device --\n";

    // Attempt to acquire the lock for 1 second, but proceed without the lock
    // after that, so we can still get some information if we're deadlocked
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex,
            std::defer_lock);
    lock.try_lock_for(1s);

    output << "Capabilities: None\n";

    output << "Displays:\n";
    //output << mDisplay->dump();
    output << '\n';

    lock.unlock();

    mDumpString = output.str();
    *outSize = static_cast<uint32_t>(mDumpString.size());
}

static bool isValid(Callback descriptor) {
    switch (descriptor) {
        case Callback::Hotplug: // Fall-through
        case Callback::Refresh: // Fall-through
        case Callback::Vsync: return true;
        default: return false;
    }
}

Error Hwc2Device::registerCallback(Callback descriptor,
        hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer)
{
    if (!isValid(descriptor)) {
        return Error::BadParameter;
    }

    ALOGV("registerCallback(%s, %p, %p)", to_string(descriptor).c_str(),
            callbackData, pointer);

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    mCallbacks[descriptor] = {callbackData, pointer};

    std::vector<hwc2_display_t> displayIds;
    std::vector<std::pair<hwc2_display_t, int64_t>> pendingVsyncs;
    std::vector<std::pair<hwc2_display_t, int>> pendingHotplugs;

    if (descriptor == Callback::Vsync) {
        for (auto pending : mPendingVsyncs) {
            auto displayId = mDisplay->getId();
            pendingVsyncs.emplace_back(displayId, pending);
        }
        mPendingVsyncs.clear();
    } else if (descriptor == Callback::Hotplug) {
        // Hotplug the primary display
        pendingHotplugs.emplace_back(mDisplay->getId(),
                static_cast<int32_t>(Connection::Connected));
    }

    // Call pending callbacks without the state lock held
    lock.unlock();

    if (!pendingVsyncs.empty()) {
        auto vsync = reinterpret_cast<HWC2_PFN_VSYNC>(pointer);
        for (auto& pendingVsync : pendingVsyncs) {
            vsync(callbackData, pendingVsync.first, pendingVsync.second);
        }
    }
    if (!pendingHotplugs.empty()) {
        auto hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
        for (auto& pendingHotplug : pendingHotplugs) {
            hotplug(callbackData, pendingHotplug.first, pendingHotplug.second);
        }
    }
    return Error::None;
}

std::atomic<hwc2_display_t> Hwc2Device::Display::sNextId(1);

Hwc2Device::Display::Display(Hwc2Device& device, HWC2::DisplayType type)
  : mId(sNextId++),
    mDevice(device),
    mHwc1Id(-1),
    mConfigs(),
    mActiveConfig(nullptr),
    mName(),
    mType(type) {}

Error Hwc2Device::Display::acceptChanges()
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        ALOGV("[%" PRIu64 "] acceptChanges failed, not validated", mId);
        return Error::NotValidated;
    }

    ALOGV("[%" PRIu64 "] acceptChanges", mId);

    for (auto& change : mChanges->getTypeChanges()) {
        auto layerId = change.first;
        auto type = change.second;
        auto layer = mDevice.mLayers[layerId];
        layer->setCompositionType(type);
    }

    mChanges->clearTypeChanges();

    mHwc1RequestedContents = std::move(mHwc1ReceivedContents);

    return Error::None;
}

Error Hwc2Device::Display::createLayer(hwc2_layer_t* outLayerId)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto layer = *mLayers.emplace(std::make_shared<Layer>(*this));
    mDevice.mLayers.emplace(std::make_pair(layer->getId(), layer));
    *outLayerId = layer->getId();
    ALOGV("[%" PRIu64 "] created layer %" PRIu64, mId, *outLayerId);
    return Error::None;
}

Error Hwc2Device::Display::destroyLayer(hwc2_layer_t layerId)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    const auto mapLayer = mDevice.mLayers.find(layerId);
    if (mapLayer == mDevice.mLayers.end()) {
        ALOGV("[%" PRIu64 "] destroyLayer(%" PRIu64 ") failed: no such layer",
                mId, layerId);
        return Error::BadLayer;
    }
    const auto layer = mapLayer->second;
    mDevice.mLayers.erase(mapLayer);
    const auto zRange = mLayers.equal_range(layer);
    for (auto current = zRange.first; current != zRange.second; ++current) {
        if (**current == *layer) {
            current = mLayers.erase(current);
            break;
        }
    }
    ALOGV("[%" PRIu64 "] destroyed layer %" PRIu64, mId, layerId);
    return Error::None;
}

Error Hwc2Device::Display::getActiveConfig(hwc2_config_t* outConfig)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mActiveConfig) {
        ALOGV("[%" PRIu64 "] getActiveConfig --> %s", mId,
                to_string(Error::BadConfig).c_str());
        return Error::BadConfig;
    }
    auto configId = mActiveConfig->getId();
    ALOGV("[%" PRIu64 "] getActiveConfig --> %u", mId, configId);
    *outConfig = configId;
    return Error::None;
}

Error Hwc2Device::Display::getAttribute(hwc2_config_t configId,
        Attribute attribute, int32_t* outValue)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (configId > mConfigs.size() || !mConfigs[configId]->isOnDisplay(*this)) {
        ALOGV("[%" PRIu64 "] getAttribute failed: bad config (%u)", mId,
                configId);
        return Error::BadConfig;
    }
    *outValue = mConfigs[configId]->getAttribute(attribute);
    ALOGV("[%" PRIu64 "] getAttribute(%u, %s) --> %d", mId, configId,
            to_string(attribute).c_str(), *outValue);
    return Error::None;
}

Error Hwc2Device::Display::getChangedCompositionTypes(
        uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outTypes)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        ALOGE("[%" PRIu64 "] getChangedCompositionTypes failed: not validated",
                mId);
        return Error::NotValidated;
    }

    if ((outLayers == nullptr) || (outTypes == nullptr)) {
        *outNumElements = mChanges->getTypeChanges().size();
        return Error::None;
    }

    uint32_t numWritten = 0;
    for (const auto& element : mChanges->getTypeChanges()) {
        if (numWritten == *outNumElements) {
            break;
        }
        auto layerId = element.first;
        auto intType = static_cast<int32_t>(element.second);
        ALOGV("Adding %" PRIu64 " %s", layerId,
                to_string(element.second).c_str());
        outLayers[numWritten] = layerId;
        outTypes[numWritten] = intType;
        ++numWritten;
    }
    *outNumElements = numWritten;

    return Error::None;
}

Error Hwc2Device::Display::getColorModes(uint32_t* outNumModes,
        int32_t* outModes)
{
    *outNumModes = 1;
    if (outModes != NULL) {
        *outModes = HAL_COLOR_MODE_NATIVE;
    }
    return Error::None;
}



Error Hwc2Device::Display::getConfigs(uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!outConfigs) {
        *outNumConfigs = mConfigs.size();
        return Error::None;
    }
    uint32_t numWritten = 0;
    for (const auto& config : mConfigs) {
        if (numWritten == *outNumConfigs) {
            break;
        }
        outConfigs[numWritten] = config->getId();
        ++numWritten;
    }
    *outNumConfigs = numWritten;
    return Error::None;
}

Error Hwc2Device::Display::getDozeSupport(int32_t* outSupport)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    *outSupport = 0;
    return Error::None;
}

Error Hwc2Device::Display::getHdrCapabilities(uint32_t* outNumTypes,
        int32_t* /*outTypes*/, float* /*outMaxLuminance*/,
        float* /*outMaxAverageLuminance*/, float* /*outMinLuminance*/)
{
    *outNumTypes = 0;
    return Error::None;
}

Error Hwc2Device::Display::getName(uint32_t* outSize, char* outName)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!outName) {
        *outSize = mName.size();
        return Error::None;
    }
    auto numCopied = mName.copy(outName, *outSize);
    *outSize = numCopied;
    return Error::None;
}

Error Hwc2Device::Display::getReleaseFences(uint32_t* outNumElements,
        hwc2_layer_t* outLayers, int32_t* outFences)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    uint32_t numWritten = 0;
    bool outputsNonNull = (outLayers != nullptr) && (outFences != nullptr);
    for (const auto& layer : mLayers) {
        if (outputsNonNull && (numWritten == *outNumElements)) {
            break;
        }

        auto releaseFence = layer->getReleaseFence();
        if (releaseFence != Fence::NO_FENCE) {
            if (outputsNonNull) {
                outLayers[numWritten] = layer->getId();
                outFences[numWritten] = releaseFence->dup();
            }
            ++numWritten;
        }
    }
    *outNumElements = numWritten;

    return Error::None;
}

Error Hwc2Device::Display::getRequests(int32_t* outDisplayRequests,
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* outLayerRequests)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges) {
        return Error::NotValidated;
    }

    if (outLayers == nullptr || outLayerRequests == nullptr) {
        *outNumElements = mChanges->getNumLayerRequests();
        return Error::None;
    }

    *outDisplayRequests = mChanges->getDisplayRequests();
    uint32_t numWritten = 0;
    for (const auto& request : mChanges->getLayerRequests()) {
        if (numWritten == *outNumElements) {
            break;
        }
        outLayers[numWritten] = request.first;
        outLayerRequests[numWritten] = static_cast<int32_t>(request.second);
        ++numWritten;
    }

    return Error::None;
}

Error Hwc2Device::Display::getType(int32_t* outType)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    *outType = static_cast<int32_t>(mType);
    return Error::None;
}


Error Hwc2Device::Display::setActiveConfig(hwc2_config_t configId)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto config = getConfig(configId);
    if (!config) {
        return Error::BadConfig;
    }
    if (config == mActiveConfig) {
        return Error::None;
    }

    return Error::None;
}

Error Hwc2Device::Display::setClientTarget(buffer_handle_t target,
        int32_t acquireFence, int32_t /*dataspace*/, hwc_region_t /*damage*/)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] setClientTarget(%p, %d)", mId, target, acquireFence);
    mClientTarget.setBuffer(target);
    mClientTarget.setFence(acquireFence);
    // dataspace and damage can't be used by HWC1, so ignore them
    return Error::None;
}

Error Hwc2Device::Display::setColorMode(android_color_mode_t mode)
{
    ALOGV("[%" PRIu64 "] setColorMode(%d)", mId, mode);
    return Error::None;
}

Error Hwc2Device::Display::setColorTransform(android_color_transform_t hint)
{
    ALOGV("%" PRIu64 "] setColorTransform(%d)", mId,
            static_cast<int32_t>(hint));
    return Error::None;
}

Error Hwc2Device::Display::setOutputBuffer(buffer_handle_t buffer,
        int32_t releaseFence)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] setOutputBuffer(%p, %d)", mId, buffer, releaseFence);
    mOutputBuffer.setBuffer(buffer);
    mOutputBuffer.setFence(releaseFence);
    return Error::None;
}

static bool isValid(PowerMode mode)
{
    switch (mode) {
        case PowerMode::Off: // Fall-through
        case PowerMode::DozeSuspend: // Fall-through
        case PowerMode::Doze: // Fall-through
        case PowerMode::On: return true;
        default: return false;
    }
}

Error Hwc2Device::Display::setPowerMode(PowerMode mode)
{
    if (!isValid(mode)) {
        return Error::BadParameter;
    }
    if (mode == mPowerMode) {
        return Error::None;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    int error = 0;
    /*error = mDevice.mHwc1Device->blank(mDevice.mHwc1Device, mHwc1Id,
                mode == PowerMode::Off);*/
    ALOGE_IF(error != 0, "setPowerMode: Failed to set power mode on HWC1 (%d)",
            error);

    ALOGV("[%" PRIu64 "] setPowerMode(%s)", mId, to_string(mode).c_str());
    mPowerMode = mode;
    return Error::None;
}

static bool isValid(Vsync enable) {
    switch (enable) {
        case Vsync::Enable: // Fall-through
        case Vsync::Disable: return true;
        default: return false;
    }
}

Error Hwc2Device::Display::setVsyncEnabled(Vsync enable)
{
    if (!isValid(enable)) {
        return Error::BadParameter;
    }
    if (enable == mVsyncEnabled) {
        return Error::None;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mDevice.mHwc1Device->vsync_thread->set_enabled(enable == Vsync::Enable);

    mVsyncEnabled = enable;
    return Error::None;
}


Error Hwc2Device::Display::validateDisplay(uint32_t* outNumTypes,
        uint32_t* outNumRequests)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] Entering validate", mId);

    if (!mChanges) {
        if (!mDevice.prepareDisplay()) {
            return Error::BadDisplay;
        }
    }

    *outNumTypes = mChanges->getNumTypes();
    *outNumRequests = mChanges->getNumLayerRequests();
    ALOGV("[%" PRIu64 "] validate --> %u types, %u requests", mId, *outNumTypes,
            *outNumRequests);
    for (auto request : mChanges->getTypeChanges()) {
        ALOGV("Layer %" PRIu64 " --> %s", request.first,
                to_string(request.second).c_str());
    }
    return *outNumTypes > 0 ? Error::HasChanges : Error::None;
}

// Display helpers

Error Hwc2Device::Display::updateLayerZ(hwc2_layer_t layerId, uint32_t z)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    const auto mapLayer = mDevice.mLayers.find(layerId);
    if (mapLayer == mDevice.mLayers.end()) {
        ALOGE("[%" PRIu64 "] updateLayerZ failed to find layer", mId);
        return Error::BadLayer;
    }

    const auto layer = mapLayer->second;
    const auto zRange = mLayers.equal_range(layer);
    bool layerOnDisplay = false;
    for (auto current = zRange.first; current != zRange.second; ++current) {
        if (**current == *layer) {
            if ((*current)->getZ() == z) {
                // Don't change anything if the Z hasn't changed
                return Error::None;
            }
            current = mLayers.erase(current);
            layerOnDisplay = true;
            break;
        }
    }

    if (!layerOnDisplay) {
        ALOGE("[%" PRIu64 "] updateLayerZ failed to find layer on display",
                mId);
        return Error::BadLayer;
    }

    layer->setZ(z);
    mLayers.emplace(std::move(layer));
    mZIsDirty = true;

    return Error::None;
}

Error Hwc2Device::Display::getClientTargetSupport(uint32_t width, uint32_t height,
                                      int32_t format, int32_t dataspace){
    if (mActiveConfig == nullptr) {
        return Error::Unsupported;
    }

    if (width == mActiveConfig->getAttribute(Attribute::Width) &&
            height == mActiveConfig->getAttribute(Attribute::Height) &&
            format == HAL_PIXEL_FORMAT_RGBA_8888 &&
            dataspace == HAL_DATASPACE_UNKNOWN) {
        return Error::None;
    }

    return Error::Unsupported;
}

Error Hwc2Device::Display::presentDisplay(int32_t* outRetireFence)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (mChanges) {
        Error error = mDevice.setDisplay();
        if (error != Error::None) {
            ALOGE("[%" PRIu64 "] present: setAllDisplaysFailed (%s)", mId,
                    to_string(error).c_str());
            return error;
        }
    }

    *outRetireFence = mRetireFence.get()->dup();
    ALOGV("[%" PRIu64 "] present returning retire fence %d", mId,
            *outRetireFence);

    return Error::None;
}

static constexpr uint32_t ATTRIBUTES[] = {
    HWC_DISPLAY_VSYNC_PERIOD,
    HWC_DISPLAY_WIDTH,
    HWC_DISPLAY_HEIGHT,
    HWC_DISPLAY_DPI_X,
    HWC_DISPLAY_DPI_Y,
    HWC_DISPLAY_NO_ATTRIBUTE,
};

static constexpr size_t NUM_ATTRIBUTES =
        sizeof(ATTRIBUTES) / sizeof(uint32_t);

static constexpr uint32_t ATTRIBUTE_MAP[] = {
    5, // HWC_DISPLAY_NO_ATTRIBUTE = 0
    0, // HWC_DISPLAY_VSYNC_PERIOD = 1,
    1, // HWC_DISPLAY_WIDTH = 2,
    2, // HWC_DISPLAY_HEIGHT = 3,
    3, // HWC_DISPLAY_DPI_X = 4,
    4, // HWC_DISPLAY_DPI_Y = 5,
};

template <uint32_t attribute>
static constexpr bool attributesMatch() {
    return attribute ==
            ATTRIBUTES[ATTRIBUTE_MAP[attribute]];
}
static_assert(attributesMatch<HWC_DISPLAY_VSYNC_PERIOD>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_WIDTH>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_HEIGHT>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_DPI_X>(), "Tables out of sync");
static_assert(attributesMatch<HWC_DISPLAY_DPI_Y>(), "Tables out of sync");

void Hwc2Device::Display::populateConfigs()
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    ALOGV("[%" PRIu64 "] populateConfigs", mId);

    if (mHwc1Id == -1) {
        ALOGE("populateConfigs: HWC1 ID not set");
        return;
    }

	uint32_t hwc1ConfigId = 0;
	auto newConfig = std::make_shared<Config>(*this);

	int32_t values[NUM_ATTRIBUTES] = {};
	bool hasColor = false;
	auto result = mDevice.mHwc1Device->getDisplayAttributes(
			mHwc1Id, hwc1ConfigId, ATTRIBUTES, values);

	newConfig->setAttribute(Attribute::VsyncPeriod,
			values[ATTRIBUTE_MAP[HWC_DISPLAY_VSYNC_PERIOD]]);
	newConfig->setAttribute(Attribute::Width,
			values[ATTRIBUTE_MAP[HWC_DISPLAY_WIDTH]]);
	newConfig->setAttribute(Attribute::Height,
			values[ATTRIBUTE_MAP[HWC_DISPLAY_HEIGHT]]);
	newConfig->setAttribute(Attribute::DpiX,
			values[ATTRIBUTE_MAP[HWC_DISPLAY_DPI_X]]);
	newConfig->setAttribute(Attribute::DpiY,
			values[ATTRIBUTE_MAP[HWC_DISPLAY_DPI_Y]]);
	newConfig->setHwc1Id(hwc1ConfigId);

	for (auto& existingConfig : mConfigs) {
		if (existingConfig->merge(*newConfig)) {
			ALOGV("Merged config %d with existing config %u: %s",
					hwc1ConfigId, existingConfig->getId(),
					existingConfig->toString().c_str());
			newConfig.reset();
			break;
		}
	}

	// If it wasn't merged with any existing config, add it to the end
	if (newConfig) {
		newConfig->setId(static_cast<hwc2_config_t>(mConfigs.size()));
		ALOGV("Found new config %u: %s", newConfig->getId(),
				newConfig->toString().c_str());
		mConfigs.emplace_back(std::move(newConfig));
	}

    initializeActiveConfig();
}

bool Hwc2Device::Display::prepare()
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    // It doesn't make sense to prepare a display for which there is no active
    // config, so return early
    if (!mActiveConfig) {
        ALOGE("[%" PRIu64 "] Attempted to prepare, but no config active", mId);
        return false;
    }

    ALOGV("[%" PRIu64 "] Entering prepare", mId);

    auto currentCount = mHwc1RequestedContents ?
            mHwc1RequestedContents->numHwLayers : 0;
    auto requiredCount = mLayers.size() + 1;
    ALOGV("[%" PRIu64 "]   Requires %zd layers, %zd allocated in %p", mId,
            requiredCount, currentCount, mHwc1RequestedContents.get());

    bool layerCountChanged = (currentCount != requiredCount);
    if (layerCountChanged) {
        reallocateHwc1Contents();
    }

    bool applyAllState = false;
    if (layerCountChanged || mZIsDirty) {
        assignHwc1LayerIds();
        mZIsDirty = false;
        applyAllState = true;
    }

    mHwc1RequestedContents->retireFenceFd = -1;
    mHwc1RequestedContents->flags = 0;
    if (isDirty() || applyAllState) {
        mHwc1RequestedContents->flags |= HWC_GEOMETRY_CHANGED;
    }

    for (auto& layer : mLayers) {
        auto& hwc1Layer = mHwc1RequestedContents->hwLayers[layer->getHwc1Id()];
        hwc1Layer.releaseFenceFd = -1;
        layer->applyState(hwc1Layer, applyAllState);
    }

    mHwc1RequestedContents->outbuf = mOutputBuffer.getBuffer();
    mHwc1RequestedContents->outbufAcquireFenceFd = mOutputBuffer.getFence();

    prepareFramebufferTarget();

    return true;
}

static std::string rectString(hwc_rect_t rect)
{
    std::stringstream output;
    output << "[" << rect.left << ", " << rect.top << ", ";
    output << rect.right << ", " << rect.bottom << "]";
    return output.str();
}

static std::string colorString(hwc_color_t color)
{
    std::stringstream output;
    output << "RGBA [";
    output << static_cast<int32_t>(color.r) << ", ";
    output << static_cast<int32_t>(color.g) << ", ";
    output << static_cast<int32_t>(color.b) << ", ";
    output << static_cast<int32_t>(color.a) << "]";
    return output.str();
}


void Hwc2Device::Display::Config::setAttribute(HWC2::Attribute attribute,
        int32_t value)
{
    mAttributes[attribute] = value;
}

bool Hwc2Device::Display::Config::merge(const Config& other)
{
    auto attributes = {HWC2::Attribute::Width, HWC2::Attribute::Height,
            HWC2::Attribute::VsyncPeriod, HWC2::Attribute::DpiX,
            HWC2::Attribute::DpiY};
    for (auto attribute : attributes) {
        if (getAttribute(attribute) != other.getAttribute(attribute)) {
            return false;
        }
    }
    return true;
}


void Hwc2Device::Display::setReceivedContents(HWC1Contents contents)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mHwc1ReceivedContents = std::move(contents);

    mChanges.reset(new Changes);

    size_t numLayers = mHwc1ReceivedContents->numHwLayers;
    for (size_t hwc1Id = 0; hwc1Id < numLayers; ++hwc1Id) {
        const auto& receivedLayer = mHwc1ReceivedContents->hwLayers[hwc1Id];
        if (mHwc1LayerMap.count(hwc1Id) == 0) {
            ALOGE_IF(receivedLayer.compositionType != HWC_FRAMEBUFFER_TARGET,
                    "setReceivedContents: HWC1 layer %zd doesn't have a"
                    " matching HWC2 layer, and isn't the framebuffer target",
                    hwc1Id);
            continue;
        }

        Layer& layer = *mHwc1LayerMap[hwc1Id];
        updateTypeChanges(receivedLayer, layer);
        updateLayerRequests(receivedLayer, layer);
    }
}

bool Hwc2Device::Display::hasChanges() const
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    return mChanges != nullptr;
}


Error Hwc2Device::Display::set(hwc_display_contents_1& hwcContents)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mChanges || (mChanges->getNumTypes() > 0)) {
        ALOGE("[%" PRIu64 "] set failed: not validated", mId);
        return Error::NotValidated;
    }

    // Set up the client/framebuffer target
    auto numLayers = hwcContents.numHwLayers;

    // Close acquire fences on FRAMEBUFFER layers, since they will not be used
    // by HWC
    for (size_t l = 0; l < numLayers - 1; ++l) {
        auto& layer = hwcContents.hwLayers[l];
        if (layer.compositionType == HWC_FRAMEBUFFER) {
            ALOGV("Closing fence %d for layer %zd", layer.acquireFenceFd, l);
            close(layer.acquireFenceFd);
            layer.acquireFenceFd = -1;
        }
    }

    auto& clientTargetLayer = hwcContents.hwLayers[numLayers - 1];
    if (clientTargetLayer.compositionType == HWC_FRAMEBUFFER_TARGET) {
        clientTargetLayer.handle = mClientTarget.getBuffer();
        clientTargetLayer.acquireFenceFd = mClientTarget.getFence();
    } else {
        ALOGE("[%" PRIu64 "] set: last HWC layer wasn't FRAMEBUFFER_TARGET",
                mId);
    }

    mChanges.reset();

    return Error::None;
}

void Hwc2Device::Display::addRetireFence(int fenceFd)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);
    mRetireFence.add(fenceFd);
}

void Hwc2Device::Display::addReleaseFences(
        const hwc_display_contents_1_t& hwcContents)
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    size_t numLayers = hwcContents.numHwLayers;
    for (size_t hwc1Id = 0; hwc1Id < numLayers; ++hwc1Id) {
        const auto& receivedLayer = hwcContents.hwLayers[hwc1Id];
        if (mHwc1LayerMap.count(hwc1Id) == 0) {
            if (receivedLayer.compositionType != HWC_FRAMEBUFFER_TARGET) {
                ALOGE("addReleaseFences: HWC1 layer %zd doesn't have a"
                        " matching HWC2 layer, and isn't the framebuffer"
                        " target", hwc1Id);
            }
            // Close the framebuffer target release fence since we will use the
            // display retire fence instead
            if (receivedLayer.releaseFenceFd != -1) {
                close(receivedLayer.releaseFenceFd);
            }
            continue;
        }

        Layer& layer = *mHwc1LayerMap[hwc1Id];
        ALOGV("Adding release fence %d to layer %" PRIu64,
                receivedLayer.releaseFenceFd, layer.getId());
        layer.addReleaseFence(receivedLayer.releaseFenceFd);
    }
}


int32_t Hwc2Device::Display::Config::getAttribute(Attribute attribute) const
{
    if (mAttributes.count(attribute) == 0) {
        return -1;
    }
    return mAttributes.at(attribute);
}

std::string Hwc2Device::Display::Config::toString(bool splitLine) const
{
    std::string output;

    const size_t BUFFER_SIZE = 100;
    char buffer[BUFFER_SIZE] = {};
    auto writtenBytes = snprintf(buffer, BUFFER_SIZE,
            "%u x %u", mAttributes.at(HWC2::Attribute::Width),
            mAttributes.at(HWC2::Attribute::Height));
    output.append(buffer, writtenBytes);

    if (mAttributes.count(HWC2::Attribute::VsyncPeriod) != 0) {
        std::memset(buffer, 0, BUFFER_SIZE);
        writtenBytes = snprintf(buffer, BUFFER_SIZE, " @ %.1f Hz",
                1e9 / mAttributes.at(HWC2::Attribute::VsyncPeriod));
        output.append(buffer, writtenBytes);
    }

    if (mAttributes.count(HWC2::Attribute::DpiX) != 0 &&
            mAttributes.at(HWC2::Attribute::DpiX) != -1) {
        std::memset(buffer, 0, BUFFER_SIZE);
        writtenBytes = snprintf(buffer, BUFFER_SIZE,
                ", DPI: %.1f x %.1f",
                mAttributes.at(HWC2::Attribute::DpiX) / 1000.0f,
                mAttributes.at(HWC2::Attribute::DpiY) / 1000.0f);
        output.append(buffer, writtenBytes);
    }

    std::memset(buffer, 0, BUFFER_SIZE);
    if (splitLine) {
        writtenBytes = snprintf(buffer, BUFFER_SIZE,
                "\n        HWC1 ID/Color transform:");
    } else {
        writtenBytes = snprintf(buffer, BUFFER_SIZE,
                ", HWC1 ID/Color transform:");
    }
    output.append(buffer, writtenBytes);


        std::memset(buffer, 0, BUFFER_SIZE);
        writtenBytes = snprintf(buffer, BUFFER_SIZE, " %u", mHwc1Id);
        output.append(buffer, writtenBytes);

    return output;
}

std::shared_ptr<const Hwc2Device::Display::Config>
        Hwc2Device::Display::getConfig(hwc2_config_t configId) const
{
    if (configId > mConfigs.size() || !mConfigs[configId]->isOnDisplay(*this)) {
        return nullptr;
    }
    return mConfigs[configId];
}

static void cloneHWCRegion(hwc_region_t& region)
{
    auto size = sizeof(hwc_rect_t) * region.numRects;
    auto newRects = static_cast<hwc_rect_t*>(std::malloc(size));
    std::copy_n(region.rects, region.numRects, newRects);
    region.rects = newRects;
}

Hwc2Device::Display::HWC1Contents
        Hwc2Device::Display::cloneRequestedContents() const
{
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    size_t size = sizeof(hwc_display_contents_1_t) +
            sizeof(hwc_layer_1_t) * (mHwc1RequestedContents->numHwLayers);
    auto contents = static_cast<hwc_display_contents_1_t*>(std::malloc(size));
    std::memcpy(contents, mHwc1RequestedContents.get(), size);
    for (size_t layerId = 0; layerId < contents->numHwLayers; ++layerId) {
        auto& layer = contents->hwLayers[layerId];
        // Deep copy the regions to avoid double-frees
        cloneHWCRegion(layer.visibleRegionScreen);
        cloneHWCRegion(layer.surfaceDamage);
    }
    return HWC1Contents(contents);
}

void Hwc2Device::Display::initializeActiveConfig()
{
	ALOGV("getActiveConfig is null, choosing config 0");
	mActiveConfig = mConfigs[0];
	return;
}


void Hwc2Device::Display::reallocateHwc1Contents()
{
    // Allocate an additional layer for the framebuffer target
    auto numLayers = mLayers.size() + 1;
    size_t size = sizeof(hwc_display_contents_1_t) +
            sizeof(hwc_layer_1_t) * numLayers;
    ALOGV("[%" PRIu64 "] reallocateHwc1Contents creating %zd layer%s", mId,
            numLayers, numLayers != 1 ? "s" : "");
    auto contents =
            static_cast<hwc_display_contents_1_t*>(std::calloc(size, 1));
    contents->numHwLayers = numLayers;
    mHwc1RequestedContents.reset(contents);
}

void Hwc2Device::Display::assignHwc1LayerIds()
{
    mHwc1LayerMap.clear();
    size_t nextHwc1Id = 0;
    for (auto& layer : mLayers) {
        mHwc1LayerMap[nextHwc1Id] = layer;
        layer->setHwc1Id(nextHwc1Id++);
    }
}


void Hwc2Device::Display::updateTypeChanges(const hwc_layer_1_t& hwc1Layer,
        const Layer& layer)
{
    auto layerId = layer.getId();
    switch (hwc1Layer.compositionType) {
        case HWC_FRAMEBUFFER:
            if (layer.getCompositionType() != Composition::Client) {
                mChanges->addTypeChange(layerId, Composition::Client);
            }
            break;
        case HWC_OVERLAY:
            if (layer.getCompositionType() != Composition::Device) {
                mChanges->addTypeChange(layerId, Composition::Device);
            }
            break;
        case HWC_BACKGROUND:
            ALOGE_IF(layer.getCompositionType() != Composition::SolidColor,
                    "updateTypeChanges: HWC1 requested BACKGROUND, but HWC2"
                    " wasn't expecting SolidColor");
            break;
        case HWC_FRAMEBUFFER_TARGET:
            // Do nothing, since it shouldn't be modified by HWC1
            break;
        case HWC_SIDEBAND:
            ALOGE_IF(layer.getCompositionType() != Composition::Sideband,
                    "updateTypeChanges: HWC1 requested SIDEBAND, but HWC2"
                    " wasn't expecting Sideband");
            break;
        case HWC_CURSOR_OVERLAY:
            ALOGE_IF(layer.getCompositionType() != Composition::Cursor,
                    "updateTypeChanges: HWC1 requested CURSOR_OVERLAY, but"
                    " HWC2 wasn't expecting Cursor");
            break;
    }
}

void Hwc2Device::Display::updateLayerRequests(
        const hwc_layer_1_t& hwc1Layer, const Layer& layer)
{
    if ((hwc1Layer.hints & HWC_HINT_CLEAR_FB) != 0) {
        mChanges->addLayerRequest(layer.getId(),
                LayerRequest::ClearClientTarget);
    }
}


void Hwc2Device::Display::prepareFramebufferTarget()
{
    // We check that mActiveConfig is valid in Display::prepare
    int32_t width = mActiveConfig->getAttribute(Attribute::Width);
    int32_t height = mActiveConfig->getAttribute(Attribute::Height);

    auto& hwc1Target = mHwc1RequestedContents->hwLayers[mLayers.size()];
    hwc1Target.compositionType = HWC_FRAMEBUFFER_TARGET;
    hwc1Target.releaseFenceFd = -1;
    hwc1Target.hints = 0;
    hwc1Target.flags = 0;
    hwc1Target.transform = 0;
    hwc1Target.blending = HWC_BLENDING_PREMULT;
    hwc1Target.sourceCropi = {0, 0, width, height};
    hwc1Target.displayFrame = {0, 0, width, height};
    hwc1Target.planeAlpha = 255;
    hwc1Target.visibleRegionScreen.numRects = 1;
    auto rects = static_cast<hwc_rect_t*>(std::malloc(sizeof(hwc_rect_t)));
    rects[0].left = 0;
    rects[0].top = 0;
    rects[0].right = width;
    rects[0].bottom = height;
    hwc1Target.visibleRegionScreen.rects = rects;

    // We will set this to the correct value in set
    hwc1Target.acquireFenceFd = -1;
}

// Layer functions

std::atomic<hwc2_layer_t> Hwc2Device::Layer::sNextId(1);

Hwc2Device::Layer::Layer(Display& display)
  : mId(sNextId++),
    mDisplay(display),
    mSurfaceDamage(),
    mCompositionType(*this, Composition::Invalid),
    mHasUnsupportedDataspace(false) {}

bool Hwc2Device::SortLayersByZ::operator()(const std::shared_ptr<Layer>& lhs,
                                               const std::shared_ptr<Layer>& rhs) const {
    return lhs->getZ() < rhs->getZ();
}


Error Hwc2Device::Layer::setBuffer(buffer_handle_t buffer,
        int32_t acquireFence)
{
    ALOGV("Setting acquireFence to %d for layer %" PRIu64, acquireFence, mId);
    mBuffer.setBuffer(buffer);
    mBuffer.setFence(acquireFence);
    return Error::None;
}

Error Hwc2Device::Layer::setCursorPosition(int32_t /*x*/, int32_t /*y*/)
{
    if (mCompositionType.getValue() != Composition::Cursor) {
        return Error::BadLayer;
    }

    if (mDisplay.hasChanges()) {
        return Error::NotValidated;
    }

    return Error::None;
}

Error Hwc2Device::Layer::setSurfaceDamage(hwc_region_t damage)
{
    mSurfaceDamage.resize(damage.numRects);
    std::copy_n(damage.rects, damage.numRects, mSurfaceDamage.begin());
    return Error::None;
}

void Hwc2Device::Layer::applyState(hwc_layer_1_t& hwc1Layer,
        bool applyAllState)
{
    applyCompositionType(hwc1Layer, applyAllState);
}


// Layer state functions

Error Hwc2Device::Layer::setBlendMode(BlendMode /*mode*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setColor(hwc_color_t /*color*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setCompositionType(Composition type)
{
    mCompositionType.setPending(type);
    return Error::None;
}

Error Hwc2Device::Layer::setDataspace(android_dataspace_t dataspace)
{
    mHasUnsupportedDataspace = (dataspace != HAL_DATASPACE_UNKNOWN);
    return Error::None;
}

Error Hwc2Device::Layer::setDisplayFrame(hwc_rect_t /*frame*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setPlaneAlpha(float /*alpha*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setSidebandStream(const native_handle_t* /*stream*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setSourceCrop(hwc_frect_t /*crop*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setTransform(Transform /*transform*/)
{
    return Error::None;
}

Error Hwc2Device::Layer::setVisibleRegion(hwc_region_t /*rawVisible*/)
{
    return Error::None;
}
Error Hwc2Device::Layer::setZ(uint32_t z)
{
    mZ = z;
    return Error::None;
}

void Hwc2Device::Layer::addReleaseFence(int fenceFd)
{
    ALOGV("addReleaseFence %d to layer %" PRIu64, fenceFd, mId);
    mReleaseFence.add(fenceFd);
}

const sp<Fence>& Hwc2Device::Layer::getReleaseFence() const
{
    return mReleaseFence.get();
}

// Layer dump helpers

static std::string regionStrings(const std::vector<hwc_rect_t>& visibleRegion,
        const std::vector<hwc_rect_t>& surfaceDamage)
{
    std::string regions;
    regions += "        Visible Region";
    regions.resize(40, ' ');
    regions += "Surface Damage\n";

    size_t numPrinted = 0;
    size_t maxSize = std::max(visibleRegion.size(), surfaceDamage.size());
    while (numPrinted < maxSize) {
        std::string line("        ");
        if (visibleRegion.empty() && numPrinted == 0) {
            line += "None";
        } else if (numPrinted < visibleRegion.size()) {
            line += rectString(visibleRegion[numPrinted]);
        }
        line.resize(40, ' ');
        if (surfaceDamage.empty() && numPrinted == 0) {
            line += "None";
        } else if (numPrinted < surfaceDamage.size()) {
            line += rectString(surfaceDamage[numPrinted]);
        }
        line += '\n';
        regions += line;
        ++numPrinted;
    }
    return regions;
}

std::string Hwc2Device::Layer::dump() const
{
    std::stringstream output;
    const char* fill = "      ";

    output << fill << to_string(mCompositionType.getPendingValue());
    output << " Layer  HWC2/1: " << mId << "/" << mHwc1Id << "  ";
    output << "Z: " << mZ;
    if (mCompositionType.getValue() == HWC2::Composition::SolidColor) {
    } else if (mCompositionType.getValue() == HWC2::Composition::Sideband) {
    } else {
        output << "  Buffer: " << mBuffer.getBuffer() << "/" <<
                mBuffer.getFence() << '\n';
    }
    return output.str();
}

void Hwc2Device::Layer::applyCompositionType(hwc_layer_1_t& hwc1Layer,
        bool applyAllState)
{
    // HWC1 never supports color transforms or dataspaces and only sometimes
    // supports plane alpha (depending on the version). These require us to drop
    // some or all layers to client composition.
    if (mHasUnsupportedDataspace) {
        hwc1Layer.compositionType = HWC_FRAMEBUFFER;
        hwc1Layer.flags = HWC_SKIP_LAYER;
        return;
    }

    if (applyAllState || mCompositionType.isDirty()) {
        hwc1Layer.flags = 0;
        switch (mCompositionType.getPendingValue()) {
            case Composition::Client:
                hwc1Layer.compositionType = HWC_FRAMEBUFFER;
                hwc1Layer.flags |= HWC_SKIP_LAYER;
                break;
            case Composition::Device:
                hwc1Layer.compositionType = HWC_FRAMEBUFFER;
                break;
            case Composition::SolidColor:
                // In theory the following line should work, but since the HWC1
                // version of SurfaceFlinger never used HWC_BACKGROUND, HWC1
                // devices may not work correctly. To be on the safe side, we
                // fall back to client composition.
                //
                // hwc1Layer.compositionType = HWC_BACKGROUND;
                hwc1Layer.compositionType = HWC_FRAMEBUFFER;
                hwc1Layer.flags |= HWC_SKIP_LAYER;
                break;
            case Composition::Cursor:
                hwc1Layer.compositionType = HWC_FRAMEBUFFER;
                break;
            case Composition::Sideband:
                hwc1Layer.compositionType = HWC_SIDEBAND;
                break;
            default:
                hwc1Layer.compositionType = HWC_FRAMEBUFFER;
                hwc1Layer.flags |= HWC_SKIP_LAYER;
                break;
        }
        ALOGV("Layer %" PRIu64 " %s set to %d", mId,
                to_string(mCompositionType.getPendingValue()).c_str(),
                hwc1Layer.compositionType);
        ALOGV_IF(hwc1Layer.flags & HWC_SKIP_LAYER, "    and skipping");
        mCompositionType.latch();
    }
}

// Adapter helpers

void Hwc2Device::populatePrimary()
{
    ALOGV("populatePrimary");

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    mDisplay = std::make_shared<Display>(*this, HWC2::DisplayType::Physical);
    mDisplay->setHwc1Id(HWC_DISPLAY_PRIMARY);
    mDisplay->populateConfigs();
}

bool Hwc2Device::prepareDisplay() {
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    if (!mDisplay->prepare()) {
        return false;
    }

    std::vector<Hwc2Device::Display::HWC1Contents> requestedContents;
    auto primaryDisplayContents = mDisplay->cloneRequestedContents();
    requestedContents.push_back(std::move(primaryDisplayContents));
    requestedContents.push_back(nullptr);

    mHwc1Contents.clear();
    for (auto& displayContents : requestedContents) {
        mHwc1Contents.push_back(displayContents.get());
        if (!displayContents) {
            continue;
        }

        ALOGV("Display %zd layers:", mHwc1Contents.size() - 1);
        for (size_t l = 0; l < displayContents->numHwLayers; ++l) {
            auto& layer = displayContents->hwLayers[l];
            ALOGV("  %zd: %d", l, layer.compositionType);
        }
    }

    ALOGV("Calling HWC1 prepare");
    {
        mHwc1Device->prepare(mHwc1Contents.size(),
                mHwc1Contents.data());
    }

    for (size_t c = 0; c < mHwc1Contents.size(); ++c) {
        auto& contents = mHwc1Contents[c];
        if (!contents) {
            continue;
        }
        ALOGV("Display %zd layers:", c);
        for (size_t l = 0; l < contents->numHwLayers; ++l) {
            ALOGV("  %zd: %d", l, contents->hwLayers[l].compositionType);
        }
    }

    // Return the received contents to their respective displays
    for (size_t hwc1Id = 0; hwc1Id < mHwc1Contents.size(); ++hwc1Id) {
        if (mHwc1Contents[hwc1Id] == nullptr) {
            continue;
        }

        auto& display = mDisplay;
        display->setReceivedContents(std::move(requestedContents[hwc1Id]));
    }

    return true;
}

Error Hwc2Device::setDisplay()
{
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    // Make sure we're ready to validate
    for (size_t hwc1Id = 0; hwc1Id < mHwc1Contents.size(); ++hwc1Id) {
        if (mHwc1Contents[hwc1Id] == nullptr) {
            continue;
        }

        auto& display = mDisplay;
        Error error = display->set(*mHwc1Contents[hwc1Id]);
        if (error != Error::None) {
            ALOGE("setAllDisplays: Failed to set display %zd: %s", hwc1Id,
                    to_string(error).c_str());
            return error;
        }
    }

    ALOGV("Calling HWC1 set");
    {
        mHwc1Device->set(mHwc1Contents.size(), mHwc1Contents.data());
    }

    // Add retire and release fences
    for (size_t hwc1Id = 0; hwc1Id < mHwc1Contents.size(); ++hwc1Id) {
        if (mHwc1Contents[hwc1Id] == nullptr) {
            continue;
        }

        auto& display = mDisplay;
        auto retireFenceFd = mHwc1Contents[hwc1Id]->retireFenceFd;
        ALOGV("setAllDisplays: Adding retire fence %d to display %zd",
                retireFenceFd, hwc1Id);
        display->addRetireFence(mHwc1Contents[hwc1Id]->retireFenceFd);
        display->addReleaseFences(*mHwc1Contents[hwc1Id]);
    }

    return Error::None;
}

Hwc2Device::Display* Hwc2Device::getDisplay(hwc2_display_t id)
{
    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    if (id == mDisplay->getId()) {
    	return mDisplay.get();
    } else {
    	return nullptr;
    }
}


std::tuple<Hwc2Device::Layer*, Error> Hwc2Device::getLayer(
        hwc2_display_t displayId, hwc2_layer_t layerId)
{
    auto display = getDisplay(displayId);
    if (!display) {
        return std::make_tuple(static_cast<Layer*>(nullptr), Error::BadDisplay);
    }

    auto layerEntry = mLayers.find(layerId);
    if (layerEntry == mLayers.end()) {
        return std::make_tuple(static_cast<Layer*>(nullptr), Error::BadLayer);
    }

    auto layer = layerEntry->second;
    if (layer->getDisplay().getId() != displayId) {
        return std::make_tuple(static_cast<Layer*>(nullptr), Error::BadLayer);
    }
    return std::make_tuple(layer.get(), Error::None);
}

void Hwc2Device::hwc1Vsync(int hwc1DisplayId, int64_t timestamp)
{
    ALOGV("Received hwc1Vsync(%d, %" PRId64 ")", hwc1DisplayId, timestamp);

    std::unique_lock<std::recursive_timed_mutex> lock(mStateMutex);

    // If the HWC2-side callback hasn't been registered yet, buffer this until
    // it is registered
    if (mCallbacks.count(Callback::Vsync) == 0) {
        mPendingVsyncs.emplace_back(timestamp);
        return;
    }

    if (mDisplay->getHwc1Id() != hwc1DisplayId) {
        ALOGE("hwc1Vsync: Couldn't find display for HWC1 id %d", hwc1DisplayId);
        return;
    }

    const auto& callbackInfo = mCallbacks[Callback::Vsync];
    auto displayId = mDisplay->getId();

    // Call back without the state lock held
    lock.unlock();

    auto vsync = reinterpret_cast<HWC2_PFN_VSYNC>(callbackInfo.pointer);
    vsync(callbackInfo.data, displayId, timestamp);
}

} // namespace android

