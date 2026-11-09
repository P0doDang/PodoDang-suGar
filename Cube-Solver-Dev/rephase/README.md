# Kociemba Phase-Boundary Exact Repair

`kociemba_twophase_업그레이드.pdf`의 절차를 구현했다. Two-Phase로 초기 해를 구하고 실제 Phase 경계 주변의 6·8수 구간을 잘라, 같은 변환을 만드는 더 짧은 수순을 양방향 BFS로 찾는다. 성공하면 해당 구간을 교체하고 다시 검증한다. 시간이나 메모리 한도에 도달하면 마지막으로 검증된 해를 반환한다.

수순 길이는 **HTM**이다. `R`, `R2`, `R'` 모두 1수로 센다. **검사한 window의 최단 수순**과 **전체 큐브의 전역 최적해**는 다르다. 이 구현은 전역 optimality를 증명하지 않는다.

## 바로 실행

현재 디렉터리에 `.venv`와 필요한 패키지를 준비해 두었다. Linux의 Python 3.10 이상과 `g++` 또는 `clang++`가 필요하다. C++ 탐색 엔진은 첫 실행에 자동 컴파일하며 이 준비 시간은 repair 예산과 별도로 표시한다.

```bash
# 스크램블 입력 없이 자동으로 20수 생성 → Two-Phase → repair
.venv/bin/python solve.py solve --window 6 8 --time-ms 100 --trace

# 자동 생성 길이와 재현용 seed 지정
.venv/bin/python solve.py solve \
  --scramble-length 25 --seed 123 --window 8 --time-ms 100 --trace

# 6수 window만, 전체 exact repair 시간 예산 50 ms
.venv/bin/python solve.py solve \
  --scramble "R U R' U' F2 D L2 B R2 U F'" \
  --window 6 --time-ms 50 --trace

# 8수 window만, 시간 예산 100 ms
.venv/bin/python solve.py solve \
  --scramble "R U R' U' F2 D L2 B R2 U F'" \
  --window 8 --time-ms 100 --trace

# 6수 → 8수 순서로 탐색, 두 크기가 합쳐서 100 ms 예산을 공유
.venv/bin/python solve.py solve \
  --scramble "R U R' U' F2 D L2 B R2 U F'" \
  --window 6 8 --time-ms 100 --trace
```

다른 환경에서 설치하려면 다음 명령을 사용한다.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

`solve`에서 `--scramble`과 `--facelets`를 생략하면 기본 20수 스크램블을 자동 생성한다. 연속해서 같은 면을 돌리는 수순은 제외한다. 생성한 스크램블과 seed를 화면 및 JSON에 기록한다. seed를 생략하면 매번 새 seed를 생성하고, 같은 seed와 길이를 지정하면 같은 스크램블을 재현한다. 시간 제한이 있는 탐색의 실행 시간이나 최종 결과까지 seed가 고정하는 것은 아니다. 생성 방식은 random-walk이며 균등 무작위 cube state 샘플링은 아니다.

`--scramble`은 완성된 큐브에 적용할 수순이다. `--facelets`로 실제 상태를 입력할 수도 있다. 각 면의 9칸을 왼쪽 위부터 행 순서로 읽고 면은 `URFDLB` 순서로 배치한다. 중심 색도 이 순서와 같아야 한다.

```bash
.venv/bin/python solve.py solve \
  --facelets DUUBULDBFRBFRRULLLBRDFFFBLURDBFDFDRFRULBLUFDURRBLBDUDL \
  --window 6 8 --time-ms 100
```

## 옵션

