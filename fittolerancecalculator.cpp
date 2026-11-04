#include "fittolerancecalculator.h"
#include "fittolerancetable.h"

bool FitToleranceCalculator::parseFitCode(const QString &fitCode, QString &outLetter, int &outGrade) {
    QString s = fitCode.trimmed();
    if (s.isEmpty()) return false;

    // 기본편차 기호는 대부분 한 글자이지만 "js"/"JS"만 예외적으로 두 글자이다.
    int letterLen = 1;
    if (s.length() >= 2 && s.left(2).compare("js", Qt::CaseInsensitive) == 0) {
        letterLen = 2;
    }
    outLetter = s.left(letterLen);

    QString gradeStr = s.mid(letterLen);
    bool ok = false;
    int grade = gradeStr.toInt(&ok);
    if (!ok || grade <= 0) return false;

    outGrade = grade;
    return true;
}

FitToleranceResult FitToleranceCalculator::calculate(double nominalSize, const QString &fitCode) {
    FitToleranceResult result;

    QString letter;
    int grade = 0;
    if (!parseFitCode(fitCode, letter, grade)) return result; // 형식 오류: valid=false

    int rangeIdx = FitToleranceTable::sizeRangeIndex(nominalSize);
    if (rangeIdx < 0) return result; // 지원 범위(0~500mm) 밖: valid=false

    int itUm = FitToleranceTable::itGradeValue(grade, rangeIdx);
    if (itUm < 0) return result; // 미지원 등급: valid=false

    double itMm = itUm / 1000.0;
    bool isHole = letter.at(0).isUpper();
    result.isHole = isHole;

    if (isHole) {
        // [v1 지원범위] 구멍기준식(Hole-basis)에서 쓰는 "H"만 지원한다.
        // G,K,N,P 등 다른 구멍 기호(축기준식용)는 등급별 보정항(Δ)이 필요해
        // 정확한 수치를 보장할 수 없으므로 이번 버전에서는 계산하지 않는다.
        if (letter.toUpper() != "H") return result;
        result.lower = 0.0;
        result.upper = itMm;
        result.valid = true;
        return result;
    }

    QString lowerLetter = letter.toLower();

    // js: 기준선을 중심으로 대칭인 공차 (상한 = +IT/2, 하한 = -IT/2)
    if (lowerLetter == "js") {
        result.upper = itMm / 2.0;
        result.lower = -itMm / 2.0;
        result.valid = true;
        return result;
    }

    bool found = false;
    int devUm = FitToleranceTable::shaftFundamentalDeviation(lowerLetter, grade, rangeIdx, found);
    if (!found) return result; // 미지원 기호: valid=false

    double devMm = devUm / 1000.0;

    // a~h 그룹: 기본편차가 상한(es)을 의미 -> 하한 = 상한 - IT
    // j~zc 그룹(여기선 k,m,n,p): 기본편차가 하한(ei)을 의미 -> 상한 = 하한 + IT
    static const QString upperDefinedGroup = "abcdefgh";
    if (upperDefinedGroup.contains(lowerLetter)) {
        result.upper = devMm;
        result.lower = devMm - itMm;
    } else {
        result.lower = devMm;
        result.upper = devMm + itMm;
    }

    result.valid = true;
    return result;
}
