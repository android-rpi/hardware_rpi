#include <hardware/hwcomposer.h>

#define HWC2_INCLUDE_STRINGIFICATION
#define HWC2_USE_CPP11
#include <hardware/hwcomposer2.h>
#undef HWC2_USE_CPP11
#undef HWC2_INCLUDE_STRINGIFICATION

#include <ui/Fence.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>
#include "hwc_context.h"

namespace android {

class Hwc2Device {
public:
    Hwc2Device();

    int32_t createLayer(hwc2_display_t displayId, hwc2_layer_t* outLayerId);
    int32_t destroyLayer(hwc2_display_t displayId, hwc2_layer_t layerId);
    int32_t getClientTargetSupport(hwc2_display_t displayId, uint32_t width, uint32_t height,
                                          int32_t format, int32_t dataspace);
    int32_t getDisplayAttribute(hwc2_display_t displayId, hwc2_config_t config,
            int32_t intAttribute, int32_t* outValue);
    int32_t getDisplayName(hwc2_display_t displayId, uint32_t* outSize, char* outName);

    int32_t setVsyncEnabled(hwc2_display_t displayId, int32_t intEnabled);

    int32_t setClientTarget(hwc2_display_t displayId, buffer_handle_t target,
            int32_t acquireFence, int32_t dataspace, hwc_region_t damage);
    int32_t validateDisplay(hwc2_display_t displayId, uint32_t* outNumTypes,
            uint32_t* outNumRequests);
    int32_t presentDisplay(hwc2_display_t displayId, int32_t* outRetireFence);
    int32_t acceptDisplayChanges(hwc2_display_t displayId);

    int32_t getChangedCompositionTypes(hwc2_display_t displayId, uint32_t* outNumElements,
            hwc2_layer_t* outLayers, int32_t* outTypes);
    int32_t setLayerCompositionType(hwc2_display_t displayId, hwc2_layer_t layerId,
            int32_t intType);

    void dump(uint32_t* outSize, char* outBuffer);

    int32_t registerCallback(int32_t intDesc, hwc2_callback_data_t callbackData,
            hwc2_function_pointer_t pointer);

private:
    struct Info {
        std::string name;
        uint32_t width;
        uint32_t height;
        int format;
        int vsync_period_ns;
        int xdpi_scaled;
        int ydpi_scaled;
    };
    Info mFbInfo{};
    const Info& getInfo() const { return mFbInfo; }

    enum class State {
        MODIFIED,
        VALIDATED_WITH_CHANGES,
        VALIDATED,
    };
    State mState{State::MODIFIED};
    void setState(State state) { mState = state; }
    State getState() const { return mState; }

    uint64_t mNextLayerId{0};
    std::unordered_set<hwc2_layer_t> mLayers;
    std::unordered_set<hwc2_layer_t> mDirtyLayers;
    hwc2_layer_t addLayer();
    bool removeLayer(hwc2_layer_t layer);
    bool hasLayer(hwc2_layer_t layer) const;
    bool markLayerDirty(hwc2_layer_t layer, bool dirty);
    const std::unordered_set<hwc2_layer_t>& getDirtyLayers() const;
    void clearDirtyLayers();

    buffer_handle_t mBuffer{nullptr};


    std::string mDumpString;

    class VsyncThread {
    public:
        static int64_t now();
        static bool sleepUntil(int64_t t);

        void start(int64_t first, int64_t period);
        void stop();
        void setCallback(HWC2_PFN_VSYNC callback, hwc2_callback_data_t data);
        void enableCallback(bool enable);

    private:
        void vsyncLoop();
        bool waitUntilNextVsync();

        std::thread mThread;
        int64_t mNextVsync{0};
        int64_t mPeriod{0};

        std::mutex mMutex;
        std::condition_variable mCondition;
        bool mStarted{false};
        HWC2_PFN_VSYNC mCallback{nullptr};
        hwc2_callback_data_t mCallbackData{nullptr};
        bool mCallbackEnabled{false};
    };
    VsyncThread mVsyncThread;

    std::unique_ptr<hwc_context> mHwcContext;
};

} // namespace android
