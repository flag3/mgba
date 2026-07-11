/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QGroupBox>
#include <QMap>

#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace QGBA {

class CoreController;
class DebuggerGuiController;

class RegisterPanel : public QGroupBox {
Q_OBJECT

public:
	RegisterPanel(DebuggerGuiController* debugger, std::shared_ptr<CoreController> controller,
	              QWidget* parent = nullptr);

	void updateRegisters();
	void setPaused(bool paused);
	void writePC(uint32_t value);

signals:
	// true when the PC or execution mode changed and the owner should re-anchor
	void registerEdited(bool reanchor);

private slots:
	void editFinished(const QString& name);
	void flagsEdited();

private:
	void writeRegister(const char* name, uint32_t value);

	DebuggerGuiController* m_debugger;
	std::shared_ptr<CoreController> m_controller;
	bool m_paused = false;

	QLabel* m_cpuModeLabel = nullptr;
	QMap<QString, QLineEdit*> m_registers;
	// Same order as the _psrFlags table
	QList<QCheckBox*> m_flags;
};

}
