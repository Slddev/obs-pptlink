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

#include "source-presenter.h"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <cstring>
#include <vector>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

extern ppt::ComBridge *g_bridge;

namespace {

// Self-contained decoder using standard Windows GDI+
bool LoadTextureFromFile(const std::wstring &path, std::vector<uint32_t> &pixels, uint32_t &w, uint32_t &h)
{
	bool success = false;
	{
		Gdiplus::Bitmap bmp(path.c_str());
		if (bmp.GetLastStatus() == Gdiplus::Ok) {
			w = bmp.GetWidth();
			h = bmp.GetHeight();
			pixels.resize(w * h);

			Gdiplus::Rect rect(0, 0, w, h);
			Gdiplus::BitmapData data;
			if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) ==
			    Gdiplus::Ok) {
				std::memcpy(pixels.data(), data.Scan0, w * h * 4);
				bmp.UnlockBits(&data);
				success = true;
			}
		}
	}
	return success;
}

} // namespace

namespace sources {

static void UpdateTextSource(obs_source_t *source, const std::wstring &text, uint32_t color, uint32_t size, bool wrap,
			     uint32_t wrap_width)
{
	if (!source)
		return;
	obs_data_t *settings = obs_data_create();

	int required_size = os_wcs_to_utf8(text.c_str(), text.length(), nullptr, 0);
	std::string utf8(required_size, '\0');
	os_wcs_to_utf8(text.c_str(), text.length(), &utf8[0], required_size + 1);

	obs_data_set_string(settings, "text", utf8.c_str());

	obs_data_t *font_obj = obs_data_create();
	obs_data_set_string(font_obj, "face", "Arial");
	obs_data_set_int(font_obj, "size", size);
	obs_data_set_obj(settings, "font", font_obj);
	obs_data_release(font_obj);

	obs_data_set_int(settings, "color", color);
	if (wrap && wrap_width > 0) {
		obs_data_set_bool(settings, "wrap", true);
		obs_data_set_int(settings, "extents_cx", wrap_width);
		obs_data_set_int(settings, "extents_cy", 2000);
	}

	obs_source_update(source, settings);
	obs_data_release(settings);
}

static void DrawFilledRect(float x, float y, float w, float h, uint32_t abgr)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	vec4 col;
	vec4_set(&col, ((abgr >> 16) & 0xFF) / 255.f, ((abgr >> 8) & 0xFF) / 255.f, (abgr & 0xFF) / 255.f,
		 ((abgr >> 24) & 0xFF) / 255.f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &col);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0);

	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; i++) {
		if (gs_technique_begin_pass(tech, i)) {
			gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
			gs_technique_end_pass(tech);
		}
	}
	gs_technique_end(tech);
	gs_matrix_pop();
}

static void DrawTexture(gs_texture_t *tex, float x, float y, float w, float h)
{
	if (!tex)
		return;

	uint32_t tw = gs_texture_get_width(tex);
	uint32_t th = gs_texture_get_height(tex);
	if (!tw || !th)
		return;

	float ar = (float)tw / (float)th;
	float dstW = w;
	float dstH = w / ar;
	if (dstH > h) {
		dstH = h;
		dstW = h * ar;
	}
	float ox = x + (w - dstW) * 0.5f;
	float oy = y + (h - dstH) * 0.5f;

	gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_OPAQUE);
	gs_eparam_t *image = gs_effect_get_param_by_name(eff, "image");
	gs_effect_set_texture(image, tex);

	gs_matrix_push();
	gs_matrix_translate3f(ox, oy, 0);

	gs_technique_t *tech = gs_effect_get_technique(eff, "Draw");
	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; i++) {
		if (gs_technique_begin_pass(tech, i)) {
			gs_draw_sprite(tex, 0, (uint32_t)dstW, (uint32_t)dstH);
			gs_technique_end_pass(tech);
		}
	}
	gs_technique_end(tech);
	gs_matrix_pop();
}

