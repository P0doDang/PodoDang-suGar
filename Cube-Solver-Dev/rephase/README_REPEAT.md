# 반복 실행 및 Phase 계측

`repeat_solve.py`는 스크램블을 자동 생성하고 Two-Phase와 boundary repair를 반복한다. **기존 Two-Phase 해보다 1수 이상 짧고 큐브 검증을 통과한 결과만 stdout에 출력한다.** 새 코드 전용 계측이며 `solve.py`, `benchmark.py`, 기존 `boundary_repair/` 및 설치한 패키지 원본을 수정하지 않는다.

## 실행

```bash
# 1,000회 반복. 단축된 결과만 상세 출력
.venv/bin/python repeat_solve.py \
  --count 1000 --window 6 8 --time-ms 100

# window 8만 검사. 시행마다 repair 예산 최대 10초
.venv/bin/python repeat_solve.py \
  --count 1000 --window 8 --time-ms 10000

# 단축 사례 1개를 찾으면 종료. 최대 1,000회까지만 시도
.venv/bin/python repeat_solve.py \
  --count 1000 --stop-after-hits 1 --window 6 8 --time-ms 100

# Ctrl+C로 멈출 때까지 반복
.venv/bin/python repeat_solve.py \
  --count 0 --window 6 8 --time-ms 100
```

이 파일 뒤에는 `solve`라는 subcommand를 붙이지 않는다. window는 앞뒤 합계 길이다. `--time-ms`는 **각 시행 전체 repair의 최대 예산**이며, 지정한 구간의 검사가 끝나면 더 일찍 반환한다.

단축이 없으면 stdout은 비어 있을 수 있다. 기본적으로 100회마다 진행 상황을 stderr에 출력하고, 종료 시 검증 횟수와 단축 사례 수를 표시한다. 마지막 요약에는 **단축이 발생한 시행 번호, 각 시행의 단축 move 수, 성공한 window 교체 횟수**와 누적 합계를 함께 출력한다. 단축 사례가 없으면 `단축이 발생한 시행: 없음`으로 표시한다. 이 요약은 Ctrl+C와 `--stop-after-hits` 종료에도 출력되며, JSONL stdout을 방해하지 않도록 stderr로 보낸다. 매 시행 진행을 보려면 `--progress-every 1`, 중간 진행을 생략하려면 `--progress-every 0`을 사용한다. 기존 100개 표본에서는 실제 단축이 관측되지 않았으므로, 반복 횟수를 늘리는 것만으로 단축 사례가 발견된다고 보장할 수는 없다.

```bash
# 같은 스크램블 집합으로 재실행 및 경계 위치 확장
.venv/bin/python repeat_solve.py \
  --count 100 --seed 123 --scramble-length 20 \
  --window 6 8 --time-ms 200 --shifts -1 0 1 --sliding \
  --progress-every 1

# 단축된 결과만 JSONL 파일에 추가 저장
.venv/bin/python repeat_solve.py \
  --count 1000 --window 6 8 --time-ms 100 \
  --json --output repeat_hits.jsonl
```

첫 시행의 seed는 `--seed`, 다음은 `seed+1`이다. seed를 생략하면 첫 seed를 무작위로 생성해 stderr에 표시한다. 결과에 기록한 seed를 `--count 1 --seed <값>`으로 지정하면 해당 스크램블을 재현할 수 있다. window, 시간 상한 등 탐색 옵션도 함께 맞춰야 한다. 시간 제한 때문에 최종 결과나 실행 시간까지 항상 같다는 보장은 없다.

## 상세 출력에 포함되는 값

단축 사례에는 원래·최종 수순, 변경된 window, 검증 결과와 다음 표를 출력한다.

