#!/usr/bin/env bash
# Reproduce the SplatDiff milestone-2 experiments: stability, appearance, real changes, ablation, scaling.
# Usage: tools/run_experiments.sh            (from the project root, after `make build`)
# Writes assets/tests/*.ply test pairs and assets/tests/results.csv + bench.csv.
set -euo pipefail
cd "$(dirname "$0")/.."
A=assets/synthetic_room.ply
E=./build/splatdiff_edit
D=./build/splatdiff_detect
OUT=assets/tests
mkdir -p "$OUT"
rm -f "$OUT/results.csv" "$OUT/bench.csv"

[ -f "$A" ] || python3 tools/make_synthetic_scene.py "$A"

echo "== making test pairs"
$E $A $OUT/jitter.ply        --seed 1 --jitter 0.01 0.1 > /dev/null
$E $A $OUT/jitter_strong.ply --seed 2 --jitter 0.03 0.2 > /dev/null
$E $A $OUT/split.ply         --seed 3 --split 0.5 > /dev/null
$E $A $OUT/prune.ply         --seed 4 --prune 0.3 0.9 > /dev/null
$E $A $OUT/colour.ply        --colour 0.6 0.8 1.3 0.1 > /dev/null
$E $A $OUT/rescan_all.ply    --seed 5 --split 0.3 --prune 0.2 0.9 --jitter 0.01 0.1 --colour 1.2 1.0 0.8 0.05 > /dev/null
$E $A $OUT/real_edits.ply    --seed 42 \
  --delete 0.2 0.62 -1.8  0.65 0.6 0.65 \
  --move -1.5 0.535 -1.0  0.55 0.515 0.55  1.2 0 0.4  35 \
  --duplicate 0.5 0.215 1.8  0.35 0.195 0.35  0 0 1.4 \
  --colour 1.15 1.05 0.85 0.03 --jitter 0.005 0.05 > /dev/null

echo "== stability + appearance (expect 0 regions) and real edits (expect 4/4)"
for t in jitter jitter_strong split prune colour rescan_all real_edits; do
  printf "%-14s " "$t"
  $D $A $OUT/$t.ply --csv $OUT/results.csv | grep -E "^grid|detected" | sed 's/^grid.*occupied, //' | tr '\n' ' '
  echo
done

echo "== ablation (changed voxels / regions on rescan_all, then real edits)"
ablate() {
  printf "%-36s " "$1"; shift
  $D $A $OUT/rescan_all.ply "$@" | grep -E "^grid" | sed 's/.*occupied, //' | tr '\n' ' '
  $D $A $OUT/real_edits.ply "$@" | grep detected
}
ablate "density, no anti-alias, no tolerance" --density --no-aa --tol 0
ablate "+ anti-alias"                       --density --tol 0
ablate "+ occupancy"                        --tol 0
ablate "+ 1-voxel tolerance (default)"

echo "== scaling (serial vs OpenMP)"
for r in 64 128 256; do
  $D $A $OUT/real_edits.ply --res $r --bench --csv $OUT/bench.csv | grep -E "speedup"
done
echo "results: $OUT/results.csv, $OUT/bench.csv"
