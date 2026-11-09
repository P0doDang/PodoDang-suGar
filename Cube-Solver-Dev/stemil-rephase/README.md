# stemil-rephase

기존 `../third_party/stemil`와 그 **전체 pruning table**을 읽어서 사용하는 큐브 solver 보완 프로그램입니다. `stemil_업그레이드.pdf`의 경계 수선에 `kociemba_twophase_업그레이드.pdf`의 전체 해 sliding-window 수선을 추가했습니다. 기존 stemil 소스·실행 파일·테이블은 수정하지 않고, 이 폴더의 코드와 실행 파일만 갱신합니다. 별도 stemil 사본이나 pruning 생성기는 없습니다.

## 바로 실행

```bash
cd <repo>/stemil-rephase
make -j2
./build/stemil-rephase
```

인수 없이 실행하면 **새로운 25수 셔플을 자동으로 만들고 그 큐브를 풉니다.** 매 실행 새 seed를 만들며 `solve`만 입력해도 같습니다. 셔플은 합법적인 무작위 move sequence이며, 전체 legal cube 상태에 대한 균등 샘플러는 아닙니다.

기본 출력에는 stemil의 Phase 1, Phase 2, 전체 solve와 rephase 결과를 구분해 표시합니다.

```text
셔플 move
  <셔플 수열>
셔플 move 수: N수
stemil phase 1 move / move 수 / 탐색 시간 / 메모리
stemil phase 2 move / move 수 / 탐색 시간 / 메모리
stemil 전체 solve move / move 수 / 시간 / 메모리
rephase solve move 수: N수
rephase로 수를 줄였는지: 예/아니오
rephase로 줄인 수: N수
rephase 탐색 시간: N.NNN ms
rephase solve 메모리: N.NN MiB
```

Phase별 메모리는 해당 phase의 pruning table과 coordinate move table의 정적 크기입니다. `stemil 전체 solve 메모리`는 기존 전체 테이블을 읽은 직후의 process peak RSS입니다. Phase별 탐색 시간은 전체 stemil 실행 중 해당 phase 검색 함수에 누적된 시간이며, 전체 시간에는 prefix 열거와 좌표 변환 같은 공통 오버헤드도 포함됩니다. `rephase solve 메모리`는 rephase 변환 DB를 구성하는 동안 필요한 table+queue의 최대 크기입니다.

기본값은 총 탐색 1000 ms, Two-Phase 900 ms, rephase 최대 100 ms, window `4,6,8,10`, 수선 방식 `boundary+sliding`입니다. 다음 예제는 이 설정을 명시하면서 진행 상황도 표시합니다.

```bash
./build/stemil-rephase solve \
  --scramble-length 25 \
  --windows 4,6,8,10 \
  --budget-ms 1000 --rephase-ms 100 \
  --trace
```

`make: Nothing to be done for 'all'.`은 실행 파일이 최신이라는 뜻입니다. 위 solver 명령으로 바로 실행하면 됩니다. 빌드는 기존 stemil 소스를 읽어 `build/stemil-rephase`만 만들며, 상위 stemil에서 make를 실행하지 않습니다. **Pruning table 생성·변환·다운로드 명령은 필요 없습니다.** 기존 원본 ASCII 테이블 네 개를 RAM에 직접 읽고, 누락·손상 시 오류로 종료합니다.

## 셔플과 반복 실행

```bash
# 매번 새 셔플, 길이는 30수
./build/stemil-rephase solve --scramble-length 30

# 동일한 셔플을 재현
./build/stemil-rephase solve --seed 2026 --scramble-length 25

# 테이블을 한 번 읽고 자동 셔플 10개를 풀이
./build/stemil-rephase solve --runs 10 --rephase-ms 100

# 재현 가능한 셔플 3개: seed 2026, 2027, 2028
./build/stemil-rephase solve --seed 2026 --runs 3
```

```bash
# Ctrl+C를 누를 때까지 새 셔플을 계속 풀이 (진행 중인 큐브는 끝까지 완료)
./build/stemil-rephase solve --runs 0 --rephase-ms 100
```

