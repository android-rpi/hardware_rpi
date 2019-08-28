/*
 * Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
 * Copyright (C) 2010-2011 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define LOG_TAG "composer@2.1-drm_kms_rpi3"

#include <cutils/properties.h>
#include <utils/Log.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <math.h>
#include <gralloc_drm.h>
#include <gralloc_drm_priv.h>
#include <hardware_legacy/uevent.h>

#include <drm_fourcc.h>

#include "hwc_context.h"

namespace android {

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

/*
 * Add a fb object for a bo.
 */
static int gralloc_drm_bo_add_fb(struct gralloc_drm_bo_t *bo)
{
	if (bo->fb_id)
		return 0;

	uint32_t pitches[4] = { 0, 0, 0, 0 };
	uint32_t offsets[4] = { 0, 0, 0, 0 };
	uint32_t handles[4] = { 0, 0, 0, 0 };

	pitches[0] = bo->handle->stride;
	handles[0] = bo->fb_handle;

	int drm_format = drm_format_from_hal(bo->handle->format);

	if (drm_format == 0) {
		ALOGE("error resolving drm format");
		return -EINVAL;
	}

	return drmModeAddFB2(bo->drm->fd,
		bo->handle->width, bo->handle->height,
		drm_format, handles, pitches, offsets,
		(uint32_t *) &bo->fb_id, 0);
}

/*
 * Program CRTC.
 */
int hwc_context::set_crtc(struct kms_output *output, int fb_id)
{
	int ret;

	ret = drmModeSetCrtc(kms_fd, output->crtc_id, fb_id,
			0, 0, &output->connector_id, 1, &output->mode);
	if (ret) {
		ALOGE("failed to set crtc (%s) (crtc_id %d, fb_id %d, conn %d, mode %dx%d)",
			strerror(errno), output->crtc_id, fb_id, output->connector_id,
			output->mode.hdisplay, output->mode.vdisplay);
		return ret;
	}

	return ret;
}

/*
 * Callback for a page flip event.
 */
static void page_flip_handler(int /*fd*/, unsigned int /*sequence*/,
			      unsigned int /*tv_sec*/, unsigned int /*tv_usec*/,
		void *user_data)
{
	class hwc_context *ctx = (class hwc_context *) user_data;

	/* ack the last scheduled flip */
	ctx->current_front = ctx->next_front;
	ctx->next_front = NULL;
}

/*
 * Schedule a page flip.
 */
int hwc_context::page_flip(struct gralloc_drm_bo_t *bo)
{
	int ret;

	/* there is another flip pending */
	while (next_front) {
		waiting_flip = 1;
		drmHandleEvent(kms_fd, &evctx);
		waiting_flip = 0;
		if (next_front) {
			/* record an error and break */
			ALOGE("drmHandleEvent returned without flipping");
			current_front = next_front;
			next_front = NULL;
		}
	}

	if (!bo)
		return 0;

	ret = drmModePageFlip(kms_fd, primary_output.crtc_id, bo->fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, (void *) this);
	if (ret) {
		ALOGE("failed to perform page flip for primary (%s) (crtc %d fb %d))",
			strerror(errno), primary_output.crtc_id, bo->fb_id);
		/* try to set mode for next frame */
		if (errno != EBUSY)
			first_post = 1;
	}
	else
		next_front = bo;

	return ret;
}

/*
 * Wait for the next post.
 */
void hwc_context::wait_for_post(int flip)
{
	unsigned int current, target;
	drmVBlank vbl;
	int ret;

	flip = !!flip;

	memset(&vbl, 0, sizeof(vbl));
	int type = DRM_VBLANK_RELATIVE;
	vbl.request.type = (drmVBlankSeqType) type;
	vbl.request.sequence = 0;

	/* get the current vblank */
	ret = drmWaitVBlank(kms_fd, &vbl);
	if (ret) {
		ALOGW("failed to get vblank");
		return;
	}

	current = vbl.reply.sequence;
	if (first_post)
		target = current;
	else
		target = last_swap + swap_interval - flip;

	/* wait for vblank */
	if (current < target || !flip) {
		memset(&vbl, 0, sizeof(vbl));
		int type = DRM_VBLANK_ABSOLUTE;
		if (!flip) {
			type |= DRM_VBLANK_NEXTONMISS;
			if (target < current)
				target = current;
		}
                vbl.request.type = (drmVBlankSeqType) type;
		vbl.request.sequence = target;

		ret = drmWaitVBlank(kms_fd, &vbl);
		if (ret) {
			ALOGW("failed to wait vblank");
			return;
		}
	}

	last_swap = vbl.reply.sequence + flip;
}

