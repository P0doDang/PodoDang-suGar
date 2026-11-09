# cube-solver

## English summary

Rubik's Cube solvers built around **Kociemba's two-phase algorithm**, plus a post-processing step I call **rephase**: after Two-Phase returns a solution, windows around the phase boundary (and then the whole solution, sliding) are re-solved exactly, and a window is replaced only if a strictly shorter equivalent sequence exists, so the move count never grows.

- `rephase/` — Python/C implementation on top of the pip `kociemba` solver.
- `stemil-rephase/` — C++ implementation on top of the third-party [stemil two-phase-solver](https://github.com/stemil01/two-phase-solver) (large pruning tables), extended with 6-way orientation search, candidate rephase, and an exact (IDA\*) solver used as a reference.
- `benchmarks/` — same-seed comparison of the 6-way solver against original stemil, kociemba, kociemba with symmetry, and an optimal solver, on 200 scrambles where rephase actually shortened the solution.

Result (200 cases, 30-move scrambles; cases were *selected* because rephase succeeded, see caveats): 6-way+rephase averages **17.52** moves vs 20.05 (stemil), 20.89 (kociemba), 20.55 (kociemba sym); in 85/200 cases its length equals a proven lower bound, i.e. it is optimal.
Third-party code is **not** included here; see [NOTICE.md](NOTICE.md) for exactly what is mine and what is not.

---

## 소개

Kociemba two-phase로 푼 해에서 **Phase 1/2 경계 주변의 짧은 구간(window)을 정확하게 다시 풀어**, 같은 변환을 만드는 더 짧은 수순이 있을 때만 교체하는 후처리(rephase)를 구현하고 측정한 프로젝트입니다. 교체는 더 짧을 때만 일어나므로 해의 수(HTM)가 늘어나는 일은 없습니다. 경계 수선 뒤에는 해 전체로 window를 밀면서(sliding) 같은 검사를 합니다. 설계는 [docs/design.md](docs/design.md)에 있습니다.

## 저장소 지도

| 폴더 | 내용 | 구분 |
|---|---|---|
| [`rephase/`](rephase) | Kociemba two-phase + boundary repair (Python/C). 사용법은 [rephase/README.md](rephase/README.md) | **내 작업** |
| [`stemil-rephase/`](stemil-rephase) | stemil 위의 boundary·sliding rephase (C++), 6방향 탐색, 후보 rephase, 최적해 검증, 비교 harness | **내 작업** (원본 헤더·테이블에 의존) |
| [`benchmarks/`](benchmarks) | 200건 비교 결과(`compare-200/`)와 이전 실험 결과(`earlier/`) | **내 작업** |
| [`docs/design.md`](docs/design.md) | 설계 노트 | **내 작업** |
| [`third_party/stemil`](third_party) | stemil two-phase-solver (submodule, `5437b8f`) | **타인 원본** |

진화 순서는 git 이력에 그대로 있습니다: `rephase` → `stemil-rephase` 기본판 → 6way 확장 → 비교 실험. 6way 옵션을 끄면(`--orientations 1 --candidates 1`, 기본값) 기본판과 같은 경로로 동작합니다.

### 출처 (Credits & Attribution)

stemil, Kociemba two-phase 알고리즘, pip `kociemba`, `RubikTwoPhase`는 **타인 원본**입니다. 코드는 저장소에 복사하지 않고 submodule 또는 의존성으로만 연결했습니다. 원본에서 파생된 파일(`stemil-rephase/compare/stemil_orig.cpp`)과 모든 구분은 [NOTICE.md](NOTICE.md)에 있습니다.

## 빠른 시작

```bash
git clone --recursive <this-repo-url> cube-solver && cd cube-solver
```

### rephase (Python)

```bash
cd rephase
python3 -m venv .venv && .venv/bin/python -m pip install -r requirements.txt
.venv/bin/python solve.py solve --window 6 8 --time-ms 100 --trace   # 자동 20수 scramble → Two-Phase → repair
```

### stemil-rephase (C++)

stemil의 pruning table(약 6 GB 파일, 실행 시 약 2 GiB RAM)이 필요합니다. 저장소에는 포함되어 있지 않으므로 **원본 README 절차대로** `third_party/stemil`에서 한 번 생성합니다. 이 저장소의 프로그램은 테이블을 읽기만 하며 생성하지 않습니다.

```bash
cd stemil-rephase
make -j4                                    # build/stemil-rephase, build/optimal, ...
./build/stemil-rephase solve --seed 2026 --scramble-length 30 \
  --orientations 6 --phase1-mode ida --budget-ms 600 --rephase-ms 100 --windows 8
make test && make test-cli                  # 테스트
```

테이블을 다른 곳에 두었다면 `--resources <dir>`로 지정합니다. 옵션 전체는 [stemil-rephase/README.md](stemil-rephase/README.md)와 [README-6way.md](stemil-rephase/README-6way.md)에 있습니다.

## 결과: 같은 seed 200건 비교

6way로 seed를 훑어 **rephase가 해를 실제로 줄인 큐브**만 모았고(82,224개 seed 중 200건, 약 0.24%), 같은 scramble(30수)을 아래 알고리즘으로 풀어 비교했습니다. 모두 같은 조건입니다: 6way는 6방향·후보 1·Two-Phase 500 ms + rephase ≤100 ms·window 8, 원본 stemil은 500 ms, 최적해는 큐브당 60초 상한·6스레드. 테이블 로딩 시간은 모두 제외했습니다.

| 알고리즘 | 평균 move | time_to_final (ms) | wall (ms) | peak 메모리 (MiB) |
|---|---:|---:|---:|---:|
| 6way + rephase | **17.52** | 144.2 | 508.5 | 2097.1 |
| stemil (원본) | 20.05 | 55.6 | 500.1 | 2090.2 |
| kociemba (pip, C) | 20.89 | 22.8 | 22.8 | 19.8 |
| kociemba + symmetry | 20.55 | 20.1 | 20.1 | 113.2 |
| 최적해 (확정 35건만) | 16.51 | 27,839 | 54,374 | 2523.1 |

- rephase 전후: 평균 18.59수 → 17.52수 (1수 단축 186건, 2수 단축 14건).
- 6way가 더 짧은 비율: stemil 대비 196/200 (4건 같음), kociemba와 kociemba+symmetry 대비 200/200.
- **하한과의 비교**: 최적해 solver가 60초 안에 최적을 확정한 것은 35건(17.5%)뿐입니다. 나머지도 증명된 하한(대부분 17수)은 얻을 수 있어서, 6way 결과가 그 하한과 같으면 최적입니다. 200건 중 85건(42.5%)이 그렇습니다. 하한과의 평균 차이는 6way 0.61수, stemil 3.13수, kociemba 3.98수, kociemba+symmetry 3.65수입니다.
- 원본 solver들의 측정값은 각 원본을 그대로 실행한 값입니다.

`time_to_final`은 "최종 해가 처음 존재한 시점"을 사후에 재는 값으로 모든 알고리즘에 같은 규칙을 적용했습니다(6way는 Two-Phase 최선 해를 찾은 시점 + rephase 개선 시점). `wall`은 실제로 걸린 시간이며 6way와 stemil은 예산을 끝까지 씁니다.

### 읽을 때 주의할 점

1. **선택 편향.** rephase가 성공한 큐브만 골랐으므로, 전체 큐브에서의 평균 이득이 아닙니다. 6way의 실제 성공률은 약 0.24%입니다. 6way의 시간이 항상 500 ms 이상으로 보이는 것도 rephase 구간에서만 개선이 나오기 때문입니다.
2. **이득의 대부분은 6방향 탐색**에서 나옵니다. 이전 100개 실험에서 rephase 단독 단축은 100개 중 2~3개(각 1수)였습니다(`benchmarks/earlier/6way`, [REPORT-6way.md](stemil-rephase/REPORT-6way.md)).
3. **스레드 수가 다릅니다.** 6way, kociemba+symmetry, 최적해는 6스레드, stemil과 kociemba는 1스레드입니다.
4. **메모리 차이는 테이블 크기**에서 나옵니다. stemil 계열은 약 2 GiB의 pruning table이 올라간 프로세스 전체 peak RSS입니다.
5. 최적해 평균은 60초 안에 확정된 **쉬운 큐브 위주**(35건)입니다.
6. 시간 제한 탐색이라 같은 seed도 실행마다 결과가 조금 달라질 수 있습니다. 표본 200건은 경향을 보기에는 충분하지만 정밀한 통계 비교에는 부족합니다.
7. 측정 환경: i5-1235U(12 스레드), WSL2, g++ 13.3 (`benchmarks/earlier/*/environment.json` 참고).

### 재현

```bash
cd stemil-rephase && make -j4
# 환경에 맞게 지정(기본값은 저장소 상대경로): REPHASE_DIR=<rephase 폴더>, STEMIL_RESOURCES=<stemil resources 폴더>
python3 compare/run_compare.py all --count 100000 --max-cases 200   # scan → solve → report (재개 가능)
python3 compare/run_compare.py report --out ../benchmarks/compare-200   # 저장된 결과로 표만 다시 생성
```

kociemba+symmetry 기준은 `RubikTwoPhase`의 테이블(약 70 MB)을 처음 한 번 생성해야 하며 30분 이상 걸릴 수 있습니다: `cd rephase && .venv/bin/python -m pip install -r requirements-reference.txt && .venv/bin/python solve.py init-twophase` (`rephase/README.md` 참고). 전체 scan은 수 시간~10시간 이상 걸립니다.

## 라이선스

아직 정하지 않았습니다. 상세는 [NOTICE.md](NOTICE.md)를 보세요.
