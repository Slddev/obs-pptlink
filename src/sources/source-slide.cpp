/*
PPTLink
Copyright (C) 2026 Slddev me@sappy.eu.org

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "source-slide.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/graphics.h>

extern ppt::ComBridge *g_bridge;

namespace sources {

static const char *slide_source_get_name(void *)
{
	return obs_module_text("PPT.SlideSource.Name");
}

static void *slide_source_create(obs_data_t * /*settings*/, obs_source_t *source)
{
	SlideSource *ctx = new SlideSource();
	ctx->source = source;
	ctx->bridge = g_bridge;

	obs_enter_graphics();

	if (!ctx->capture.Init(nullptr)) {
		obs_log(LOG_ERROR, "SlideSource: WGC init failed");
	}

	ctx->capture.OnFrameArrived = [source]() {
		obs_source_update(source, nullptr);
	};

	ctx->capture.ShouldAcceptFrame = [ctx]() {
		return ctx->slideshowActive.load();
	};

	obs_leave_graphics();

	return ctx;
}

static void slide_source_destroy(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);

	ctx->capture.Destroy();
	delete ctx;
}

static uint32_t slide_source_get_width(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);
	uint32_t w = ctx->capture.Width();
	return w > 0 ? w : 1920;
}

static uint32_t slide_source_get_height(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);
	uint32_t h = ctx->capture.Height();
	return h > 0 ? h : 1080;
}

static void slide_source_tick(void *data, float seconds)
{
	auto *ctx = static_cast<SlideSource *>(data);
	if (!ctx)
		return;

	if (ctx->bridge) {
		bool connected = ctx->bridge->IsConnected();
		if (connected != ctx->bridgeConnected) {
			ctx->bridgeConnected = connected;
			if (!connected)
				ctx->slideshowActive.store(false);
		}
	}

	if (ctx->capture.IsRunning() && !ctx->slideshowActive.load()) {
		ctx->lastHwnd = nullptr;
		ctx->capture.StopCapture();
	}

	ctx->pollAccum += seconds;
	if (ctx->pollAccum < 0.5f)
		return;
	ctx->pollAccum = 0.0f;

	HWND hwnd = nullptr;
	if (ctx->bridge && ctx->bridge->IsConnected())
		hwnd = ctx->bridge->GetSlideInfo().slideshowHwnd;
	if (!hwnd)
		hwnd = wgc::FindSlideshowWindow(nullptr);

	if (hwnd != ctx->lastHwnd) {
		ctx->lastHwnd = hwnd;
		if (hwnd) {
			ctx->slideshowActive.store(true);
			ctx->capture.StartCapture(hwnd);
		} else {
			ctx->capture.StopCapture();
		}
	}

	if (ctx->bridge && ctx->bridge->IsConnected())
		ctx->capture.SetSlideAspect(ctx->bridge->GetSlideAspect());
}

static void slide_source_render(void *data, gs_effect_t *effect)
{
	auto *ctx = static_cast<SlideSource *>(data);
	if (!ctx)
		return;

	ctx->RenderFrame(effect);
}

void SlideSource::RenderFrame(gs_effect_t *effect)
{
	capture.AcquireLatestFrame(texture);

	if (!texture)
		return;

	uint32_t w = gs_texture_get_width(texture);
	uint32_t h = gs_texture_get_height(texture);

	if (!w || !h)
		return;

	gs_effect_t *eff = effect ? effect : obs_get_base_effect(OBS_EFFECT_OPAQUE);

	gs_eparam_t *image = gs_effect_get_param_by_name(eff, "image");
	gs_effect_set_texture(image, texture);

	if (effect) {
		gs_draw_sprite(texture, 0, w, h);
	} else {
		gs_technique_t *tech = gs_effect_get_technique(eff, "Draw");
		size_t passes = gs_technique_begin(tech);
		for (size_t i = 0; i < passes; i++) {
			if (gs_technique_begin_pass(tech, i)) {
				gs_draw_sprite(texture, 0, w, h);
				gs_technique_end_pass(tech);
			}
		}
		gs_technique_end(tech);
	}
}

void RegisterSlideSource()
{
	obs_source_info info = {};
	info.id = "ppt_slide_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = slide_source_get_name;
	info.create = slide_source_create;
	info.destroy = slide_source_destroy;
	info.video_tick = slide_source_tick;
	info.video_render = slide_source_render;
	info.get_width = slide_source_get_width;
	info.get_height = slide_source_get_height;

	obs_register_source(&info);
	obs_log(LOG_INFO, "Registered source: ppt_slide_source");
}

} // namespace sources