/*
 * Post a bo.  This is not thread-safe.
 */
int hwc_context::bo_post(struct gralloc_drm_bo_t *bo)
{
	int ret;

	if (!bo->fb_id) {
		int err = gralloc_drm_bo_add_fb(bo);
		if (err) {
			ALOGE("%s: could not create drm fb, (%s)",
				__func__, strerror(-err));
			ALOGE("unable to post bo %p without fb", bo);
			return err;
		}
	}

	/* TODO spawn a thread to avoid waiting and race */

	if (first_post) {
		ret = set_crtc(&primary_output, bo->fb_id);
		if (!ret) {
			first_post = 0;
			current_front = bo;
			if (next_front == bo)
				next_front = NULL;
		}
		return ret;
	}

	if (swap_interval > 1)
		wait_for_post(1);
	ret = page_flip(bo);
	if (next_front) {
		/*
		 * wait if the driver says so or the current front
		 * will be written by CPU
		 */
		page_flip(NULL);
	}

	return ret;
}

static class hwc_context *ctx_singleton;

static void on_signal(int /*sig*/)
{
	class hwc_context *ctx = ctx_singleton;

	/* wait the pending flip */
	if (ctx && ctx->next_front) {
		/* there is race, but this function is hacky enough to ignore that */
		if (ctx->waiting_flip)
			usleep(100 * 1000); /* 100ms */
		else
			ctx->page_flip(NULL);
	}

	exit(-1);
}

void hwc_context::init_features()
{
	switch (primary_output.fb_format) {
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_RGB_565:
		break;
	default:
		primary_output.fb_format = HAL_PIXEL_FORMAT_RGBA_8888;
		break;
	}

	swap_interval = 1;

	struct sigaction act;
	memset(&evctx, 0, sizeof(evctx));
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.page_flip_handler = page_flip_handler;

	/*
	 * XXX GPU tends to freeze if the program is terminiated with a
	 * flip pending.  What is the right way to handle the
	 * situation?
	 */
	sigemptyset(&act.sa_mask);
	act.sa_handler = on_signal;
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);

	ctx_singleton = this;

	ALOGD("will use flip for fb posting");
}

#define MARGIN_PERCENT 1.8   /* % of active vertical image*/
#define CELL_GRAN 8.0   /* assumed character cell granularity*/
#define MIN_PORCH 1 /* minimum front porch   */
#define V_SYNC_RQD 3 /* width of vsync in lines   */
#define H_SYNC_PERCENT 8.0   /* width of hsync as % of total line */
#define MIN_VSYNC_PLUS_BP 550.0 /* min time of vsync + back porch (microsec) */
#define M 600.0 /* blanking formula gradient */
#define C 40.0  /* blanking formula offset   */
#define K 128.0 /* blanking formula scaling factor   */
#define J 20.0  /* blanking formula scaling factor   */
/* C' and M' are part of the Blanking Duty Cycle computation */
#define C_PRIME   (((C - J) * K / 256.0) + J)
#define M_PRIME   (K / 256.0 * M)

