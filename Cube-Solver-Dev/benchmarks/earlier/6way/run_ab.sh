#!/bin/bash
# Same 100 shuffles (seed 1000..1099, length 30), one process per configuration, run sequentially.
cd "$(dirname "$0")/.."
C="--seed 1000 --runs 100 --scramble-length 30 --json --windows 4,6,8,10"
./build/stemil-rephase solve $C --budget-ms 1000 --rephase-ms 100 --output-file results/shortened-A.txt > results/A_baseline.jsonl 2> results/A.err
./build/stemil-rephase solve $C --budget-ms 1000 --rephase-ms 100 --orientations 6 --candidates 1 --output-file results/shortened-B.txt > results/B_6dir.jsonl 2> results/B.err
./build/stemil-rephase solve $C --budget-ms 1000 --rephase-ms 100 --orientations 6 --candidates 6 --output-file results/shortened-C.txt > results/C_6dir_cand6.jsonl 2> results/C.err
./build/stemil-rephase solve $C --budget-ms 5000 --rephase-ms 500 --orientations 6 --candidates 6 --output-file results/shortened-R.txt > results/R_reference5s.jsonl 2> results/R.err
echo done > results/ab.done