`--runs 0`은 Ctrl+C(SIGINT/SIGTERM)까지 반복합니다. `--runs`가 1이 아니면 종료 시 stderr에 요약(큐브·해결 수, 평균 stemil/최종 HTM, rephase 단축 수·합계, 평균 탐색 시간)을 출력합니다. 요약 뒤에는 **rephase로 수를 줄인 사례**를 큐브 번호·seed·전후 HTM·셔플·stemil 해·rephase 해와 함께 나열하며, 없으면 `없음`으로 표시합니다. 다중 rephase 모드는 통계 대신 큐브·해결 수만 집계하고, 단축 사례는 window별로 각각 나열합니다.

rephase로 수가 줄어든 사례는 **발견 즉시 `output.txt`에 이어 붙입니다**(실행 위치 기준, 기존 내용은 유지). 시각·큐브 번호·seed·전후 수·셔플·stemil 해·rephase 해·설정을 기록하며, 단축 사례가 없으면 파일을 만들지 않습니다. `--runs`와 무관하게 단일 실행에서도 동작하고, Ctrl+C로 중단해도 이미 기록된 내용은 남습니다. 경로는 `--output-file PATH`로 바꿀 수 있습니다.

`--seed`는 `0..4294967295`, `--scramble-length`는 `1..1000`, `--runs`는 `0..10000`(0은 무한 반복)입니다. 큐브 i의 seed는 첫 seed+i이며 32비트 범위에서 순환합니다. 같은 seed와 길이는 같은 셔플을 만듭니다. 시간 제한 탐색이므로 최종 해와 방문 node 수는 CPU 상태와 trace 출력 비용에 따라 달라질 수 있습니다. 자동 셔플 옵션은 명시적 scramble/facelet 입력이나 `batch`와 함께 사용할 수 없습니다.

## Stemil 탐색 시간 여러 개 비교

```bash
./build/stemil-rephase solve \
  --stemil-max-ms 500,1000,2000
```

하나의 무작위 seed와 셔플을 만든 뒤 같은 큐브에서 stemil을 500 ms, 1000 ms, 2000 ms 조건으로 각각 처음부터 독립 실행합니다. 탐색 시간이 긴 실행은 더 많은 후보를 검사하므로 더 짧은 해를 찾을 가능성이 커집니다. 각 실행이 끝나는 즉시 `[stemil 1/3]` 형식으로 Phase 1, Phase 2, 전체 solve의 move·시간·메모리를 출력합니다.

`--stemil-max-ms`를 사용하면 `--budget-ms`, 기존 `--rephase-ms`, `--repair-fraction`, `--adaptive`는 함께 사용할 수 없습니다. 각 값은 `0` 초과, `60000` ms 이하여야 합니다. `--json`에서도 완료되는 즉시 `result_type: "stemil"` JSONL 한 줄을 출력합니다.

다중 stemil과 다중 rephase는 함께 사용할 수 있습니다.

```bash
./build/stemil-rephase solve \
  --stemil-max-ms 500,1000,2000 \
  --windows 6,7,8 \
  --rephase-max-ms 100,200,300 \
  --rephase-min-ms 10
```

위 명령은 stemil 결과 3개를 먼저 출력합니다. 그중 **설정 시간이 가장 큰 2000 ms stemil 결과만** rephase 기준으로 사용합니다. Rephase는 window 3개 × 최대시간 3개 × 최소시간 1개이므로 총 9회 실행하며, 각 결과도 완료되는 즉시 출력합니다. 전체 탐색 상한은 대략 `stemil 시간의 합 + 모든 rephase 조합의 최대시간 합`입니다. Pruning table과 짧은 변환 DB는 한 번만 로딩합니다.

## Window와 rephase 시간

### 같은 stemil 해에 여러 rephase를 독립 실행

```bash
./build/stemil-rephase solve \
  --windows 6,7,8 \
  --rephase-max-ms 100,200,300 \
  --rephase-min-ms 5,10
```

Seed를 생략하면 실행할 때마다 새 무작위 seed와 25수 셔플을 만듭니다. Stemil Two-Phase는 한 번만 실행하고, 그 결과를 변경하지 않은 기준 해로 삼아 다음 곱집합을 서로 독립적으로 실행합니다.

