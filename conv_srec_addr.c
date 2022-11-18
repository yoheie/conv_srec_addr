/*
 * Copyright (c) 2021-2022 Yohei Endo <yoheie@gmail.com>
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must
 *    not claim that you wrote the original software. If you use this
 *    software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must
 *    not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 */
#if __STDC_VERSION__ < 199901L
#error "C99 required."
#endif

#if ! __STDC_HOSTED__
#error "Hosted environment required."
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifndef LINE_MAX
#define LINE_MAX 2048
#endif

static const unsigned int srec_addr_len[10] = { 2, 2, 3, 4, 0, 2, 3, 4, 3, 2 };

static int get_offset(const char *text, int *p_sign, uint32_t *p_offset)
{
	int base = 10;
	int digit;

	*p_sign = 0;
	*p_offset = 0;

	if (*text == '-') {
		*p_sign = 1;
		text++;
	}

	if (*text == '0') {
		text++;
		if (*text == '\0') {
			return 0;
		}
		else if ((*text == 'x') || (*text == 'X')) {
			base = 16;
			text++;
		}
		else {
			base = 8;
		}
	}

	if (*text == '\0') {
		return -1;
	}

	do {
		if (('0' <= *text) && (*text <= '7')) {
			digit = *text - '0';
		}
		else if (('8' <= *text) && (*text <= '9')) {
			if (base < 10) {
				return -1;
			}
			digit = *text - '0';
		}
		else if (('A' <= *text) && (*text <= 'F')) {
			if (base < 16) {
				return -1;
			}
			digit = *text - 'A' + 10;
		}
		else if (('a' <= *text) && (*text <= 'f')) {
			if (base < 16) {
				return -1;
			}
			digit = *text - 'a' + 10;
		}
		else {
			return -1;
		}

		if (*p_offset > ((UINT32_MAX - digit) / base)) {
			return -1;
		}
		*p_offset = *p_offset * base + digit;

		text++;
	} while (*text != '\0');

	return 0;
}

static int get_line(char *line, size_t size, size_t *p_len, FILE *pfile)
{
	size_t n = 0;
	int c;

	while (--size > 0) {
		if ((c = getc(pfile)) == EOF) {
			if (ferror(pfile) == 0) {
				*line = '\0';
				*p_len = n;
				return 0;
			}
			else {
				return -1;
			}
		}
		*line++ = c;
		n++;
		if (c == '\n') {
			*line = '\0';
			*p_len = n;
			return 0;
		}
	}

	return -1;
}

static int hexstr_to_u32(const char *c, size_t len, uint32_t *n)
{
	*n = 0;
	while (len > 0) {
		if (('0' <= *c) && (*c <= '9')) {
			*n = *n * 16 + (*c - '0');
		}
		else if (('A' <= *c) && (*c <= 'F')) {
			*n = *n * 16 + (*c - 'A' + 10);
		}
		else {
			return -1;
		}
		c++;
		len--;
	}

	return 0;
}

static void u32_to_hexstr(uint32_t n, size_t len, char *c)
{
	const char hexc[16] = "0123456789ABCDEF";

	while (len > 0) {
		c[--len] = hexc[n % 16];
		n /= 16;
	}
}

static int srec_check(const char *line)
{
	size_t line_len;
	uint32_t srec_len;
	uint32_t srec_byte;
	uint8_t srec_sum;

	line_len = strlen(line);

	if ((line_len < 6) || (line[0] != 'S')
	 || ((line[1] < '0') || ('9' < line[1]))
	 || (hexstr_to_u32(&line[2], 2, &srec_len) != 0)
	 || (srec_len < srec_addr_len[line[1] - '0'] + 1)
	 || (line_len < srec_len * 2 + 4)
	) {
		return -1;
	}

	line += 4;
	srec_sum = srec_len;
	do {
		if (hexstr_to_u32(line, 2, &srec_byte) != 0) {
			return -1;
		}
		srec_sum += srec_byte;
		line += 2;
	} while (--srec_len > 0);
	if (srec_sum != 0xFF) {
		return -1;
	}

	if (*line == '\r') {
		line++;
	}
	if (*line == '\n') {
		line++;
	}
	if (*line != '\0') {
		return -1;
	}

	return 0;
}