void PresenterSource::ComputeLayout()
{
	int W = (int)outWidth;
	int H = (int)outHeight;

	int barH = 60;
	layout.controls = {0, H - barH, W, barH};

	int availableH = H - barH;

	if (layoutPreset == "classic") {
		int notesH = (int)(availableH * 0.15f);
		int slidesH = availableH - notesH;
		int leftW = (int)(W * 0.65f);

		layout.current = {0, 0, leftW, slidesH};
		layout.next = {leftW, 0, W - leftW, slidesH};
		layout.notes = {0, slidesH, W, notesH};

	} else if (layoutPreset == "top_bottom") {
		int notesH = (int)(availableH * 0.30f);
		int remainingH = availableH - notesH;
		int currentH = (int)(remainingH * 0.60f);
		int nextH = remainingH - currentH;

		layout.current = {0, 0, W, currentH};
		layout.next = {0, currentH, W, nextH};
		layout.notes = {0, remainingH, W, notesH};

	} else if (layoutPreset == "notes_focused") {
		int notesH = (int)(availableH * 0.60f);
		int slidesH = availableH - notesH;
		int halfW = W / 2;

		layout.current = {0, 0, halfW, slidesH};
		layout.next = {halfW, 0, halfW, slidesH};
		layout.notes = {0, slidesH, W, notesH};

	} else if (layoutPreset == "next_dominant") {
		int notesH = (int)(availableH * 0.15f);
		int slidesH = availableH - notesH;
		int leftW = (int)(W * 0.35f);

		layout.current = {0, 0, leftW, slidesH};
		layout.next = {leftW, 0, W - leftW, slidesH};
		layout.notes = {0, slidesH, W, notesH};

	} else {
		int notesH = (int)(availableH * (1.0f - splitY));
		int slidesH = availableH - notesH;
		int leftW = (int)(W * splitX);

		layout.current = {0, 0, leftW, slidesH};
		layout.next = {leftW, 0, W - leftW, slidesH};
		layout.notes = {0, slidesH, W, notesH};
	}
}

static const char *presenter_get_name(void *)
{
	return obs_module_text("PPT.PresenterSource.Name");
}

static void *presenter_create(obs_data_t *settings, obs_source_t *source)
{
	PresenterSource *ctx = new PresenterSource();
	ctx->source = source;
	ctx->bridge = g_bridge;

	obs_enter_graphics();

	ctx->outWidth = (uint32_t)obs_data_get_int(settings, "output_width");
	ctx->outHeight = (uint32_t)obs_data_get_int(settings, "output_height");
	if (!ctx->outWidth)
		ctx->outWidth = 1920;
	if (!ctx->outHeight)
		ctx->outHeight = 1080;

	ctx->notes_source = obs_source_create_private("text_gdiplus", "presenter_notes_txt", nullptr);
	ctx->counter_source = obs_source_create_private("text_gdiplus", "presenter_counter_txt", nullptr);

	ctx->currentCapture.Init(nullptr);
	ctx->currentCapture.OnFrameArrived = nullptr;

	obs_leave_graphics();

	obs_source_update(source, settings);

	blog(LOG_INFO, "[obs-pptlink] PresenterSource created");
	return ctx;
}

static void presenter_destroy(void *data)
{
	auto *ctx = static_cast<PresenterSource *>(data);
	ctx->currentCapture.OnFrameArrived = nullptr;
	ctx->currentCapture.Destroy();

	if (ctx->notes_source) {
		obs_source_release(ctx->notes_source);
		ctx->notes_source = nullptr;
	}
	if (ctx->counter_source) {
		obs_source_release(ctx->counter_source);
		ctx->counter_source = nullptr;
	}

	obs_enter_graphics();
	if (ctx->nextTex) {
		gs_texture_destroy(ctx->nextTex);
	}
	obs_leave_graphics();

	delete ctx;
	blog(LOG_INFO, "[obs-pptlink] PresenterSource destroyed");
}

