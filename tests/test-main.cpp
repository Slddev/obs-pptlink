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

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace test {

int failures = 0;

std::vector<Case> &Registry()
{
	static std::vector<Case> cases;
	return cases;
}

} // namespace test

int main()
{
	ULONG_PTR token = 0;
	Gdiplus::GdiplusStartupInput si;
	Gdiplus::GdiplusStartup(&token, &si, nullptr);

	for (const test::Case &c : test::Registry()) {
		int before = test::failures;
		c.fn();
		std::printf("%s %s\n", test::failures == before ? "PASS" : "FAIL", c.name);
	}

	Gdiplus::GdiplusShutdown(token);

	std::printf("%zu tests, %d failed checks\n", test::Registry().size(), test::failures);
	return test::failures ? 1 : 0;
}