| 단계 | 시간 | 수순 수 | 메모리 |
| --- | --- | --- | --- |
| Phase 1 | native 전체 시간에서 Phase 2 누적 시간을 뺀 나머지 | 초기 Phase 1의 HTM 길이 | Phase 1 활성 중 관측최대 process RSS |
| Phase 2 | 실제 Phase 2 진입·복귀 사이 시간의 누적 합 | 초기 Phase 2의 HTM 길이 | Phase 2 활성 중 관측최대 process RSS |
| Two-Phase 합계 | native 초기 해 탐색 시간 | 초기 해의 전체 HTM 길이 | 두 Phase의 관측최대 RSS 중 최대 |
| Exact repair | 각 window의 exact-search 호출 시간 합계 | 전체 해에서 줄어든 수 | repair 단계 활성 중 관측최대 process RSS |
| 전체 solve | Two-Phase + Exact repair 시간 | repair 후 전체 HTM 길이 | 활성 단계 전체의 관측최대 process RSS |

`pure_solve_ms`는 **native Two-Phase 시간 + 각 exact-search 호출 경과 시간의 합**이다. 스크램블 생성, 프로세스 시작, 모듈·테이블 로딩, 최초 컴파일, Python의 window 구성·변환 검증 및 출력 시간은 제외한다. native 호출 자체의 작은 내부 초기화·입력 확인과 계측 오버헤드는 포함한다. 계측 없는 실행 시간을 추정해서 출력하지 않는다.

별도로 `preparation_ms`, `repair_stage_ms`, `trial_wall_ms`를 기록한다. `repair_stage_ms`는 Python의 window 구성·검증을 포함한 실제 repair 단계 시간이고, `trial_wall_ms`는 부모가 프로세스를 시작하기 전부터 결과를 받을 때까지의 시간이다. 전자는 순수 exact-search 합계보다 길 수 있고, 후자는 프로세스 시작·준비 시간까지 포함하므로 더 길다.

### Phase 계측 기준

기본 `kociemba==1.2.1` native 구현의 `totalDepth()`는 Phase 1 후보가 subgroup에 도달했을 때 Phase 2를 시도한다. 새 계측용 빌드에서는 이 함수의 진입·복귀만 감싸 `CLOCK_MONOTONIC`으로 측정한다. Phase 2는 여러 번 실패하고 다시 시도할 수 있으므로, **최종 경로의 한 번뿐인 Phase 2가 아니라 해당 큐브를 푸는 동안의 모든 Phase 2 시도**를 합산한다. `phase2_search_calls`도 출력한다. Phase 1 시간에는 나머지 native 검색·좌표 계산·제어 비용이 포함된다.

계측용 빌드는 설치 패키지의 C 소스를 읽고 임시 복사본에서 함수 정의 하나의 이름을 변경한다. 원래 탐색 순서, pruning, 길이 상한, Phase separator는 그대로 사용한다. 첫 실행에 별도의 `.so`를 `repeat_metrics/native/`에 생성하며, 공유 라이브러리 내부의 심볼이 자체 계측 함수에 연결되도록 빌드한다. 원본 패키지 소스와 테이블은 수정하지 않는다. 처음 빌드하려면 `gcc` 또는 `clang`이 필요하다. 이 새 runner는 계측을 검증한 **C kociemba backend**를 사용한다.

Phase별 move 수는 **초기 Two-Phase 수순 기준**이다. Repair가 G1 제약 없이 경계를 교체하므로 최종 수순을 다시 진짜 Phase 1·2로 나눌 수 있다고 가정하지 않는다. 최종 전체 move 수와 단축 수를 따로 표시한다.

### 메모리 계측 기준

매 시행을 새 Python 프로세스에서 실행한다. 각 시행에서 테이블과 탐색 엔진을 준비한 후 native 관측 스레드를 시작한다. 관측 스레드는 현재 활성 Phase를 확인하고 `/proc/self/statm`의 process RSS를 기본 **0.5 ms 간격**으로 읽는다. 각 단계의 첫 진입과 Two-Phase·repair 종료 시에도 관측하며, 단계가 바뀌는 도중 읽은 표본은 제외한다.