| 옵션 | 의미 | 기본값 |
| --- | --- | --- |
| `--random` | 자동 스크램블 생성. 입력 큐브 생략 시에는 이 플래그도 생략 가능 | 자동 |
| `--scramble-length 20` | 자동 생성할 스크램블의 HTM 길이, 1~100 | 20 |
| `--seed 123` | 자동 스크램블 재현용 seed | 매번 새 seed |
| `--window 6` / `--window 8` / `--window 6 8` | 검사할 구간 길이. 지정한 크기만 작은 순서로 탐색 | `6 8` |
| `--time-ms 100` | **전체 repair 단계**의 공유 예산. 초기 Two-Phase와 엔진 준비 시간은 별도 | 100 ms |
| `--per-window-ms 10` | 한 window의 추가 시간 상한. 항상 전체 남은 예산 이하 | 미지정 |
| `--shifts -1 0 1` | 경계 중심 구간의 시작 위치를 옮겨 여러 window 검사 | `0` |
| `--sliding` | 경계 window 이후 남은 같은 예산으로 전체 수순의 sliding window 검사 | 꺼짐 |
| `--max-states 200000` | 한 탐색에서 양쪽 visited에 저장할 총 state 수 상한 | 200,000 |
| `--base-seconds 3` | 초기 Two-Phase timeout. repair 시간과 독립적 | 3초 |
| `--max-length 24` | 기본 backend의 초기 해 길이 상한. 참고 backend에서는 조기 종료 목표 | 24 |
| `--backend kociemba` / `twophase` | 초기 해를 만드는 구현 | `kociemba` |
| `--trace` | window 크기, 위치, 결과, 시간, state 수를 즉시 stderr에 출력 | 꺼짐 |
| `--json` | stdout에 JSON 결과 출력 | 꺼짐 |
| `--output result.json` | 동일한 JSON 결과를 파일에 저장 | 미지정 |

window는 2~10 정수를 받는다. 6·8이 주 사용 범위이며 더 큰 구간은 탐색량이 급증한다. `--time-ms 0`이면 repair를 수행하지 않고 입력 해를 그대로 유지한다.

시간 제한은 `steady_clock` / `monotonic()`으로 검사하는 **협력적 제한**이다. 검사 간격, Python 호출, 검증, 메모리 해제, 운영체제 스케줄링 때문에 실제 경과 시간이 조금 초과할 수 있다. 전체 프로그램의 엄격한 wall-clock 제한은 아니다. `repair_elapsed_ms`, `base_elapsed_ms`, `engine_prepare_ms`, `wall_elapsed_ms`를 따로 기록한다.

기본 native Two-Phase의 timeout은 정수 초 단위이며 설정값을 올림한다. 참고 backend는 첫 해가 없으면 timeout 이후에도 계속 탐색하는 upstream 동작을 따른다. 이 차이는 exact repair의 ms 예산에 영향을 주지 않는다.

```bash
.venv/bin/python solve.py solve \
  --scramble "R U R' U' F2 D L2 B R2 U F'" \
  --window 6 8 --time-ms 200 --per-window-ms 25 \
  --shifts -1 0 1 --sliding --trace --output result.json
```

## 이미 구한 해에 적용

Phase 경계는 수순에 `|` 또는 `.`으로 표시하거나 `--boundary`로 지정한다. `--boundary 9`는 9수를 적용한 직후라는 뜻이다.

```bash
.venv/bin/python solve.py repair \
  --solution "R U D | U' D' F2" \
  --window 6 --time-ms 100 --trace

# 동일한 입력
.venv/bin/python solve.py repair \
  --solution "R U D U' D' F2" --boundary 3 \
  --window 6 --time-ms 100
```

이 예시는 동작을 확인하기 위해 만든 수순이다. `U`와 `D`가 교환하므로 detour를 제거해 `R F2`, 6수 → 2수가 된다. 같은 방식으로 `R U D | U' D' F2 L2 B2`는 8수 → `R F2 L2 B2`, 4수가 된다. 이 인위적 예시가 실제 Two-Phase의 평균 개선율을 의미하지는 않는다.

`repair`에서는 경계의 의미를 사용자가 지정한다. `--scramble` 또는 `--facelets`를 함께 주면 그 큐브가 풀리는지도 검사한다. 큐브를 생략하면 입력 수순의 역변환을 초기 상태로 삼아 동등성과 완성 상태를 검증한다. 입력 해가 제공된 큐브를 풀지 못하면 오류로 중단한다.

## 탐색 절차

