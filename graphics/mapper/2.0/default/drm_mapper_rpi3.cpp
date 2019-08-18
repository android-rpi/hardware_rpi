#define LOG_TAG "mapper@2.0-drm_mapper_rpi3"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <sys/errno.h>

#include "drm_mapper_rpi3.h"

int drm_init(struct drm_module_t *mod) {
	int err = 0;
	if (!mod->drm) {
		mod->drm = gralloc_drm_create();
		if (!mod->drm)
			err = -EINVAL;
	}
	return err;
}

int drm_register(struct drm_module_t *mod, buffer_handle_t handle) {
	int err = drm_init(mod);
	if (err)
		return err;
	return gralloc_drm_handle_register(handle, mod->drm);
}

int drm_unregister(buffer_handle_t handle) {
	return gralloc_drm_handle_unregister(handle);
}

int drm_lock(buffer_handle_t handle, int usage, int x, int y, int w, int h, void **ptr) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	return gralloc_drm_bo_lock(bo, usage, x, y, w, h, ptr);
}

int drm_unlock(buffer_handle_t handle) {
	struct gralloc_drm_bo_t *bo;
	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;
	gralloc_drm_bo_unlock(bo);
	return 0;
}


