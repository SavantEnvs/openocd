# xilinx .bit: 32-bit section length stored in a signed int, malloc result unchecked

`read_section()` (`src/pld/xilinx_bit.c`) reads the section length into `int length`, then `malloc(length)` and
`fread(*buffer, 1, length, ...)` without checking either. `repro.bit` has an `e` section length of `0xffffffff`, which becomes
`-1` and a `malloc(SIZE_MAX)` request; a length of 2 GiB or more behaves the same way, and a failed `malloc` is then passed to `fread`.

- Report: AddressSanitizer allocation-size-too-big in `malloc` from `xilinx_bit.c:50`.
- Impact: unchecked huge allocation, then NULL passed to `fread`, when loading an attacker-supplied bitstream (`pld load`).
- Fix: use `uint32_t length`, cap it by the file size, and check the `malloc` result.
- Reproduce: `/mayhem/fuzz_xilinx_bit-standalone mayhem/fuzz_xilinx_bit/known-findings/negative-section-length/repro.bit`
