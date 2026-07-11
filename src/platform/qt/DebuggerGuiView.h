/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QWidget>
#include <QVector>

#include <mgba/core/core.h>

#include <functional>
#include <memory>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolBar;

namespace QGBA {

class BreakpointPanel;
class CoreController;
class DebuggerGuiController;
class DisassemblyView;
class MemoryModel;
class RegisterPanel;

class DebuggerGuiView : public QWidget {
Q_OBJECT

public:
	DebuggerGuiView(DebuggerGuiController* debugger, std::shared_ptr<CoreController> controller,
	                QWidget* parent = nullptr);

protected:
	void closeEvent(QCloseEvent*) override;

private slots:
	void debuggerEntered(quint32 pc, const QString& description);
	void debuggerResumed();
	void toggleRun();
	void gotoAddress();
	void frameLevelPaused();
	void refreshLiveViews();
	void stateChanged();

private:
	QWidget* createStackPanel();
	QWidget* createDisassemblyPanel();
	QWidget* createMemoryPanel();
	QToolBar* createToolBar();
	void populateRegionSelector(QComboBox* box, uint32_t anyFlags);
	const mCoreMemoryBlock* memoryBlockForItem(QComboBox* box, int index);
	int findRegionItem(QComboBox* box, const std::function<bool(const mCoreMemoryBlock&)>& match);
	void setMemoryRegion(int index);
	void jumpMemoryToAddress(uint32_t address);
	void setDisassemblyRegion(int index);
	void disassemblyRegionChanged(uint32_t base);
	void refreshAll(quint32 pc);
	void updateStack();
	void setPausedState(bool paused);
	void setPC(uint32_t address);
	void runToAddress(uint32_t address);
	void toggleBreakpointAt(uint32_t address);
	quint32 executionPC();

	DebuggerGuiController* m_debugger;
	std::shared_ptr<CoreController> m_controller;
	bool m_paused = false;
	QVector<mCoreMemoryBlock> m_memoryBlocks;

	DisassemblyView* m_disassembly = nullptr;
	QComboBox* m_disasmRegions = nullptr;
	MemoryModel* m_memory = nullptr;
	QComboBox* m_memoryRegions = nullptr;
	QTableWidget* m_stack = nullptr;
	QLabel* m_status = nullptr;
	QLineEdit* m_gotoEdit = nullptr;
	RegisterPanel* m_registerPanel = nullptr;
	BreakpointPanel* m_breakpointPanel = nullptr;

	QAction* m_runAction = nullptr;
	QAction* m_stepIntoAction = nullptr;
	QAction* m_stepOverAction = nullptr;
	QAction* m_stepOutAction = nullptr;
	QAction* m_runToCursorAction = nullptr;
};

}
