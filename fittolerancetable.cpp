#include "fittolerancetable.h"

int FitToleranceTable::sizeRangeIndex(double nominalSize) {
    // 치수구간 상한값 목록 (mm). 각 구간은 "이전 상한값 초과 ~ 이번 상한값 이하".
    // 예: bounds[0]=3 -> (0, 3]mm, bounds[1]=6 -> (3, 6]mm ...
    static const double bounds[FitToleranceTable::kRangeCount] = {
        3, 6, 10, 18, 30, 50, 80, 120, 180, 250, 315, 400, 500
    };

    if (nominalSize <= 0.0 || nominalSize > 500.0) return -1;

    for (int i = 0; i < FitToleranceTable::kRangeCount; ++i) {
        if (nominalSize <= bounds[i]) return i;
    }
    return -1;
}

int FitToleranceTable::itGradeValue(int grade, int rangeIndex) {
    if (rangeIndex < 0 || rangeIndex >= FitToleranceTable::kRangeCount) return -1;
    if (grade < 5 || grade > 11) return -1;

    // IT5~IT11 공차값(um) 표. 행 = 등급(5~11), 열 = 치수구간(0~12).
    static const int table[7][FitToleranceTable::kRangeCount] = {
        {4, 5, 6, 8, 9, 11, 13, 15, 18, 20, 23, 25, 27},          // IT5
        {6, 8, 9, 11, 13, 16, 19, 22, 25, 29, 32, 36, 40},        // IT6
        {10, 12, 15, 18, 21, 25, 30, 35, 40, 46, 52, 57, 63},     // IT7
        {14, 18, 22, 27, 33, 39, 46, 54, 63, 72, 81, 89, 97},     // IT8
        {25, 30, 36, 43, 52, 62, 74, 87, 100, 115, 130, 140, 155},// IT9
        {40, 48, 58, 70, 84, 100, 120, 140, 160, 185, 210, 230, 250}, // IT10
        {60, 75, 90, 110, 130, 160, 190, 220, 250, 290, 320, 360, 400} // IT11
    };

    return table[grade - 5][rangeIndex];
}

int FitToleranceTable::shaftFundamentalDeviation(const QString &letterLower, int grade, int rangeIndex, bool &found) {
    found = false;
    if (rangeIndex < 0 || rangeIndex >= FitToleranceTable::kRangeCount) return 0;

    // h: 기준선(0선)이 축의 상한(es)인 기호. 모든 등급/구간에서 항상 0.
    if (letterLower == "h") {
        found = true;
        return 0;
    }

    // a~h 그룹(기준선 아래): 기본편차가 상한(es)을 의미하는 음수값.
    static const int devG[FitToleranceTable::kRangeCount] = {-2, -4, -5, -6, -7, -9, -10, -12, -14, -15, -17, -18, -20};
    static const int devF[FitToleranceTable::kRangeCount] = {-6, -10, -13, -16, -20, -25, -30, -36, -43, -50, -56, -62, -68};

    // j~zc 그룹(기준선 위): 기본편차가 하한(ei)을 의미하는 양수값.
    static const int devK[FitToleranceTable::kRangeCount] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5};
    static const int devM[FitToleranceTable::kRangeCount] = {2, 4, 6, 7, 8, 9, 11, 13, 15, 17, 20, 21, 23};
    static const int devN[FitToleranceTable::kRangeCount] = {4, 8, 10, 12, 15, 17, 20, 23, 27, 31, 34, 37, 40};
    static const int devP[FitToleranceTable::kRangeCount] = {6, 12, 15, 18, 22, 26, 32, 37, 43, 50, 56, 62, 68};

    if (letterLower == "g") { found = true; return devG[rangeIndex]; }
    if (letterLower == "f") { found = true; return devF[rangeIndex]; }
    if (letterLower == "m") { found = true; return devM[rangeIndex]; }
    if (letterLower == "n") { found = true; return devN[rangeIndex]; }
    if (letterLower == "p") { found = true; return devP[rangeIndex]; }

    if (letterLower == "k") {
        found = true;
        // k는 IT8 이하 등급에서만 표의 값을 쓰고, IT9 이상은 관례상 0(=h와 사실상 동일)으로 취급한다.
        return (grade <= 8) ? devK[rangeIndex] : 0;
    }

    // 미지원 기호 (js는 대칭공차라 이 함수를 거치지 않고 호출측에서 ±IT/2로 직접 계산함)
    return 0;
}
