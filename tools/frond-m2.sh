#!/bin/sh
# Runs build/PalmBolo.prc (a debug build) and two map databases in Frond with
# an input replay, and exports PalmBoloLog.
# Usage: frond-m2.sh DEVICE OUTDIR "SESSION FLAGS" REPLAY MAP.pdb MAP.pdb [more frond-run flags]
set -e
dev=$1; out=$2; sess=$3; replay=$4; m1=$5; m2=$6; shift 6
here=$(dirname "$0")
# shellcheck disable=SC2086
exec python3 /home/dan/Projects/palm/frond/tools/frond-run.py \
  --core /home/dan/Projects/palm/frond/out/frond --device "$dev" $sess \
  --install "$here/../build/PalmBolo.prc" --install "$m1" --install "$m2" \
  --app PalmBolo --replay "$replay" --run-for 19 --headless --output "$out" \
  --export-db PalmBoloLog "$@"
