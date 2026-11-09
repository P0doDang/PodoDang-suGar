#!/usr/bin/env python3
"""Paired comparison of two evaluate.py result files: shorter/longer counts and a two-sided sign test."""
import json,math,sys
def load(p):return [json.loads(l) for l in open(p)]
def compare(a,b,name_a,name_b):
    x=[p['htm']-q['htm'] for p,q in zip(load(a),load(b))]
    better=sum(v<0 for v in x);worse=sum(v>0 for v in x);n=better+worse
    k=min(better,worse);p=min(1.0,2*sum(math.comb(n,i) for i in range(k+1))/2**n) if n else 1.0
    print(f'{name_a} vs {name_b}: {name_a} shorter {better}, longer {worse}, same {len(x)-n}, mean {sum(x)/len(x):+.3f}, sign-test p={p:.3f}')
if __name__=='__main__':
    compare(sys.argv[1],sys.argv[2],sys.argv[3] if len(sys.argv)>3 else 'A',sys.argv[4] if len(sys.argv)>4 else 'B')
