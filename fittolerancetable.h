#ifndef FITTOLERANCETABLE_H
#define FITTOLERANCETABLE_H

#include <QString>

// ================================================================
// FitToleranceTable
// ----------------------------------------------------------------
// ISO 286 / KS B 0401 규격에 정의된 "끼워맞춤 공차(Fit Tolerance)" 표를
// 코드로 옮긴 순수 데이터 조회용 클래스이다. GdtCalculator처럼 상태를
// 갖지 않는 정적함수(static function) 모음으로 구성한다.
//
// [중요 주의사항]
// 아래 수치들은 KS B 0401 부속서에 실린 대표적인 "우선 끼워맞춤(preferred fit)"
// 표를 옮긴 값이다. 실제 생산 도면에 적용하기 전에는 반드시 최신 규격서
// 원본과 대조하여 재확인할 것 (특히 등급이 낮은 미세공차 K,M,N,P 계열은
// 등급별 보정항(Δ)이 존재하는 경우가 있어 오차 가능성이 있음).
//
// 지원 범위(v1):
//  - 치수구간: 0 초과 ~ 500mm (KS B 0401 표의 앞부분 13개 구간)
//  - IT등급: IT5 ~ IT11 (실무에서 가장 많이 쓰이는 끼워맞춤 등급 범위)
//  - 축(shaft) 기본편차 기호: h, js, g, f, k, m, n, p
//  - 구멍(hole) 기본편차 기호: H(구멍기준식, EI=0)만 지원
// ================================================================
class FitToleranceTable {
public:
    // 지원하는 치수구간 개수 (0~3, 3~6, ... 400~500mm)
    static const int kRangeCount = 13;

    // 기준치수(nominalSize, mm 단위)가 속하는 치수구간의 인덱스(0~12)를 반환.
    // 표의 지원범위(0mm 초과 ~ 500mm 이하)를 벗어나면 -1을 반환한다.
    static int sizeRangeIndex(double nominalSize);

    // IT등급(grade, 5~11)과 치수구간 인덱스로 공차값(um, 마이크로미터)을 조회.
    // 지원하지 않는 등급이거나 구간 인덱스가 범위를 벗어나면 -1을 반환한다.
    static int itGradeValue(int grade, int rangeIndex);

    // 축(소문자) 기본편차 기호로 기본편차값(um)을 조회한다.
    // grade는 'k' 기호에서만 실제로 사용되며(IT등급 8 이하에서만 0이 아닌 값을
    // 가짐, 9등급 이상은 관례상 0으로 취급), 나머지 기호는 등급과 무관하게
    // 치수구간에만 의존하는 상수값이다.
    // found: 지원하는 기호였는지 여부를 돌려주는 출력 파라미터.
    static int shaftFundamentalDeviation(const QString &letterLower, int grade, int rangeIndex, bool &found);
};

#endif // FITTOLERANCETABLE_H
