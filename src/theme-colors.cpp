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

#include "theme-colors.h"
#include <obs-frontend-api.h>
#include <QApplication>
#include <QPalette>
#include <mutex>

namespace theme {

static std::mutex g_mutex;
static Colors g_colors;

static void Refresh()
{
	QPalette pal = QApplication::palette();
	Colors c;
	c.bg = pal.color(QPalette::Window).rgb();
	c.panel = pal.color(QPalette::Base).rgb();
	c.text = pal.color(QPalette::WindowText).rgb();
	c.accent = pal.color(QPalette::Highlight).rgb();

	std::lock_guard<std::mutex> lk(g_mutex);
	g_colors = c;
}

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_THEME_CHANGED || event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		Refresh();
}

void Init()
{
	Refresh();
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}

void Shutdown()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
}

Colors Get()
{
	std::lock_guard<std::mutex> lk(g_mutex);
	return g_colors;
}

} // namespace theme
