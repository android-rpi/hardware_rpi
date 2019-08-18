#define LOG_TAG "composer@2.1-CommandEngine"
//#define LOG_NDEBUG 0
#include <android-base/logging.h>
#include <utils/Log.h>

#include "ComposerCommandEngine.h"

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_1 {
namespace implementation {

bool ComposerCommandEngine::setInputMQDescriptor(const MQDescriptorSync<uint32_t>& descriptor) {
    return setMQDescriptor(descriptor);
}

const MQDescriptorSync<uint32_t>* ComposerCommandEngine::getOutputMQDescriptor() {
	return mWriter.getMQDescriptor();
}

void ComposerCommandEngine::reset() {
    CommandReaderBase::reset();
    mWriter.reset();
}


Error ComposerCommandEngine::execute(uint32_t inLength, const hidl_vec<hidl_handle>& inHandles, bool* outQueueChanged,
              uint32_t* outCommandLength, hidl_vec<hidl_handle>* outCommandHandles) {
    if (!readQueue(inLength, inHandles)) {
        return Error::BAD_PARAMETER;
    }
    IComposerClient::Command command;
    uint16_t length = 0;
    while (!isEmpty()) {
        if (!beginCommand(&command, &length)) {
            break;
        }
        ALOGV("execute() command 0x%x, length %" PRIu16, command, length);
        bool parsed = executeCommand(command, length);
        endCommand();
        if (!parsed) {
            ALOGE("failed to parse command 0x%x, length %" PRIu16, command, length);
            break;
        }
    }
    if (!isEmpty()) {
        return Error::BAD_PARAMETER;
    }
    return mWriter.writeQueue(outQueueChanged, outCommandLength, outCommandHandles)
               ? Error::NONE
               : Error::NO_RESOURCES;
}

bool ComposerCommandEngine::executeCommand(IComposerClient::Command command, uint16_t length) {
     switch (command) {
         case IComposerClient::Command::SELECT_DISPLAY:
             return executeSelectDisplay(length);
         case IComposerClient::Command::SELECT_LAYER:
             return executeSelectLayer(length);
         case IComposerClient::Command::SET_COLOR_TRANSFORM:
             return executeSetColorTransform(length);
         case IComposerClient::Command::SET_CLIENT_TARGET:
             return executeSetClientTarget(length);
         case IComposerClient::Command::SET_OUTPUT_BUFFER:
             return executeSetOutputBuffer(length);
         case IComposerClient::Command::VALIDATE_DISPLAY:
             return executeValidateDisplay(length);
         case IComposerClient::Command::PRESENT_OR_VALIDATE_DISPLAY:
             return executePresentOrValidateDisplay(length);
         case IComposerClient::Command::ACCEPT_DISPLAY_CHANGES:
             return executeAcceptDisplayChanges(length);
         case IComposerClient::Command::PRESENT_DISPLAY:
             return executePresentDisplay(length);
         case IComposerClient::Command::SET_LAYER_CURSOR_POSITION:
             return executeSetLayerCursorPosition(length);
         case IComposerClient::Command::SET_LAYER_BUFFER:
             return executeSetLayerBuffer(length);
         case IComposerClient::Command::SET_LAYER_SURFACE_DAMAGE:
             return executeSetLayerSurfaceDamage(length);
         case IComposerClient::Command::SET_LAYER_BLEND_MODE:
             return executeSetLayerBlendMode(length);
         case IComposerClient::Command::SET_LAYER_COLOR:
             return executeSetLayerColor(length);
         case IComposerClient::Command::SET_LAYER_COMPOSITION_TYPE:
             return executeSetLayerCompositionType(length);
         case IComposerClient::Command::SET_LAYER_DATASPACE:
             return executeSetLayerDataspace(length);
         case IComposerClient::Command::SET_LAYER_DISPLAY_FRAME:
             return executeSetLayerDisplayFrame(length);
         case IComposerClient::Command::SET_LAYER_PLANE_ALPHA:
             return executeSetLayerPlaneAlpha(length);
         case IComposerClient::Command::SET_LAYER_SIDEBAND_STREAM:
             return executeSetLayerSidebandStream(length);
         case IComposerClient::Command::SET_LAYER_SOURCE_CROP:
             return executeSetLayerSourceCrop(length);
         case IComposerClient::Command::SET_LAYER_TRANSFORM:
             return executeSetLayerTransform(length);
         case IComposerClient::Command::SET_LAYER_VISIBLE_REGION:
             return executeSetLayerVisibleRegion(length);
         case IComposerClient::Command::SET_LAYER_Z_ORDER:
             return executeSetLayerZOrder(length);
         default:
             return false;
     }
}

bool ComposerCommandEngine::executeSelectDisplay(uint16_t length) {
    if (length != CommandWriterBase::kSelectDisplayLength) {
        return false;
    }
    mCurrentDisplay = read64();
    mWriter.selectDisplay(mCurrentDisplay);
    return true;
}

bool ComposerCommandEngine::executeSelectLayer(uint16_t length) {
    if (length != CommandWriterBase::kSelectLayerLength) {
        return false;
    }
    mCurrentLayer = read64();
    return true;
}

bool ComposerCommandEngine::executeSetColorTransform(uint16_t length) {
    if (length != CommandWriterBase::kSetColorTransformLength) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        readFloat();
    }
    readSigned();
    return true;
}


bool ComposerCommandEngine::executeSetClientTarget(uint16_t length) {
    // 4 parameters followed by N rectangles
    if ((length - 4) % 4 != 0) {
        return false;
    }

    bool useCache = false;
    auto slot = read();
    auto rawHandle = readHandle(&useCache);
    auto fence = readFence();
    auto dataspace = readSigned();
    auto damage = readRegion((length - 4) / 4);
    bool closeFence = true;

    const native_handle_t* clientTarget;
    ComposerResources::ReplacedBufferHandle replacedClientTarget;
    auto err = mResources->getDisplayClientTarget(mCurrentDisplay, slot, useCache, rawHandle,
                                                  &clientTarget, &replacedClientTarget);
    if (err == Error::NONE) {
        err = mHal->setClientTarget(mCurrentDisplay, clientTarget, fence, dataspace, damage);
        if (err == Error::NONE) {
            closeFence = false;
        }
    }
    if (closeFence) {
        close(fence);
    }
    if (err != Error::NONE) {
        mWriter.setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetOutputBuffer(uint16_t length) {
    if (length != CommandWriterBase::kSetOutputBufferLength) {
        return false;
    }

    bool useCache = false;
    read();
    readHandle(&useCache);
    auto fence = readFence();
    auto err = Error::BAD_DISPLAY;

    close(fence);

    if (err != Error::NONE) {
        mWriter.setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeValidateDisplay(uint16_t length) {
    if (length != CommandWriterBase::kValidateDisplayLength) {
        return false;
    }

    std::vector<Layer> changedLayers;
    std::vector<IComposerClient::Composition> compositionTypes;
    uint32_t displayRequestMask = 0x0;
    std::vector<Layer> requestedLayers;
    std::vector<uint32_t> requestMasks;

    auto err = mHal->validateDisplay(mCurrentDisplay, &changedLayers, &compositionTypes,
                                     &displayRequestMask, &requestedLayers, &requestMasks);
    if (err == Error::NONE) {
        mWriter.setChangedCompositionTypes(changedLayers, compositionTypes);
        mWriter.setDisplayRequests(displayRequestMask, requestedLayers, requestMasks);
    } else {
        mWriter.setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executePresentOrValidateDisplay(uint16_t length) {
    if (length != CommandWriterBase::kPresentOrValidateDisplayLength) {
        return false;
    }

    // Present has failed. We need to fallback to validate
    std::vector<Layer> changedLayers;
    std::vector<IComposerClient::Composition> compositionTypes;
    uint32_t displayRequestMask = 0x0;
    std::vector<Layer> requestedLayers;
    std::vector<uint32_t> requestMasks;

    auto err = mHal->validateDisplay(mCurrentDisplay, &changedLayers, &compositionTypes,
                                     &displayRequestMask, &requestedLayers, &requestMasks);
    if (err == Error::NONE) {
        mWriter.setPresentOrValidateResult(0);
        mWriter.setChangedCompositionTypes(changedLayers, compositionTypes);
        mWriter.setDisplayRequests(displayRequestMask, requestedLayers, requestMasks);
    } else {
        mWriter.setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeAcceptDisplayChanges(uint16_t length) {
    if (length != CommandWriterBase::kAcceptDisplayChangesLength) {
        return false;
    }

    auto err = mHal->acceptDisplayChanges(mCurrentDisplay);
    if (err != Error::NONE) {
        mWriter.setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executePresentDisplay(uint16_t length) {
    if (length != CommandWriterBase::kPresentDisplayLength) {
        return false;
    }

    int presentFence = -1;
    std::vector<Layer> layers;
    std::vector<int> fences;
    auto err = mHal->presentDisplay(mCurrentDisplay, &presentFence, &layers, &fences);
    if (err == Error::NONE) {
        mWriter.setPresentFence(presentFence);
        mWriter.setReleaseFences(layers, fences);
    } else {
        mWriter.setError(getCommandLoc(), err);
    }

    return true;
}

bool ComposerCommandEngine::executeSetLayerCursorPosition(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerCursorPositionLength) {
        return false;
    }
    readSigned();
    readSigned();
    auto err = Error::BAD_DISPLAY;
    if (err != Error::NONE) {
        mWriter.setError(getCommandLoc(), err);
    }
    return true;
}

bool ComposerCommandEngine::executeSetLayerBuffer(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerBufferLength) {
        return false;
    }

    bool useCache = false;
    read();
    readHandle(&useCache);
    auto fence = readFence();

    if (fence >= 0) {
        sync_wait(fence, -1);
        close(fence);
    }
    return true;
}

bool ComposerCommandEngine::executeSetLayerSurfaceDamage(uint16_t length) {
    // N rectangles
    if (length % 4 != 0) {
        return false;
    }
    readRegion(length / 4);
    return true;
}

bool ComposerCommandEngine::executeSetLayerBlendMode(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerBlendModeLength) {
        return false;
    }
    readSigned();
    return true;
}

bool ComposerCommandEngine::executeSetLayerColor(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerColorLength) {
        return false;
    }
    readColor();
    return true;
}


bool ComposerCommandEngine::executeSetLayerCompositionType(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerCompositionTypeLength) {
        return false;
    }
    auto err = mHal->setLayerCompositionType(mCurrentDisplay, mCurrentLayer, readSigned());
    if (err != Error::NONE) {
        mWriter.setError(getCommandLoc(), err);
    }
    return true;
}


bool ComposerCommandEngine::executeSetLayerDataspace(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerDataspaceLength) {
        return false;
    }
    readSigned();
    return true;
}


bool ComposerCommandEngine::executeSetLayerDisplayFrame(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerDisplayFrameLength) {
        return false;
    }
    readRect();
    return true;
}

bool ComposerCommandEngine::executeSetLayerPlaneAlpha(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerPlaneAlphaLength) {
        return false;
    }
    readFloat();
    return true;
}

bool ComposerCommandEngine::executeSetLayerSidebandStream(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerSidebandStreamLength) {
        return false;
    }
    readHandle();
    return true;
}

bool ComposerCommandEngine::executeSetLayerSourceCrop(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerSourceCropLength) {
        return false;
    }
    readFRect();
    return true;
}

bool ComposerCommandEngine::executeSetLayerTransform(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerTransformLength) {
        return false;
    }
    readSigned();
    return true;
}

bool ComposerCommandEngine::executeSetLayerVisibleRegion(uint16_t length) {
    // N rectangles
    if (length % 4 != 0) {
        return false;
    }
    readRegion(length / 4);
    return true;
}

bool ComposerCommandEngine::executeSetLayerZOrder(uint16_t length) {
    if (length != CommandWriterBase::kSetLayerZOrderLength) {
        return false;
    }
    read();
    return true;
}


}  // namespace implementation
}  // namespace V2_1
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android
