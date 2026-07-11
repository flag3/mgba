/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include "DebuggerController.h"

#include <QList>
#include <QMutex>
#include <QWaitCondition>

#include <mgba/debugger/debugger.h>

struct ARMCore;

namespace QGBA {

class CoreController;

class DebuggerGuiController : public DebuggerController {
Q_OBJECT

public:
	DebuggerGuiController(QObject* parent = nullptr);
	~DebuggerGuiController();

	QList<mBreakpoint> breakpoints();
	QList<mWatchpoint> watchpoints();
	// addressHi > address makes an instruction-word breakpoint (see mBreakpoint); 0 is single-address
	void setBreakpointAt(uint32_t address, uint32_t addressHi = 0);
	// clearAddressHi widens only the clear-overlap test, never the set span
	void toggleBreakpointAt(uint32_t address, uint32_t addressHi, uint32_t clearAddressHi);
	void setWatchpoint(uint32_t minAddress, uint32_t maxAddress, mWatchpointType type);
	void clearPoint(ssize_t id);
	void enablePoint(ssize_t id, bool enable);
	// Pipeline-adjusted; hold the core when calling
	uint32_t executionPC();
	// Identity when unattached or not a GBA
	uint32_t foldAddress(uint32_t address);
	// nullptr when unattached or not a GBA
	ARMCore* armCpu();

signals:
	void debuggerEntered(quint32 pc, const QString& description);
	void debuggerResumed();
	void breakpointsChanged();

public slots:
	void stepInto();
	void stepOver();
	void stepOut();
	void runToAddress(uint32_t address, uint32_t addressHi);
	void continueRun();
	void resumeAndDetach();

protected:
	void attachInternal() override;
	void shutdownInternal() override;

private:
	enum class Command {
		STEP_INTO,
		STEP_OVER,
		STEP_OUT,
		CONTINUE,
	};

	struct GuiDebugger : public mDebuggerModule {
		DebuggerGuiController* self;
	};

	static DebuggerGuiController* controllerFor(mDebuggerModule*);
	static void moduleEntered(struct mDebuggerModule*, enum mDebuggerEntryReason, struct mDebuggerEntryInfo*);
	static void modulePaused(struct mDebuggerModule*, int32_t timeoutMs);
	static void moduleInterrupt(struct mDebuggerModule*);

	// These run on the core thread from within the paused callback
	void processCommands(int32_t timeoutMs);
	void executeCommand(Command);
	void stepIntoInternal();
	void stepOverInternal();
	void stepOutInternal();
	void continueInternal();
	ssize_t addBreakpoint(uint32_t address, uint32_t addressHi, bool temporary);
	void enqueue(Command);
	// Caller must hold m_mutex
	void clearAndResumeLocked();

	GuiDebugger m_module{};

	QMutex m_mutex;
	QWaitCondition m_cond;
	QList<Command> m_commands;
};

}
