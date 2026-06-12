/*
obs-pptlink
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
#include <graphics/graphics.h>

extern ppt::ComBridge *g_bridge;

namespace sources {

static const char *slide_source_get_name(void *)
{
	return obs_module_text("PPT.SlideSource.Name");
}

static void *slide_source_create(obs_data_t *settings, obs_source_t *source)
{
	SlideSource *ctx = new SlideSource();
	ctx->source = source;
	ctx->bridge = g_bridge;
	ctx->showBorder = obs_data_get_bool(settings, "show_border");

	obs_enter_graphics();

	if (!ctx->capture.Init(nullptr)) {
		blog(LOG_ERROR, "[obs-pptlink] SlideSource: WGC init failed");
	}

	ctx->capture.OnFrameArrived = [source]() {
		obs_source_update(source, nullptr);
	};

	ctx->capture.ShouldAcceptFrame = [ctx]() {
		return ctx->slideshowActive.load();
	};

	if (ctx->bridge) {
		ctx->bridge->OnSlideChanged = [ctx](const ppt::SlideInfo &info) {
			ctx->slideshowActive.store(info.slideshowActive);
		};
		ctx->bridge->OnConnectionChanged = [ctx](bool connected) {
			if (!connected)
				ctx->slideshowActive.store(false);
		};
	}

	ctx->hotkeyNext = obs_hotkey_register_source(
		source, "ppt.next_slide", obs_module_text("PPT.Hotkey.NextSlide"),
		[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (pressed) {
				auto *ctx = static_cast<SlideSource *>(data);
				if (ctx->bridge)
					ctx->bridge->NextSlide();
			}
		},
		ctx);

	ctx->hotkeyPrev = obs_hotkey_register_source(
		source, "ppt.prev_slide", obs_module_text("PPT.Hotkey.PrevSlide"),
		[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (pressed) {
				auto *ctx = static_cast<SlideSource *>(data);
				if (ctx->bridge)
					ctx->bridge->PrevSlide();
			}
		},
		ctx);

	obs_leave_graphics();

	obs_source_update(source, settings);

	return ctx;
}

static void slide_source_destroy(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);

	if (ctx->bridge) {
		ctx->bridge->OnSlideChanged = nullptr;
		ctx->bridge->OnConnectionChanged = nullptr;
	}

	if (ctx->hotkeyNext != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(ctx->hotkeyNext);
	if (ctx->hotkeyPrev != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(ctx->hotkeyPrev);

	ctx->capture.Destroy();
	delete ctx;
}

static void slide_source_update(void *data, obs_data_t *settings)
{
	auto *ctx = static_cast<SlideSource *>(data);
	ctx->showBorder = obs_data_get_bool(settings, "show_border");
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

	ctx->pollAccum += seconds;
	if (ctx->pollAccum < 0.5f)
		return;
	ctx->pollAccum = 0.0f;

	HWND hwnd = wgc::FindSlideshowWindow(nullptr);

	if (hwnd != ctx->lastHwnd) {
		ctx->lastHwnd = hwnd;
		if (hwnd) {
			ctx->slideshowActive.store(true);
			ctx->capture.StartCapture(hwnd);
		} else {
			ctx->capture.StopCapture();
		}
	}

	if (ctx->bridge && ctx->bridge->IsConnected()) {
		ppt::SlidePaneRect pr = ctx->bridge->GetSlidePaneRect();
		if (pr.valid && pr.w > 0 && pr.h > 0)
			ctx->capture.SetSlideAspect(static_cast<float>(pr.w) / static_cast<float>(pr.h));
	}
}

static void slide_source_render(void *data, gs_effect_t *effect)
{
	auto *ctx = static_cast<SlideSource *>(data);
	if (!ctx)
		return;

	if (ctx->capture.IsRunning() && !ctx->slideshowActive.load()) {
		ctx->lastHwnd = nullptr;
		ctx->capture.StopCapture();
	}

	gs_texture_t *texture = nullptr;
	ctx->capture.AcquireLatestFrame(texture);

	if (!texture)
		return;

	obs_source_draw(texture, 0, 0, 0, 0, false);
}

void SlideSource::TryStartCapture()
{
	HWND hwnd = lastHwnd;
	if (!hwnd) {
		hwnd = wgc::FindSlideshowWindow();
		if (!hwnd)
			return;
		lastHwnd = hwnd;
	}
	capture.StartCapture(hwnd);
}

void SlideSource::RenderFrame(gs_effect_t *effect)
{
	(void)effect; // We'll use our own effect

	gs_texture_t *tex = nullptr;
	bool hasNew = capture.AcquireLatestFrame(tex);

	if (hasNew && tex) {
		texture = tex;
	}

	if (!texture) {
		return;
	}

	uint32_t w = gs_texture_get_width(texture);
	uint32_t h = gs_texture_get_height(texture);

	if (!w || !h) {
		return;
	}

	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_OPAQUE);
	gs_eparam_t *image = gs_effect_get_param_by_name(def, "image");
	gs_effect_set_texture(image, texture);

	while (gs_effect_loop(def, "Draw")) {
		gs_draw_sprite(texture, 0, w, h);
	}
}

static obs_properties_t *slide_source_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "show_border", obs_module_text("PPT.SlideSource.ShowBorder"));

	return props;
}

static void slide_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "show_border", false);
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
	info.update = slide_source_update;
	info.video_tick = slide_source_tick;
	info.video_render = slide_source_render;
	info.get_width = slide_source_get_width;
	info.get_height = slide_source_get_height;
	info.get_properties = slide_source_get_properties;
	info.get_defaults = slide_source_get_defaults;

	obs_register_source(&info);
}

} // namespace sources