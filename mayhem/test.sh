#!/usr/bin/env bash
#
# mayhem/test.sh — runs OpenOCD's upstream Tcl command tests and the known-answer parser tests that
# mayhem/build.sh built with the project's normal flags. Nothing is compiled here.
#
# Every check must print a positive marker, so a binary that merely exits 0 cannot pass:
#   * upstream testing/tcl_commands/*.cfg end in `shutdown`; a passing run prints "shutdown command
#     invoked" and exits 0, a failing one echoes the failure and runs `shutdown error` (non-zero exit);
#   * mayhem/kat/ocd_kat.c prints one KAT_PASS/KAT_FAIL line per assertion and a KAT_DONE trailer.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

T="$SRC/mayhem/build-test"
OCD="$T/src/openocd"
KAT="$T/ocd_kat"
passed=0
failed=0

for f in "$OCD" "$KAT"; do
  if [ ! -x "$f" ]; then
    echo "missing $f: mayhem/build.sh did not build the test suite" >&2
    emit_ctrf "openocd-tcl+kat" 0 1 0
    exit 1
  fi
done

# Upstream Tcl command tests (testing/tcl_commands/Makefile.am runs them the same way).
for cfg in test-target-create-command test-target-configure-cget-command test-target-smp-command; do
  out=$("$OCD" -s "$SRC/tcl" -f "$SRC/testing/tcl_commands/utils.tcl" -f "$SRC/testing/tcl_commands/$cfg.cfg" 2>&1)
  rc=$?
  if [ "$rc" -eq 0 ] && grep -q '^shutdown command invoked' <<<"$out"; then
    echo "PASS $cfg"
    passed=$((passed + 1))
  else
    echo "FAIL $cfg (rc=$rc)"
    tail -5 <<<"$out"
    failed=$((failed + 1))
  fi
done

# Known-answer parser tests.
kat_out=$("$KAT" 2>&1)
kat_rc=$?
echo "$kat_out"
kat_pass=$(grep -c '^KAT_PASS ' <<<"$kat_out")
kat_fail=$(grep -c '^KAT_FAIL ' <<<"$kat_out")
if [ "$kat_rc" -ne 0 ] || ! grep -q '^KAT_DONE failures=0$' <<<"$kat_out"; then
  kat_fail=$((kat_fail + 1))
fi
EXPECTED_KATS=41
if [ "$kat_pass" -ne "$EXPECTED_KATS" ]; then
  echo "expected $EXPECTED_KATS KAT_PASS lines, saw $kat_pass" >&2
  kat_fail=$((kat_fail + 1))
fi
passed=$((passed + kat_pass))
failed=$((failed + kat_fail))

emit_ctrf "openocd-tcl+kat" "$passed" "$failed" 0