- window: 6, 7, 8의 3개
- 최대시간: 100, 200, 300의 3개
- 최소시간: 5, 10의 2개
- 전체: `3 × 3 × 2 = 18회`, 각 window마다 6회

각 실행은 앞 작업의 단축 결과를 다음 작업에 넘기지 않고 같은 stemil 초기해에서 시작합니다. 입력 순서는 window → 최대시간 → 최소시간 순서로 유지됩니다. 최소시간이 대응되는 최대시간보다 크면 실행 불가능한 조합이 생기므로 옵션 오류로 종료합니다.

다중 rephase만 사용하는 경우 `--budget-ms`는 stemil 탐색에 전부 사용됩니다. 각 rephase 최대 시간은 그 뒤에 조합별로 더해집니다. 기존 `--rephase-ms` 방식은 총 예산 안에서 한 번의 결합 rephase를 실행하는 호환 모드이며 두 시간 방식을 한 명령에 섞을 수 없습니다.

최소 시간은 각 exact window 시도가 너무 짧게 잘리지 않도록 주는 최소 **탐색 배정량**입니다. 내부 기본 slice 8 ms보다 큰 값이면 해당 값으로 slice를 늘리되 각 rephase의 최대 시간은 넘지 않습니다. 모든 후보 구간의 최단거리 또는 개선 불가능성이 먼저 증명되면 기다리기 위한 sleep 없이 일찍 종료하므로 실측 탐색 시간은 설정한 최소값보다 짧을 수 있습니다.

상세 JSON이 필요하면 같은 명령에 `--json`을 붙입니다. Stemil은 `result_type: "stemil"`, rephase는 `result_type: "rephase"`인 JSONL 한 줄로 완료 즉시 출력됩니다. Rephase 행에는 전체 조합에서의 `rephase_index`, `rephase_total`, 기준 stemil index·시간, window와 최소·최대시간이 기록됩니다.

### 기존 결합 rephase

```bash
# 8수 구간만 검사, 총 2초 중 rephase에 최대 300 ms
./build/stemil-rephase solve \
  --window 8 --budget-ms 2000 --rephase-ms 300

# 여러 길이 선택, 한 구간당 최대 12 ms
./build/stemil-rephase solve \
  --windows 4,6,8,10 --window-slice-ms 12 \
  --budget-ms 1500 --rephase-ms 200

# 동일 seed의 수선 없는 비교 기준
./build/stemil-rephase solve --seed 2026 --rephase-ms 0
```

| 옵션 | 기본값 | 의미 |
|---|---|---|
| `--budget-ms` | `1000` | 테이블 로딩 후 한 큐브의 총 탐색 시간, 0 초과·60000 ms 이하 |
| `--stemil-max-ms` | 미지정 | 동일 큐브에서 독립 실행할 stemil 최대 탐색 시간 목록, 각각 0 초과·60000 ms 이하 |
| `--rephase-ms` | 미지정 | 총 예산 안에서 수선에 예약할 최대 ms. 0은 비활성화, 총 예산보다 작아야 함 |
| `--rephase-max-ms` | 미지정 | 모든 window와 조합할 최대시간 목록 |
| `--rephase-min-ms` | `0` | 모든 window·최대시간과 조합할 최소 exact-attempt 배정 목록 |
| `--repair-fraction` | `0.1` | 고정 ms 미지정 시 수선 비율, 0 이상·1 미만 |
| `--windows` | `4,6,8,10` | HTM 구간 길이 목록, 각각 2..10 |
| `--window` | 미지정 | 단일 길이. `--windows`와 동시에 지정할 수 없음 |
| `--window-slice-ms` | `8` | 한 구간의 exact 탐색 상한 ms, 0 초과 |
| `--repair-mode` | `boundary+sliding` | 경계 후 전체 sliding / 경계만 `boundary` / 전체만 `sliding` |
| `--boundary-fraction` | `0.5` | 두 단계 사용 시 경계 수선에 주는 최대 rephase 비율, 0 초과·1 미만 |
| `--no-repeat` | 비활성화 | 첫 성공 교체 후 수선 종료 |
| `--adaptive` | 비활성화 | 최초 해 길이에 따라 기존 비율 조정 |
| `--table-depth` | `4` | 짧은 전체 변환 DB의 BFS 반경, 0..5 |
| `--repair-memory-mib` | `64` | 변환 DB와 구성 queue의 메모리 제한 |

