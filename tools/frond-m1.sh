#!/bin/sh
# Runs build/PalmBolo.prc (a debug build) in Frond, takes screenshots and
# exports PalmBoloLog.
# Usage: frond-m1.sh DEVICE OUTDIR "SESSION FLAGS" [more frond-run flags]
set -e
dev=$1; out=$2; sess=$3; shift 3
here=$(dirname "$0")
# shellcheck disable=SC2086
exec python3 /home/dan/Projects/palm/frond/tools/frond-run.py \
  --core /home/dan/Projects/palm/frond/out/frond --device "$dev" $sess \
  --install "$here/../build/PalmBolo.prc" --app PalmBolo \
  --screenshot 1500 --screenshot 4000 --screenshot 7000 \
  --export-db PalmBoloLog --run-for 9 --headless --output "$out" "$@"
