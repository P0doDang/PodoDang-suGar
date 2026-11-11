# rephase compare

rephase를 stemil, kociemba, kociemba_sym, optimal과 같은 스크램블에서 비교한 결과다. 결과 표는 `summary.md`에 있다.

## 파일

| 경로 | 내용 |
|---|---|
| `summary.md` | 결과 요약 표(가장 먼저 볼 파일) |
| `summary.txt` | 같은 요약의 일반 텍스트판 |
| `compare.csv` | case 하나가 한 행인 전체 수치와 차이 |
| `compare_long.csv` | 알고리즘별 한 행으로 펼친 수치와 해 수순 |
| `cases.tsv` | 비교에 쓴 200개 seed와 스크램블 |
| `data/rephase.jsonl` | rephase 원자료(200건) |
| `data/<알고리즘>.jsonl` | stemil·kociemba·hk_symmetry(kociemba_sym)·optimal 원자료 |
| `scan/` | case 선정용 scan 기록. seed 200개 단위 `scan_<첫 seed>.jsonl`과 그 로그 |
| `logs/` | 실행 로그(`run.log`)와 각 알고리즘의 stderr |

## 재실행

`stemil-rephase/` 디렉터리에서 실행한다. 각 단계는 이어서 실행할 수 있다.

```bash
python3 compare/run_compare.py all --subject rephase --out "../benchmarks/compare-rephase" --max-cases 200
```

> `scan/`(case 선정 기록, 21~227 MB)은 크기 때문에 저장소에 넣지 않았다. 위 명령으로 다시 만들 수 있다. 그래서 이 폴더에서 `report`만 단독으로 다시 돌릴 수는 없다.
