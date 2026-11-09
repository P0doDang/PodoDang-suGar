#!/usr/bin/env python3
"""Summarise the A/B/C/R runs: average HTM, distribution, and agreement with the 5 s reference."""
import json,statistics,sys,pathlib
root=pathlib.Path(__file__).parent
runs={'A 기준(1방향)':'A_baseline.jsonl','B 6방향':'B_6dir.jsonl','C 6방향+후보6':'C_6dir_cand6.jsonl','R 참조(5s, 6방향+후보6)':'R_reference5s.jsonl'}
data={}
for name,file in runs.items():
    path=root/file
    if not path.exists():continue
    rows=[json.loads(l) for l in path.read_text().splitlines() if l.strip()]
    data[name]={r['seed']:r for r in rows}
ref=data.get('R 참조(5s, 6방향+후보6)',{})
print(f"{'설정':<26}{'해결':>5}{'평균HTM':>9}{'최소':>5}{'최대':>5}{'평균ms':>9}{'<=참조':>8}{'==참조':>8}")
out={}
for name,rows in data.items():
    ok=[r for r in rows.values() if r['solved']]
    h=[r['htm'] for r in ok]
    common=[s for s in rows if s in ref and rows[s]['solved'] and ref[s]['solved']]
    le=sum(rows[s]['htm']<=ref[s]['htm'] for s in common);eq=sum(rows[s]['htm']==ref[s]['htm'] for s in common)
    t=statistics.mean(r['elapsed_ms'] for r in ok)
    print(f"{name:<26}{len(ok):>5}{statistics.mean(h):>9.2f}{min(h):>5}{max(h):>5}{t:>9.1f}{le:>8}{eq:>8}")
    out[name]={'solved':len(ok),'avg_htm':statistics.mean(h),'min':min(h),'max':max(h),'avg_ms':t,'le_ref':le,'eq_ref':eq,'n_common':len(common)}
for name in ('B 6방향','C 6방향+후보6'):
    rows=data.get(name);base=data.get('A 기준(1방향)')
    if not rows or not base:continue
    common=[s for s in rows if s in base and rows[s]['solved'] and base[s]['solved']]
    better=sum(rows[s]['htm']<base[s]['htm'] for s in common);worse=sum(rows[s]['htm']>base[s]['htm'] for s in common)
    print(f"{name} vs A: 더 짧음 {better}, 같음 {len(common)-better-worse}, 더 김 {worse} (평균차 {statistics.mean(rows[s]['htm']-base[s]['htm'] for s in common):+.2f})")
    out[name+' vs A']={'shorter':better,'same':len(common)-better-worse,'longer':worse}
c=data.get('C 6방향+후보6')
if c:
    rows=[r for r in c.values() if r['solved']]
    print('C 분해: 방향0 단독 %.2f → 6방향 최선 %.2f → rephase 최종 %.2f'%(
        statistics.mean(r['single_orientation_htm'] or r['initial_htm'] for r in rows),
        statistics.mean(r['initial_htm'] for r in rows),statistics.mean(r['htm'] for r in rows)))
    print('C rephase 단축 큐브: %d개'%sum(r['htm']<r['initial_htm'] for r in rows))
(root/'summary.json').write_text(json.dumps(out,ensure_ascii=False,indent=1))
