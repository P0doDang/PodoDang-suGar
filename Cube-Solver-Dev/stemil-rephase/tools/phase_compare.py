#!/usr/bin/env python3
"""Per-phase lower bound vs stemil Two-Phase phase lengths, and whole-solve lengths vs bounds.
  phase_compare.py [N] [scramble_length] [seed]   (default 50 25 1)"""
import json,subprocess,sys,statistics as st
from pathlib import Path
R=Path(__file__).resolve().parents[1]
n,L,seed=(int(x) for x in (sys.argv[1:]+['50','25','1'][len(sys.argv)-1:])[:3])
out=subprocess.run([R/'build/stemil-rephase','solve','--runs',str(n),'--seed',str(seed),'--scramble-length',str(L),'--json'],
                   capture_output=True,text=True,cwd=R).stdout
rows=[json.loads(l) for l in out.splitlines() if l.startswith('{')]
inp=''.join(f"{r['facelets']}\t{r['phase1_sequence']}\t{r['phase2_sequence']}\n" for r in rows)
b=subprocess.run([R/'build/phase_bounds'],input=inp,capture_output=True,text=True,cwd=R)
assert b.returncode==0,b.stderr[-300:]
bd=[tuple(map(int,l.split())) for l in b.stdout.splitlines()]
m=lambda x:f"{st.mean(x):.2f}"
root,lb1,lb2=zip(*bd)
p1=[r['phase1_htm'] for r in rows];p2=[r['phase2_htm'] for r in rows]
init=[r['initial_htm'] for r in rows];fin=[r['htm'] for r in rows]
print(f"{n} cubes, scramble {L}, seed {seed}")
print(f"{'':28}{'avg len':>8}{'avg lb':>8}{'gap':>7}{'max gap':>8}{'len==lb':>9}")
def line(name,x,y):
    g=[a-c for a,c in zip(x,y)];print(f"{name:28}{m(x):>8}{m(y):>8}{m(g):>7}{max(g):>8}{sum(v==0 for v in g):>8}/{len(g)}")
line("phase1 len vs P1 lb",p1,lb1)
line("phase2 len vs P2 lb(after P1)",p2,lb2)
line("initial total vs root lb",init,root)
line("initial total vs lb1+lb2",init,[a+c for a,c in zip(lb1,lb2)])
line("final total vs root lb",fin,root)
line("final total vs lb1+lb2",fin,[a+c for a,c in zip(lb1,lb2)])
