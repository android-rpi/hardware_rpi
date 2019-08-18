#define LOG_TAG "composer@2.0-ComposerClient"
//#define LOG_NDEBUG 0
#include <android-base/logging.h>
#include <utils/Log.h>

#include "ComposerClient.h"

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_1 {
namespace implementation {

ComposerClient::ComposerClient(ComposerHal *hal) {
	mHal = hal;
    mResources = createResources();
    if (!mResources) {
        ALOGE("failed to create composer resources");
        return;
    }
    mCommandEngine = createCommandEngine();
}

ComposerClient::~ComposerClient() {
    if (!mCommandEngine) {
        return;
    }
    ALOGD("destroying composer client");
    mHal->unregisterEventCallback();
    destroyResources();
    if (mOnClientDestroyed) {
        mOnClientDestroyed();
    }
    ALOGD("removed composer client");
}

void ComposerClient::destroyResources() {
    mResources->clear([this](Display display, bool isVirtual, const std::vector<Layer> layers) {
        ALOGW("destroying client resources for display %" PRIu64, display);

        for (auto layer : layers) {
            mHal->destroyLayer(display, layer);
        }

        if (isVirtual) {
            //mHal->destroyVirtualDisplay(display);
        } else {
            ALOGW("performing a final presentDisplay");

            std::vector<Layer> changedLayers;
            std::vector<IComposerClient::Composition> compositionTypes;
            uint32_t displayRequestMask = 0;
            std::vector<Layer> requestedLayers;
            std::vector<uint32_t> requestMasks;
            mHal->validateDisplay(display, &changedLayers, &compositionTypes,
                                  &displayRequestMask, &requestedLayers, &requestMasks);

            mHal->acceptDisplayChanges(display);

            int32_t presentFence = -1;
            std::vector<Layer> releasedLayers;
            std::vector<int32_t> releaseFences;
            mHal->presentDisplay(display, &presentFence, &releasedLayers, &releaseFences);
            if (presentFence >= 0) {
                close(presentFence);
            }
            for (auto fence : releaseFences) {
                if (fence >= 0) {
                    close(fence);
                }
            }
        }
    });

    mResources.reset();
}

Return<void> ComposerClient::registerCallback(const sp<IComposerCallback>& callback) {
    // no locking as we require this function to be called only once
    mHalEventCallback = std::make_unique<HalEventCallback>(callback, mResources.get());
    mHal->registerEventCallback(mHalEventCallback.get());
    return Void();
}

Return<uint32_t> ComposerClient::getMaxVirtualDisplayCount() {
    return 0;
}

Return<void> ComposerClient::createVirtualDisplay(uint32_t /*width*/, uint32_t /*height*/, PixelFormat formatHint,
                                  uint32_t /*outputBufferSlotCount*/,
                                  IComposerClient::createVirtualDisplay_cb hidl_cb) {
    Display display = 0;
    Error err = Error::NO_RESOURCES;
    hidl_cb(err, display, formatHint);
    return Void();
}

Return<Error> ComposerClient::destroyVirtualDisplay(Display /*display*/) {
    return Error::NONE;
}

Return<void> ComposerClient::createLayer(Display display, uint32_t bufferSlotCount,
                         IComposerClient::createLayer_cb hidl_cb) {
    Layer layer = 0;
    Error err = mHal->createLayer(display, &layer);
    if (err == Error::NONE) {
        err = mResources->addLayer(display, layer, bufferSlotCount);
        if (err != Error::NONE) {
            layer = 0;
        }
    }
    hidl_cb(err, layer);
    return Void();
}

Return<Error> ComposerClient::destroyLayer(Display display, Layer layer) {
    Error err = mHal->destroyLayer(display, layer);
    if (err == Error::NONE) {
        mResources->removeLayer(display, layer);
    }
    return err;
}

Return<void> ComposerClient::getActiveConfig(Display display,
                             IComposerClient::getActiveConfig_cb hidl_cb) {
    Config config = 0;
    Error err = (0 == display) ? Error::NONE : Error::BAD_DISPLAY;
    hidl_cb(err, config);
    return Void();
}

Return<Error> ComposerClient::getClientTargetSupport(Display display, uint32_t width, uint32_t height,
                                     PixelFormat format, Dataspace dataspace) {
    Error err = mHal->getClientTargetSupport(display, width, height, format, dataspace);
    return err;
}

Return<void> ComposerClient::getColorModes(Display display,
                           IComposerClient::getColorModes_cb hidl_cb) {
    hidl_vec<ColorMode> modes;
    Error err = Error::BAD_DISPLAY;
    if (0 == display) {
        modes.resize(1);
        modes.data()[0]=ColorMode::NATIVE;
        err = Error::NONE;
    }
    hidl_cb(err, modes);
    return Void();
}

Return<void> ComposerClient::getDisplayAttribute(Display display, Config config,
                                 IComposerClient::Attribute attribute,
                                 IComposerClient::getDisplayAttribute_cb hidl_cb) {
    int32_t value = 0;
    Error err = mHal->getDisplayAttribute(display, config, attribute, &value);
    hidl_cb(err, value);
    return Void();
}

Return<void> ComposerClient::getDisplayConfigs(Display display,
                               IComposerClient::getDisplayConfigs_cb hidl_cb) {
    hidl_vec<Config> configs;
    Error err = Error::BAD_DISPLAY;
    if (0 == display) {
        configs.resize(1);
        configs.data()[0]=0;
        err = Error::NONE;
    }
    hidl_cb(err, configs);
    return Void();
}

Return<void> ComposerClient::getDisplayName(Display display,
                            IComposerClient::getDisplayName_cb hidl_cb) {
    hidl_string name;
    Error err = mHal->getDisplayName(display, &name);
    hidl_cb(err, name);
    return Void();
}

Return<void> ComposerClient::getDisplayType(Display display,
                            IComposerClient::getDisplayType_cb hidl_cb) {
    DisplayType type = DisplayType::INVALID;
    Error err = Error::BAD_DISPLAY;
    if (0 == display) {
        type = DisplayType::PHYSICAL;
        err = Error::NONE;
    }
    hidl_cb(err, type);
    return Void();
}

Return<void> ComposerClient::getDozeSupport(Display /*display*/,
                            IComposerClient::getDozeSupport_cb hidl_cb) {
    hidl_cb(Error::NONE, false);
    return Void();
}

Return<void> ComposerClient::getHdrCapabilities(Display /*display*/,
                                IComposerClient::getHdrCapabilities_cb hidl_cb) {
    hidl_vec<Hdr> types;
    hidl_cb(Error::NONE, types, 0.0f, 0.0f, 0.0f);
    return Void();
}

Return<Error> ComposerClient::setClientTargetSlotCount(Display display,
                                       uint32_t clientTargetSlotCount) {
    return mResources->setDisplayClientTargetCacheSize(display, clientTargetSlotCount);
}

Return<Error> ComposerClient::setActiveConfig(Display display, Config config) {
    if (0 != display) return Error::BAD_DISPLAY;
    if (0 != config) return Error::BAD_CONFIG;
    return Error::NONE;
}

Return<Error> ComposerClient::setColorMode(Display display, ColorMode mode) {
    if (0 != display) return Error::BAD_DISPLAY;
    if (ColorMode::NATIVE != mode) return Error::BAD_PARAMETER;
    return Error::NONE;
}

Return<Error> ComposerClient::setPowerMode(Display /*display*/, IComposerClient::PowerMode /*mode*/) {
    return Error::NONE;
}

Return<Error> ComposerClient::setVsyncEnabled(Display display, IComposerClient::Vsync enabled) {
    Error err = mHal->setVsyncEnabled(display, enabled);
    return err;
}

Return<Error> ComposerClient::setInputCommandQueue(const MQDescriptorSync<uint32_t>& descriptor) {
    std::lock_guard<std::mutex> lock(mCommandEngineMutex);
    return mCommandEngine->setInputMQDescriptor(descriptor) ? Error::NONE : Error::NO_RESOURCES;
}

Return<void> ComposerClient::getOutputCommandQueue(IComposerClient::getOutputCommandQueue_cb hidl_cb) {
    auto outDescriptor = mCommandEngine->getOutputMQDescriptor();
    if (outDescriptor) {
        hidl_cb(Error::NONE, *outDescriptor);
    } else {
        hidl_cb(Error::NO_RESOURCES, CommandQueueType::Descriptor());
    }
    return Void();
}

Return<void> ComposerClient::executeCommands(uint32_t inLength, const hidl_vec<hidl_handle>& inHandles,
                             IComposerClient::executeCommands_cb hidl_cb) {
    std::lock_guard<std::mutex> lock(mCommandEngineMutex);
    bool outChanged = false;
    uint32_t outLength = 0;
    hidl_vec<hidl_handle> outHandles;
    Error error = mCommandEngine->execute(inLength, inHandles, &outChanged, &outLength, &outHandles);
    hidl_cb(error, outChanged, outLength, outHandles);
    mCommandEngine->reset();
    return Void();
}


}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android
