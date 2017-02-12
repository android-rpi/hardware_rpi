#undef LOG_TAG
#define LOG_TAG "GRALLOC-FB"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include <stdlib.h>
#include <GLES/gl.h>

#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>

static int drm_init_kms(const struct drm_gralloc1_module_t *cmod) {
	struct drm_gralloc1_module_t *mod = (struct drm_gralloc1_module_t *) cmod;
	int err = 0;
	pthread_mutex_lock(&mod->mutex);
	if (!mod->drm) {
		mod->drm = gralloc_drm_create();
		if (!mod->drm)
			err = -EINVAL;
	}
	if (!err)
		err = gralloc_drm_init_kms(mod->drm);
	pthread_mutex_unlock(&mod->mutex);
	return err;
}

static int drm_mod_close_fb0(struct hw_device_t *dev)
{
	struct framebuffer_device_t *fb = (struct framebuffer_device_t *) dev;
	free(fb);
	return 0;
}

static int drm_mod_set_swap_interval_fb0(struct framebuffer_device_t *fb,
		int interval)
{
	if (interval < fb->minSwapInterval || interval > fb->maxSwapInterval)
		return -EINVAL;
	return 0;
}

static int drm_mod_post_fb0(struct framebuffer_device_t *fb,
		buffer_handle_t handle)
{
	struct drm_gralloc1_module_t *dmod = (struct drm_gralloc1_module_t *) fb->common.module;
	struct gralloc_drm_bo_t *bo;

	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;

	return gralloc_drm_bo_post(bo);
}

static int drm_mod_composition_complete_fb0(struct framebuffer_device_t *fb)
{
	struct drm_gralloc1_module_t *dmod = (struct drm_gralloc1_module_t *) fb->common.module;

	if (gralloc_drm_is_kms_pipelined(dmod->drm))
		glFlush();
	else
		glFinish();

	return 0;
}

int drm_mod_open_fb0(struct drm_gralloc1_module_t *dmod, struct hw_device_t **dev)
{
	struct framebuffer_device_t *fb;
	int err;

	err = drm_init_kms(dmod);
	if (err)
		return err;

	fb = (struct framebuffer_device_t *)calloc(1, sizeof(*fb));
	if (!fb)
		return -ENOMEM;

	fb->common.tag = HARDWARE_DEVICE_TAG;
	fb->common.version = 0;
	fb->common.module = (struct hw_module_t*)&dmod->common;
	fb->common.close = drm_mod_close_fb0;

	fb->setSwapInterval = drm_mod_set_swap_interval_fb0;
	fb->post = drm_mod_post_fb0;
	fb->compositionComplete = drm_mod_composition_complete_fb0;

	gralloc_drm_get_kms_info(dmod->drm, fb);

	*dev = &fb->common;

	ALOGI("mode.hdisplay %d\n"
	     "mode.vdisplay %d\n"
	     "mode.vrefresh %f\n"
	     "format 0x%x\n"
	     "xdpi %f\n"
	     "ydpi %f\n",
	     fb->width,
	     fb->height,
	     fb->fps,
	     fb->format,
	     fb->xdpi, fb->ydpi);
	return 0;
}
 
