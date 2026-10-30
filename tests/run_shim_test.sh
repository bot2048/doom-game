#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds csrc/libc_shim.c for the host (as the simulator does) and checks it
# against the host libc.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

cc=${CC:-cc}
$cc -O2 -w -DDG_HOSTED -include "$root/csrc/dg_libc.h" -c "$root/csrc/libc_shim.c" -o "$out/shim.o"
$cc -O0 -w -Wno-format -fno-builtin -c "$root/tests/shim_test.c" -o "$out/test.o"
$cc "$out/test.o" "$out/shim.o" -o "$out/shim_test"
"$out/shim_test"
