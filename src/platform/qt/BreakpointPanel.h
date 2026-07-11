/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QList>
#include <QSet>
#include <QWidget>

#include <mgba/debugger/debugger.h>

class QTreeWidget;
class QTreeWidgetItem;

namespace QGBA {

class DebuggerGuiController;

class BreakpointPanel : public QWidget {
Q_OBJECT

public:
	BreakpointPanel(DebuggerGuiController* debugger, QWidget* parent = nullptr);

public slots:
	void updateBreakpointList();

signals:
	// Folded span ends of every non-temporary breakpoint, for gutter markers
	void breakpointAddressesChanged(const QSet<uint32_t>& addresses);

private slots:
	void addBreakpointClicked();
	void addWatchpointClicked();
	void removePointClicked();
	void pointItemChanged(QTreeWidgetItem*, int column);

private:
	struct WatchpointTypeName {
		mWatchpointType type;
		QString name;
	};
	static QList<WatchpointTypeName> watchpointTypeNames();
	bool promptAddress(const QString& title, uint32_t* address);

	DebuggerGuiController* m_debugger;
	QTreeWidget* m_points = nullptr;
};

}
