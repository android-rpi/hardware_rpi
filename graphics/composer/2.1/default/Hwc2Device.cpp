#define LOG_TAG "composer@2.1-Hwc2Device"
//#define LOG_NDEBUG 0
#include <android-base/logging.h>
#include <utils/Log.h>
#include <utils/Trace.h>

#include <sys/prctl.h>
#include <sstream>

#include <sync/sync.h>

#include "Hwc2Device.h"

namespace android {

Hwc2Device::Hwc2Device()
{
    ALOGV("Hwc2Device()");
    mHwcContext = std::make_unique<hwc_context>();

    mFbInfo.name = "hwc-rpi3";
    mFbInfo.width = mHwcContext->width;
    mFbInfo.height = mHwcContext->height;
    mFbInfo.format = mHwcContext->format;
    mFbInfo.vsync_period_ns = int(1e9 / mHwcContext->fps);
    mFbInfo.xdpi_scaled = int(mHwcContext->xdpi * 1000.0f);
    mFbInfo.ydpi_scaled = int(mHwcContext->ydpi * 1000.0f);

    mVsyncThread.start(0, mFbInfo.vsync_period_ns);
}

int32_t Hwc2Device::createLayer(hwc2_display_t displayId, hwc2_layer_t* outLayerId) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    *outLayerId = addLayer();
    setState(State::MODIFIED);
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::destroyLayer(hwc2_display_t displayId, hwc2_layer_t layerId) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (removeLayer(layerId)) {
        setState(State::MODIFIED);
        return HWC2_ERROR_NONE;
    } else {
        return HWC2_ERROR_BAD_LAYER;
    }
}

int32_t Hwc2Device::getClientTargetSupport(hwc2_display_t displayId, uint32_t width, uint32_t height,
                                      int32_t format, int32_t dataspace) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (dataspace != HAL_DATASPACE_UNKNOWN) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    const auto& info = getInfo();
    return (info.width == width && info.height == height && info.format == format)
            ? HWC2_ERROR_NONE
            : HWC2_ERROR_UNSUPPORTED;
}


int32_t Hwc2Device::getDisplayAttribute(hwc2_display_t displayId, hwc2_config_t config,
        int32_t intAttribute, int32_t* outValue) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (0 != config) {
        return HWC2_ERROR_BAD_CONFIG;
    }
    const auto& info = getInfo();
    switch (intAttribute) {
        case HWC2_ATTRIBUTE_WIDTH:
            *outValue = int32_t(info.width);
            break;
        case HWC2_ATTRIBUTE_HEIGHT:
            *outValue = int32_t(info.height);
            break;
        case HWC2_ATTRIBUTE_VSYNC_PERIOD:
            *outValue = int32_t(info.vsync_period_ns);
            break;
        case HWC2_ATTRIBUTE_DPI_X:
            *outValue = int32_t(info.xdpi_scaled);
            break;
        case HWC2_ATTRIBUTE_DPI_Y:
            *outValue = int32_t(info.ydpi_scaled);
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::getDisplayName(hwc2_display_t displayId, uint32_t* outSize, char* outName) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    const auto& info = mFbInfo;
    if (outName) {
        *outSize = info.name.copy(outName, *outSize);
    } else {
        *outSize = info.name.size();
    }
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::setVsyncEnabled(hwc2_display_t displayId, int32_t intEnabled) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    mVsyncThread.enableCallback(intEnabled == HWC2_VSYNC_ENABLE);
    return HWC2_ERROR_NONE;
}


int32_t Hwc2Device::setClientTarget(hwc2_display_t displayId, buffer_handle_t target,
        int32_t acquireFence, int32_t dataspace, hwc_region_t /*damage*/) {
    ALOGV("setClientTarget(%p, %d)", target, acquireFence);
    if (acquireFence >= 0) {
        sync_wait(acquireFence, -1);
        close(acquireFence);
    }
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (dataspace != HAL_DATASPACE_UNKNOWN) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    mBuffer = target;
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::validateDisplay(hwc2_display_t displayId, uint32_t* outNumTypes,
        uint32_t* outNumRequests) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    const auto& dirtyLayers = getDirtyLayers();
    *outNumTypes = dirtyLayers.size();
    *outNumRequests = 0;
    ALOGV("validateDisplay() %u types", *outNumTypes);
    if (*outNumTypes > 0) {
        setState(State::VALIDATED_WITH_CHANGES);
        return HWC2_ERROR_HAS_CHANGES;
    } else {
        setState(State::VALIDATED);
        return HWC2_ERROR_NONE;
    }
}

int32_t Hwc2Device::presentDisplay(hwc2_display_t displayId, int32_t* outRetireFence) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (getState() != State::VALIDATED) {
        return HWC2_ERROR_NOT_VALIDATED;
    }
    ALOGV("presentDisplay(%p)", mBuffer);
    mHwcContext->hwc_post(mBuffer);
    *outRetireFence = -1;
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::acceptDisplayChanges(hwc2_display_t displayId) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (getState() == State::MODIFIED) {
        return HWC2_ERROR_NOT_VALIDATED;
    }
    clearDirtyLayers();
    setState(State::VALIDATED);
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::getChangedCompositionTypes(hwc2_display_t displayId, uint32_t* outNumElements,
        hwc2_layer_t* outLayers, int32_t* outTypes){
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (getState() == State::MODIFIED) {
        return HWC2_ERROR_NOT_VALIDATED;
    }
    const auto& dirtyLayers = getDirtyLayers();
    if (outLayers && outTypes) {
        *outNumElements = std::min(*outNumElements, uint32_t(dirtyLayers.size()));
        auto iter = dirtyLayers.cbegin();
        for (uint32_t i = 0; i < *outNumElements; i++) {
            outLayers[i] = *iter++;
            outTypes[i] = HWC2_COMPOSITION_CLIENT;
        }
    } else {
        *outNumElements = dirtyLayers.size();
    }
    return HWC2_ERROR_NONE;
}

