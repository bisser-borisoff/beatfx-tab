#!/bin/sh
# Final review, Important 1: editing one of the mod's headers (geometry.h when calibrating)
# must make the library out of date, or a "calibrated" build ships the old values.
# Usage: sh tests/check-header-deps.sh   (from the mod's folder, after a build)
set -u
lib=out/beatfx-tab.so
[ -f "$lib" ] || { echo "check-header-deps: build first ($lib missing)"; exit 2; }
fail=0
for h in geometry.h fx_decode.h panel_track.h label_draw.h; do
    touch -t 203001010000 "$h"          # newer than any build
    if make -q "$lib" >/dev/null 2>&1; then
        echo "FAIL: $lib is up to date after touching $h"
        fail=1
    fi
    touch "$h"                          # back to now (still newer than the build: a rebuild follows)
done
[ $fail -eq 0 ] && echo "check-header-deps: every header rebuilds the library"
exit $fail
