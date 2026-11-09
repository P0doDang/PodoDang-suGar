# stemil-rephase 6way

> 이 문서는 6way가 별도 폴더였을 때 작성되었고, 지금은 이 폴더(`stemil-rephase/`)에 통합되어 **같은 바이너리**입니다. 아래 "상위 프로그램"은 6way 옵션을 끈 기본 동작을 뜻합니다. 기본판 설명은 [README.md](README.md), 측정은 [REPORT-6way.md](REPORT-6way.md)를 보세요.

**6방향 동시 탐색**과 **후보 전체 rephase**를 추가한 기능입니다. 원본 stemil은 수정하지 않습니다. 옵션을 끄면(`--orientations 1 --candidates 1`, 기본값) 기본판과 동일한 단일 스레드 경로로 동작합니다.

## 추가된 두 가지

1. **6방향 탐색 (`--orientations N`, 1..6)**
   같은 큐브를 3개 축(URF 3-fold 회전 0/1/2회) × 역변환 큐브의 6가지 등가 문제로 바꿔 각각 별도 스레드에서 Two-Phase로 풉니다. 방향마다 Phase 1이 원본 큐브의 서로 다른 면 축을 목표로 하므로 서로 다른 후보 해가 나옵니다. 각 해를 원래 큐브의 수순으로 되돌려 검증한 뒤 가장 짧은 것을 기준해로 씁니다. pruning table 4개는 모든 스레드가 읽기 전용으로 공유하므로 메모리는 거의 늘지 않습니다.
   방향 순서: `0=원본, 1=역, 2=URF, 3=URF 역, 4=URF², 5=URF² 역`.
2. **후보 rephase (`--candidates K`, 1..64)**
   방향별 탐색 중 발견한 해(개선 이력 포함)에서 중복을 제거하고 짧은 순서로 상위 K개를 골라 각각 별도 스레드에서 rephase(경계+sliding)합니다. 수선 후 가장 짧은 해를 최종 해로 씁니다. 각 후보는 자기 방향의 실제 Phase 경계를 기준으로 수선합니다.

시간 배분은 기존과 같습니다. `--budget-ms 1000 --rephase-ms 100`이면 모든 방향의 Two-Phase가 900 ms 벽시계 안에서 동시에 돌고, 이어서 모든 후보의 rephase가 최대 100 ms 동안 동시에 돌아갑니다. 따라서 벽시계 시간은 그대로이고 CPU 시간이 스레드 수만큼 늘어납니다.

## 실행

```bash
cd <repo>/stemil-rephase
make -j2
./build/stemil-rephase solve --orientations 6 --phase1-mode ida \
  --budget-ms 1000 --rephase-ms 100 --windows 4,6,8,10

# 반복 실행 (Ctrl+C까지). 요약과 단축 사례는 기존과 같이 stderr / output.txt
./build/stemil-rephase solve --runs 0 --scramble-length 30 \
  --orientations 6 --phase1-mode ida --budget-ms 1000 --rephase-ms 100
```

출력에는 방향별 move 수, 방향 0 단독 move 수(비교 기준), 6방향 최선, 선택된 방향, 후보별 수선 전후 move 수와 선택된 후보가 추가됩니다. `--json`에는 `orientations`, `winning_orientation`, `single_orientation_htm`, `orientation_htm[]`, `candidate_htm[]`, `repaired_candidate`가 추가됩니다. `--trace`에는 `orientation_end`, `candidate_repair_end` 이벤트가 들어갑니다.

제약: `--adaptive`와는 함께 쓸 수 없습니다. `--orientations`/`--candidates`는 `--stemil-max-ms` 다중 실행에서도 각 stemil 실행에 적용됩니다. 코어 수보다 많은 스레드는 OS가 시분할하므로 이득이 줄어듭니다.

## 12방향과 Phase 1 표준 열거 (추가)

- `--orientations 7..12`: 기존 6방향에 좌우 거울 대칭(R↔L, 회전 방향 반전)을 곱한 방향을 추가합니다. G1 축은 같지만 탐색 순서가 달라 다른 후보가 나옵니다.
- `--phase1-mode prefix|ida` (기본 `prefix`):
  - `prefix`: 원본 stemil 방식. 앞부분 수순(prefix)마다 Phase 1 경로를 greedy로 1개만 시도합니다.
  - `ida`: Kociemba 표준 방식. Phase 1 해를 길이 h, h+1, …순으로 **전부** 열거하고(마지막 move가 Phase 2 move인 경로는 중복이라 제외), 각 해에서 현재 최선보다 짧아질 수 있는 한도로 Phase 2를 탐색합니다.
- `tools/evaluate.py --config 이름:바이너리:방향:후보[:prefix|ida]`, `tools/compare.py A.jsonl B.jsonl`(쌍별 부호검정).

## 최적해 검증 옵션 (`--verify-optimal`)

`--runs 0`은 Ctrl+C까지 계속 반복합니다(진행 중인 큐브는 끝까지 풀고 종료, 종료 시 요약 출력). `--verify-optimal`을 켜면 큐브를 풀 때마다 IDA* 최적해 solver로 "찾은 길이보다 짧은 해가 있는가"를 검사합니다.

