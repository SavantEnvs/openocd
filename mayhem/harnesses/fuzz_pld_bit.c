/*
 * Fuzzes the FPGA bitstream file readers in src/pld: Xilinx .bit (xilinx_bit.c) or Lattice .bit
 * (lattice_bit.c), selected at compile time with -DPLD_XILINX or -DPLD_LATTICE.
 */
#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <helper/log.h>
#include <helper/command.h>
#include <pld/xilinx_bit.h>
#include <pld/lattice_bit.h>

#include "scratch_file.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	debug_level = LOG_LVL_SILENT;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char path[256];
	if (scratch_write(path, sizeof(path), ".bit", data, size) != 0)
		return 0;

#if defined(PLD_XILINX)
	struct xilinx_bit_file bit;
	memset(&bit, 0, sizeof(bit));
	if (xilinx_read_bit_file(&bit, path) == ERROR_OK)
		xilinx_free_bit_file(&bit);
#elif defined(PLD_LATTICE)
	struct lattice_bit_file bit;
	memset(&bit, 0, sizeof(bit));
	if (lattice_read_file(&bit, path, LATTICE_ECP5) == ERROR_OK)
		free(bit.raw_bit.data);
#else
#error "define PLD_XILINX or PLD_LATTICE"
#endif

	unlink(path);
	return 0;
}