static drmModeModeInfoPtr generate_mode(int h_pixels, int v_lines, float freq)
{
	float h_pixels_rnd;
	float v_lines_rnd;
	float v_field_rate_rqd;
	float top_margin;
	float bottom_margin;
	float interlace;
	float h_period_est;
	float vsync_plus_bp;
	float v_back_porch;
	float total_v_lines;
	float v_field_rate_est;
	float h_period;
	float v_field_rate;
	float v_frame_rate;
	float left_margin;
	float right_margin;
	float total_active_pixels;
	float ideal_duty_cycle;
	float h_blank;
	float total_pixels;
	float pixel_freq;
	float h_freq;

	float h_sync;
	float h_front_porch;
	float v_odd_front_porch_lines;
	int interlaced = 0;
	int margins = 0;

	drmModeModeInfoPtr m = (drmModeModeInfoPtr) malloc(sizeof(drmModeModeInfo));

	h_pixels_rnd = rint((float) h_pixels / CELL_GRAN) * CELL_GRAN;
	v_lines_rnd = interlaced ? rint((float) v_lines) / 2.0 : rint((float) v_lines);
	v_field_rate_rqd = interlaced ? (freq * 2.0) : (freq);
	top_margin = margins ? rint(MARGIN_PERCENT / 100.0 * v_lines_rnd) : (0.0);
	bottom_margin = margins ? rint(MARGIN_PERCENT / 100.0 * v_lines_rnd) : (0.0);
	interlace = interlaced ? 0.5 : 0.0;
	h_period_est = (((1.0 / v_field_rate_rqd) - (MIN_VSYNC_PLUS_BP / 1000000.0)) / (v_lines_rnd + (2 * top_margin) + MIN_PORCH + interlace) * 1000000.0);
	vsync_plus_bp = rint(MIN_VSYNC_PLUS_BP / h_period_est);
	v_back_porch = vsync_plus_bp - V_SYNC_RQD;
	total_v_lines = v_lines_rnd + top_margin + bottom_margin + vsync_plus_bp + interlace + MIN_PORCH;
	v_field_rate_est = 1.0 / h_period_est / total_v_lines * 1000000.0;
	h_period = h_period_est / (v_field_rate_rqd / v_field_rate_est);
	v_field_rate = 1.0 / h_period / total_v_lines * 1000000.0;
	v_frame_rate = interlaced ? v_field_rate / 2.0 : v_field_rate;
	left_margin = margins ? rint(h_pixels_rnd * MARGIN_PERCENT / 100.0 / CELL_GRAN) * CELL_GRAN : 0.0;
	right_margin = margins ? rint(h_pixels_rnd * MARGIN_PERCENT / 100.0 / CELL_GRAN) * CELL_GRAN : 0.0;
	total_active_pixels = h_pixels_rnd + left_margin + right_margin;
	ideal_duty_cycle = C_PRIME - (M_PRIME * h_period / 1000.0);
	h_blank = rint(total_active_pixels * ideal_duty_cycle / (100.0 - ideal_duty_cycle) / (2.0 * CELL_GRAN)) * (2.0 * CELL_GRAN);
	total_pixels = total_active_pixels + h_blank;
	pixel_freq = total_pixels / h_period;
	h_freq = 1000.0 / h_period;
	h_sync = rint(H_SYNC_PERCENT / 100.0 * total_pixels / CELL_GRAN) * CELL_GRAN;
	h_front_porch = (h_blank / 2.0) - h_sync;
	v_odd_front_porch_lines = MIN_PORCH + interlace;

	m->clock = ceil(pixel_freq) * 1000;
	m->hdisplay = (int) (h_pixels_rnd);
	m->hsync_start = (int) (h_pixels_rnd + h_front_porch);
	m->hsync_end = (int) (h_pixels_rnd + h_front_porch + h_sync);
	m->htotal = (int) (total_pixels);
	m->hskew = 0;
	m->vdisplay = (int) (v_lines_rnd);
	m->vsync_start = (int) (v_lines_rnd + v_odd_front_porch_lines);
	m->vsync_end = (int) (int) (v_lines_rnd + v_odd_front_porch_lines + V_SYNC_RQD);
	m->vtotal = (int) (total_v_lines);
	m->vscan = 0;
	m->vrefresh = freq;
	m->flags = 10;
	m->type = 64;

	return (m);
}

