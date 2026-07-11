/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#ifdef M_CORE_GBA

#include <mgba/core/core.h>
#include <mgba/internal/arm/decoder.h>

namespace QGBA {

struct ThumbInstruction {
	ARMInstructionInfo info;
	uint16_t opcode = 0;
	uint16_t opcode2 = 0;
	bool isPair = false;
};

struct ArmInstruction {
	ARMInstructionInfo info;
	uint32_t opcode = 0;
};

// Side-effect-free (rawRead only), safe while the core runs
inline ArmInstruction decodeArmAt(mCore* core, uint32_t address) {
	ArmInstruction result;
	result.opcode = core->rawRead32(core, address, -1);
	ARMDecodeARM(result.opcode, &result.info);
	return result;
}

// Side-effect-free (rawRead only), safe while the core runs. Combines a BL/BLX pair when
// possible; the pair's second halfword is not read at or past readLimit
inline ThumbInstruction decodeThumbAt(mCore* core, uint32_t address, uint32_t readLimit) {
	ThumbInstruction result;
	result.opcode = core->rawRead16(core, address, -1);
	ARMDecodeThumb(result.opcode, &result.info);
	if (address + WORD_SIZE_THUMB < readLimit) {
		result.opcode2 = core->rawRead16(core, address + WORD_SIZE_THUMB, -1);
		ARMInstructionInfo info2;
		ARMInstructionInfo combined;
		ARMDecodeThumb(result.opcode2, &info2);
		if (ARMDecodeThumbCombine(&result.info, &info2, &combined)) {
			result.info = combined;
			result.isPair = true;
		}
	}
	return result;
}

}

#endif