`--rephase-ms 100 --budget-ms 1000`은 **900 ms Two-Phase + 최대 100 ms rephase**이며 1000 ms에 100 ms를 추가하는 설정이 아닙니다. Two-Phase가 일찍 끝나더라도 rephase 상한이 늘어나지 않습니다. 남은 총 예산과 지정한 수선 상한 중 더 작은 값으로 제한합니다. 고정 `--rephase-ms`는 명시적 `--repair-fraction` 또는 `--adaptive`와 함께 사용할 수 없습니다.

두 단계와 `--boundary-fraction 0.5`이면 100 ms 중 경계는 최대 50 ms를 받고 sliding이 나머지를 받습니다. 경계가 20 ms만 사용하면 sliding은 남은 최대 80 ms를 사용할 수 있습니다. 단독 모드는 해당 단계에 전체 rephase 예산을 줍니다.

기존 결합 모드에서 window는 중복을 제거하고 작은 길이부터 검사합니다. `--windows 10,4,8,4`는 `[4,8,10]`으로 실행합니다. 다중 모드는 조합과 출력 순서를 보존하기 위해 window·시간의 입력 순서와 중복을 유지합니다. 현재 해보다 큰 window는 전체 해 길이로 줄이며, 한 rephase 내부에서 실제 길이가 같은 구간은 한 번만 검사합니다. HTM은 90도와 180도 회전을 모두 1수로 셉니다.

더 짧은 동치 구간을 교체하면 작은 window부터 다시 검사합니다. 모든 예정 구간에서 개선이 없으면 남은 시간이 있어도 종료합니다. 구간 timeout을 더 짧은 해가 없다는 증거나 최소성 증명으로 취급하지 않습니다. Node 사이 시간 확인과 OS scheduling으로 작은 시간 초과는 가능하며 hard real-time 제한은 아닙니다.

```bash
./build/stemil-rephase solve --repair-mode boundary --rephase-ms 100
./build/stemil-rephase solve --repair-mode sliding --rephase-ms 100
# 최초 해 21수 이상: 비율 절반, 18수 이하: 최대 0.5까지 두 배
./build/stemil-rephase solve --repair-fraction 0.1 --adaptive
```

## Trace와 상세 JSON

```bash
# 진행 JSONL은 stderr, 간단한 결과는 stdout
./build/stemil-rephase solve --trace > result.txt 2> progress.log

# 자동화용 상세 JSON/JSONL이 필요할 때만 --json 사용
./build/stemil-rephase solve --json > result.jsonl

# trace만 새 파일에 저장; --trace-file 자체로 trace 활성화
./build/stemil-rephase solve --trace-file results/my-run.trace.jsonl \
  > results/my-run.txt
```

`--trace-file`은 **기존 파일을 덮어쓰지 않습니다.** 같은 이름이 있으면 새 이름을 지정해야 합니다. 원본 stemil 디렉터리를 trace 저장 위치로 지정할 수도 없습니다. `--trace-file`에는 trace JSONL만 들어갑니다. 기본 실행에서는 테이블 로딩 진행 메시지도 출력하지 않습니다.

Trace에는 `shuffle`, `setup_start/end`, 최초 `initial_solution`, 후보 개선 `candidate_improved`, `two_phase_end`, `stage_start/end`, `window_start/end`, 실제 교체 `repair_applied`, `solve_end`가 기록됩니다. `stage`로 `two_phase`, `boundary`, `sliding`을 구분하고, `index`로 반복 실행의 큐브를 구분합니다. Setup index는 -1입니다. Window `[begin,end)`는 현재 해의 0-based move 구간입니다. Trace의 `elapsed_ms`는 프로그램 시작부터, 결과의 `elapsed_ms`는 해당 큐브의 로딩 후 탐색 시간입니다. Trace 출력에 쓰는 시간도 탐색 시간에 포함됩니다.

