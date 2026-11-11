/*
 * Fuzzes OpenOCD's text/binary conversion helpers (src/helper/binarybuffer.c, base64.c).
 * Input: byte 0 = bit width for str_to_buf() (1..256); the remaining bytes are the string / hex / base64 payload.
 */
#include "config.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <helper/log.h>
#include <helper/command.h>
#include <helper/binarybuffer.h>
#include <helper/base64.h>

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	debug_level = LOG_LVL_SILENT;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/* The first byte is the str_to_buf() bit width parameter. */
	if (size < 1)
		return -1;

	unsigned int bits = (unsigned int)data[0] + 1;
	size_t len = size - 1;

	char *str = malloc(len + 1);
	if (!str)
		return 0;
	memcpy(str, data + 1, len);
	str[len] = '\0';

	uint8_t num[32];
	memset(num, 0, sizeof(num));
	if (str_to_buf(str, num, bits) == ERROR_OK) {
		char *hex = buf_to_hex_str(num, bits);
		free(hex);
	}

	size_t nbytes = len / 2;
	uint8_t *bin = malloc(nbytes + 1);
	char *hexout = malloc(nbytes * 2 + 1);
	if (bin && hexout) {
		size_t n = unhexify(bin, str, nbytes);
		hexify(hexout, bin, n, nbytes * 2 + 1);
	}
	free(bin);
	free(hexout);

	size_t out_len = 0;
	unsigned char *dec = base64_decode((const unsigned char *)str, len, &out_len);
	if (dec) {
		unsigned char *enc = base64_encode(dec, out_len, &out_len);
		free(enc);
		free(dec);
	}

	free(str);
	return 0;
}