static drmModeModeInfoPtr find_mode(drmModeConnectorPtr connector, int *bpp)
{
	char value[PROPERTY_VALUE_MAX];
	drmModeModeInfoPtr mode;
	int dist, i;
	int xres = 0, yres = 0, rate = 0;
	int forcemode = 0;

	if (property_get("debug.drm.mode", value, NULL)) {
		/* parse <xres>x<yres>[@<bpp>] */
		if (sscanf(value, "%dx%d@%d", &xres, &yres, bpp) != 3) {
			*bpp = 0;
			if (sscanf(value, "%dx%d", &xres, &yres) != 2)
				xres = yres = 0;
		}

		if ((xres && yres) || *bpp) {
			ALOGI("will find the closest match for %dx%d@%d",
					xres, yres, *bpp);
		}
	} else if (property_get("debug.drm.mode.force", value, NULL)) {
		*bpp = 0;

		/* parse <xres>x<yres>[@<refreshrate>] */
		if (sscanf(value, "%dx%d@%d", &xres, &yres, &rate) != 3) {
			rate = 60;
			if (sscanf(value, "%dx%d", &xres, &yres) != 2)
				xres = yres = 0;
		}

		if (xres && yres && rate) {
			ALOGI("will use %dx%d@%dHz", xres, yres, rate);
			forcemode = 1;
		}
	} else {
		*bpp = 0;
	}

	dist = INT_MAX;

	if (forcemode)
		mode = generate_mode(xres, yres, rate);
	else {
		mode = NULL;
		for (i = 0; i < connector->count_modes; i++) {
			drmModeModeInfoPtr m = &connector->modes[i];
			int tmp;

			if (xres && yres) {
				tmp = (m->hdisplay - xres) * (m->hdisplay - xres) +
					(m->vdisplay - yres) * (m->vdisplay - yres);
			}
			else {
				/* use the first preferred mode */
				tmp = (m->type & DRM_MODE_TYPE_PREFERRED) ? 0 : dist;
			}

			if (tmp < dist) {
				mode = m;
				dist = tmp;
				if (!dist)
					break;
			}
		}
	}

	/* fallback to the first mode */
	if (!mode)
		mode = &connector->modes[0];

	ALOGI("Established mode:");
	ALOGI("clock: %d, hdisplay: %d, hsync_start: %d, hsync_end: %d, htotal: %d, hskew: %d", mode->clock, mode->hdisplay, mode->hsync_start, mode->hsync_end, mode->htotal, mode->hskew);
	ALOGI("vdisplay: %d, vsync_start: %d, vsync_end: %d, vtotal: %d, vscan: %d, vrefresh: %d", mode->vdisplay, mode->vsync_start, mode->vsync_end, mode->vtotal, mode->vscan, mode->vrefresh);
	ALOGI("flags: %d, type: %d, name %s", mode->flags, mode->type, mode->name);

	*bpp /= 8;

	return mode;
}

/*
 * Initialize KMS with a connector.
 */
int hwc_context::init_with_connector(struct kms_output *output,
		drmModeConnectorPtr connector) {
	drmModeEncoderPtr encoder;
	drmModeModeInfoPtr mode;
	static int used_crtcs = 0;
	int bpp, i;

	encoder = drmModeGetEncoder(kms_fd, connector->encoders[0]);
	if (!encoder)
		return -EINVAL;

	/* find first possible crtc which is not used yet */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (encoder->possible_crtcs & (1 << i) &&
			(used_crtcs & (1 << i)) != (1 << i))
			break;
	}

	used_crtcs |= (1 << i);

	drmModeFreeEncoder(encoder);
	if (i == resources->count_crtcs)
		return -EINVAL;

	output->crtc_id = resources->crtcs[i];
	output->connector_id = connector->connector_id;
	output->pipe = i;

	/* print connector info */
	ALOGI("there are %d modes on connector 0x%x, type %d",
		connector->count_modes,
		connector->connector_id,
		connector->connector_type);
	for (i = 0; i < connector->count_modes; i++)
		ALOGI("  %s", connector->modes[i].name);

	mode = find_mode(connector, &bpp);
	ALOGI("the best mode is %s", mode->name);

	output->mode = *mode;
	switch (bpp) {
	case 2:
		output->fb_format = HAL_PIXEL_FORMAT_RGB_565;
		break;
	case 4:
	default:
		output->fb_format = HAL_PIXEL_FORMAT_RGBA_8888;
		break;
	}

	if (connector->mmWidth && connector->mmHeight) {
		output->xdpi = (output->mode.hdisplay * 25.4 / connector->mmWidth);
		output->ydpi = (output->mode.vdisplay * 25.4 / connector->mmHeight);
	}
	else {
		output->xdpi = 75;
		output->ydpi = 75;
	}

	return 0;
}