```bash
./build/stemil-rephase solve --runs 0 --scramble-length 15 --orientations 6 --phase1-mode ida \
  --budget-ms 1000 --rephase-ms 100 --verify-optimal --verify-cap-sec 60
```

- 결과는 큐브마다 **최적해(증명됨) / 최적 아님(최적 N수) / 미확정(시간 상한 도달, 하한 N수)** 중 하나입니다. 미확정은 최적으로 세지 않습니다.
- 종료할 때(Ctrl+C 포함) 최적해 비율(판정된 큐브 기준과 전체 기준), 평균 solve move 수(전체·최적·최적 아님·미확정), 평균 최적 길이, 평균 gap, 검증 시간 합계를 stderr에 출력하고 `--stats-file`(기본 `stats.txt`)에 이어 붙입니다. 큐브마다 한 줄 기록(셔플·solve 수순 포함)도 즉시 추가됩니다.
- `--verify-cap-sec`(기본 60), `--verify-threads`(기본 전체 코어), `--stats-file`로 조절합니다. `--json`에는 `optimal_status`, `optimal_htm`, `optimal_lower_bound`, `verify_seconds`가 추가됩니다.
- 한계: 증명 비용이 깊이에 따라 급증합니다. 길이 14~15는 수초 이내, 16 이상은 큐브당 수 분, 17~18은 수십 분~수 시간이라 `--scramble-length 30` 같은 깊은 큐브는 대부분 "미확정"이 됩니다. 검증은 풀이 시간에 포함되지 않지만(풀이 후 별도 실행) 큐브당 소요 시간은 그만큼 늘어납니다. 시작할 때 corner database 구성에 약 20~30초가 더 듭니다.
- `repair`, `verify`, `--stemil-max-ms`, `--rephase-max-ms`와는 함께 쓸 수 없습니다.

## 최적해 기준 도구 (추가)

- `build/optimal`: 전체 큐브 IDA* 최적해 solver. 하한은 `max(corner PDB, 3축 Phase-1 거리)`이며 Phase-1 거리는 기존 stemil table의 mod-3 gradient에서, corner PDB(84 MiB)는 시작할 때 RAM에서 BFS로 만듭니다(약 20~30초, stemil 디렉터리에는 쓰지 않음).
  ```bash
  ./build/optimal selftest                      # BFS oracle(거리 ≤4)·scramble 검증
  ./build/optimal solve --scramble "R U F2 ..." # 한 큐브의 최적해
  ./build/optimal gen --count 60 --length 15 --seed 6000 --threads 11 --out corpus/x.jsonl
  ```
- `corpus/optimal_corpus.jsonl`: 최적 길이를 증명한 192개(scramble 길이 14/15/16, 최적 12~16). 재생성: `corpus/make_corpus.sh`(약 15분).
- `tools/evaluate.py`: 설정별로 `batch`를 돌려 코퍼스와 비교해 최적해 적중률·gap을 출력. 예:
  ```bash
  python3 tools/evaluate.py --budget-ms 1000 --rephase-ms 100 \
    --config A1:build/stemil-rephase:1:1 --config C6x6:build/stemil-rephase:6:6
  ```

## Two-Phase 처리량 개선 (추가)
`src/kernel.hpp/.cpp`: 원본과 탐색 순서가 같은 인라인 kernel. Phase-1 하강에서 cubie 변환·중복 table 검증 제거, Phase 2의 `std::function` 재귀 제거와 이동 목록 사전 계산, `move_cube` 대신 `fast_move`. 테스트가 원본 `phase1_search`·`move_cube`·이전 Phase-2 구현과 동일한 결과를 확인합니다. 후보/초는 약 18만에서 약 29만으로 증가했습니다. 테이블 로딩 뒤 모든 페이지를 한 번 순차 접근해(setup에 포함) swap-out된 table이 탐색 중 무작위 page fault를 일으키지 않게 했습니다.

## 구현 위치

| 파일 | 내용 |
|---|---|
| `src/symmetry.hpp/.cpp` | 회전 conjugation(sticker 기하), 역변환, 해·경계 복원 |
| `src/solver.cpp` `Solver::solve_multi` | 방향별 스레드 탐색, 후보 취합·중복 제거, 후보별 rephase 스레드, 최종 검증 |
| `src/kernel.*` | 인라인 Two-Phase kernel (원본과 동일 순서) |
| `src/optimal.*`, `tools/optimal_main.cpp` | corner PDB + IDA* 최적해 solver, 코퍼스 생성기 |
| `tools/evaluate.py` | 최적해 적중률 평가 |
| `src/optimal.cpp` `solve_parallel` | 큐브 하나를 여러 스레드로 증명하는 병렬 IDA* (`--verify-optimal`) |
| `src/main.cpp` | `--orientations`, `--candidates`, 출력·JSON |
| `tests/test.cpp` | conjugation이 relabel된 move와 같은지, 6방향 해가 원래 큐브를 푸는지 |
| `results/` | A/B/C/R 비교 실행 스크립트와 결과 |

`make test`, `make test-cli`, `make sanitize`는 상위와 같습니다. 측정 결과와 해석은 [REPORT.md](REPORT.md)에 있습니다.
