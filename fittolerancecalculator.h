#ifndef FITTOLERANCECALCULATOR_H
#define FITTOLERANCECALCULATOR_H

#include <QString>

// 끼워맞춤 공차 계산 결과를 담는 구조체
struct FitToleranceResult {
    bool valid = false;   // true면 upper/lower가 유효한 계산 결과, false면 미지원 조합(등급/기호/치수구간)
    double upper = 0.0;   // 상한 편차 (mm 단위, 0선 기준 +쪽이면 양수)
    double lower = 0.0;   // 하한 편차 (mm 단위, 0선 기준 +쪽이면 양수)
    bool isHole = false;  // 대문자(구멍) 기호였는지 여부
};

// ================================================================
// FitToleranceCalculator
// ----------------------------------------------------------------
// "H7", "g6" 같은 끼워맞춤 등급기호 문자열과 기준치수를 받아 상/하한
// 편차값(mm)을 계산한다. GdtCalculator와 동일하게 상태를 갖지 않는
// 정적함수(static function) 모음 패턴을 따른다.
// 실제 표 데이터는 FitToleranceTable에서 조회한다.
// ================================================================
class FitToleranceCalculator {
public:
    // nominalSize: 치수선의 측정값(mm, 기준치수). fitCode: "H7", "g6", "js6" 등.
    static FitToleranceResult calculate(double nominalSize, const QString &fitCode);

    // fitCode 문자열을 기본편차 기호(outLetter)와 IT등급(outGrade)으로 분리한다.
    // 예: "H7" -> outLetter="H", outGrade=7 / "js6" -> outLetter="js", outGrade=6
    // 형식이 올바르지 않으면 false를 반환한다.
    static bool parseFitCode(const QString &fitCode, QString &outLetter, int &outGrade);
};

#endif // FITTOLERANCECALCULATOR_H
