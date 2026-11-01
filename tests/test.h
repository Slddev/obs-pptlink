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

#pragma once

#include <cstdio>
#include <vector>

namespace test {

struct Case {
	const char *name;
	void (*fn)();
};

std::vector<Case> &Registry();
extern int failures;

struct Register {
	Register(const char *name, void (*fn)()) { Registry().push_back({name, fn}); }
};

} // namespace test

#define TEST_CASE(name)                                \
	static void name();                            \
	static test::Register name##_reg(#name, name); \
	static void name()

#define CHECK(cond)                                                                            \
	do {                                                                                   \
		if (!(cond)) {                                                                 \
			std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			++test::failures;                                                      \
		}                                                                              \
	} while (0)
