#ifndef GDTCALCULATOR_H
#define GDTCALCULATOR_H

#include <QString>
#include <QList>
#include "mygdt.h"

class GdtCalculator {
public:
    static double cellHeight(double textHeight);

    // [수정] 기호 정보를 받아 기호별 특수 너비 반영
    static double symbolCellWidth(double textHeight, GdtSymbol sym = GdtSymbol::Position, bool forAutoCAD = false);

    static double toleranceCellWidth(double textHeight, const QString& toleranceValue, bool hasDiameter, bool forAutoCAD = false);

    static double datumCellWidth(double textHeight, const QString& datumRef, bool forAutoCAD = false);

    // [수정] 기호 정보를 받아 전체 너비 계산
    static double totalGdtWidth(double textHeight, const QString& toleranceValue, bool hasDiameter, const QList<QString>& datumRefs, GdtSymbol sym = GdtSymbol::Position, bool forAutoCAD = false);

    static double datumBoxWidth(double textHeight, const QString& text, bool forAutoCAD = false);
};

#endif // GDTCALCULATOR_H