기본 출력은 읽기 쉬운 한국어 항목으로 결과가 완성될 때마다 flush합니다. 다중 실행의 `--json`은 결과 하나당 JSONL 한 줄을 즉시 출력하므로 긴 실행 중간에도 앞선 결과를 확인할 수 있습니다.

| 상세 JSON 필드 | 의미 |
|---|---|
| `result_type` | 다중 실행의 `stemil` 또는 `rephase` 결과 종류 |
| `input_mode`, `seed`, `scramble`, `scramble_length` | 입력 방식·실제 셔플·재현 seed |
| `sequence`, `htm` | 최종 해와 HTM 길이 |
| `initial_sequence`, `initial_htm` | Two-Phase 후보 개선을 마친 뒤, rephase 직전 해 |
| `phase1_sequence/htm/ms/memory_bytes` | 최종 stemil 해의 Phase 1 구간과 누적 Phase 1 탐색 시간·전용 테이블 크기 |
| `phase2_sequence/htm/ms/memory_bytes` | 최종 stemil 해의 Phase 2 구간과 누적 Phase 2 탐색 시간·전용 테이블 크기 |
| `selected_for_rephase` | 다중 stemil 중 설정 시간이 가장 커 rephase 기준으로 선택됐는지 여부 |
| `allocated_rephase_ms`, `two_phase_budget_ms` | 배분된 수선/초기 탐색 상한 |
| `repair_ms`, `rephase_ms` | 수선 진입부터 최종 검증까지의 시간; 같은 값 |
| `rephase_search_ms` | 경계·sliding 수선 단계의 실제 탐색 시간 합계 |
| `stemil_peak_rss_kib` | 원본 테이블 로딩 직후 process peak RSS |
| `rephase_peak_bytes` | rephase DB 구성 중 table+queue 최대 크기 |
| `boundary_repair`, `sliding_repair` | 단계별 구간·완료·timeout·cache hit·node·교체·절약 수수·시간 |
| `repairs` | 실제 교체 단계·구간·전후 길이 |
| `setup_ms`, `elapsed_ms` | 테이블·DB 준비 시간 / 큐브 탐색 시간 |
| `tables`, `stemil_resources` | `expanded`와 실제 읽은 원본 resources 경로 |

`solved:true`의 모든 최종 해는 입력 큐브에 다시 적용해 검증합니다. `two_phase_timed_out:true`는 후보 개선 탐색이 상한에 도달했다는 뜻일 수 있으며 그 자체로 풀이 실패는 아닙니다. `solved:false`와 종료 코드 2는 예산 안에 초기해를 얻지 못했다는 뜻입니다. 입력·파일 오류는 종료 코드 1입니다.

## 명시적 큐브 입력과 저장된 해 수선

```bash
./build/stemil-rephase solve --scramble "R U F2 L' D B2" \
  --windows 4,6,8,10 --rephase-ms 100

./build/stemil-rephase solve --facelets \
  UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB \
  --rephase-ms 100

# cubes.txt: 줄마다 URFDLB 54문자 facelet 하나, 큐브별 간단한 결과 출력
./build/stemil-rephase batch --budget-ms 1000 --rephase-ms 100 < cubes.txt

# batch 결과를 JSONL로 받을 때
./build/stemil-rephase batch --json --budget-ms 1000 --rephase-ms 100 < cubes.txt

# 저장된 해만 수선. boundary는 move 사이의 0-based index
./build/stemil-rephase repair \
  --sequence "R R' U F L B D" --boundary 7 \
  --window 4 --repair-mode boundary+sliding --rephase-ms 100
```

Facelet은 각 면을 밖에서 바라본 행 우선 **URFDLB 54문자**이고 센터도 U/R/F/D/L/B여야 합니다. 색상 수, cubie 중복, orientation 합과 parity를 검사합니다. `repair` 단독은 대형 pruning을 로딩하지 않으며, 시간 상한은 `--rephase-ms` 또는 생략 시 `--budget-ms`입니다. 위 예제는 경계에서 먼 `R R'`를 sliding이 찾아 7수에서 `U F L B D` 5수로 줄입니다.

## 기반 구현과 메모리

