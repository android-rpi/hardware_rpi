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

static unsigned int drm_format_from_hal(int hal_format)
{
	switch(hal_format) {
		case HAL_PIXEL_FORMAT_RGB_888:
		case HAL_PIXEL_FORMAT_BGRA_8888:
			return DRM_FORMAT_XRGB8888;
		case HAL_PIXEL_FORMAT_RGBX_8888:
			return DRM_FORMAT_XBGR8888;
		case HAL_PIXEL_FORMAT_RGBA_8888:
			return DRM_FORMAT_RGBA8888;
		case HAL_PIXEL_FORMAT_RGB_565:
			return DRM_FORMAT_RGB565;
		case HAL_PIXEL_FORMAT_YV12:
			return DRM_FORMAT_YUV420;
		default:
			return 0;
	}
}

static int resolve_drm_format(struct gralloc_drm_bo_t *bo,
	uint32_t *pitches, uint32_t *offsets, uint32_t *handles)
{
	struct gralloc_drm_t *drm = bo->drm;

	pitches[0] = bo->handle->stride;
	handles[0] = bo->fb_handle;

	/* driver takes care of HW specific padding, alignment etc. */
	if (drm->drv->resolve_format)
		drm->drv->resolve_format(drm->drv, bo,
			pitches, offsets, handles);

	return drm_format_from_hal(bo->handle->format);
}

static int gralloc_drm_bo_add_fb(struct gralloc_drm_bo_t *bo)
{
	uint32_t pitches[4] = { 0, 0, 0, 0 };
	uint32_t offsets[4] = { 0, 0, 0, 0 };
	uint32_t handles[4] = { 0, 0, 0, 0 };

	if (bo->fb_id)
		return 0;

	int drm_format = resolve_drm_format(bo, pitches, offsets, handles);

	if (drm_format == 0) {
		ALOGE("error resolving drm format");
		return -EINVAL;
	}

	return drmModeAddFB2(bo->drm->fd,
		bo->handle->width, bo->handle->height,
		drm_format, handles, pitches, offsets,
		(uint32_t *) &bo->fb_id, 0);
}

int drm_alloc(const struct drm_module_t *mod, int w, int h, int format, int usage,
		buffer_handle_t *handle, int *stride) {
	struct gralloc_drm_bo_t *bo;
	int bpp, err;
	bpp = gralloc_drm_get_bpp(format);
	if (!bpp) return -EINVAL;
	bo = gralloc_drm_bo_create(mod->drm, w, h, format, usage);
	if (!bo) return -ENOMEM;
	if (bo->handle->usage & GRALLOC_USAGE_HW_FB) {
		err = gralloc_drm_bo_add_fb(bo);
		if (err) {
			ALOGE("failed to add fb");
			gralloc_drm_bo_decref(bo);
			return err;
		}
	}
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

