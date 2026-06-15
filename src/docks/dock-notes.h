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

#include <obs-module.h>
#include "../com-bridge/ppt-com-bridge.h"

#include <QWidget>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace dock {

class NotesDock : public QWidget {
	Q_OBJECT
public:
	explicit NotesDock(ppt::ComBridge *bridge, QWidget *parent = nullptr);
	~NotesDock();

	void updateFromInfo(const ppt::SlideInfo &info, bool connected);

private slots:
	void onPrevClicked();
	void onNextClicked();

private:
	void buildUi();

	ppt::ComBridge *m_bridge = nullptr;
	QTextEdit *m_notes = nullptr;
	QPushButton *m_btnPrev = nullptr;
	QPushButton *m_btnNext = nullptr;
};

} // namespace dock