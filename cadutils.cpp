#include "cadutils.h"
#include <QRegularExpression>

QString CadUtils::processText(const QString &input) {
    if (input.isEmpty()) return input;
    
    QString result = input;
    
    // 1. 유니코드 이스케이프 문자 변환 (\U+00D8 -> Ø)을 가장 먼저 수행하여 삭제 방지
    QRegularExpression uniRegex("\\\\U\\+([0-9A-Fa-f]{4})");
    QRegularExpressionMatchIterator it = uniRegex.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString hex = match.captured(1);
        bool ok;
        int code = hex.toInt(&ok, 16);
        if (ok) {
            result.replace(match.captured(0), QChar(code));
        }
    }

    // 2. AutoCAD MText 포맷팅 코드 제거 (\A1;, \H0.7x;, {\L...} 등)
    // 인자 없는 태그 (\P 등)와 인자 있는 태그 (세미콜론 필수)를 구분하여 과도한 삭제 방지
    result.remove(QRegularExpression("\\\\[PLlOo~]"));
    result.remove(QRegularExpression("\\\\[A-Za-z][^;\\\\]*;"));
    
    // - 남아있는 중괄호 { } 제거 (내용물은 유지)
    result.replace("{", "");
    result.replace("}", "");

    // 3. 고전적인 %% 제어 코드 변환 (대소문자 구분 없음)
    result.replace("%%C", QChar(0x00D8), Qt::CaseInsensitive); // Diameter Ø
    result.replace("%%D", QChar(0x00B0), Qt::CaseInsensitive); // Degree °
    result.replace("%%P", QChar(0x00B1), Qt::CaseInsensitive); // Plus/Minus ±
    
    // 4. 불필요한 공백 정리
    result = result.trimmed();
    
    return result;
}
