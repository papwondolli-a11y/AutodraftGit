#include "gdtcalculator.h"
#include <QtMath>
#include <algorithm>

double GdtCalculator::cellHeight(double textHeight) {
    return textHeight * 2.0;
}

double GdtCalculator::symbolCellWidth(double textHeight, GdtSymbol sym, bool forAutoCAD) {
    double h = cellHeight(textHeight);
    double factor = 1.0;

    if (forAutoCAD) {
        switch (sym) {
            case GdtSymbol::TotalRunout: factor = 1.542; break; // 전체흔들림
            case GdtSymbol::SurfaceProfile: factor = 1.25; break; // 면의윤곽도
            case GdtSymbol::LineProfile: factor = 1.25; break;    // 선의윤곽도
            case GdtSymbol::Perpendicularity: factor = 1.25; break;// 직각도
            case GdtSymbol::Parallelism: factor = 0.977; break ;    // 평행도
            case GdtSymbol::Cylindricity: factor = 1.25; break  ;    // 원통도
            case GdtSymbol::Flatness: factor = 1.285; break  ;    // 평면도
            case GdtSymbol::Straightness: factor = 1.25; break  ;    // 직진도

            default: factor = 1.0; break;
        }
    } else {
        // 화면 UI용은 조금 더 여유 있게
        factor = (sym == GdtSymbol::TotalRunout) ? 1.6 : 1.1;
    }
    return h * factor;
}

double GdtCalculator::toleranceCellWidth(double textHeight, const QString& toleranceValue, bool hasDiameter, bool forAutoCAD) {
    double th = textHeight;
    double textW = 0;

    if (forAutoCAD) {
        // AutoCAD 전용 정밀 계수
        for (int i = 0; i < toleranceValue.length(); ++i) {
            QChar c = toleranceValue[i];
            if (c == '.') textW += th * 0.3;
            else if (c == '1') textW += th * 0.5;
            else if (c == ' ') textW += th * 0.45;
            else textW += th * 0.6;
        }
        if (hasDiameter) textW += th * 1.1020;
        return qMax(th * 2.5, textW + th * 0.5559);  // 숫자커짐 --- 벌어짐
    } else {
        // 화면 UI용: 넉넉한 가독성 보장
        textW = th * 0.7 * static_cast<double>(toleranceValue.length());
        if (hasDiameter) textW += th * 0.8;
        return qMax(th * 3.5, textW + th * 1.5);
    }
}

double GdtCalculator::datumCellWidth(double textHeight, const QString& datumRef, bool forAutoCAD) {
    double th = textHeight;
    if (forAutoCAD) {
        double textW = 0;
        for (int i = 0; i < datumRef.length(); ++i) {
            if (datumRef[i] == '1') textW += th * 0.5;
            else textW += th * 0.6;
        }
        return qMax(th * 2.0, textW + th * 0.8);
    } else {
        return qMax(th * 2.5, (th * 0.7 * static_cast<double>(datumRef.length())) + th * 1.2);
    }
}

// double GdtCalculator::datumCellWidth(double textHeight, const QString& datumRef, bool forAutoCAD) {
//     double th = textHeight;
//     // AutoCAD 실측 역산: 데이텀 칸 최솟값 2.0 (단문자 기준)
//     double fixedW = th * 2.0;
//     double textW = th * 0.7 * static_cast<double>(datumRef.length());
//     return qMax(fixedW, textW + th * 0.4);
// }



double GdtCalculator::totalGdtWidth(double textHeight, const QString& toleranceValue, bool hasDiameter, const QList<QString>& datumRefs, GdtSymbol sym, bool forAutoCAD) {
    double width = symbolCellWidth(textHeight, sym, forAutoCAD) + toleranceCellWidth(textHeight, toleranceValue, hasDiameter, forAutoCAD);
    for (int i = 0; i < datumRefs.size(); ++i) {
        width += datumCellWidth(textHeight, datumRefs[i], forAutoCAD);
    }
    return width;
}

double GdtCalculator::datumBoxWidth(double textHeight, const QString& text, bool forAutoCAD) {
    double th = textHeight;
    if (forAutoCAD) {
        // [수정] 단일 문자의 경우 2.0배로는 정방형이 부족하여 2.2배로 상향 조정합니다.
        // 이는 내보낼 때 추가되는 공백(패딩)과 AutoCAD의 내부 여백을 고려한 값입니다.
        return th * 2.2; 
    } else {
        return qMax(th * 2.5, (th * 0.7 * static_cast<double>(text.length())) + th * 1.2);
    }
}