static int srec_addr_change(char *line, size_t size, uint32_t addr)
{
	size_t line_len;
	uint32_t srec_len;
	size_t move_n;
	uint32_t srec_byte;
	uint8_t srec_sum;

	line_len = strlen(line);
	hexstr_to_u32(&line[2], 2, &srec_len);
	move_n = 8 - srec_addr_len[line[1] - '0'] * 2;

	if (move_n > 0) {
		if ((size - (line_len + 1)) < move_n) {
			return -1;
		}
		if ((srec_len + move_n / 2) > 0xFF) {
			return -1;
		}
		srec_len += move_n / 2;
		memmove(&line[12], &line[12 - move_n], line_len + 1 - (12 - move_n));
		u32_to_hexstr(srec_len, 2, &line[2]);
	}

	if ((line[1] == '1') || (line[1] == '2')) {
		line[1] = '3';
	}
	else if ((line[1] == '9') || (line[1] == '8')) {
		line[1] = '7';
	}

	u32_to_hexstr(addr, 8, &line[4]);

	line += 4;
	srec_sum = srec_len;
	do {
		if (hexstr_to_u32(line, 2, &srec_byte) != 0) {
			return -1;
		}
		srec_sum += srec_byte;
		line += 2;
	} while (--srec_len > 1);

	u32_to_hexstr(0xFF - srec_sum, 2, line);

	return 0;
}

int main(int argc, char *argv[])
{
	FILE *pfile;
	int sign;
	uint32_t offset;
	char line[LINE_MAX + 1];
	size_t line_len;
	size_t linenum = 0;
	uint32_t srec_addr;

#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	if ((argc != 2) && (argc != 3)) {
		fprintf(stderr, "ERROR: Usage: conv_srec_addr <offset> [<srecfile>]\n");
		return 1;
	}
	if (get_offset(argv[1], &sign, &offset) != 0) {
		fprintf(stderr, "ERROR: Cannot get offset\n");
		return 2;
	}
	if ((argc == 3) && (strcmp(argv[2], "-") != 0)) {
		if ((pfile = fopen(argv[2], "rb")) == NULL) {
			fprintf(stderr, "ERROR: Cannot open %s\n", argv[2]);
			return 3;
		}
	}
	else {
		pfile = stdin;
	}

	do {
		if ((get_line(line, sizeof(line), &line_len, pfile)) != 0) {
			fprintf(stderr, "ERROR: File read error\n");
			fclose(pfile);
			return 4;
		}
		if (line_len == 0) {
			if (feof(pfile) == 0) {
				fprintf(stderr, "ERROR: File read error\n");
				fclose(pfile);
				return 4;
			}
			break;
		}
		linenum++;
		if (srec_check(line) != 0) {
			fprintf(stderr, "Warning: Non S-Record line found at line %zu\n", linenum);
		}
		else if (((('1' <= line[1]) && (line[1] <= '3'))
		       || (('7' <= line[1]) && (line[1] <= '9')))
		      && (offset != 0)
		) {
			hexstr_to_u32(&line[4], srec_addr_len[line[1] - '0'] * 2, &srec_addr);
			if (sign) {
				if (srec_addr < offset) {
					fprintf(stderr, "ERROR: Address out of range\n");
					fclose(pfile);
					return 5;
				}
				srec_addr -= offset;
			}
			else {
				if (UINT32_MAX - offset < srec_addr) {
					fprintf(stderr, "ERROR: Address out of range\n");
					fclose(pfile);
					return 5;
				}
				srec_addr += offset;
			}
			if (srec_addr_change(line, sizeof(line), srec_addr) != 0) {
				fprintf(stderr, "ERROR: S Record conversion failure\n");
				fclose(pfile);
				return 6;
			}
		}

		if (fputs(line, stdout) < 0) {
			fprintf(stderr, "ERROR: Output error\n");
			fclose(pfile);
			return 7;
		}
	} while (feof(pfile) == 0);

	fclose(pfile);

	return 0;
}
