#ifndef DXFDIMENSIONPARSER_H
#define DXFDIMENSIONPARSER_H

#include <QString>
#include "mydimension.h"

// DXF 치수 텍스트(Group 1)를 분석하여 MyDimension 속성으로 채워주는 클래스
class DxfDimensionParser {
public:
    struct ParseResult {
        QString prefix;
        QString suffix;
        QString cleanText; // <> 등이 처리된 순수 텍스트
        ToleranceType tolType = ToleranceType::None;
        double tolUpper = 0.0;
        double tolLower = 0.0;
        int tolPrecision = -1;
        bool hasAutoMeasurement = false;
    };

    static ParseResult parse(const QString& rawDxfText, double measurement, int precision);

private:
    static QString convertSymbols(const QString& input);
};

#endif // DXFDIMENSIONPARSER_H