static void presenter_update(void *data, obs_data_t *settings)
{
	auto *ctx = static_cast<PresenterSource *>(data);
	ctx->outWidth = (uint32_t)obs_data_get_int(settings, "output_width");
	ctx->outHeight = (uint32_t)obs_data_get_int(settings, "output_height");

	ctx->splitX = (float)obs_data_get_double(settings, "split_x");
	ctx->splitY = (float)obs_data_get_double(settings, "split_y");

	const char *layout = obs_data_get_string(settings, "layout_preset");
	ctx->layoutPreset = layout ? layout : "classic";

	const char *theme = obs_data_get_string(settings, "theme_preset");
	ctx->themePreset = theme ? theme : "yami";

	// Apply colors sourced directly from OBS .obt/.ovt theme variable declarations.
	// Mapping: colBg    -> --grey8 (palette_window / darkest bg)
	//          colPanel -> --grey6 (palette_dark  / dock/panel bg)
	//          colBorder-> --grey5 (palette_mid   / separator / input border)
	//          colText  -> --text  (palette_windowText)
	//          colAccent-> --primary (palette_highlight)
	if (ctx->themePreset == "yami") {
		// Yami -- default OBS dark theme, navy-blue tinted grey ramp.
		// --grey8 #1e2328, --grey6 #2c3440, --grey5 #333d4a, --primary #336699
		ctx->colBg = 0xFF1E2328;
		ctx->colPanel = 0xFF2C3440;
		ctx->colBorder = 0xFF333D4A;
		ctx->colText = 0xFFEEEEEE;
		ctx->colAccent = 0xFF336699;
	} else if (ctx->themePreset == "acri") {
		// Acri -- "darker than Dark", deep near-black with slight blue tint.
		// --grey8 #0d0f14, --grey6 #181d26, --grey5 #1e2530, --primary #2d6a9f
		ctx->colBg = 0xFF0D0F14;
		ctx->colPanel = 0xFF181D26;
		ctx->colBorder = 0xFF1E2530;
		ctx->colText = 0xFFDDDDDD;
		ctx->colAccent = 0xFF2D6A9F;
	} else if (ctx->themePreset == "rachni") {
		// Rachni -- teal + red highlights on very dark purple-tinted base.
		// --grey8 #12101a, --grey6 #1e1a2a, --grey5 #252235, --primary teal #00b4c8
		ctx->colBg = 0xFF12101A;
		ctx->colPanel = 0xFF1E1A2A;
		ctx->colBorder = 0xFF252235;
		ctx->colText = 0xFFDDDDDD;
		ctx->colAccent = 0xFF00B4C8; // Rachni teal
	} else if (ctx->themePreset == "grey") {
		// Grey -- neutral greyscale, no blue tint. Same Yami proportions.
		// --grey8 #131313, --grey6 #272727, --grey5 #323232, --primary #336699
		ctx->colBg = 0xFF131313;
		ctx->colPanel = 0xFF272727;
		ctx->colBorder = 0xFF323232;
		ctx->colText = 0xFFEEEEEE;
		ctx->colAccent = 0xFF336699;
	} else if (ctx->themePreset == "system_light") {
		// Light -- inverted Yami palette for light desktop environments.
		// Bg is near-white, panel slightly darker, accent same blue.
		ctx->colBg = 0xFFF0F0F0;
		ctx->colPanel = 0xFFDEDEDE;
		ctx->colBorder = 0xFFB8B8B8;
		ctx->colText = 0xFF1A1A1A;
		ctx->colAccent = 0xFF336699;
	} else if (ctx->themePreset == "dark_legacy") {
		// Dark (Classic) -- pre-Yami OBS default, pure neutral dark, no tint.
		// Straight-grey ramp matching the legacy Dark.qss palette.
		ctx->colBg = 0xFF1C1C1C;
		ctx->colPanel = 0xFF292929;
		ctx->colBorder = 0xFF3D3D3D;
		ctx->colText = 0xFFCCCCCC;
		ctx->colAccent = 0xFF4C7894; // legacy muted blue accent
	} else {                             // Custom -- manual colour pickers
		ctx->colBg = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "col_bg");
		ctx->colPanel = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "col_panel");
		ctx->colBorder = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "col_border");
		ctx->colText = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "col_text");
		ctx->colAccent = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "col_accent");
	}
}

