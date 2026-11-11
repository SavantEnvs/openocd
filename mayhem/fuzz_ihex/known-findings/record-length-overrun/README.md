# ihex: record length field is trusted over the real line length

`image_ihex_buffer_complete_inner()` (`src/target/image.c`, the `while (count-- > 0) sscanf(&lpsz_line[bytes_read], "%2x", ...)` loop)
walks `count` bytes taken from the record header without checking that the line is that long.
`repro.hex` (`:0DEAF00`) declares 13 data bytes on a 7-character line, so `sscanf` reads past the end of the line buffer and
`ihex->buffer` (sized `filesize / 2`) is written out of bounds.

- Report: AddressSanitizer heap-buffer-overflow at `image.c:200` in `image_ihex_buffer_complete_inner` via `image_open`.
- Impact: out-of-bounds heap read and write when loading an attacker-supplied `.hex` file (`load_image`, `flash write_image`).
- Fix: reject the record unless `bytes_read + 2 * (count + 1) <= strlen(line)` before parsing its data.
- Reproduce: `/mayhem/fuzz_ihex-standalone mayhem/fuzz_ihex/known-findings/record-length-overrun/repro.hex`
