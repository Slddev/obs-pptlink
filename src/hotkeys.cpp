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

#include "hotkeys.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

namespace hotkeys {

static obs_hotkey_id g_first = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_next = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_prev = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_last = OBS_INVALID_HOTKEY_ID;
static ppt::ComBridge *g_br = nullptr;

static void SaveBindings()
{
	config_t *cfg = obs_frontend_get_global_config();
	if (!cfg)
		return;

	struct {
		obs_hotkey_id id;
		const char *key;
	} entries[] = {
		{g_first, "ppt_first_slide"},
		{g_next, "ppt_next_slide"},
		{g_prev, "ppt_prev_slide"},
		{g_last, "ppt_last_slide"},
	};

	for (auto &e : entries) {
		if (e.id == OBS_INVALID_HOTKEY_ID)
			continue;

		obs_data_array_t *arr = obs_hotkey_save(e.id);
		obs_data_t *obj = obs_data_create();
		obs_data_set_array(obj, "bindings", arr);
		const char *json = obs_data_get_json(obj);

		config_set_string(cfg, "Hotkeys", e.key, json ? json : "");

		obs_data_array_release(arr);
		obs_data_release(obj);
	}

	config_save_safe(cfg, "tmp", nullptr);
}

static void LoadBindings()
{
	config_t *cfg = obs_frontend_get_global_config();
	if (!cfg)
		return;

	struct {
		obs_hotkey_id *id;
		const char *key;
	} entries[] = {
		{&g_first, "ppt_first_slide"},
		{&g_next, "ppt_next_slide"},
		{&g_prev, "ppt_prev_slide"},
		{&g_last, "ppt_last_slide"},
	};

	for (auto &e : entries) {
		if (*e.id == OBS_INVALID_HOTKEY_ID)
			continue;

		const char *json = config_get_string(cfg, "Hotkeys", e.key);
		if (!json || !*json)
			continue;

		obs_data_t *obj = obs_data_create_from_json(json);
		if (!obj)
			continue;

		obs_data_array_t *arr = obs_data_get_array(obj, "bindings");
		if (arr) {
			obs_hotkey_load(*e.id, arr);
			obs_data_array_release(arr);
		}
		obs_data_release(obj);
	}
}

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT ||
	    event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP)
		SaveBindings();
}

void Register(ppt::ComBridge *bridge)
{
	g_br = bridge;

	g_first = obs_hotkey_register_frontend(
		"ppt.global.first_slide", obs_module_text("PPT.Hotkey.FirstSlide"),
		[](void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (pressed && g_br)
				g_br->GotoSlide(1);
		},
		nullptr);

	g_next = obs_hotkey_register_frontend(
		"ppt.global.next_slide", obs_module_text("PPT.Hotkey.NextSlide"),
		[](void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (pressed && g_br)
				g_br->NextSlide();
		},
		nullptr);

	g_prev = obs_hotkey_register_frontend(
		"ppt.global.prev_slide", obs_module_text("PPT.Hotkey.PrevSlide"),
		[](void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (pressed && g_br)
				g_br->PrevSlide();
		},
		nullptr);

	g_last = obs_hotkey_register_frontend(
		"ppt.global.last_slide", obs_module_text("PPT.Hotkey.LastSlide"),
		[](void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			if (pressed && g_br)
				g_br->GotoSlide(g_br->GetSlideInfo().totalSlides);
		},
		nullptr);

	LoadBindings();
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}

void Save()
{
	SaveBindings();
}

void Unregister()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	SaveBindings();

	if (g_first != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(g_first);
		g_first = OBS_INVALID_HOTKEY_ID;
	}
	if (g_next != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(g_next);
		g_next = OBS_INVALID_HOTKEY_ID;
	}
	if (g_prev != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(g_prev);
		g_prev = OBS_INVALID_HOTKEY_ID;
	}
	if (g_last != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(g_last);
		g_last = OBS_INVALID_HOTKEY_ID;
	}
	g_br = nullptr;
}

} // namespace hotkeys
