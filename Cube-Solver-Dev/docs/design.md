# 설계 노트: Phase-Boundary Exact Repair와 sliding window

이 문서는 프로젝트 초기에 정리한 설계 메모 PDF 2종(`kociemba_twophase_업그레이드.pdf`, `stemil_업그레이드.pdf`)의 핵심을 요약한 것입니다. PDF 원본은 공개하지 않습니다. PDF에는 설계 논의 과정이 그대로 담겨 있어서, **실제로 구현하고 측정한 내용만** 남겼습니다.

> 용어: 수(move)는 HTM(half-turn metric)으로 셉니다. `R`, `R2`, `R'`는 모두 1수입니다.

## 1. 문제의식

Kociemba two-phase는 해를 두 단계로 만듭니다.

```
Scramble → Phase 1 → G1 subgroup → Phase 2 → Solved
              R U F2 L ... D | R2 U D2 ...
                              ↑ phase boundary
```

Phase 1이 끝나는 순간 코너 방향, 엣지 방향, E-slice 조건을 **반드시** 만족해야 합니다. 전체 최단 경로가 그 subgroup을 지날 필요는 없으므로, 경계 주변에서 국소적으로 낭비되는 수가 생길 수 있습니다.

## 2. Phase-Boundary Exact Repair

1. Two-Phase로 초기 해를 구하고 Phase 경계를 기록합니다.
2. 경계 주변의 짧은 구간(window, 4~10수)을 잘라 시작 상태 `A`와 끝 상태 `B`를 구합니다.
3. `A → B`를 **더 짧게** 만드는 수순이 있는지 meet-in-the-middle로 정확히 검색합니다. 전체 큐브가 아니라 가까운 두 상태 사이만 보므로 탐색 비용이 작습니다. 일방향 `b^8`이 양방향 `2·b^4` 수준이 됩니다. (Python/C `rephase/`는 양방향 BFS, C++ `stemil-rephase/`는 미리 만든 변환 DB 조회 + 반대편 깊이 우선 탐색으로 구현했습니다.)
4. 더 짧은 수순이 있으면 교체하고 전체 해를 다시 검증합니다. 없으면 원래 해를 그대로 둡니다. **그래서 수는 절대 늘지 않습니다.**
5. 시간 예산(예: 총 1000 ms 중 rephase 100 ms)을 명시적으로 나눕니다.

한계도 분명합니다. 전역 최적해를 보장하지 않습니다. 최적해가 Two-Phase 경로와 완전히 다른 경로라면 국소 수선으로는 찾지 못합니다.

## 3. Sliding window

경계 수선 뒤 시간이 남으면 해 전체에 window를 한 칸씩 밀면서(1~6수, 2~7수, …) 같은 검사를 합니다. 항상 작은 window부터 시도하고 시간이 다 되면 마지막으로 검증된 해를 반환합니다.

## 4. 변환(transformation) 기준 비교

window를 큐브 상태가 아니라 "이 수순이 일으키는 순열·방향 변환 `T = A⁻¹B`"로 표현하면, 같은 변환을 만드는 더 짧은 수순을 찾는 문제가 됩니다. 짧은 수순들의 변환을 미리 저장해 두면(`depth 4` 테이블) 검색 없이 조회만으로 교체할 수 있습니다. C++ 구현(`stemil-rephase/`)은 이 DB를 `--table-depth`(기본 4, 0~5)와 `--repair-memory-mib`(기본 64) 한도로 만들고, 목표 변환에서 `길이 − table_depth` 깊이까지 탐색해 DB에서 만나는 수순을 찾습니다.

## 5. stemil과 결합할 때의 설계

- stemil은 큰 pruning table(약 2 GiB RAM)로 전역 탐색 품질을 높이고, repair는 그 결과의 국소 비효율을 제거합니다. 두 최적화는 서로 다른 축을 건드립니다.
- 원본 stemil의 큰 pruning table은 **repair 검색에 쓰지 않습니다.** Phase 1·2 table은 "G1까지 남은 거리", "G1 안에서 solved까지 남은 거리"를 추정하는 것이라, solved가 아닌 상태 `B`로 가는 `A → B` 문제에는 그대로 해석할 수 없기 때문입니다.
- 시간 예산을 나눠 비교하는 실험 변수가 생깁니다. 예: Two-Phase 1.0초 vs Two-Phase 0.9초 + repair 0.1초.
- 해 길이에 따라 repair 시간을 조정하는 adaptive 모드(해가 길면 Two-Phase에 더, 짧으면 repair에 더)를 `--adaptive`로 구현했습니다.

## 6. 이 설계가 실제 코드에서 어디에 있는가

| 설계 요소 | 구현 위치 |
|---|---|
| Kociemba + boundary repair (Python/C) | [`rephase/`](../rephase) |
| stemil + boundary/sliding repair (C++) | [`stemil-rephase/src/repair.cpp`](../stemil-rephase/src/repair.cpp), `solver.cpp` |
| 6방향 동시 탐색, 후보 rephase | [`stemil-rephase/src/solver.cpp`](../stemil-rephase/src/solver.cpp) (`solve_multi`), `symmetry.cpp` |
| 최적해 검증 (IDA\*) | [`stemil-rephase/src/optimal.cpp`](../stemil-rephase/src/optimal.cpp) |

## 7. 측정한 것과 해석

- 실험 결과는 [`benchmarks/`](../benchmarks)와 루트 [README](../README.md)에 있습니다.
- stemil README의 시간별 평균 해 길이(0.5초 20.17수, 1초 20.09수, 2초 19.96수, 5초 19.73수)는 **원본 저장소가 공개한 수치**이며 이 프로젝트의 측정값이 아닙니다.
- 설계 메모 PDF에는 비슷한 구조의 공개 구현이 없다는 취지의 서술이 있으나, 검색만으로는 증명할 수 없는 주장이라 여기에는 옮기지 않았습니다.
