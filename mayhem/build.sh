#!/usr/bin/env bash
#
# mayhem/build.sh — builds the OpenOCD fuzz harnesses and the test suite.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: builds OpenOCD twice out of tree (autotools, system Jim Tcl from libjim-dev):
#   mayhem/build-fuzz  sanitized + fuzzer-no-link   -> libs linked into the 7 fuzz harnesses (+ -standalone repro)
#   mayhem/build-test  project's normal flags       -> src/openocd + the KAT binary that mayhem/test.sh runs
# Both dirs are gitignored (mayhem/.gitignore) so `git clean -ffdX` removes them.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
B="$SRC/mayhem/build-fuzz"
T="$SRC/mayhem/build-test"

# 1) Generate the autotools build system (offline; jimtcl comes from libjim-dev, no submodule fetch).
./bootstrap nosubmodule

# 2) Sanitized library build. -fsanitize=fuzzer-no-link adds SanCov to the fuzzed code.
#    UBSan shift-base/shift-exponent are relaxed: OpenOCD's bit_reverse/flip_u32 and the ARM decoder's ror()
#    shift into the sign bit or by 32 on every ordinary input, which would halt every seed before any
#    parser logic runs. Everything else in ASan+UBSan stays on and halting.
UBSAN_RELAX="-fno-sanitize=shift-base,shift-exponent"
rm -rf "$B"; mkdir -p "$B"
(cd "$B" && ../../configure CC="$CC" CFLAGS="$SANITIZER_FLAGS $UBSAN_RELAX -fsanitize=fuzzer-no-link -O1 $DEBUG_FLAGS" \
    --disable-werror >configure.log 2>&1 || { tail -50 configure.log >&2; exit 1; }
 make -j"$MAYHEM_JOBS")

# libopenocd.a already contains every subsystem archive (helper, target, pld, flash, ...).
JIM_LIBS="$(pkg-config --libs jimtcl) -lssl -lcrypto"
INC=(-DHAVE_CONFIG_H -I"$B" -I"$SRC/src" -I"$SRC/mayhem/harnesses")
LINK=("$B/src/.libs/libopenocd.a" $JIM_LIBS -lm -lpthread -ldl)

# LeakSanitizer off at build time (leaks are not the bug class fuzzed here).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"

# 3) Harnesses: name | source | extra defines. Each is linked twice: libFuzzer and standalone reproducer.
HARNESSES=(
  "fuzz_ihex|fuzz_image.c|-DIMAGE_TYPE=\"ihex\""
  "fuzz_elf|fuzz_image.c|-DIMAGE_TYPE=\"elf\""
  "fuzz_srec|fuzz_image.c|-DIMAGE_TYPE=\"s19\""
  "fuzz_xilinx_bit|fuzz_pld_bit.c|-DPLD_XILINX"
  "fuzz_lattice_bit|fuzz_pld_bit.c|-DPLD_LATTICE"
  "fuzz_arm_disasm|fuzz_arm_disasm.c|"
  "fuzz_codec|fuzz_codec.c|"
)
for h in "${HARNESSES[@]}"; do
  IFS='|' read -r name src defs <<<"$h"
  # shellcheck disable=SC2086
  $CC $SANITIZER_FLAGS $UBSAN_RELAX -fsanitize=fuzzer-no-link -O1 $DEBUG_FLAGS "${INC[@]}" $defs -c "$SRC/mayhem/harnesses/$src" -o "$B/$name.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/$name.o" "$B/lsan_off.o" "${LINK[@]}" -o "$SRC/$name"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$B/standalone_main.o" "$B/$name.o" "$B/lsan_off.o" "${LINK[@]}" -o "$SRC/$name-standalone"
done

# 4) The test suite, with OpenOCD's normal flags in a separate build tree.
rm -rf "$T"; mkdir -p "$T"
(cd "$T" && env -u CFLAGS -u LDFLAGS ../../configure CC="$CC" CFLAGS="-g -O2 $COVERAGE_FLAGS" LDFLAGS="$COVERAGE_FLAGS" \
    --disable-werror >configure.log 2>&1 || { tail -50 configure.log >&2; exit 1; }
 make -j"$MAYHEM_JOBS")
$CC -g -O2 $COVERAGE_FLAGS -DHAVE_CONFIG_H -I"$T" -I"$SRC/src" "$SRC/mayhem/kat/ocd_kat.c" \
    "$T/src/.libs/libopenocd.a" $JIM_LIBS -lm -lpthread -ldl $COVERAGE_FLAGS -o "$T/ocd_kat"
