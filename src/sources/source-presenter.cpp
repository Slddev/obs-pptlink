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

#include "source-presenter.h"
#include "../theme-colors.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <algorithm>
#include <cstring>

extern ppt::ComBridge *g_bridge;
extern wgc::SharedCapture *g_capture;

namespace sources {

static std::wstring g_strNoNotes;
static std::wstring g_strCounter;
static std::wstring g_strNoSlideshow;
static std::wstring g_strCurrentSlide;
static std::wstring g_strNextSlide;
static std::wstring g_strNotes;
static std::wstring g_strWaiting;
static std::wstring g_strEndOfShow;
static std::wstring g_strBlackScreen;
static std::wstring g_strWhiteScreen;

static uint32_t SwapRedBlue(uint32_t color)
{
	return (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color >> 16) & 0xFF);
}

static uint32_t Mix(uint32_t a, uint32_t b, float t)
{
	uint32_t out = 0xFF000000;
	for (int shift = 0; shift < 24; shift += 8) {
		float ca = (float)((a >> shift) & 0xFF);
		float cb = (float)((b >> shift) & 0xFF);
		out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << shift;
	}
	return out;
}

static std::wstring ModuleTextW(const char *key)
{
	wchar_t *wide = nullptr;
	os_utf8_to_wcs_ptr(obs_module_text(key), 0, &wide);
	std::wstring text = wide ? wide : L"";
	bfree(wide);
	return text;
}

static void ReplaceArg(std::wstring &text, const wchar_t *arg, int value)
{
	size_t pos = text.find(arg);
	if (pos != std::wstring::npos)
		text.replace(pos, wcslen(arg), std::to_wstring(value));
}

static void UpdateTextSource(obs_source_t *source, const std::wstring &text, uint32_t color, uint32_t size, bool bold,
			     uint32_t cx, uint32_t cy)
{
	obs_data_t *settings = obs_data_create();

	int required_size = os_wcs_to_utf8(text.c_str(), text.length(), nullptr, 0);
	std::string utf8(required_size, '\0');
	os_wcs_to_utf8(text.c_str(), text.length(), &utf8[0], required_size + 1);

	obs_data_set_string(settings, "text", utf8.c_str());

	obs_data_t *font_obj = obs_data_create();
	obs_data_set_string(font_obj, "face", "Segoe UI");
	obs_data_set_string(font_obj, "style", bold ? "Bold" : "Regular");
	obs_data_set_int(font_obj, "size", size);
	obs_data_set_int(font_obj, "flags", bold ? OBS_FONT_BOLD : 0);
	obs_data_set_obj(settings, "font", font_obj);
	obs_data_release(font_obj);

	obs_data_set_int(settings, "color", SwapRedBlue(color));
	bool extents = cx > 0 && cy > 0;
	obs_data_set_bool(settings, "extents", extents);
	obs_data_set_bool(settings, "extents_wrap", extents);
	if (extents) {
		obs_data_set_int(settings, "extents_cx", cx);
		obs_data_set_int(settings, "extents_cy", cy);
	}

	obs_source_update(source, settings);
	obs_data_release(settings);
}