1. 초기 Two-Phase 해와 실제 Phase cut을 받는다.
2. 선택한 크기만 정렬해 검사한다. 경계가 10수 뒤라면 6수 window는 8~13수, 8수 window는 7~14수다. 양쪽 끝에 가까우면 구간을 옮겨 정확한 길이를 유지한다. 해 자체가 window보다 짧으면 해당 크기를 건너뛴다.
3. window 직전의 상태를 `A`, 직후 상태를 `B`라고 하면, `T = A⁻¹ × B`는 window의 move transformation이다. 나머지 수순과 독립적으로 구할 수 있다.
4. `Id → T`를 **18개 모든 face move**로 탐색한다. repair 경로에 G1 통과 제약을 걸지 않는다. 6수 window는 최대 5수, 8수 window는 최대 7수까지 검사한다.
5. identity와 target에서 BFS layer를 번갈아 완전히 확장한다. 이미 완료한 양쪽 깊이의 합까지 만남이 없었다면 그 길이 이하의 경로가 없다는 뜻이다. 다음 layer에서 처음 만나면 더 짧은 길이를 배제한 최단 경로를 복원할 수 있다.
6. 더 짧은 경로를 찾으면 window의 **전체 cubie 변환**을 비교하고 교체한다. 연결부의 같은 면 회전도 합친다. 전체 초기 해와 변환이 같고 move 수가 감소했는지 확인한다.
7. 단축했으면 바뀐 구간 근처에서 선택한 작은 window부터 다시 시작한다. 남은 예산이 있으면 선택적으로 sliding window도 수행한다. 같은 수순의 탐색을 반복하지 않는다.
8. 반환 전에 초기 큐브에 최종 수순을 적용해 solved 상태를 검증한다.

교체 후에는 G1 제약을 해제했으므로 기존 cut이 여전히 Phase 1/2를 나누지 않을 수 있다. JSON의 `original_boundary`는 실제 초기 경계이고 `final_repair_anchor`는 이후 국소 탐색의 중심 위치다.

visited key는 corner permutation/orientation과 edge permutation/orientation을 모두 포함한 20바이트 상태다. 해시가 충돌해도 전체 상태를 비교한다. 경로 문맥에 의존하는 pruning 대신 BFS state 중복 제거를 사용한다. 따라서 path의 마지막 면을 무시하는 pruning으로 최단 경로를 누락하는 문제가 없다.

| 탐색 상태 | 의미 |
| --- | --- |
| `FOUND` | 더 짧은 최단 window 수순 발견 및 검증 |
| `EXHAUSTED` | 모든 더 짧은 길이 탐색 완료. 해당 window는 이미 최단 |
| `TIMEOUT` | 시간 한도로 중단. 더 짧은 경로가 없다는 증명은 아님 |
| `STATE_LIMIT` | state 수 한도로 중단. 더 짧은 경로가 없다는 증명은 아님 |

## hkociemba 참고 구현 연결

