# NOTICE

이 저장소에는 **직접 만든 것**과 **다른 사람이 만든 원본**이 함께 나온다. 어디까지가 직접 만든 작업인지 아래 표로 구분했다. 원본 코드는 이 저장소에 복사해 넣지 않았고, submodule이나 의존성으로만 연결했다.

## 1. 다른 사람의 원본 (코드가 들어 있지 않다)

| 원본 | 출처 | 이 저장소에서 쓰는 방식 |
|---|---|---|
| **stemil two-phase-solver** | https://github.com/stemil01/two-phase-solver (커밋 `5437b8f`), 작성자 stemil01. 원본 README가 링크한 세르비아어 논문: https://www.mg.edu.rs/uploads/files/dokumenta/najbolji-maturski/stefan-milenkovic.pdf | `third_party/stemil`에 **git submodule**로 고정한다. 소스를 컴파일해 링크하고 pruning table을 읽기만 한다. 수정하지 않는다. |
| **Kociemba two-phase 알고리즘** | Herbert Kociemba, http://kociemba.org/cube.htm | 알고리즘에 대한 크레딧이다. 위 stemil과 아래 두 패키지가 이 알고리즘의 구현이다. |
| **kociemba** (pip, `==1.2.1`) | https://github.com/muodov/kociemba (작성자 muodov, 라이선스 표기 GPLv2), Kociemba two-phase의 C 구현 | `rephase/requirements.txt`의 **의존성**이다. 설치된 패키지를 실행할 때 호출하고, 계측용으로 `search.c`를 읽어 빌드한다. 코드는 저장소에 없다. |
| **RubikTwoPhase** (pip, `==1.1.1`) | https://github.com/hkociemba/RubiksCube-TwophaseSolver (작성자 Herbert Kociemba, 라이선스 GPLv3+), symmetry를 쓰는 Python 구현 | `rephase/requirements-reference.txt`의 **의존성**이다. 비교 실험의 "kociemba + symmetry" 기준으로 호출한다. 코드는 저장소에 없다. |

## 2. 원본에서 파생한 코드

| 파일 | 설명 |
|---|---|
| `stemil-rephase/compare/stemil_orig.cpp` | stemil `src/search.cpp`의 `improved_search` / `random_moves_search`를 거의 그대로 옮기고 **"최종 해를 찾은 시각" 기록만 더한** 파일이다. 원본 stemil의 탐색을 같은 조건에서 재기 위한 것이다. 원본 소스와 테이블은 읽기만 한다. **원본 저장소에는 LICENSE가 없다.** |

## 3. 원본 헤더·테이블에 기대는 내 코드

| 파일 | 관계 |
|---|---|
| `stemil-rephase/src/cube.*`, `tables.*`, `solver.*`, `kernel.*`, `tests/test.cpp` | stemil 헤더(`coordinates.hpp`, `moves.hpp`, `pruning_tables.hpp`, `search.hpp`)를 include하고, stemil의 `.cpp`와 함께 빌드한다(`Makefile`의 `UPSTREAM`). `kernel.hpp`는 stemil의 탐색 순서를 **재현한 자체 구현**이며, 테이블 형식은 원본과 같다. |
| `rephase/boundary_repair/backends.py`, `stemil-rephase/compare/py_solvers.py` | 위 pip 패키지의 API를 호출하는 어댑터이다. |

## 4. 직접 만든 것

| 위치 | 내용 |
|---|---|
| `rephase/` | Kociemba two-phase에 Phase-Boundary Exact Repair를 붙인 Python/C 구현 |
| `stemil-rephase/src/repair.*`, `solver.*`(rephase 경로), `symmetry.*`, `optimal.*` | stemil 위의 경계·sliding rephase, 6방향 동시 탐색, 후보 rephase, 최적해(IDA\*) 검증 |
| `stemil-rephase/compare/run_compare.py` 등 비교 도구 | 같은 seed 비교 실험 도구 |
| `benchmarks/`, `docs/` | 실험 결과와 설계 노트 |

## 5. 측정값을 읽는 법

- 비교표의 **stemil / kociemba / kociemba + symmetry** 값은 각 원본(또는 위 파생 드라이버)을 **그대로 실행해 얻은 측정값**이다. 원본의 성능을 이 프로젝트가 올렸다는 뜻이 아니라, 원본의 결과 위에 rephase를 적용했을 때 어떻게 달라지는지를 본 것이다.
- 원본 stemil README의 시간별 평균 해 길이(0.5초 20.17수 등)는 원본 저장소가 공개한 수치이며, 이 프로젝트에서 잰 값이 아니다.

## 6. 라이선스

이 저장소의 라이선스는 아직 정하지 않았다. 정해지기 전에는 별도 허락 없이 재사용할 수 없다. 의존하는 패키지(kociemba GPLv2, RubikTwoPhase GPLv3+)와, 라이선스가 없는 stemil 원본의 조건을 살펴본 뒤 정할 예정이다.
