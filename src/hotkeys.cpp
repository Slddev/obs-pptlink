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

static ppt::ComBridge *g_br = nullptr;

struct Hotkey {
	const char *name;
	const char *textKey;
	const char *configKey;
	void (*action)(ppt::ComBridge *bridge);
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

static Hotkey g_hotkeys[] = {
	{"ppt.global.first_slide", "PPT.Hotkey.FirstSlide", "ppt_first_slide",
	 [](ppt::ComBridge *br) {
		 br->FirstSlide();
	 }},
	{"ppt.global.next_slide", "PPT.Hotkey.NextSlide", "ppt_next_slide",
	 [](ppt::ComBridge *br) {
		 br->NextSlide();
	 }},
	{"ppt.global.prev_slide", "PPT.Hotkey.PrevSlide", "ppt_prev_slide",
	 [](ppt::ComBridge *br) {
		 br->PrevSlide();
	 }},
	{"ppt.global.last_slide", "PPT.Hotkey.LastSlide", "ppt_last_slide",
	 [](ppt::ComBridge *br) {
		 br->LastSlide();
	 }},
	{"ppt.global.black_screen", "PPT.Hotkey.BlackScreen", "ppt_black_screen",
	 [](ppt::ComBridge *br) {
		 br->ToggleBlackScreen();
	 }},
	{"ppt.global.white_screen", "PPT.Hotkey.WhiteScreen", "ppt_white_screen",
	 [](ppt::ComBridge *br) {
		 br->ToggleWhiteScreen();
	 }},
};

static void SaveBindings()
{
	config_t *cfg = obs_frontend_get_user_config();
	if (!cfg)
		return;

	for (const Hotkey &hk : g_hotkeys) {
		if (hk.id == OBS_INVALID_HOTKEY_ID)
			continue;

		obs_data_array_t *arr = obs_hotkey_save(hk.id);
		obs_data_t *obj = obs_data_create();
		obs_data_set_array(obj, "bindings", arr);
		const char *json = obs_data_get_json(obj);

		config_set_string(cfg, "Hotkeys", hk.configKey, json ? json : "");

		obs_data_array_release(arr);
		obs_data_release(obj);
	}

	config_save_safe(cfg, "tmp", nullptr);
}

static void LoadBindings()
{
	config_t *cfg = obs_frontend_get_user_config();
	if (!cfg)
		return;

	for (const Hotkey &hk : g_hotkeys) {
		if (hk.id == OBS_INVALID_HOTKEY_ID)
			continue;

		const char *json = config_get_string(cfg, "Hotkeys", hk.configKey);
		if (!json || !*json)
			continue;

		obs_data_t *obj = obs_data_create_from_json(json);
		if (!obj)
			continue;

		obs_data_array_t *arr = obs_data_get_array(obj, "bindings");
		if (arr) {
			obs_hotkey_load(hk.id, arr);
			obs_data_array_release(arr);
		}
		obs_data_release(obj);
	}
}

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP)
		SaveBindings();
}

static void OnHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_br)
		static_cast<Hotkey *>(data)->action(g_br);
}

void Register(ppt::ComBridge *bridge)
{
	g_br = bridge;

	for (Hotkey &hk : g_hotkeys)
		hk.id = obs_hotkey_register_frontend(hk.name, obs_module_text(hk.textKey), OnHotkey, &hk);

	LoadBindings();
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}

void Unregister()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	SaveBindings();

	for (Hotkey &hk : g_hotkeys) {
		if (hk.id != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(hk.id);
			hk.id = OBS_INVALID_HOTKEY_ID;
		}
	}
	g_br = nullptr;
}

} // namespace hotkeys