static void DrawFilledRect(float x, float y, float w, float h, uint32_t argb)
{
	if (w <= 0 || h <= 0)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	vec4 col;
	vec4_set(&col, ((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
		 ((argb >> 24) & 0xFF) / 255.f);
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

static void FillRect(const LayoutRect &r, uint32_t argb)
{
	DrawFilledRect((float)r.x, (float)r.y, (float)r.cx, (float)r.cy, argb);
}

static void OutlineRect(const LayoutRect &r, int t, uint32_t argb)
{
	FillRect({r.x, r.y, r.cx, t}, argb);
	FillRect({r.x, r.y + r.cy - t, r.cx, t}, argb);
	FillRect({r.x, r.y + t, t, r.cy - 2 * t}, argb);
	FillRect({r.x + r.cx - t, r.y + t, t, r.cy - 2 * t}, argb);
}

static LayoutRect FitRect(const LayoutRect &area, float aspect)
{
	if (area.cx <= 0 || area.cy <= 0 || aspect <= 0)
		return {area.x, area.y, 0, 0};

	int w = area.cx;
	int h = (int)(w / aspect + 0.5f);
	if (h > area.cy) {
		h = area.cy;
		w = (int)(h * aspect + 0.5f);
	}
	return {area.x + (area.cx - w) / 2, area.y + (area.cy - h) / 2, w, h};
}

static void DrawTexture(gs_texture_t *tex, const LayoutRect &dst)
{
	if (!tex || dst.cx <= 0 || dst.cy <= 0)
		return;

	gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_OPAQUE);
	gs_eparam_t *image = gs_effect_get_param_by_name(eff, "image");
	gs_effect_set_texture(image, tex);

	gs_matrix_push();
	gs_matrix_translate3f((float)dst.x, (float)dst.y, 0);

	gs_technique_t *tech = gs_effect_get_technique(eff, "Draw");
	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; i++) {
		if (gs_technique_begin_pass(tech, i)) {
			gs_draw_sprite(tex, 0, (uint32_t)dst.cx, (uint32_t)dst.cy);
			gs_technique_end_pass(tech);
		}
	}
	gs_technique_end(tech);
	gs_matrix_pop();
}

void PresenterSource::Text::Set(const std::wstring &newText, uint32_t newColor, uint32_t newSize, bool newBold,
				uint32_t newCx, uint32_t newCy)
{
	if (!source ||
	    (newText == text && newColor == color && newSize == size && newBold == bold && newCx == cx && newCy == cy))
		return;

	text = newText;
	color = newColor;
	size = newSize;
	bold = newBold;
	cx = newCx;
	cy = newCy;
	UpdateTextSource(source, text, color, size, bold, cx, cy);
}

void PresenterSource::Text::Draw(float x, float y) const
{
	if (!source)
		return;
	gs_matrix_push();
	gs_matrix_translate3f((float)(int)x, (float)(int)y, 0.0f);
	obs_source_video_render(source);
	gs_matrix_pop();
}

uint32_t PresenterSource::Text::Width() const
{
	return source ? obs_source_get_width(source) : 0;
}

uint32_t PresenterSource::Text::Height() const
{
	return source ? obs_source_get_height(source) : 0;
}

int PresenterSource::Px(float value) const
{
	return std::max(1, (int)(value * scale + 0.5f));
}

void PresenterSource::ComputeLayout()
{
	scale = std::max(0.25f, std::min(outWidth / 1920.0f, outHeight / 1080.0f));

	int gap = Px(16);
	layout = ComputePresenterLayout(layoutPreset, outWidth - gap, outHeight - gap, splitX, splitY, Px(64) + gap);
	for (LayoutRect *r : {&layout.current, &layout.next, &layout.notes, &layout.controls}) {
		r->x += gap;
		r->y += gap;
		r->cx = std::max(0, r->cx - gap);
		r->cy = std::max(0, r->cy - gap);
	}
}

LayoutRect PresenterSource::PanelBody(const LayoutRect &panel) const
{
	int header = Px(44);
	int pad = Px(16);
	return {panel.x + pad, panel.y + header, std::max(0, panel.cx - 2 * pad), std::max(0, panel.cy - header - pad)};
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

	ctx->outWidth = (uint32_t)obs_data_get_int(settings, "output_width");
	ctx->outHeight = (uint32_t)obs_data_get_int(settings, "output_height");
	if (!ctx->outWidth)
		ctx->outWidth = 1920;
	if (!ctx->outHeight)
		ctx->outHeight = 1080;

	for (PresenterSource::Text *t : {&ctx->notes, &ctx->counter, &ctx->labelCurrent, &ctx->labelNext,
					 &ctx->labelNotes, &ctx->statusCurrent, &ctx->statusNext, &ctx->statusScreen})
		t->source = obs_source_create_private("text_gdiplus", "presenter_text", nullptr);

	ctx->capture = g_capture;
	if (ctx->capture)
		ctx->capture->AddUser();

	obs_source_update(source, settings);

	obs_log(LOG_INFO, "PresenterSource created");
	return ctx;
}

static void presenter_destroy(void *data)
{
	auto *ctx = static_cast<PresenterSource *>(data);
	if (ctx->capture)
		ctx->capture->RemoveUser();

	for (PresenterSource::Text *t : {&ctx->notes, &ctx->counter, &ctx->labelCurrent, &ctx->labelNext,
					 &ctx->labelNotes, &ctx->statusCurrent, &ctx->statusNext, &ctx->statusScreen}) {
		obs_source_release(t->source);
		t->source = nullptr;
	}

	obs_enter_graphics();
	if (ctx->nextTex) {
		gs_texture_destroy(ctx->nextTex);
	}
	obs_leave_graphics();

	delete ctx;
	obs_log(LOG_INFO, "PresenterSource destroyed");
}

static void presenter_update(void *data, obs_data_t *settings)
{
	auto *ctx = static_cast<PresenterSource *>(data);
	ctx->outWidth = (uint32_t)obs_data_get_int(settings, "output_width");
	ctx->outHeight = (uint32_t)obs_data_get_int(settings, "output_height");

	ctx->splitX = (float)obs_data_get_double(settings, "split_x");
	ctx->splitY = (float)obs_data_get_double(settings, "split_y");

	const char *layoutStr = obs_data_get_string(settings, "layout_preset");
	std::string layout = layoutStr ? layoutStr : "classic";
	if (layout == "classic")
		ctx->layoutPreset = LayoutPreset::Classic;
	else if (layout == "top_bottom")
		ctx->layoutPreset = LayoutPreset::TopBottom;
	else if (layout == "notes_focused")
		ctx->layoutPreset = LayoutPreset::NotesFocused;
	else if (layout == "next_dominant")
		ctx->layoutPreset = LayoutPreset::NextDominant;
	else
		ctx->layoutPreset = LayoutPreset::Custom;

	const char *theme = obs_data_get_string(settings, "theme_preset");
	std::string themePreset = theme ? theme : "obs";
	ctx->followTheme = themePreset == "obs";

	// Fixed presets use the window, panel, text and accent colours of the matching OBS theme
	if (themePreset == "yami") {
		ctx->colBg = 0xFF1D1F26;
		ctx->colPanel = 0xFF272A33;
		ctx->colText = 0xFFFEFEFE;
		ctx->colAccent = 0xFF284CB8;
	} else if (themePreset == "acri") {
		ctx->colBg = 0xFF0D0F14;
		ctx->colPanel = 0xFF181D26;
		ctx->colText = 0xFFDDDDDD;
		ctx->colAccent = 0xFF2D6A9F;
	} else if (themePreset == "rachni") {
		ctx->colBg = 0xFF12101A;
		ctx->colPanel = 0xFF1E1A2A;
		ctx->colText = 0xFFDDDDDD;
		ctx->colAccent = 0xFF00B4C8;
	} else if (themePreset == "grey") {
		ctx->colBg = 0xFF131313;
		ctx->colPanel = 0xFF272727;
		ctx->colText = 0xFFEEEEEE;
		ctx->colAccent = 0xFF336699;
	} else if (themePreset == "system_light") {
		ctx->colBg = 0xFFD3D3D3;
		ctx->colPanel = 0xFFE5E5E5;
		ctx->colText = 0xFF1A1A1A;
		ctx->colAccent = 0xFF3A6FD8;
	} else if (themePreset == "dark_legacy") {
		ctx->colBg = 0xFF1C1C1C;
		ctx->colPanel = 0xFF292929;
		ctx->colText = 0xFFCCCCCC;
		ctx->colAccent = 0xFF336699;
	} else if (themePreset == "custom") {
		ctx->colBg = 0xFF000000 | SwapRedBlue((uint32_t)obs_data_get_int(settings, "col_bg"));
		ctx->colPanel = 0xFF000000 | SwapRedBlue((uint32_t)obs_data_get_int(settings, "col_panel"));
		ctx->colText = 0xFF000000 | SwapRedBlue((uint32_t)obs_data_get_int(settings, "col_text"));
		ctx->colAccent = 0xFF000000 | SwapRedBlue((uint32_t)obs_data_get_int(settings, "col_accent"));
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
	ctx->nextSlide = info.nextSlide;
	ctx->blackScreen = info.blackScreen;
	ctx->whiteScreen = info.whiteScreen;
	ctx->currentNotes = info.notes;
	ctx->slideAspect = ctx->bridge->GetSlideAspect();

	if (ctx->followTheme) {
		theme::Colors c = theme::Get();
		ctx->colBg = c.bg;
		ctx->colPanel = c.panel;
		ctx->colText = c.text;
		ctx->colAccent = c.accent;
	}

	ctx->ComputeLayout();

	uint32_t muted = Mix(ctx->colPanel, ctx->colText, 0.55f);
	ctx->labelCurrent.Set(g_strCurrentSlide, muted, ctx->Px(18), true);
	ctx->labelNext.Set(g_strNextSlide, muted, ctx->Px(18), true);
	ctx->labelNotes.Set(g_strNotes, muted, ctx->Px(18), true);
	ctx->statusCurrent.Set(g_strWaiting, muted, ctx->Px(26), false);
	ctx->statusNext.Set(g_strEndOfShow, muted, ctx->Px(22), false);

	LayoutRect body = ctx->PanelBody(ctx->layout.notes);
	if (ctx->currentNotes.empty())
		ctx->notes.Set(g_strNoNotes, muted, ctx->Px(28), false, body.cx, body.cy);
	else
		ctx->notes.Set(ctx->currentNotes, ctx->colText, ctx->Px(28), false, body.cx, body.cy);

	std::wstring counter = g_strNoSlideshow;
	if (ctx->currentSlide > 0) {
		counter = g_strCounter;
		ReplaceArg(counter, L"%1", ctx->currentSlide);
		ReplaceArg(counter, L"%2", ctx->totalSlides);
	}
	ctx->counter.Set(counter, ctx->colText, ctx->Px(26), true);
	ctx->statusScreen.Set(ctx->whiteScreen ? g_strWhiteScreen : g_strBlackScreen, ctx->colAccent, ctx->Px(20),
			      true);

	std::shared_ptr<const ppt::SlideThumb> thumb = ctx->currentSlide > 0 ? info.nextThumb : nullptr;
	if (thumb != ctx->nextThumb) {
		ctx->nextThumb = thumb;
		obs_enter_graphics();
		if (ctx->nextTex) {
			gs_texture_destroy(ctx->nextTex);
			ctx->nextTex = nullptr;
		}
		if (thumb) {
			const uint8_t *pData = reinterpret_cast<const uint8_t *>(thumb->pixels.data());
			ctx->nextTex = gs_texture_create(thumb->width, thumb->height, GS_BGRA, 1, &pData, 0);
		}
		obs_leave_graphics();
	}
}

static void presenter_render(void *data, gs_effect_t * /*effect*/)
{
	auto *ctx = static_cast<PresenterSource *>(data);

	DrawFilledRect(0, 0, (float)ctx->outWidth, (float)ctx->outHeight, ctx->colBg);

	gs_texture_t *tex = nullptr;
	if (!ctx->capture || !ctx->capture->AcquireFrame(tex))
		tex = nullptr;

	ctx->RenderSlide(ctx->layout.current, ctx->labelCurrent, tex, &ctx->statusCurrent);
	ctx->RenderSlide(ctx->layout.next, ctx->labelNext, ctx->nextTex,
			 ctx->currentSlide > 0 && ctx->nextSlide == 0 ? &ctx->statusNext : nullptr);
	ctx->RenderNotes();
	ctx->RenderControls();
}

void PresenterSource::RenderCard(const LayoutRect &panel, const Text &label)
{
	if (panel.cx <= 0 || panel.cy <= 0)
		return;

	FillRect(panel, colPanel);
	OutlineRect(panel, Px(1), Mix(colPanel, colText, 0.12f));

	int header = Px(44);
	label.Draw((float)(panel.x + Px(16)), (float)panel.y + (header - (int)label.Height()) * 0.5f);
}

void PresenterSource::RenderSlide(const LayoutRect &panel, const Text &label, gs_texture_t *tex, const Text *status)
{
	RenderCard(panel, label);

	LayoutRect body = PanelBody(panel);
	float aspect = slideAspect;
	if (tex && gs_texture_get_width(tex) && gs_texture_get_height(tex))
		aspect = (float)gs_texture_get_width(tex) / (float)gs_texture_get_height(tex);

	LayoutRect well = FitRect(body, aspect);
	FillRect(well, Mix(colBg, 0xFF000000, 0.45f));

	if (tex) {
		DrawTexture(tex, well);
	} else if (status && well.cx > 0) {
		status->Draw(well.x + (well.cx - (int)status->Width()) * 0.5f,
			     well.y + (well.cy - (int)status->Height()) * 0.5f);
	}
}

void PresenterSource::RenderNotes()
{
	RenderCard(layout.notes, labelNotes);

	LayoutRect body = PanelBody(layout.notes);
	if (body.cx > 0 && body.cy > 0)
		notes.Draw((float)body.x, (float)body.y);
}

void PresenterSource::RenderControls()
{
	auto &r = layout.controls;
	if (r.cx <= 0 || r.cy <= 0)
		return;

	FillRect(r, colPanel);
	OutlineRect(r, Px(1), Mix(colPanel, colText, 0.12f));

	int pad = Px(20);
	counter.Draw((float)(r.x + pad), r.y + (r.cy - (int)counter.Height()) * 0.5f);

	if (currentSlide <= 0 || totalSlides <= 0)
		return;

	int barRight = r.x + r.cx - pad;
	if (blackScreen || whiteScreen) {
		barRight -= (int)statusScreen.Width();
		statusScreen.Draw((float)barRight, r.y + (r.cy - (int)statusScreen.Height()) * 0.5f);
		barRight -= pad;
	}

	int barX = r.x + pad + (int)counter.Width() + pad;
	int barW = barRight - barX;
	int barH = Px(6);
	int barY = r.y + (r.cy - barH) / 2;
	if (barW <= 0)
		return;

	FillRect({barX, barY, barW, barH}, Mix(colPanel, colText, 0.15f));
	int fillW = (int)((float)barW * std::min(currentSlide, totalSlides) / totalSlides + 0.5f);
	FillRect({barX, barY, fillW, barH}, colAccent);
}

static bool theme_preset_changed(obs_properties_t *props, obs_property_t * /*p*/, obs_data_t *settings)
{
	const char *theme = obs_data_get_string(settings, "theme_preset");
	bool is_custom = (theme && strcmp(theme, "custom") == 0);

	obs_property_set_visible(obs_properties_get(props, "col_bg"), is_custom);
	obs_property_set_visible(obs_properties_get(props, "col_panel"), is_custom);
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
	obs_property_list_add_string(p_theme, obs_module_text("PPT.PresenterSource.Theme.OBS"), "obs");
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
	obs_properties_add_color(props, "col_text", obs_module_text("PPT.PresenterSource.ColText"));
	obs_properties_add_color(props, "col_accent", obs_module_text("PPT.PresenterSource.ColAccent"));
	return props;
}

static void presenter_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "output_width", 1920);
	obs_data_set_default_int(settings, "output_height", 1080);
	obs_data_set_default_string(settings, "theme_preset", "obs");
	obs_data_set_default_string(settings, "layout_preset", "classic");
	obs_data_set_default_double(settings, "split_x", 0.65);
	obs_data_set_default_double(settings, "split_y", 0.85);
	// Custom colour defaults mirror Yami
	obs_data_set_default_int(settings, "col_bg", 0x261F1D);
	obs_data_set_default_int(settings, "col_panel", 0x332A27);
	obs_data_set_default_int(settings, "col_text", 0xFEFEFE);
	obs_data_set_default_int(settings, "col_accent", 0xB84C28);
}

void RegisterPresenterSource()
{
	g_strNoNotes = ModuleTextW("PPT.PresenterSource.NoNotes");
	g_strCounter = ModuleTextW("PPT.PresenterSource.SlideCounter");
	g_strNoSlideshow = ModuleTextW("PPT.PresenterSource.NoSlideshow");
	g_strCurrentSlide = ModuleTextW("PPT.PresenterSource.CurrentSlide");
	g_strNextSlide = ModuleTextW("PPT.PresenterSource.NextSlide");
	g_strNotes = ModuleTextW("PPT.PresenterSource.Notes");
	g_strWaiting = ModuleTextW("PPT.PresenterSource.Waiting");
	g_strEndOfShow = ModuleTextW("PPT.PresenterSource.EndOfShow");
	g_strBlackScreen = ModuleTextW("PPT.PresenterSource.BlackScreen");
	g_strWhiteScreen = ModuleTextW("PPT.PresenterSource.WhiteScreen");

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
	obs_log(LOG_INFO, "Registered source: ppt_presenter_source");
}

} // namespace sources
