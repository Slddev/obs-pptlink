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

extern wgc::SharedCapture *g_capture;

namespace sources {

static const char *slide_source_get_name(void *)
{
	return obs_module_text("PPT.SlideSource.Name");
}

static void *slide_source_create(obs_data_t * /*settings*/, obs_source_t *source)
{
	SlideSource *ctx = new SlideSource();
	ctx->source = source;
	ctx->capture = g_capture;
	if (ctx->capture)
		ctx->capture->AddUser();
	return ctx;
}

static void slide_source_destroy(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);

	if (ctx->capture)
		ctx->capture->RemoveUser();
	delete ctx;
}

static uint32_t slide_source_get_width(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);
	uint32_t w = ctx->capture ? ctx->capture->OutputWidth() : 0;
	return w > 0 ? w : 1920;
}

static uint32_t slide_source_get_height(void *data)
{
	auto *ctx = static_cast<SlideSource *>(data);
	uint32_t h = ctx->capture ? ctx->capture->OutputHeight() : 0;
	return h > 0 ? h : 1080;
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
	gs_texture_t *texture = nullptr;
	if (capture)
		capture->AcquireOutputFrame(texture);

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
	info.video_render = slide_source_render;
	info.get_width = slide_source_get_width;
	info.get_height = slide_source_get_height;

	obs_register_source(&info);
	obs_log(LOG_INFO, "Registered source: ppt_slide_source");
}

} // namespace sources