static uint32_t presenter_get_width(void *data)
{
	return static_cast<PresenterSource *>(data)->outWidth;
}

static uint32_t presenter_get_height(void *data)
{
	return static_cast<PresenterSource *>(data)->outHeight;
}

static void presenter_tick(void *data, float /*seconds*/)
{
	auto *ctx = static_cast<PresenterSource *>(data);
	if (!ctx->bridge)
		return;

	auto info = ctx->bridge->GetSlideInfo();

	ctx->currentSlide = info.currentSlide;
	ctx->totalSlides = info.totalSlides;
	ctx->currentNotes = info.notes;
	ctx->nextNotes = info.nextNotes;

	if (ctx->notes_source) {
		uint32_t wWidth = ctx->layout.notes.cx > 40 ? ctx->layout.notes.cx - 40 : 400;
		UpdateTextSource(ctx->notes_source,
				 ctx->currentNotes.empty() ? L"No speaker notes." : ctx->currentNotes, ctx->colText, 24,
				 true, wWidth);
	}
	if (ctx->counter_source) {
		wchar_t cBuf[64];
		swprintf_s(cBuf, L"Slide %d / %d", ctx->currentSlide, ctx->totalSlides);
		UpdateTextSource(ctx->counter_source, cBuf, ctx->colText, 28, false, 0);
	}

	// Load next preview thumbnail dynamically when slide changes
	if (ctx->currentSlide > 0 && ctx->currentSlide != ctx->nextTexSlide) {
		wchar_t tempPath[MAX_PATH];
		GetTempPathW(MAX_PATH, tempPath);
		std::wstring path = std::wstring(tempPath) + L"obs_ppt_next.png";

		std::vector<uint32_t> pixels;
		uint32_t w = 0, h = 0;
		if (LoadTextureFromFile(path, pixels, w, h)) {
			obs_enter_graphics();
			if (ctx->nextTex) {
				gs_texture_destroy(ctx->nextTex);
			}
			const uint8_t *pData = reinterpret_cast<const uint8_t *>(pixels.data());
			ctx->nextTex = gs_texture_create(w, h, GS_BGRA, 1, &pData, 0);
			obs_leave_graphics();
			ctx->nextTexSlide = ctx->currentSlide;
		}
	}

	HWND hwnd = info.slideshowHwnd;
	if (!hwnd)
		hwnd = wgc::FindSlideshowWindow();

	if (hwnd && hwnd != ctx->lastHwnd) {
		ctx->lastHwnd = hwnd;
		ctx->currentCapture.StartCapture(hwnd);
	} else if (!hwnd && ctx->currentCapture.IsRunning()) {
		ctx->currentCapture.StopCapture();
		ctx->lastHwnd = nullptr;

		obs_enter_graphics();
		ctx->currentTex = nullptr;
		if (ctx->nextTex) {
			gs_texture_destroy(ctx->nextTex);
			ctx->nextTex = nullptr;
		}
		obs_leave_graphics();
		ctx->nextTexSlide = -1;
	}
}

static void presenter_render(void *data, gs_effect_t * /*effect*/)
{
	auto *ctx = static_cast<PresenterSource *>(data);

	ctx->ComputeLayout();
	DrawFilledRect(0, 0, (float)ctx->outWidth, (float)ctx->outHeight, ctx->colBg);

	ctx->RenderCurrentSlide();
	ctx->RenderNextSlide();
	ctx->RenderNotes(ctx->currentNotes, ctx->layout.notes);
	ctx->RenderControls();
}

