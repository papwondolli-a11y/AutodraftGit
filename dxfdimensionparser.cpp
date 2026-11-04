#include "dxfdimensionparser.h"
#include "cadutils.h"
#include <QRegularExpression>
#include <QDebug>

DxfDimensionParser::ParseResult DxfDimensionParser::parse(const QString& rawDxfText, double /*measurement*/, int /*precision*/) {
    ParseResult result;
    if (rawDxfText.isEmpty()) {
        result.hasAutoMeasurement = true;
        result.cleanText = "";
        return result;
    }

    QString text = rawDxfText;

    // 1. [구조적 파싱] { ... \S ... ; } 형태 또는 단독 \S ... ; 형태 파싱
    // 중괄호 {} 가 선택적인 정규식으로 개선
    QRegularExpression stackRegex("(.*?)(?:\\{?.*?\\\\S([^\\\\^#]*)(?:([\\\\^#/])([^;]*))?;\\}?)");
    QRegularExpressionMatch stackMatch = stackRegex.match(text);
    if (stackMatch.hasMatch()) {
        result.tolType = ToleranceType::Deviation;
        QString baseValue = stackMatch.captured(1).trimmed();
        QString upperStr = stackMatch.captured(2).trimmed();
        QString lowerStr = stackMatch.captured(4).trimmed();

        result.tolUpper = upperStr.toDouble();
        
        // 하한 공차 부호 처리 (Autodraft 렌더링 엔진 호환: 하한은 뺄 수로 취급)
        if (lowerStr.startsWith("-")) {
            result.tolLower = lowerStr.mid(1).trimmed().toDouble();
        } else if (lowerStr.startsWith("+")) {
            result.tolLower = -lowerStr.mid(1).trimmed().toDouble();
        } else {
            result.tolLower = lowerStr.toDouble();
        }

        // 명시된 문자열로부터 정밀도(소수점 자리수) 추출 (DIMSTYLE 정밀도 오버라이드)
        auto getDecimals = [](const QString& s) -> int {
            int dotIdx = s.indexOf('.');
            if (dotIdx == -1) return 0;
            return s.length() - dotIdx - 1;
        };
        result.tolPrecision = std::max(getDecimals(upperStr), getDecimals(lowerStr));

        // 매칭된 전체 태그를 제거하고 본문만 남김
        text.replace(stackMatch.captured(0), baseValue);
    }

    // 2. [구조적 파싱] 대칭 공차 기호 감지 (%%P, ±, \U+00B1) - 중괄호 선택적 적용
    QRegularExpression symTolRegex("(.*?)(?:\\{?.*?)?(%%P|±|\\\\U\\+00B1)\\s*([0-9.]+)(?:.*?\\}?)?");
    QRegularExpressionMatch symMatch = symTolRegex.match(text);
    if (symMatch.hasMatch()) {
        result.tolType = ToleranceType::Symmetrical;
        QString baseValue = symMatch.captured(1).trimmed();
        result.tolUpper = symMatch.captured(3).toDouble();
        result.tolLower = result.tolUpper;
        
        text.replace(symMatch.captured(0), baseValue);
    }

    // 3. 서식 태그 제거 (\H, \C, \f, \A 등)
    // 유니코드 \U 문자가 삭제되지 않도록 하고, 세미콜론 없는 서식으로 인해 문자열이 잘리는 현상 방지
    text.remove(QRegularExpression("\\\\[PLlOo~]"));             // 인자 없는 MText 태그

    //text.remove(QRegularExpression("\\\\[A-Za-z][^;\\\\]*;"));    // 인자 있는 파라미터형 MText 태그 (세미콜론 필수)
    // 수정된 코드 (U와 u 제외)
    text.remove(QRegularExpression("\\\\[A-TV-Za-tv-z][^;\\\\]*;"));
    
    text.replace("{", "");
    text.replace("}", "");
    
    // 4. 심볼 변환 (%%c, %%d 등 및 \U+ 유니코드)
    text = convertSymbols(text);

    // 5. [보완] 괄호 치수 감지 및 분리 (예: (<>) -> prefix='(', suffix=')')
    if (text.startsWith("(") && text.endsWith(")")) {
        QString content = text.mid(1, text.length() - 2).trimmed();
        bool isNumeric = false;
        content.toDouble(&isNumeric);
        if (content.isEmpty() || content == "<>" || isNumeric) {
            result.prefix = "(";
            result.suffix = ")";
            text = (content == "<>" || content.isEmpty()) ? "<>" : content;
        }
    }

    // 6. 자동 측정값(<>) 처리
    if (text.contains("<>") || text.isEmpty()) {
        result.hasAutoMeasurement = true;
        int idx = text.indexOf("<>");
        if (idx >= 0) {
            result.prefix += text.left(idx);
            result.suffix = text.mid(idx + 2) + result.suffix;
        }
        result.cleanText = ""; 
    } else {
        result.hasAutoMeasurement = false;
        result.cleanText = text;
    }

    return result;
}

QString DxfDimensionParser::convertSymbols(const QString& input) {
    QString res = input;
    // %%C, %%c -> Ø, %%D -> °, %%P -> ±
    res.replace("%%C", QString(QChar(0x00D8)), Qt::CaseInsensitive); 
    // res.replace("%%c", QString(QChar(0x00D8)), Qt::CaseInsensitive); // <-- 이 줄은 삭제해도 무방합니다.
    res.replace("%%D", QString(QChar(0x00B0)), Qt::CaseInsensitive); 
    res.replace("%%P", QString(QChar(0x00B1)), Qt::CaseInsensitive); 
    
    return CadUtils::processText(res); 
}

