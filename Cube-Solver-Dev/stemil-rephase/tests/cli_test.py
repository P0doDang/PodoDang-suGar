#!/usr/bin/env python3
"""Exercise actual CLI modes, JSON/trace separation, and independent stickers."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from stickers import SOLVED,apply
BINARY=str(ROOT/'build/stemil-rephase')

def run(arguments,**kwargs):
    return subprocess.run([BINARY,*arguments],cwd=ROOT,text=True,capture_output=True,timeout=300,**kwargs)

def rows(result):
    assert result.returncode in (0,2),result.stderr
    data=[json.loads(line) for line in result.stdout.splitlines()]
    for row in data:
        assert row['tables']=='expanded'
        if row['scramble'] is not None:
            assert apply(SOLVED,row['scramble'])==row['facelets']
        if row['solved']:
            assert apply(row['facelets'],row['sequence'])==SOLVED
            assert apply(row['facelets'],row['initial_sequence'])==SOLVED
            assert row['htm']<=row['initial_htm']
    return data

saved={}
for arguments in [
    ['solve','--window','11'],['solve','--windows','4,6,'],
    ['solve','--windows','4,6','--window','8'],
    ['solve','--budget-ms','100','--rephase-ms','100'],
    ['solve','--rephase-ms','10','--repair-fraction','0.1'],
    ['solve','--rephase-ms','10','--adaptive'],
    ['solve','--rephase-min-ms','1'],
    ['solve','--windows','6,7','--rephase-max-ms','10,20','--rephase-min-ms','5,15'],
    ['solve','--rephase-max-ms','10','--rephase-ms','5'],
    ['solve','--rephase-max-ms','0'],
    ['solve','--stemil-max-ms','0,100'],
    ['solve','--stemil-max-ms','100,nan'],
    ['solve','--stemil-max-ms','100,200','--budget-ms','500'],
    ['solve','--stemil-max-ms','100,200','--rephase-ms','10'],
    ['repair','--sequence','R','--boundary','0','--stemil-max-ms','100'],
    ['solve','--rephase-ms','-1'],['solve','--window','8junk'],
    ['solve','--seed','4294967296'],['solve','--runs','-1'],['solve','--runs','10001'],
    ['solve','--facelets',SOLVED,'--seed','1'],
    ['solve','--window-slice-ms','nan'],
    ['repair','--sequence','R','--boundary','2'],
    ['repair','--sequence','R','--boundary','0','--trace-file','../third_party/stemil/trace-should-not-exist.jsonl'],
]:
    result=run(arguments)
    assert result.returncode==1,(arguments,result.stdout,result.stderr)
    assert 'error' in json.loads(result.stderr)

sequence="R R' U F L B D"
with tempfile.TemporaryDirectory(prefix='rephase-cli-') as directory:
    trace=Path(directory)/'trace.jsonl'
    result=run(['repair','--sequence',sequence,'--boundary','7','--window','4','--rephase-ms','100','--trace-file',str(trace)])
    row=json.loads(result.stdout)
    assert result.returncode==0 and row['htm']==5 and row['equivalent']
    events=[json.loads(line) for line in trace.read_text().splitlines()]
    assert any(e['event']=='repair_applied' and e['stage']=='sliding' for e in events)
    assert apply(SOLVED,sequence)==apply(SOLVED,row['sequence'])
    saved['sliding_example']=row;saved['trace_events']=events
    before=trace.read_bytes()
    again=run(['repair','--sequence',sequence,'--boundary','7','--trace-file',str(trace)])
    assert again.returncode==1 and trace.read_bytes()==before
    result=run(['repair','--sequence',sequence,'--boundary','7','--window','4','--rephase-ms','100','--trace'])
    json.loads(result.stdout)
    assert any(json.loads(line)['event']=='window_end' for line in result.stderr.splitlines())

human=run([])
assert human.returncode in (0,2),human.stderr
assert human.stdout.count('셔플 move\n')==1,human.stdout
assert all(human.stdout.count(label+'\n')==1 for label in ['stemil phase 1 move','stemil phase 2 move','stemil 전체 solve move']),human.stdout
labels=['셔플 move 수:','stemil phase 1 move 수:','stemil phase 1 탐색 시간:','stemil phase 1 메모리:',
        'stemil phase 2 move 수:','stemil phase 2 탐색 시간:','stemil phase 2 메모리:',
        'stemil 전체 solve move 수:','stemil 전체 solve 시간:','stemil 전체 solve 메모리:',
        'rephase solve move 수:','rephase로 수를 줄였는지:',
        'rephase로 줄인 수:','rephase 탐색 시간:','rephase solve 메모리:']
assert all(human.stdout.count(label)==1 for label in labels),human.stdout
assert not human.stderr.strip() and not human.stdout.lstrip().startswith('{')
saved['automatic_no_arguments_human']=human.stdout
saved['automatic_solve']=rows(run(['solve','--json']))
assert saved['automatic_solve'][0]['stemil_peak_rss_kib']>0
assert saved['automatic_solve'][0]['rephase_peak_bytes']>0
seeded=run(['solve','--json','--seed','2026','--runs','3','--scramble-length','25','--budget-ms','1000','--rephase-ms','100','--windows','8,4,6,4','--trace'])
saved['seeded_runs']=rows(seeded)
assert len(saved['seeded_runs'])==3
assert [r['seed'] for r in saved['seeded_runs']]==[2026,2027,2028]
assert all(r['windows']==[4,6,8] and r['scramble_length']==25 and r['allocated_rephase_ms']==100 for r in saved['seeded_runs'])
saved['seeded_trace']=[json.loads(line) for line in seeded.stderr.splitlines() if line.startswith('{')]
assert any(e['event']=='initial_solution' for e in saved['seeded_trace'])
assert sum(e['event']=='shuffle' for e in saved['seeded_trace'])==3
multi=run(['solve','--json','--seed','2026','--stemil-max-ms','100,200','--windows','6,7,8','--rephase-max-ms','2,3,4','--rephase-min-ms','0.5,1','--trace'])
multi_rows=rows(multi)
assert [r['result_type'] for r in multi_rows]==['stemil','stemil']+['rephase']*18
stemil_runs=[r for r in multi_rows if r['result_type']=='stemil']
rephase_runs=[r for r in multi_rows if r['result_type']=='rephase']
assert [r['maximum_ms'] for r in stemil_runs]==[100,200]
assert [r['stemil_index'] for r in stemil_runs if r['selected_for_rephase']]==[1]
assert stemil_runs[1]['htm']<=stemil_runs[0]['htm']
assert all(r['phase1_memory_bytes']>0 and r['phase2_memory_bytes']>0 and r['phase1_ms']>=0 and r['phase2_ms']>=0 for r in stemil_runs)
assert all(' '.join(filter(None,[r['phase1_sequence'],r['phase2_sequence']]))==r['sequence'] for r in stemil_runs)
assert len(rephase_runs)==18 and all(r['base_stemil_index']==1 and r['base_stemil_maximum_ms']==200 for r in rephase_runs)
assert {w:sum(r['window']==w for r in rephase_runs) for w in (6,7,8)}=={6:6,7:6,8:6}
expected_pairs={(2,0.5),(2,1),(3,0.5),(3,1),(4,0.5),(4,1)}
assert all({(r['maximum_ms'],r['minimum_ms']) for r in rephase_runs if r['window']==w}==expected_pairs for w in (6,7,8))
assert all(r['initial_sequence']==stemil_runs[1]['sequence'] and r['initial_htm']==stemil_runs[1]['htm'] for r in rephase_runs)
multi_trace=[json.loads(line) for line in multi.stderr.splitlines()]
assert sum(e['event']=='stemil_run_start' for e in multi_trace)==2
assert sum(e['event']=='stemil_run_end' for e in multi_trace)==2
assert [e['message'].split()[0] for e in multi_trace if e['event']=='rephase_run_start']==[f'window={w}' for w in (6,7,8) for _ in range(6)]
assert sum(e['event']=='rephase_run_end' for e in multi_trace)==18
saved['multi_stemil_rephase']=multi_rows
saved['multi_stemil_rephase_trace']=multi_trace
result=run(['batch','--json','--budget-ms','100','--rephase-ms','0'],input=SOLVED+'\r\n')
assert rows(result)[0]['htm']==0
out=ROOT/'results/sliding-upgrade';out.mkdir(parents=True,exist_ok=True)
(out/'cli-verification.json').write_text(json.dumps(saved,indent=2)+'\n')
print('CLI tests passed: automatic random shuffle, move counts, independent multi-stemil budgets, multi-rephase cross product, legacy fixed caps, trace sinks, file preservation, input errors, and independent stickers.')
