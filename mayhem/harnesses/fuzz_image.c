/*
 * Fuzzes OpenOCD's firmware image loader (src/target/image.c). Compiled once per format with
 * -DIMAGE_TYPE="ihex" / "elf" / "s19": the input file is opened as that image type, every section is
 * read back and checksummed, then the image is closed.
 */
#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <helper/log.h>
#include <helper/command.h>
#include <target/image.h>

#include "scratch_file.h"

#ifndef IMAGE_TYPE
#error "IMAGE_TYPE must be defined (ihex, elf or s19)"
#endif

#define CHUNK 4096
/* Sections can claim up to 4 GiB; read at most the first MAX_CHUNKS chunks of each. */
#define MAX_CHUNKS 256

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
	if (scratch_write(path, sizeof(path), "", data, size) != 0)
		return 0;

	struct image image;
	memset(&image, 0, sizeof(image));

	if (image_open(&image, path, IMAGE_TYPE) == ERROR_OK) {
		static uint8_t buf[CHUNK];
		uint32_t checksum = 0;
		for (unsigned int s = 0; s < image.num_sections; s++) {
			uint32_t sec_size = image.sections[s].size;
			for (uint64_t off = 0; off < sec_size && off < (uint64_t)MAX_CHUNKS * CHUNK; off += CHUNK) {
				uint32_t want = sec_size - off > CHUNK ? CHUNK : (uint32_t)(sec_size - off);
				size_t got = 0;
				memset(buf, 0, sizeof(buf));
				if (image_read_section(&image, s, off, want, buf, &got) != ERROR_OK)
					break;
				image_calculate_checksum(buf, got, &checksum);
				if (got < want)
					break;
			}
		}
		image_close(&image);
	}

	unlink(path);
	return 0;
}