[RubiksCube-TwophaseSolver](https://github.com/hkociemba/RubiksCube-TwophaseSolver)의 `SolverThread`, 회전 conjugation, 역변환 결과 복원, 공개 `RubikTwoPhase` 패키지를 확인해 연결했다.

기본 backend는 즉시 실행할 수 있는 `kociemba==1.2.1`의 C 구현이다. 공개 `solve()`가 숨기는 실제 Phase 구분자를 native `solution(..., useSeparator=1, ...)`로 받는다. 내부 ABI를 사용하므로 버전을 고정했다. Phase 1 직후 `twist=flip=0` 및 E-slice 조건을 별도로 검증한다.

`--backend twophase`는 사용자 제공 저장소의 패키지 구현을 사용한다. 결과를 기록할 때 해당 `SolverThread`가 보유한 두 Phase 수순 길이, `rot`, `inv`를 함께 저장하며 upstream 파일을 수정하지 않는다. 역변환 탐색의 수순은 전체가 역순으로 복원되어 **Phase 2가 prefix**가 되므로, cut은 원래 Phase 2 길이로 바꾼다. 회전된 경우 subgroup 축도 함께 추적한다.

참고 구현의 최초 테이블 생성은 약 80 MB의 디스크와 하드웨어에 따라 30분 이상이 필요하다. 일반 `solve` 실행에서 자동 생성하지 않는다. 다음 명령으로 명시적으로 준비한다.

```bash
.venv/bin/python -m pip install -r requirements-reference.txt
.venv/bin/python solve.py init-twophase

.venv/bin/python solve.py solve \
  --backend twophase \
  --scramble "R U R' U' F2 D L2 B R2 U F'" \
  --max-length 19 --base-seconds 2 \
  --window 6 8 --time-ms 100 --trace
```

기본 테이블 위치는 이 프로젝트의 `twophase_tables/`다. 이미 생성한 테이블이 있다면 두 명령에 `--table-dir /정확한/테이블/디렉터리`를 지정한다. 다른 프로젝트의 테이블을 변경하지 않고 새로운 세트를 원하면 별도 디렉터리를 사용한다. 참고 구현은 작은 패키지까지 설치했지만 이번 검증에서 대형 테이블을 생성하거나 실제 reference solve를 실행하지 않았다.

## Python에서 사용

```python
from boundary_repair import (
    RepairConfig, format_moves, initial_solution,
    parse_moves, repair_solution, transform,
)

cube = transform(parse_moves("R U R' U' F2 D L2 B R2 U F'"))
base = initial_solution(cube, backend="kociemba")
result = repair_solution(
    base.moves,
    base.boundary,
    config=RepairConfig(windows=(6, 8), time_limit_ms=100),
    initial_state=cube,
)
print(format_moves(result.moves), result.saved_moves, result.verified)
```

`exact_search(target, max_depth, time_limit, max_states)`도 별도로 사용할 수 있다. 이 저수준 함수의 시간 단위는 **초**다. `RepairConfig`와 CLI는 **ms**다. 반복 측정 전 `prepare_engine()`을 호출하면 컴파일/로딩 비용을 측정 밖으로 뺄 수 있다.

## 검증과 현재 측정 결과

```bash
.venv/bin/python -m unittest discover -s tests -v

.venv/bin/python benchmark.py \
  --count 100 --seed 2026 --window 6 8 --time-ms 100 \
  --output examples/benchmark_100.json
```

20개 테스트를 통과했다. 독립적인 Python 단방향 BFS의 depth≤3 상태 3,502개 중 depth≤2 전체와 depth 3 표본을 비교해 최단 거리를 확인했다. 추가로 4~7수의 경로 복원 및 더 짧은 길이의 exhaustive 결과, 6·8수 교체, 실제 Two-Phase 경계와 end-to-end solve, 시간 초과·메모리 한도 fallback, sliding, 입력 오류, CLI를 검사했다. 별도 참고 구현의 cubie 연산과도 무작위 수순 30개를 비교했다. 자동 스크램블의 기본·선택 길이, seed 재현, 생성 큐브의 solve, 충돌하는 입력 옵션도 검증했다.

현재 환경에서 seed 2026의 20수 random-walk scramble 100개를 측정했다. 이 표본은 균등 무작위 cube state가 아니다.

| 설정 | 평균 초기 HTM | 평균 최종 HTM | 단축된 큐브 | 평균 repair 시간 |
| --- | ---: | ---: | ---: | ---: |
| 경계 중앙 6·8수, 100 ms 예산 | 20.69 | 20.69 | 0 / 100 | 약 21.72 ms |
| 경계 이동 -1/0/1 + sliding, 100 ms 예산 | 20.69 | 20.69 | 0 / 100 | 약 100.75 ms |

모든 반환 해가 큐브를 풀고 길이가 증가하지 않았다. **이 표본에서는 평균 move 수 감소가 관측되지 않았다.** PDF의 연구 가설이나 평균 개선율, optimal hit-rate 증가는 확인된 사실이 아니다. 기본 exact search 기능은 인위적 단축 예시와 독립 최단 거리 비교로 검증했지만, 실용적인 수순 개선 효과는 더 조사해야 한다. 전역 optimal oracle가 없으므로 benchmark의 `global_optimal_hit_rate`는 `null`이다.

원시 측정은 `examples/benchmark_100*.json`, 단축 예시는 `examples/window6.json`, `examples/window8.json`, 시간 한도 예시는 `examples/timeout.json`에 저장했다. 실행 시간은 컴퓨터와 동시 실행 부하에 따라 달라진다.

## 코드 구조

```text
solve.py                         CLI / JSON / 탐색 로그
benchmark.py                     같은 초기 해에 대한 baseline vs repair 측정
boundary_repair/cube.py           cubie 변환, 입력 검증, facelets 변환
boundary_repair/backends.py       실제 Phase cut을 보존하는 Two-Phase 연결
boundary_repair/scramble.py       자동 및 benchmark 스크램블 생성
boundary_repair/repair.py         window 선택, 공유 예산, 교체, sliding
boundary_repair/exact.py          C++ 엔진 빌드 및 ctypes 호출
boundary_repair/native/search.cpp 전체 cubie 상태의 양방향 BFS
tests/test_solver.py              독립 최단 거리와 동작 검증
```

원본 PDF와 upstream 패키지 파일은 변경하지 않았다.
