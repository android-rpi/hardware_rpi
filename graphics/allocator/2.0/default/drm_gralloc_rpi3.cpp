#define LOG_TAG "allocator@2.0-drm_gralloc_rpi3"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <sys/errno.h>
#include <drm_fourcc.h>
#include "drm_gralloc_rpi3.h"

int drm_init(struct drm_module_t *mod) {
	int err = 0;
	if (!mod->drm) {
		mod->drm = gralloc_drm_create();
		if (!mod->drm)
			err = -EINVAL;
	}
	return err;
}

void drm_deinit(struct drm_module_t *mod) {
	gralloc_drm_destroy(mod->drm);
	mod->drm = NULL;
}

int drm_alloc(const struct drm_module_t *mod, int w, int h, int format, int usage,
		buffer_handle_t *handle, int *stride) {
	struct gralloc_drm_bo_t *bo;
	int bpp = gralloc_drm_get_bpp(format);
	if (!bpp) return -EINVAL;
	bo = gralloc_drm_bo_create(mod->drm, w, h, format, usage);
	if (!bo) return -ENOMEM;
	*handle = gralloc_drm_bo_get_handle(bo, stride);
	/* in pixels */
	*stride /= bpp;
	return 0;
}

int drm_free(buffer_handle_t handle) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	gralloc_drm_bo_decref(bo);
	return 0;
}

