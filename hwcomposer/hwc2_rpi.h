#ifndef _HWC2_RPI_H_
#define _HWC2_RPI_H_

#include <fcntl.h>
#include <errno.h>

#include <EGL/egl.h>
#include <Condition.h>
#include <Mutex.h>
#include <StrongPointer.h>
#include <Thread.h>

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/gralloc.h>

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>
#include <gralloc_drm_handle.h>

#define SEC_TO_NANOSEC (1000 * 1000 * 1000)

class hwc_context;

/**
* Fake VSync class.
* To provide refresh timestamps to the surface flinger, using the
* same fake mechanism as SF uses on its own, and this is because one
* cannot start using hwc until it provides certain mandatory things - the
* refresh time stamps being one of them.
*/
class vsync_worker : public android::Thread {
public:
	vsync_worker(hwc_context *c);
	void set_enabled(bool enabled);
private:
	virtual void onFirstRef();
	virtual bool threadLoop();
	void wait_until_enabled();
private:
	class hwc_context *ctx;
	mutable android::Mutex lock;
	android::Condition condition;
	bool enabled;
	mutable int64_t next_fake_vsync;
public:
	int64_t refresh_period;
};

class hwc_context {
public :
	struct drm_gralloc1_module_t *gralloc_module;
	android::sp<vsync_worker> vsync_thread;

    int prepare(size_t numDisplays, hwc_display_contents_1_t** displays);
    int set(size_t numDisplays, hwc_display_contents_1_t** displays);
    int getDisplayAttributes(int disp,
            uint32_t config, const uint32_t* attributes, int32_t* values);

    android::Hwc2Device *device;
};

#endif // _HWC2_RPI_H_