/*
 * Fetch a connector of particular type
 */
drmModeConnectorPtr hwc_context::fetch_connector(uint32_t type)
{
	int i;

	if (!resources)
		return NULL;

	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnectorPtr connector =
			connector = drmModeGetConnector(kms_fd,
				resources->connectors[i]);
		if (connector) {
			if (connector->connector_type == type &&
				connector->connection == DRM_MODE_CONNECTED)
				return connector;
			drmModeFreeConnector(connector);
		}
	}
	return NULL;
}

/*
 * Initialize KMS.
 */
int hwc_context::init_kms()
{
	drmModeConnectorPtr primary;
	int i;

	resources = drmModeGetResources(kms_fd);
	if (!resources) {
		ALOGE("failed to get modeset resources");
		return -EINVAL;
	}

	/* find the crtc/connector/mode to use */
	primary = fetch_connector(DRM_MODE_CONNECTOR_HDMIA);
	if (primary) {
		init_with_connector(&primary_output, primary);
		drmModeFreeConnector(primary);
		primary_output.active = 1;
	}

	/* if still no connector, find first connected connector and try it */
	int lastValidConnectorIndex = -1;
	if (!primary_output.active) {

		for (i = 0; i < resources->count_connectors; i++) {
			drmModeConnectorPtr connector;

			connector = drmModeGetConnector(kms_fd,
					resources->connectors[i]);
			if (connector) {
				lastValidConnectorIndex = i;
				if (connector->connection == DRM_MODE_CONNECTED) {
					if (!init_with_connector(
							&primary_output, connector))
						break;
				}

				drmModeFreeConnector(connector);
			}
		}

		/* if no connected connector found, try to enforce the use of the last valid one */
		if (i == resources->count_connectors) {
			if (lastValidConnectorIndex > -1) {
				ALOGD("no connected connector found, enforcing the use of valid connector %d", lastValidConnectorIndex);
				drmModeConnectorPtr connector = drmModeGetConnector(kms_fd, resources->connectors[lastValidConnectorIndex]);
				init_with_connector(&primary_output, connector);
				drmModeFreeConnector(connector);
			}
			else {
				ALOGE("failed to find a valid crtc/connector/mode combination");
				drmModeFreeResources(resources);
				resources = NULL;

				return -EINVAL;
			}
		}
	}

	init_features();
	first_post = 1;
	return 0;
}

int hwc_context::hwc_init(struct drm_module_t *mod) {
    int err = 0;
    pthread_mutex_lock(&mod->mutex);
    if (!mod->drm) {
    	mod->drm = gralloc_drm_create();
        if (!mod->drm)
            err = -EINVAL;
    }
    if (!err) {
    	kms_fd = mod->drm->fd;
        err = init_kms();
    }
    pthread_mutex_unlock(&mod->mutex);
	return err;
}


hwc_context::hwc_context() {
    fps = 60.0;
    int error = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
           (const hw_module_t **)&mModule);
    if (error) {
        ALOGE("Failed to get mModule %d", error);
    } else {
        error = hwc_init(mModule);
        if (error != 0) {
            ALOGE("failed hwc_init_kms() %d", error);
        } else {
            width = (uint32_t)primary_output.mode.hdisplay;
            height = (uint32_t)primary_output.mode.vdisplay;
            fps = (float)primary_output.mode.vrefresh;
            format = primary_output.fb_format;
            xdpi = (float)primary_output.xdpi;
            ydpi = (float)primary_output.ydpi;
        }
    }
}


int hwc_context::hwc_post(buffer_handle_t handle)
{
	struct gralloc_drm_bo_t *bo;

	bo = gralloc_drm_bo_from_handle(handle);
	if (!bo)
		return -EINVAL;

	return bo_post(bo);
}

} // namespace anroid

