#!/bin/bash
# Known-optimal corpus: random-walk scrambles, exact optimal length proven by build/optimal (IDA*).
cd "$(dirname "$0")/.."
./build/optimal gen --count 48 --length 16 --seed 5024 --threads 11 --cap-sec 1800 --out corpus/len16b.jsonl 2> corpus/len16b.log
./build/optimal gen --count 60 --length 15 --seed 6000 --threads 11 --cap-sec 1800 --out corpus/len15.jsonl 2> corpus/len15.log
./build/optimal gen --count 60 --length 14 --seed 7000 --threads 11 --cap-sec 1800 --out corpus/len14.jsonl 2> corpus/len14.log
cat corpus/probe16.jsonl corpus/len16b.jsonl corpus/len15.jsonl corpus/len14.jsonl > corpus/optimal_corpus.jsonl
echo done > corpus/corpus.done
