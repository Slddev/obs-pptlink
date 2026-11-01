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

#include <obs-module.h>
#include <plugin-support.h>

#include "com-bridge/ppt-com-bridge.h"
#include "sources/source-slide.h"
#include "hotkeys.h"
#include "wgc-capture/wgc-capture.h"
#include "wgc-capture/shared-capture.h"
#include <gdiplus.h>
#include "docks/dock-next-slide.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

ppt::ComBridge *g_bridge = nullptr;
wgc::SharedCapture *g_capture = nullptr;

static ULONG_PTR g_gdiplusToken = 0;

namespace sources {
void RegisterPresenterSource();
}

extern "C" {

MODULE_EXPORT const char *obs_module_description()
{
	return "PowerPoint live capture plugin (WGC + COM)";
}

MODULE_EXPORT bool obs_module_load()
{
	obs_log(LOG_INFO, "Loading PPTLink v%s", PLUGIN_VERSION);

	// Check WGC availability before anything else
	if (!wgc::IsWGCSupported()) {
		obs_log(LOG_ERROR, "Windows Graphics Capture is not supported "
				   "on this system (requires Windows 10 1903 / build 18362 or later). "
				   "Plugin will not load.");
		return false;
	}

	// Start the global COM bridge (STA thread)
	g_bridge = new ppt::ComBridge();

	g_bridge->OnConnectionChanged = [](bool connected) {
		obs_log(LOG_INFO, "PowerPoint %s", connected ? "connected" : "disconnected");
	};

	if (!g_bridge->Start()) {
		obs_log(LOG_ERROR, "Failed to start COM bridge");
		delete g_bridge;
		g_bridge = nullptr;
		return false;
	}

	g_capture = new wgc::SharedCapture(g_bridge);
	g_capture->Start();

	// Register OBS source types
	sources::RegisterSlideSource();
	sources::RegisterPresenterSource();

	// Register OBS docks
	dock::RegisterDocks(g_bridge);

	// Register plugin-level hotkeys
	hotkeys::Register(g_bridge);

	Gdiplus::GdiplusStartupInput si;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);

	obs_log(LOG_INFO, "Plugin loaded successfully");
	return true;
}

MODULE_EXPORT void obs_module_unload()
{
	obs_log(LOG_INFO, "Unloading PPTLink");

	hotkeys::Unregister();

	if (g_gdiplusToken) {
		Gdiplus::GdiplusShutdown(g_gdiplusToken);
		g_gdiplusToken = 0;
	}

	if (g_capture) {
		g_capture->Stop();
		delete g_capture;
		g_capture = nullptr;
	}

	if (g_bridge) {
		g_bridge->Stop();
		delete g_bridge;
		g_bridge = nullptr;
	}

	obs_log(LOG_INFO, "Plugin unloaded");
}

} // extern "C"