**RSS는 해당 프로세스가 점유한 resident memory**다. 공유 탐색 테이블, Python 런타임, 관측 스레드와 allocator가 유지하는 메모리도 포함한다. 특정 Phase만의 독립적인 heap 할당량은 아니다. Phase 1과 Phase 2의 값이 같을 수 있으며, 단계별 RSS를 합산하면 안 된다. 전체 메모리는 각 활성 단계의 관측값 중 최대다. 준비된 상태의 `baseline_rss_bytes`와 단계별 표본 수도 JSON에 기록한다.

짧은 메모리 peak는 표본 사이에서 발생해 관측되지 않을 수 있다. 따라서 출력 라벨은 **관측최대 RSS**이며 실제 peak의 정확한 상한이라는 주장은 하지 않는다. 실행되지 않았거나 관측되지 않은 단계는 `null` / `미관측`으로 표시한다. `--sample-ms 0.1`처럼 간격을 줄이면 관측 빈도가 증가하면서 계측 오버헤드도 증가한다.

## 주요 옵션

| 옵션 | 기본값 | 의미 |
| --- | --- | --- |
| `--count` | 1000 | 최대 시행 수. 0이면 Ctrl+C까지 반복 |
| `--stop-after-hits` | 0 | 단축 사례 N개 후 종료. 0이면 별도 제한 없음 |
| `--seed` | 무작위 | 첫 시행 seed. 이후 seed+1 |
| `--scramble-length` | 20 | 자동 스크램블 HTM 길이, 1~100 |
| `--window` / `--windows` | 6 8 | 선택한 window 크기 |
| `--time-ms` | 100 | 시행별 공유 repair 예산 |
| `--per-window-ms` | 미지정 | 각 window의 추가 상한 |
| `--shifts` | 0 | 경계 중앙의 시작 위치 이동량 |
| `--sliding` | 꺼짐 | 남은 예산으로 전체 수순 window 검사 |
| `--max-states` | 200000 | 한 exact-search의 저장 상태 수 상한 |
| `--base-seconds` | 3 | native 초기 해 timeout; 정수 초로 올림 |
| `--max-length` | 24 | native 초기 해 길이 상한 |
| `--sample-ms` | 0.5 | RSS 표본 간격, 0.1~100 ms |
| `--trial-timeout` | 자동 | 자식 프로세스 전체 wall-clock 안전 상한, 초 |
| `--progress-every` | 100 | 진행 출력 간격. 0이면 중간 출력 생략 |
| `--json` | 꺼짐 | 단축된 해만 JSONL로 stdout 출력 |
| `--output` | 미지정 | 단축된 해만 JSONL에 추가 저장 |

시행 오류는 stderr에 표시하며 연속 3회 실패하면 종료한다. Ctrl+C 및 시행 timeout 시 실행 중인 자식 프로세스를 정리한다. stdout과 JSONL에는 검증 실패나 단축 0수 결과를 기록하지 않는다.

## 검증

```bash
.venv/bin/python -m unittest discover -s tests -v
```

기존 20개와 새 runner 7개, 총 27개 테스트를 실행한다. 새 검증은 원래 native 수순·Phase 경계와의 일치, Phase별 시간 합, 전체 수순과 큐브 검증, repair 포함 측정값, RSS 최대값 관계, 출력 필터·상세 형식, seed 진행, 단축 개수 종료 조건, 실제 반복 CLI, timeout 시 자식 프로세스 정리를 다룬다. 단축 출력의 경계 조건은 출력용 unit fixture로 검사하며 이를 실제 무작위 단축 사례라고 주장하지 않는다.

참고: [사용자 제공 Two-Phase 기준 구현](https://github.com/hkociemba/RubiksCube-TwophaseSolver). 새 runner는 기존 프로젝트의 `kociemba==1.2.1` 초기 해와 동등한 C 탐색에 계측을 덧붙인다.