void PresenterSource::RenderCurrentSlide()
{
	auto &r = layout.current;
	DrawFilledRect((float)r.x, (float)r.y, (float)r.cx, (float)r.cy, 0xFF000000);

	gs_texture_t *tex = nullptr;
	if (currentCapture.AcquireLatestFrame(tex)) {
		currentTex = tex;
	}

	if (currentTex) {
		DrawTexture(currentTex, (float)r.x, (float)r.y, (float)r.cx, (float)r.cy);
	} else {
		DrawFilledRect((float)r.x + 10, (float)r.cy * 0.5f - 20, (float)r.cx - 20, 40, 0x44FFFFFF);
	}

	// Optional outline edge matching theme boundaries
	DrawFilledRect((float)r.x, (float)r.y, (float)r.cx, 1, colBorder);
	DrawFilledRect((float)r.x, (float)r.y, 1, (float)r.cy, colBorder);
}

void PresenterSource::RenderNextSlide()
{
	auto &r = layout.next;

	DrawFilledRect((float)r.x, (float)r.y + 28, (float)r.cx, (float)r.cy - 28, colBg);

	DrawFilledRect((float)r.x, (float)r.y, (float)r.cx, 1, colBorder);
	DrawFilledRect((float)r.x, (float)r.y, 1, (float)r.cy, colBorder);

	if (nextTex) {
		DrawTexture(nextTex, (float)r.x + 4, (float)r.y + 32, (float)r.cx - 8, (float)r.cy - 36);
	}
}

void PresenterSource::RenderNotes(const std::wstring & /*text*/, gs_rect area)
{
	DrawFilledRect((float)area.x, (float)area.y, (float)area.cx, (float)area.cy, colPanel);
	DrawFilledRect((float)area.x, (float)area.y, (float)area.cx, 2, colBorder);
	DrawFilledRect((float)area.x, (float)area.y, 1, (float)area.cy, colBorder);

	if (notes_source) {
		gs_matrix_push();
		gs_matrix_translate3f((float)area.x + 20, (float)area.y + 20, 0.0f);
		obs_source_video_render(notes_source);
		gs_matrix_pop();
	}
}

void PresenterSource::RenderControls()
{
	auto &r = layout.controls;
	DrawFilledRect((float)r.x, (float)r.y, (float)r.cx, (float)r.cy, colPanel);
	DrawFilledRect((float)r.x, (float)r.y, (float)r.cx, 2, colBorder);

	if (counter_source) {
		gs_matrix_push();
		gs_matrix_translate3f((float)r.x + 40, (float)r.y + (r.cy * 0.3f), 0.0f);
		obs_source_video_render(counter_source);
		gs_matrix_pop();
	}
}

static bool theme_preset_changed(obs_properties_t *props, obs_property_t * /*p*/, obs_data_t *settings)
{
	const char *theme = obs_data_get_string(settings, "theme_preset");
	bool is_custom = (theme && strcmp(theme, "custom") == 0);

	obs_property_set_visible(obs_properties_get(props, "col_bg"), is_custom);
	obs_property_set_visible(obs_properties_get(props, "col_panel"), is_custom);
	obs_property_set_visible(obs_properties_get(props, "col_border"), is_custom);
	obs_property_set_visible(obs_properties_get(props, "col_text"), is_custom);
	obs_property_set_visible(obs_properties_get(props, "col_accent"), is_custom);
	return true;
}

static bool layout_preset_changed(obs_properties_t *props, obs_property_t * /*p*/, obs_data_t *settings)
{
	const char *layout = obs_data_get_string(settings, "layout_preset");
	bool is_custom = (layout && (strcmp(layout, "custom") == 0));

	obs_property_set_visible(obs_properties_get(props, "split_x"), is_custom);
	obs_property_set_visible(obs_properties_get(props, "split_y"), is_custom);
	return true;
}