원본 stemil commit은 `5437b8f6f5c10b6da1857d4bf1d93000ba393063`입니다. 기존 `coordinates.cpp`, `moves.cpp`, `moving_tables.cpp`, `pruning_tables.cpp`, `search.cpp`를 그대로 빌드 입력으로 사용합니다. 원본 `phase1_search`와 pruning lookup을 직접 사용하고 Phase 2는 원본 세 거리의 최댓값을 사용합니다.

`src/tables.cpp`가 원본 ASCII 파일을 읽어 기존 전역 배열을 채웁니다. `src/solver.cpp`의 adapter가 deadline, 초기해·경계 반환, Phase-2 첫 move sentinel 처리를 관리합니다. 기존 `a.out`과 모든 탐색 결과가 동일하다고 주장하지 않습니다. 원본 파일을 패치하지 않고 이 폴더의 adapter에서 보완합니다.

| 기존 `resources/pruning_tables/` 파일 | RAM 크기 |
|---|---:|
| `phase1_pruning` | 554,273,280 bytes |
| `phase2_corner_UD_edge` | 1,625,702,400 bytes |
| `phase2_corner_UDslice_edge` | 967,680 bytes |
| `phase2_UD_edge_UDslice_edge` | 967,680 bytes |

전체 pruning의 논리 크기는 **2,181,911,040 bytes, 약 2.03 GiB**이며 실제 RAM은 moving table·런타임 등을 포함해 조금 더 필요합니다. ASCII 로딩은 측정 환경에서 약 12초가 걸립니다. 로딩·수선 DB 구성 비용은 `setup_ms`로 별도 표시하고 큐브당 `--budget-ms`에서 제외합니다. `--runs`나 `batch`로 초기화를 한 번만 할 수 있습니다.

수선용 DB는 Identity로부터 짧은 **전체 큐브 변환**을 저장하는 별도 자료구조입니다. 기본 깊이 4는 46,741 상태, 상주 table 4 MiB, 구성 중 table+queue 약 6.50 MiB입니다. DB와 역방향 제한 탐색으로 동치 변환을 찾고, **엄격하게 짧아지고 전체 변환을 보존하는 교체만** 적용합니다. 이 DB는 원본 pruning을 대신하지 않습니다. 깊이 5에는 메모리 상한을 올려야 합니다(예: `--table-depth 5 --repair-memory-mib 128`).

수선 후 해가 Two-Phase 구조를 유지할 필요는 없습니다. `final_focal_index`는 다음 경계 주변 구간 선택 중심이며 새로운 Phase-1/2 경계를 보증하지 않습니다. Local window 최소성과 전체 해의 global optimal은 서로 다른 조건이므로 임의의 큐브에 대한 전체 최소해 보장은 없습니다.

원본 위치가 바뀌면 빌드에 `make STEMIL_ROOT=/path/to/two-phase-solver`, 실행에 `--resources /path/to/two-phase-solver/resources`를 지정합니다. 기본 resources는 상대 경로이므로 위 예제는 이 프로젝트 폴더에서 실행합니다.

## 검증과 실험 재현

```bash
make test
make test-cli
make sanitize

# 같은 legal-state corpus, 총 1초, 고정 rephase 100 ms로 경계/경계+sliding 비교
python3 tools/benchmark.py \
  --input results/uniform-100/corpus.jsonl \
  --budget-ms 1000 --rephase-ms 100 \
  --windows 4,6,8,10 --repair-modes boundary,boundary+sliding \
  --out results/my-comparison
```

원본 pruning의 얕은 거리 BFS 비교, 그룹·facelet 연산, exact repair 거리, splice 동치성, 고정 시간 상한, 단계·cache 동작, 자동 셔플·CLI·trace를 검증합니다. Benchmark의 모든 초기·최종 해는 별도의 정수 3D 회전 기반 54-sticker 모델로 검증합니다. 변경 내역과 측정은 [REPORT.md](REPORT.md)에 있습니다. 이전 구현은 [reports/pre-sliding-source/](reports/pre-sliding-source/)에 보존했고 이전 compact 실험은 [reports/compact-experiment.md](reports/compact-experiment.md)에 구분해 기록했습니다.
