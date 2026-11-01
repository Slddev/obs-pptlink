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

#include "test.h"
#include "sources/presenter-layout.h"

using sources::ComputePresenterLayout;
using sources::LayoutPreset;
using sources::LayoutRect;
using sources::PresenterLayout;

namespace {

const LayoutPreset kPresets[] = {LayoutPreset::Classic, LayoutPreset::TopBottom, LayoutPreset::NotesFocused,
				 LayoutPreset::NextDominant, LayoutPreset::Custom};

const struct {
	uint32_t width;
	uint32_t height;
} kSizes[] = {{1920, 1080}, {1280, 720}, {1921, 1081}, {3840, 2160}, {800, 600}, {1080, 1920}};

bool Inside(const LayoutRect &r, int w, int h)
{
	return r.x >= 0 && r.y >= 0 && r.cx >= 0 && r.cy >= 0 && r.x + r.cx <= w && r.y + r.cy <= h;
}

bool Overlap(const LayoutRect &a, const LayoutRect &b)
{
	return a.x < b.x + b.cx && b.x < a.x + a.cx && a.y < b.y + b.cy && b.y < a.y + a.cy;
}

long long Area(const LayoutRect &r)
{
	return static_cast<long long>(r.cx) * r.cy;
}

} // namespace

TEST_CASE(layout_controls_bar_spans_bottom)
{
	for (LayoutPreset preset : kPresets) {
		for (auto size : kSizes) {
			PresenterLayout l = ComputePresenterLayout(preset, size.width, size.height, 0.65f, 0.85f);
			int w = static_cast<int>(size.width);
			int h = static_cast<int>(size.height);
			CHECK(l.controls.x == 0);
			CHECK(l.controls.cx == w);
			CHECK(l.controls.y + l.controls.cy == h);
			CHECK(l.controls.cy == 60);
		}
	}
}

TEST_CASE(layout_panels_tile_area_above_controls)
{
	for (LayoutPreset preset : kPresets) {
		for (auto size : kSizes) {
			PresenterLayout l = ComputePresenterLayout(preset, size.width, size.height, 0.65f, 0.85f);
			int w = static_cast<int>(size.width);
			int h = l.controls.y;
			CHECK(Inside(l.current, w, h));
			CHECK(Inside(l.next, w, h));
			CHECK(Inside(l.notes, w, h));
			CHECK(!Overlap(l.current, l.next));
			CHECK(!Overlap(l.current, l.notes));
			CHECK(!Overlap(l.next, l.notes));
			CHECK(Area(l.current) + Area(l.next) + Area(l.notes) == static_cast<long long>(w) * h);
		}
	}
}

TEST_CASE(layout_presets_emphasise_their_panel)
{
	PresenterLayout classic = ComputePresenterLayout(LayoutPreset::Classic, 1920, 1080, 0, 0);
	PresenterLayout nextDominant = ComputePresenterLayout(LayoutPreset::NextDominant, 1920, 1080, 0, 0);
	PresenterLayout notesFocused = ComputePresenterLayout(LayoutPreset::NotesFocused, 1920, 1080, 0, 0);
	PresenterLayout topBottom = ComputePresenterLayout(LayoutPreset::TopBottom, 1920, 1080, 0, 0);

	CHECK(classic.current.cx > classic.next.cx);
	CHECK(nextDominant.next.cx > nextDominant.current.cx);
	CHECK(notesFocused.notes.cy > classic.notes.cy);
	CHECK(topBottom.current.cx == 1920);
	CHECK(topBottom.next.y > topBottom.current.y);
}

TEST_CASE(layout_custom_follows_splits)
{
	PresenterLayout a = ComputePresenterLayout(LayoutPreset::Custom, 1000, 1060, 0.5f, 0.5f);
	CHECK(a.current.cx == 500);
	CHECK(a.next.cx == 500);
	CHECK(a.notes.cy == 500);

	PresenterLayout b = ComputePresenterLayout(LayoutPreset::Custom, 1000, 1060, 0.25f, 0.75f);
	CHECK(b.current.cx == 250);
	CHECK(b.next.x == 250);
	CHECK(b.notes.cy == 250);
}