static obs_properties_t *presenter_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int(props, "output_width", obs_module_text("PPT.PresenterSource.OutputWidth"), 640, 7680, 2);
	obs_properties_add_int(props, "output_height", obs_module_text("PPT.PresenterSource.OutputHeight"), 360, 4320,
			       2);

	// Theme dropdown -- names match OBS's own theme labels
	obs_property_t *p_theme = obs_properties_add_list(props, "theme_preset",
							  obs_module_text("PPT.PresenterSource.ThemePreset"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.Yami"), "yami");
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.Acri"), "acri");
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.Rachni"), "rachni");
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.Grey"), "grey");
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.Light"), "system_light");
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.DarkLegacy"), "dark_legacy");
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.Custom"), "custom");
	obs_property_set_modified_callback(p_theme, theme_preset_changed);

	// Expanded Window View Presentation Layouts
	obs_property_t *p_layout = obs_properties_add_list(props, "layout_preset",
							   obs_module_text("PPT.PresenterSource.LayoutPreset"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_layout, obs_module_text("PPT.PresenterSource.Layout.Classic"), "classic");
	obs_property_list_add_string(p_layout, obs_module_text("PPT.PresenterSource.Layout.TopBottom"), "top_bottom");
	obs_property_list_add_string(p_layout, obs_module_text("PPT.PresenterSource.Layout.NotesFocused"),
				     "notes_focused");
	obs_property_list_add_string(p_layout, obs_module_text("PPT.PresenterSource.Layout.NextDominant"),
				     "next_dominant");
	obs_property_list_add_string(p_layout, obs_module_text("PPT.PresenterSource.Layout.Custom"), "custom");
	obs_property_set_modified_callback(p_layout, layout_preset_changed);

	obs_properties_add_float_slider(props, "split_x", obs_module_text("PPT.PresenterSource.SplitX"), 0.4, 0.85,
					0.01);
	obs_properties_add_float_slider(props, "split_y", obs_module_text("PPT.PresenterSource.SplitY"), 0.5, 0.95,
					0.01);

	obs_properties_add_color(props, "col_bg", obs_module_text("PPT.PresenterSource.ColBg"));
	obs_properties_add_color(props, "col_panel", obs_module_text("PPT.PresenterSource.ColPanel"));
	obs_properties_add_color(props, "col_border", obs_module_text("PPT.PresenterSource.ColBorder"));
	obs_properties_add_color(props, "col_text", obs_module_text("PPT.PresenterSource.ColText"));
	obs_properties_add_color(props, "col_accent", obs_module_text("PPT.PresenterSource.ColAccent"));
	return props;
}

static void presenter_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "output_width", 1920);
	obs_data_set_default_int(settings, "output_height", 1080);
	obs_data_set_default_string(settings, "theme_preset", "yami");
	obs_data_set_default_string(settings, "layout_preset", "classic");
	obs_data_set_default_double(settings, "split_x", 0.65);
	obs_data_set_default_double(settings, "split_y", 0.85);
	// Custom colour defaults mirror Yami
	obs_data_set_default_int(settings, "col_bg", 0x1A1A1A);
	obs_data_set_default_int(settings, "col_panel", 0x2D2D2D);
	obs_data_set_default_int(settings, "col_border", 0x3D3D3D);
	obs_data_set_default_int(settings, "col_text", 0xE0E0E0);
	obs_data_set_default_int(settings, "col_accent", 0x009AC7);
}

void RegisterPresenterSource()
{
	obs_source_info info = {};
	info.id = "ppt_presenter_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = presenter_get_name;
	info.create = presenter_create;
	info.destroy = presenter_destroy;
	info.update = presenter_update;
	info.video_tick = presenter_tick;
	info.video_render = presenter_render;
	info.get_width = presenter_get_width;
	info.get_height = presenter_get_height;
	info.get_properties = presenter_get_properties;
	info.get_defaults = presenter_get_defaults;

	obs_register_source(&info);
	blog(LOG_INFO, "[obs-pptlink] Registered source: ppt_presenter_source");
}

} // namespace sources