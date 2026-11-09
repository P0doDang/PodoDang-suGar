#!/usr/bin/env python3
"""Run stemil-rephase configurations on the known-optimal corpus and report optimal hit rate.

  evaluate.py --corpus corpus/optimal_corpus.jsonl --budget-ms 1000 --rephase-ms 100 \
      --config A:build/stemil-rephase.pre:1:1 --config B:build/stemil-rephase:6:6 ...
A config is name:binary:orientations:candidates[:prefix|ida]. Every run is one process (tables loaded once)
that solves all corpus cubes through `batch`; final solutions are re-verified with an
independent sticker model and compared against the proven optimal length.
"""
import argparse,json,statistics,subprocess,sys,time
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from stickers import SOLVED,apply
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--corpus',default=str(ROOT/'corpus/optimal_corpus.jsonl'))
    ap.add_argument('--budget-ms',type=float,default=1000)
    ap.add_argument('--rephase-ms',type=float,default=100)
    ap.add_argument('--windows',default='4,6,8,10')
    ap.add_argument('--config',action='append',required=True)
    ap.add_argument('--out',default=str(ROOT/'results/optimal-eval'))
    a=ap.parse_args()
    corpus=[json.loads(l) for l in Path(a.corpus).read_text().splitlines() if l.strip()]
    corpus=[c for c in corpus if c['optimal'] is not None]
    out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    summary={}
    print(f"corpus: {len(corpus)} cubes, optimal distribution {sorted(Counter(c['optimal'] for c in corpus).items())}")
    print(f"{'config':<14}{'avg HTM':>8}{'avg opt':>8}{'gap':>6}{'hit%':>7}{'+1':>5}{'+2':>5}{'+3+':>5}{'ms':>8}{'time-s':>8}")
    for spec in a.config:
        parts=spec.split(':');name,binary,o,k=parts[:4];mode=parts[4] if len(parts)>4 else 'prefix'
        cmd=[str(ROOT/binary),'batch','--json','--budget-ms',str(a.budget_ms),'--rephase-ms',str(a.rephase_ms),'--windows',a.windows,
             '--orientations',o,'--candidates',k,'--phase1-mode',mode,'--output-file',str(out/f'{name}-shortened.txt')]
        start=time.time()
        result=subprocess.run(cmd,input=''.join(c['facelets']+'\n' for c in corpus),text=True,capture_output=True,cwd=ROOT)
        rows=[json.loads(l) for l in result.stdout.splitlines() if l.strip().startswith('{')]
        assert len(rows)==len(corpus),(name,len(rows),result.stderr[-500:])
        (out/f'{name}.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows))
        gaps=[];bad=0
        for c,r in zip(corpus,rows):
            ok=r['solved'] and apply(c['facelets'],r['sequence'])==SOLVED
            if not ok:bad+=1;continue
            assert r['htm']>=c['optimal'],('shorter than proven optimal!',name,c['index'])
            gaps.append(r['htm']-c['optimal'])
        n=len(gaps)
        s={'cubes':len(corpus),'verified':n,'avg_htm':statistics.mean(c['optimal']+g for c,g in zip(corpus,gaps)) if n else None,
           'avg_opt':statistics.mean(c['optimal'] for c in corpus),'hit_rate':sum(g==0 for g in gaps)/len(corpus),
           'gap':Counter(gaps),'avg_ms':statistics.mean(r['elapsed_ms'] for r in rows),'wall_s':time.time()-start}
        summary[name]={k:(dict(v) if isinstance(v,Counter) else v) for k,v in s.items()}
        g=s['gap']
        print(f"{name:<14}{s['avg_htm']:>8.2f}{s['avg_opt']:>8.2f}{s['avg_htm']-s['avg_opt']:>6.2f}{100*s['hit_rate']:>6.1f}%{g[1]:>5}{g[2]:>5}{sum(v for k2,v in g.items() if k2>=3):>5}{s['avg_ms']:>8.0f}{s['wall_s']:>8.0f}",flush=True)
    (out/'summary.json').write_text(json.dumps(summary,indent=1))
main()
