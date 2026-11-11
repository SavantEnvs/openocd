# s19: record length field is trusted over the real line length

`image_mot_buffer_complete_inner()` (`src/target/image.c`) parses `count` bytes with `sscanf(&lpsz_line[bytes_read], "%2x", ...)`
for S0 and S1/S2/S3 records without checking the line is that long. `repro.s19` (`S11`) declares a record longer than the
line, so the parser reads past the NUL terminator and writes beyond `mot->buffer`. A bare `S00` makes `sscanf` run `strlen` over
unmapped memory (SEGV in `__strlen_evex`, called from `image.c:812`).

- Report: AddressSanitizer heap-buffer-overflow at `image.c:868` (S1 data loop) / SEGV from `image.c:812` (S0 loop).
- Impact: out-of-bounds heap read and write when loading an attacker-supplied `.s19` file.
- Fix: validate `count` against the remaining line length before each record's data loop.
- Reproduce: `/mayhem/fuzz_srec-standalone mayhem/fuzz_srec/known-findings/record-length-overrun/repro.s19`
