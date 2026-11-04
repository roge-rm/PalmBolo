#!/bin/sh
# Runs build/PalmBolo.prc (a debug build) in Frond on a cold-booted TX with
# an input replay, and exports PalmBoloLog.
# Usage: frond-m3.sh OUTDIR REPLAY [more frond-run flags]
set -e
out=$1; replay=$2; shift 2
here=$(dirname "$0")
img=/srv/downloads/temp/debug/palm/frond
exec python3 /home/dan/Projects/palm/frond/tools/frond-run.py \
  --core /home/dan/Projects/palm/frond/out/frond --device palm-tx \
  --nor $img/Palm-TX-5.4-nor.bin --nand $img/tx-calibrated.bin \
  --install "$here/../build/PalmBolo.prc" \
  --app PalmBolo --replay "$replay" --headless --output "$out" \
  --export-db PalmBoloLog "$@"
