#define LOG_TAG "composer@2.0-ComposerHal"
//#define LOG_NDEBUG 0
#include <android-base/logging.h>
#include <utils/Log.h>

#include "ComposerHal.h"

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_1 {
namespace implementation {

ComposerHal::ComposerHal() {
    mDevice = std::make_unique<Hwc2Device>();
}

std::string ComposerHal::dumpDebugInfo() {
    uint32_t len = 0;
    mDevice->dump(&len, nullptr);

    std::vector<char> buf(len + 1);
    mDevice->dump(&len, buf.data());
    buf.resize(len + 1);
    buf[len] = '\0';

    return buf.data();
}

void ComposerHal::registerEventCallback(ComposerHal::EventCallback* callback) {
    mMustValidateDisplay = true;
    mEventCallback = callback;

    mDevice->registerCallback(HWC2_CALLBACK_HOTPLUG, this,
                               reinterpret_cast<hwc2_function_pointer_t>(hotplugHook));
    mDevice->registerCallback(HWC2_CALLBACK_VSYNC, this,
                               reinterpret_cast<hwc2_function_pointer_t>(vsyncHook));
}

void ComposerHal::unregisterEventCallback() {
    mDevice->registerCallback(HWC2_CALLBACK_HOTPLUG, this, nullptr);
    mDevice->registerCallback(HWC2_CALLBACK_VSYNC, this, nullptr);

    mEventCallback = nullptr;
}

Error ComposerHal::createLayer(Display display, Layer* outLayer) {
    int32_t err = mDevice->createLayer(display, outLayer);
    return static_cast<Error>(err);
}

Error ComposerHal::destroyLayer(Display display, Layer layer) {
    int32_t err = mDevice->destroyLayer(display, layer);
    return static_cast<Error>(err);
}

Error ComposerHal::getClientTargetSupport(Display display, uint32_t width, uint32_t height,
                             PixelFormat format, Dataspace dataspace) {
    int32_t err = mDevice->getClientTargetSupport(display, width, height,
                                                   static_cast<int32_t>(format),
                                                   static_cast<int32_t>(dataspace));
    return static_cast<Error>(err);
}

Error ComposerHal::getDisplayAttribute(Display display, Config config,
		IComposerClient::Attribute attribute, int32_t* outValue) {
    int32_t err = mDevice->getDisplayAttribute(display, config,
                                       static_cast<int32_t>(attribute), outValue);
    return static_cast<Error>(err);
}

Error ComposerHal::getDisplayName(Display display, hidl_string* outName) {
    uint32_t count = 0;
    int32_t err = mDevice->getDisplayName(display, &count, nullptr);
    if (err != HWC2_ERROR_NONE) {
        return static_cast<Error>(err);
    }

    std::vector<char> buf(count + 1);
    err = mDevice->getDisplayName(display, &count, buf.data());
    if (err != HWC2_ERROR_NONE) {
        return static_cast<Error>(err);
    }
    buf.resize(count + 1);
    buf[count] = '\0';

    *outName = buf.data();

    return Error::NONE;
}

Error ComposerHal::setVsyncEnabled(Display display, IComposerClient::Vsync enabled) {
    int32_t err = mDevice->setVsyncEnabled(display, static_cast<int32_t>(enabled));
    return static_cast<Error>(err);
}

Error ComposerHal::setClientTarget(Display display, buffer_handle_t target, int32_t acquireFence,
                      int32_t dataspace, const std::vector<hwc_rect_t>& damage) {
    hwc_region region = {damage.size(), damage.data()};
    int32_t err =
        mDevice->setClientTarget(display, target, acquireFence, dataspace, region);
    return static_cast<Error>(err);
}

Error ComposerHal::validateDisplay(Display display, std::vector<Layer>* outChangedLayers,
                      std::vector<IComposerClient::Composition>* outCompositionTypes,
                      uint32_t* outDisplayRequestMask, std::vector<Layer>* outRequestedLayers,
                      std::vector<uint32_t>* outRequestMasks) {
    uint32_t typesCount = 0;
    uint32_t reqsCount = 0;
    int32_t err = mDevice->validateDisplay(display, &typesCount, &reqsCount);
    mMustValidateDisplay = false;

    if (err != HWC2_ERROR_NONE && err != HWC2_ERROR_HAS_CHANGES) {
        return static_cast<Error>(err);
    }

    err = mDevice->getChangedCompositionTypes(display, &typesCount, nullptr, nullptr);
    if (err != HWC2_ERROR_NONE) {
        return static_cast<Error>(err);
    }

    std::vector<Layer> changedLayers(typesCount);
    std::vector<IComposerClient::Composition> compositionTypes(typesCount);
    err = mDevice->getChangedCompositionTypes(display, &typesCount, changedLayers.data(),
        reinterpret_cast<std::underlying_type<IComposerClient::Composition>::type*>(
            compositionTypes.data()));
    if (err != HWC2_ERROR_NONE) {
        return static_cast<Error>(err);
    }

    int32_t displayReqs = 0;
    std::vector<Layer> requestedLayers(0);
    std::vector<uint32_t> requestMasks(0);

    *outChangedLayers = std::move(changedLayers);
    *outCompositionTypes = std::move(compositionTypes);
    *outDisplayRequestMask = displayReqs;
    *outRequestedLayers = std::move(requestedLayers);
    *outRequestMasks = std::move(requestMasks);

    return static_cast<Error>(err);
}

Error ComposerHal::presentDisplay(Display display, int32_t* outPresentFence,
		std::vector<Layer>* outLayers, std::vector<int32_t>* outReleaseFences) {
    if (mMustValidateDisplay) {
        return Error::NOT_VALIDATED;
    }

    *outPresentFence = -1;
    int32_t err = mDevice->presentDisplay(display, outPresentFence);
    if (err != HWC2_ERROR_NONE) {
        return static_cast<Error>(err);
    }

    outLayers->resize(0);
    outReleaseFences->resize(0);
    return Error::NONE;
}

Error ComposerHal::acceptDisplayChanges(Display display) {
    int32_t err = mDevice->acceptDisplayChanges(display);
    return static_cast<Error>(err);
}

Error ComposerHal::setLayerCompositionType(Display display, Layer layer, int32_t type) {
    int32_t err = mDevice->setLayerCompositionType(display, layer, type);
    return static_cast<Error>(err);
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android