int32_t Hwc2Device::setLayerCompositionType(hwc2_display_t displayId, hwc2_layer_t layerId,
        int32_t intType) {
    if (0 != displayId) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    if (!markLayerDirty(layerId, intType != HWC2_COMPOSITION_CLIENT)) {
        return HWC2_ERROR_BAD_LAYER;
    }
    setState(State::MODIFIED);
    return HWC2_ERROR_NONE;
}

void Hwc2Device::dump(uint32_t* outSize, char* outBuffer)
{
    if (outBuffer != nullptr) {
        auto copiedBytes = mDumpString.copy(outBuffer, *outSize);
        *outSize = static_cast<uint32_t>(copiedBytes);
        return;
    }

    std::stringstream output;
    output << "-- hwc-rpi3 --\n";
    mDumpString = output.str();
    *outSize = static_cast<uint32_t>(mDumpString.size());
}

int32_t Hwc2Device::registerCallback(int32_t intDesc, hwc2_callback_data_t callbackData,
        hwc2_function_pointer_t pointer) {
    switch (intDesc) {
        case HWC2_CALLBACK_HOTPLUG:
            if (pointer) {
                reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer)(callbackData, 0,
                                                            HWC2_CONNECTION_CONNECTED);
            }
            break;
        case HWC2_CALLBACK_REFRESH:
            break;
        case HWC2_CALLBACK_VSYNC:
            mVsyncThread.setCallback(reinterpret_cast<HWC2_PFN_VSYNC>(pointer), callbackData);
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}


hwc2_layer_t Hwc2Device::addLayer() {
    hwc2_layer_t id = ++mNextLayerId;

    mLayers.insert(id);
    mDirtyLayers.insert(id);

    return id;
}

bool Hwc2Device::removeLayer(hwc2_layer_t layer) {
    mDirtyLayers.erase(layer);
    return mLayers.erase(layer);
}

bool Hwc2Device::hasLayer(hwc2_layer_t layer) const {
    return mLayers.count(layer) > 0;
}

bool Hwc2Device::markLayerDirty(hwc2_layer_t layer, bool dirty) {
    if (mLayers.count(layer) == 0) {
        return false;
    }

    if (dirty) {
        mDirtyLayers.insert(layer);
    } else {
        mDirtyLayers.erase(layer);
    }

    return true;
}

const std::unordered_set<hwc2_layer_t>& Hwc2Device::getDirtyLayers() const {
    return mDirtyLayers;
}

void Hwc2Device::clearDirtyLayers() {
    mDirtyLayers.clear();
}


int64_t Hwc2Device::VsyncThread::now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

bool Hwc2Device::VsyncThread::sleepUntil(int64_t t) {
    struct timespec ts;
    ts.tv_sec = t / 1'000'000'000;
    ts.tv_nsec = t % 1'000'000'000;

    while (true) {
        int error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        if (error) {
            if (error == EINTR) {
                continue;
            }
            return false;
        } else {
            return true;
        }
    }
}

void Hwc2Device::VsyncThread::start(int64_t firstVsync, int64_t period) {
    mNextVsync = firstVsync;
    mPeriod = period;
    mStarted = true;
    mThread = std::thread(&VsyncThread::vsyncLoop, this);
}

void Hwc2Device::VsyncThread::stop() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStarted = false;
    }
    mCondition.notify_all();
    mThread.join();
}

void Hwc2Device::VsyncThread::setCallback(HWC2_PFN_VSYNC callback, hwc2_callback_data_t data) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCallback = callback;
    mCallbackData = data;
}

void Hwc2Device::VsyncThread::enableCallback(bool enable) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mCallbackEnabled = enable;
    }
    mCondition.notify_all();
}

void Hwc2Device::VsyncThread::vsyncLoop() {
    prctl(PR_SET_NAME, "VsyncThread", 0, 0, 0);

    std::unique_lock<std::mutex> lock(mMutex);
    if (!mStarted) {
        return;
    }

    while (true) {
        if (!mCallbackEnabled) {
            mCondition.wait(lock, [this] { return mCallbackEnabled || !mStarted; });
            if (!mStarted) {
                break;
            }
        }

        lock.unlock();

        // adjust mNextVsync if necessary
        int64_t t = now();
        if (mNextVsync < t) {
            int64_t n = (t - mNextVsync + mPeriod - 1) / mPeriod;
            mNextVsync += mPeriod * n;
        }
        bool fire = sleepUntil(mNextVsync);

        lock.lock();

        if (fire) {
            ALOGV("VsyncThread(%" PRId64 ")", mNextVsync);
            if (mCallback) {
                mCallback(mCallbackData, 0, mNextVsync);
            }
            mNextVsync += mPeriod;
        }
    }
}



} // namespace android

