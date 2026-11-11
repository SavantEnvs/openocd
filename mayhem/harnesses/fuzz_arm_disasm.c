/*
 * Fuzzes the ARM/Thumb instruction decoder (src/target/arm_disassembler.c).
 * Input: 4-byte opcode (little endian) + 4-byte address; the low halfword is also decoded as Thumb.
 */
#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <helper/log.h>
#include <helper/command.h>
#include <target/arm_disassembler.h>

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	debug_level = LOG_LVL_SILENT;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/* arm_evaluate_opcode() takes a uint32_t opcode and a uint32_t address. */
	if (size < 8)
		return -1;

	uint32_t opcode = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
	uint32_t address = data[4] | (data[5] << 8) | (data[6] << 16) | ((uint32_t)data[7] << 24);

	struct arm_instruction insn;
	memset(&insn, 0, sizeof(insn));
	if (arm_evaluate_opcode(opcode, address, &insn) == ERROR_OK)
		arm_access_size(&insn);

	memset(&insn, 0, sizeof(insn));
	thumb_evaluate_opcode((uint16_t)opcode, address, &insn);
	return 0;
}
