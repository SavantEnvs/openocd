/* Per-input scratch file helper shared by the file-parsing harnesses. */
#ifndef MAYHEM_SCRATCH_FILE_H
#define MAYHEM_SCRATCH_FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Writes data to a fresh mkstemps() file under $TMPDIR (or /tmp) and returns 0 on success. */
static inline int scratch_write(char *path, size_t path_len, const char *suffix,
		const uint8_t *data, size_t size)
{
	const char *tmp = getenv("TMPDIR");
	if (!tmp || !*tmp)
		tmp = "/tmp";
	if ((size_t)snprintf(path, path_len, "%s/mayhem-ocd-XXXXXX%s", tmp, suffix) >= path_len)
		return -1;
	int fd = mkstemps(path, (int)strlen(suffix));
	if (fd < 0)
		return -1;
	size_t off = 0;
	while (off < size) {
		ssize_t n = write(fd, data + off, size - off);
		if (n <= 0) {
			close(fd);
			unlink(path);
			return -1;
		}
		off += (size_t)n;
	}
	close(fd);
	return 0;
}

#endif
