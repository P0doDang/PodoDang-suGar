# NOTICE — 내 작업과 타인 원본의 구분

이 저장소에는 **내가 만든 것**과 **다른 사람이 만든 원본**이 함께 나옵니다. 아래 표가 기준입니다. 원본 코드는 이 저장소에 복사해 넣지 않았고, 아래 방식으로만 연결합니다.

## 1. 타인 원본 (이 저장소에 코드가 들어 있지 않음)

| 원본 | 출처 | 이 저장소에서의 사용 방식 |
|---|---|---|
| **stemil two-phase-solver** | https://github.com/stemil01/two-phase-solver (commit `5437b8f`) 작성자 stemil01. 원본 README가 링크한 세르비아어 논문: https://www.mg.edu.rs/uploads/files/dokumenta/najbolji-maturski/stefan-milenkovic.pdf | `third_party/stemil`에 **git submodule**로 고정. 소스를 컴파일해 링크하고 pruning table을 읽기만 함. 수정하지 않음 |
| **Kociemba two-phase 알고리즘** | Herbert Kociemba, http://kociemba.org/cube.htm | 알고리즘 크레딧. 위 stemil과 아래 두 패키지가 이 알고리즘의 구현 |
| **kociemba** (pip, `==1.2.1`) | https://github.com/muodov/kociemba (작성자 muodov, 라이선스 표기 GPLv2) — Kociemba two-phase의 C 구현 | `rephase/requirements.txt`의 **의존성**. 설치된 패키지를 실행 시 호출하고 `search.c`를 읽어 계측용으로 빌드. 코드는 저장소에 없음 |
| **RubikTwoPhase** (pip, `==1.1.1`) | https://github.com/hkociemba/RubiksCube-TwophaseSolver (작성자 Herbert Kociemba, 라이선스 GPLv3+) — symmetry를 사용하는 Python 구현 | `rephase/requirements-reference.txt`의 **의존성**. 비교 실험의 "kociemba_sym" 기준으로 호출. 코드는 저장소에 없음 |

## 2. 원본에서 파생된 코드 (출처 명시)

| 파일 | 설명 |
|---|---|
| `stemil-rephase/compare/stemil_orig.cpp` | stemil `src/search.cpp`의 `improved_search` / `random_moves_search`를 거의 그대로 옮기고, **"최종 해를 찾은 시각" 기록만 추가**한 파일. 원본 stemil의 탐색을 같은 조건에서 측정하기 위한 것. 원본 소스·테이블은 읽기만 함. **원본 저장소에는 LICENSE가 없음** |

## 3. 원본 헤더·테이블에 의존하는 내 코드

| 파일 | 관계 |
|---|---|
| `stemil-rephase/src/cube.*`, `tables.*`, `solver.*`, `kernel.*`, `tests/test.cpp` | stemil 헤더(`coordinates.hpp`, `moves.hpp`, `pruning_tables.hpp`, `search.hpp`)를 include하고 stemil의 `.cpp`와 함께 빌드(`Makefile`의 `UPSTREAM`). `kernel.hpp`는 stemil 탐색 순서를 **재현한 자체 구현**이며 테이블 포맷은 원본과 같음 |
| `rephase/boundary_repair/backends.py`, `stemil-rephase/compare/py_solvers.py` | 위 pip 패키지의 API를 호출하는 어댑터 |

## 4. 내가 만든 것

| 위치 | 내용 |
|---|---|
| `rephase/` | Kociemba two-phase 위에 Phase-Boundary Exact Repair를 붙인 Python/C 구현 |
| `stemil-rephase/src/repair.*`, `solver.*`(rephase 경로), `symmetry.*`, `optimal.*` | stemil 위의 boundary·sliding rephase, 6방향 동시 탐색, 후보 rephase, 최적해(IDA\*) 검증 |
| `stemil-rephase/compare/run_compare.py` 외 비교 도구 | 같은 seed 비교 실험 harness |
| `benchmarks/`, `docs/` | 실험 결과와 설계 노트 |

## 5. 측정값에 대한 표기

- 비교표의 **stemil / kociemba / kociemba_sym** 값은 각 원본(또는 위 파생 드라이버)을 **그대로 실행해 얻은 측정값**입니다. 원본의 성능을 이 프로젝트가 개선했다고 주장하는 것이 아니라, 원본 결과 위에 rephase를 적용했을 때의 차이를 보는 것입니다.
- 원본 stemil README의 시간별 평균 해 길이(0.5초 20.17수 등)는 원본 저장소가 공개한 수치이며 이 프로젝트의 측정값이 아닙니다.

## 6. 라이선스

이 저장소 자체의 라이선스는 아직 정하지 않았습니다(저장소 소유자가 결정 예정). 정해지기 전까지는 별도 허락 없이 재사용할 수 없습니다. 의존 패키지(kociemba GPLv2, RubikTwoPhase GPLv3+)와 라이선스가 없는 stemil 원본의 조건을 고려해서 결정합니다.
