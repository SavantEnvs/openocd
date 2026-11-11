/*
 * Known-answer tests for OpenOCD's parsers, built with the project's normal flags and linked against the
 * normal (unsanitized) static libraries. Each check prints "KAT_PASS <name>" or "KAT_FAIL <name>"; the
 * runner (mayhem/test.sh) requires the exact set of PASS lines, so a program that merely exits 0 fails.
 */
#include "config.h"
#include <stdint.h>
#include <helper/log.h>
#include <helper/command.h>
#include <helper/binarybuffer.h>
#include <helper/base64.h>
#include <target/image.h>
#include <target/arm_disassembler.h>
#include <pld/xilinx_bit.h>
#include <pld/lattice_bit.h>
#include <unistd.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("%s %s\n", ok ? "KAT_PASS" : "KAT_FAIL", name);
	if (!ok)
		failures++;
}

static char dir[256];

static const char *write_file(const char *name, const void *data, size_t len)
{
	static char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror("fopen");
		exit(2);
	}
	fwrite(data, 1, len, f);
	fclose(f);
	return path;
}

static void le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void le16(uint8_t *p, uint16_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void test_ihex(void)
{
	static const char text[] =
		":04000000DEADBEEFC4\n"
		":020000040800F2\n"
		":040000001122334452\n"
		":00000001FF\n";
	const char *path = write_file("k.hex", text, sizeof(text) - 1);
	struct image image;
	memset(&image, 0, sizeof(image));
	int rc = image_open(&image, path, "ihex");
	check("ihex_open", rc == ERROR_OK);
	if (rc != ERROR_OK)
		return;
	check("ihex_sections", image.num_sections == 2);
	check("ihex_section0_geometry", image.sections[0].base_address == 0 && image.sections[0].size == 4);
	check("ihex_section1_geometry", image.sections[1].base_address == 0x08000000 && image.sections[1].size == 4);
	uint8_t buf[4] = {0};
	size_t got = 0;
	rc = image_read_section(&image, 0, 0, 4, buf, &got);
	check("ihex_section0_data", rc == ERROR_OK && got == 4 && !memcmp(buf, "\xde\xad\xbe\xef", 4));
	rc = image_read_section(&image, 1, 0, 4, buf, &got);
	check("ihex_section1_data", rc == ERROR_OK && got == 4 && !memcmp(buf, "\x11\x22\x33\x44", 4));
	uint32_t sum = 0;
	/* CRC-32/MPEG-2 check value for "123456789" */
	check("image_checksum", image_calculate_checksum((const uint8_t *)"123456789", 9, &sum) == ERROR_OK &&
		sum == 0x0376e6e7u);
	image_close(&image);
}

static void test_srec(void)
{
	static const char text[] =
		"S1070000DEADBEEFC0\n"
		"S9030000FC\n";
	const char *path = write_file("k.s19", text, sizeof(text) - 1);
	struct image image;
	memset(&image, 0, sizeof(image));
	int rc = image_open(&image, path, "s19");
	check("srec_open", rc == ERROR_OK);
	if (rc != ERROR_OK)
		return;
	check("srec_geometry", image.num_sections == 1 && image.sections[0].size == 4 &&
		image.sections[0].base_address == 0);
	uint8_t buf[4] = {0};
	size_t got = 0;
	rc = image_read_section(&image, 0, 0, 4, buf, &got);
	check("srec_data", rc == ERROR_OK && got == 4 && !memcmp(buf, "\xde\xad\xbe\xef", 4));
	image_close(&image);
}

static void test_elf(void)
{
	uint8_t elf[52 + 32 + 4];
	memset(elf, 0, sizeof(elf));
	memcpy(elf, "\x7f" "ELF", 4);
	elf[4] = 1;	/* ELFCLASS32 */
	elf[5] = 1;	/* little endian */
	elf[6] = 1;
	le16(elf + 16, 2);	/* ET_EXEC */
	le16(elf + 18, 40);	/* EM_ARM */
	le32(elf + 20, 1);
	le32(elf + 24, 0x20000101);	/* e_entry */
	le32(elf + 28, 52);	/* e_phoff */
	le16(elf + 40, 52);	/* e_ehsize */
	le16(elf + 42, 32);	/* e_phentsize */
	le16(elf + 44, 1);	/* e_phnum */
	uint8_t *ph = elf + 52;
	le32(ph + 0, 1);	/* PT_LOAD */
	le32(ph + 4, 84);	/* p_offset */
	le32(ph + 8, 0x20000100);	/* p_vaddr */
	le32(ph + 12, 0x20000100);	/* p_paddr */
	le32(ph + 16, 4);	/* p_filesz */
	le32(ph + 20, 4);	/* p_memsz */
	le32(ph + 24, 5);	/* PF_R | PF_X */
	memcpy(elf + 84, "\x01\x02\x03\x04", 4);
	const char *path = write_file("k.elf", elf, sizeof(elf));
	struct image image;
	memset(&image, 0, sizeof(image));
	int rc = image_open(&image, path, "elf");
	check("elf_open", rc == ERROR_OK);
	if (rc != ERROR_OK)
		return;
	check("elf_geometry", image.num_sections == 1 && image.sections[0].size == 4 &&
		image.sections[0].base_address == 0x20000100);
	check("elf_entry", image.start_address_set && image.start_address == 0x20000101);
	uint8_t buf[4] = {0};
	size_t got = 0;
	rc = image_read_section(&image, 0, 0, 4, buf, &got);
	check("elf_data", rc == ERROR_OK && got == 4 && !memcmp(buf, "\x01\x02\x03\x04", 4));
	image_close(&image);

	elf[0] = 'X';
	path = write_file("bad.elf", elf, sizeof(elf));
	memset(&image, 0, sizeof(image));
	check("elf_bad_magic_rejected", image_open(&image, path, "elf") != ERROR_OK);
}

static size_t put_section(uint8_t *p, char tag, const char *s, size_t slen, int len_size)
{
	p[0] = tag;
	if (len_size == 2)
		p[1] = slen >> 8, p[2] = slen;
	else
		p[1] = slen >> 24, p[2] = slen >> 16, p[3] = slen >> 8, p[4] = slen;
	memcpy(p + 1 + len_size, s, slen);
	return 1 + len_size + slen;
}

static void test_xilinx(void)
{
	uint8_t f[256];
	size_t n = 13;
	memset(f, 0, sizeof(f));
	n += put_section(f + n, 'a', "top.ncd\0", 8, 2);
	n += put_section(f + n, 'b', "7a35tcsg324\0", 12, 2);
	n += put_section(f + n, 'c', "2021/03/04\0", 11, 2);
	n += put_section(f + n, 'd', "05:06:07\0", 9, 2);
	n += put_section(f + n, 'e', "\xaa\x99\x55\x66\x01\x02", 6, 4);
	const char *path = write_file("k.bit", f, n);
	struct xilinx_bit_file bit;
	int rc = xilinx_read_bit_file(&bit, path);
	check("xilinx_open", rc == ERROR_OK);
	if (rc == ERROR_OK) {
		check("xilinx_fields", !strcmp((char *)bit.source_file, "top.ncd") &&
			!strcmp((char *)bit.part_name, "7a35tcsg324") &&
			!strcmp((char *)bit.date, "2021/03/04") && !strcmp((char *)bit.time, "05:06:07"));
		check("xilinx_payload", bit.length == 6 && !memcmp(bit.data, "\xaa\x99\x55\x66\x01\x02", 6));
		xilinx_free_bit_file(&bit);
	}
	f[13] = 'z';
	path = write_file("bad.bit", f, n);
	check("xilinx_bad_tag_rejected", xilinx_read_bit_file(&bit, path) != ERROR_OK);
}

static void test_lattice(void)
{
	static const uint8_t f[] = {
		0xff, 0x00, 'P', 'a', 'r', 't', ':', ' ', 'L', 'F', 'E', '5', 'U', '-', '2', '5', 'F', 0x00,
		0xff, 0xff, 0xbd, 0xb3, 0xe2, 0xff, 0xff, 0xff, 0x41, 0x11, 0x10, 0x43, 0x01, 0x02, 0x03, 0x04,
	};
	const char *path = write_file("k5.bit", f, sizeof(f));
	struct lattice_bit_file bit;
	memset(&bit, 0, sizeof(bit));
	int rc = lattice_read_file(&bit, path, LATTICE_ECP5);
	check("lattice_open", rc == ERROR_OK);
	if (rc == ERROR_OK) {
		check("lattice_part", bit.part && !strcmp(bit.part, "LFE5U-25F"));
		check("lattice_idcode", bit.has_id && bit.idcode == 0x41111043u);
		check("lattice_offset", bit.offset == 18);
		/* bytes from the offset on are bit-reversed: 0xb3 -> 0xcd, 0x01 -> 0x80 */
		check("lattice_bitflip", bit.raw_bit.data[21] == 0xcd && bit.raw_bit.data[30] == 0x80);
		free(bit.raw_bit.data);
	}
	path = write_file("notbit.bin", f, sizeof(f));
	check("lattice_bad_suffix_rejected", lattice_read_file(&bit, path, LATTICE_ECP5) != ERROR_OK);
}

static void test_arm(void)
{
	struct arm_instruction insn;
	memset(&insn, 0, sizeof(insn));
	int rc = arm_evaluate_opcode(0xe1a00000, 0x1000, &insn);
	check("arm_mov_nop", rc == ERROR_OK && insn.type == ARM_MOV && insn.instruction_size == 4 &&
		strstr(insn.text, "\tNOP"));
	memset(&insn, 0, sizeof(insn));
	rc = arm_evaluate_opcode(0xe3a0102a, 0, &insn);
	check("arm_mov_imm", rc == ERROR_OK && insn.type == ARM_MOV && insn.info.data_proc.rd == 1 &&
		insn.info.data_proc.shifter_operand.immediate.immediate == 42);
	memset(&insn, 0, sizeof(insn));
	rc = arm_evaluate_opcode(0xea000002, 0x1000, &insn);
	check("arm_branch_target", rc == ERROR_OK && insn.type == ARM_B &&
		insn.info.b_bl_bx_blx.target_address == 0x1000 + 8 + 8);
	memset(&insn, 0, sizeof(insn));
	rc = arm_evaluate_opcode(0xe12fff1e, 0, &insn);
	check("arm_bx_lr", rc == ERROR_OK && insn.type == ARM_BX && insn.info.b_bl_bx_blx.reg_operand == 14);
	memset(&insn, 0, sizeof(insn));
	rc = thumb_evaluate_opcode(0x4770, 0, &insn);
	check("thumb_bx_lr", rc == ERROR_OK && insn.type == ARM_BX && insn.instruction_size == 2 &&
		insn.info.b_bl_bx_blx.reg_operand == 14);
	memset(&insn, 0, sizeof(insn));
	rc = thumb_evaluate_opcode(0x2105, 0, &insn);
	check("thumb_movs_imm", rc == ERROR_OK && insn.type == ARM_MOV && strstr(insn.text, "MOVS\tr1, #0x05"));
}

static void test_codec(void)
{
	uint8_t bin[4];
	check("unhexify", unhexify(bin, "DEADbeef", 4) == 4 && !memcmp(bin, "\xde\xad\xbe\xef", 4));
	char hex[9];
	check("hexify", hexify(hex, bin, 4, sizeof(hex)) == 8 && !strcmp(hex, "deadbeef"));
	check("unhexify_stops_at_nonhex", unhexify(bin, "12zz", 2) == 1);

	size_t len = 0;
	unsigned char *dec = base64_decode((const unsigned char *)"TWFuTWE=", 8, &len);
	check("base64_decode", dec && len == 5 && !memcmp(dec, "ManMa", 5));
	free(dec);
	unsigned char *enc = base64_encode((const unsigned char *)"ManMa", 5, &len);
	check("base64_encode", enc && len >= 8 && !memcmp(enc, "TWFuTWE=", 8));
	free(enc);

	uint8_t num[4] = {0};
	check("str_to_buf_hex", str_to_buf("0x1234", num, 16) == ERROR_OK && num[0] == 0x34 && num[1] == 0x12);
	memset(num, 0, sizeof(num));
	check("str_to_buf_bin", str_to_buf("0b101", num, 8) == ERROR_OK && num[0] == 5);
	memset(num, 0, sizeof(num));
	check("str_to_buf_dec", str_to_buf("300", num, 16) == ERROR_OK && num[0] == 0x2c && num[1] == 0x01);
	check("str_to_buf_invalid", str_to_buf("12q", num, 16) != ERROR_OK);
	char *s = buf_to_hex_str("\x34\x12", 16);
	check("buf_to_hex_str", s && !strcmp(s, "1234"));
	free(s);
}

int main(void)
{
	debug_level = LOG_LVL_SILENT;
	const char *tmp = getenv("TMPDIR");
	snprintf(dir, sizeof(dir), "%s/ocd-kat-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 2;
	}
	test_ihex();
	test_srec();
	test_elf();
	test_xilinx();
	test_lattice();
	test_arm();
	test_codec();
	char cmd[300];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd) != 0)
		return 2;
	printf("KAT_DONE failures=%d\n", failures);
	return failures ? 1 : 0;
}
