/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "BreakpointPanel.h"
#include "moc_BreakpointPanel.cpp"

#include "DebuggerGuiController.h"
#include "GBAApp.h"
#include "utils.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace QGBA;

BreakpointPanel::BreakpointPanel(DebuggerGuiController* debugger, QWidget* parent)
	: QWidget(parent)
	, m_debugger(debugger)
{
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(2, 2, 2, 2);

	m_points = new QTreeWidget;
	m_points->setColumnCount(4);
	m_points->setHeaderLabels({tr("ID"), tr("Type"), tr("Address"), tr("Extra")});
	m_points->setRootIsDecorated(false);
	m_points->setFont(GBAApp::app()->monospaceFont());
	connect(m_points, &QTreeWidget::itemChanged, this, &BreakpointPanel::pointItemChanged);
	layout->addWidget(m_points, 1);

	QVBoxLayout* buttons = new QVBoxLayout;
	QPushButton* addBreak = new QPushButton(tr("Add breakpoint..."));
	connect(addBreak, &QPushButton::clicked, this, &BreakpointPanel::addBreakpointClicked);
	buttons->addWidget(addBreak);
	QPushButton* addWatch = new QPushButton(tr("Add watchpoint..."));
	connect(addWatch, &QPushButton::clicked, this, &BreakpointPanel::addWatchpointClicked);
	buttons->addWidget(addWatch);
	QPushButton* remove = new QPushButton(tr("Remove"));
	connect(remove, &QPushButton::clicked, this, &BreakpointPanel::removePointClicked);
	buttons->addWidget(remove);
	buttons->addStretch();
	layout->addLayout(buttons);
}

QList<BreakpointPanel::WatchpointTypeName> BreakpointPanel::watchpointTypeNames() {
	return {
		{WATCHPOINT_WRITE, tr("Write")},
		{WATCHPOINT_READ, tr("Read")},
		{WATCHPOINT_RW, tr("Read/Write")},
		{WATCHPOINT_CHANGE, tr("Change")},
		{WATCHPOINT_WRITE_CHANGE, tr("Write (changed)")},
	};
}

bool BreakpointPanel::promptAddress(const QString& title, uint32_t* address) {
	bool ok = false;
	QString text = QInputDialog::getText(this, title, tr("Address (hex):"), QLineEdit::Normal, QString(), &ok);
	return ok && parseHex32(text, address);
}

void BreakpointPanel::addBreakpointClicked() {
	uint32_t address;
	if (promptAddress(tr("Add breakpoint"), &address)) {
		m_debugger->setBreakpointAt(address);
	}
}

void BreakpointPanel::addWatchpointClicked() {
	uint32_t address;
	if (!promptAddress(tr("Add watchpoint"), &address)) {
		return;
	}
	const QList<WatchpointTypeName> typeNames = watchpointTypeNames();
	QStringList types;
	for (const WatchpointTypeName& typeName : typeNames) {
		types.append(typeName.name);
	}
	bool ok = false;
	QString type = QInputDialog::getItem(this, tr("Add watchpoint"), tr("Type:"), types, 0, false, &ok);
	if (!ok) {
		return;
	}
	m_debugger->setWatchpoint(address, address + 1, typeNames[types.indexOf(type)].type);
}

void BreakpointPanel::removePointClicked() {
	QTreeWidgetItem* item = m_points->currentItem();
	if (!item) {
		return;
	}
	ssize_t id = item->data(0, Qt::UserRole).toLongLong();
	m_debugger->clearPoint(id);
}

void BreakpointPanel::pointItemChanged(QTreeWidgetItem* item, int column) {
	if (column != 0) {
		return;
	}
	ssize_t id = item->data(0, Qt::UserRole).toLongLong();
	m_debugger->enablePoint(id, item->checkState(0) == Qt::Checked);
}

void BreakpointPanel::updateBreakpointList() {
	QSignalBlocker blocker(m_points);
	m_points->clear();
	auto makePointItem = [this](ssize_t id, bool disabled) {
		QTreeWidgetItem* item = new QTreeWidgetItem;
		item->setText(0, QString::number(id));
		item->setData(0, Qt::UserRole, static_cast<qlonglong>(id));
		item->setCheckState(0, disabled ? Qt::Unchecked : Qt::Checked);
		m_points->addTopLevelItem(item);
		return item;
	};
	QSet<uint32_t> breakpointAddresses;
	const QList<mBreakpoint> breakpoints = m_debugger->breakpoints();
	for (const mBreakpoint& breakpoint : breakpoints) {
		if (breakpoint.isTemporary) {
			continue;
		}
		// Both folded span ends get gutter markers, covering mirrors and the second halfword
		breakpointAddresses.insert(m_debugger->foldAddress(breakpoint.address));
		breakpointAddresses.insert(m_debugger->foldAddress(breakpoint.addressHi));
		QTreeWidgetItem* item = makePointItem(breakpoint.id, breakpoint.disabled);
		item->setText(1, tr("Break"));
		item->setText(2, formatHex32(breakpoint.address));
	}
	const QList<WatchpointTypeName> typeNames = watchpointTypeNames();
	const QList<mWatchpoint> watchpoints = m_debugger->watchpoints();
	for (const mWatchpoint& watchpoint : watchpoints) {
		QTreeWidgetItem* item = makePointItem(watchpoint.id, watchpoint.disabled);
		QString type = tr("Watch");
		for (const WatchpointTypeName& typeName : typeNames) {
			if (typeName.type == watchpoint.type) {
				type = tr("Watch: %1").arg(typeName.name);
				break;
			}
		}
		item->setText(1, type);
		item->setText(2, formatHex32(watchpoint.minAddress));
		if (watchpoint.maxAddress > watchpoint.minAddress + 1) {
			item->setText(3, tr("to %1").arg(formatHex32(watchpoint.maxAddress)));
		}
	}
	emit breakpointAddressesChanged(breakpointAddresses);
}
