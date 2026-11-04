#include "inputparser.h"
#include <QtMath>
#include <QStringList>
#include <QLineF>

InputParser::InputParser()
{
}

InputParser::~InputParser()
{
}

bool InputParser::parseCoordinate(const QString& input,
                                  const QPointF& basePoint,
                                  const QPointF& mousePos,
                                  bool isOrtho,
                                  QPointF& resultPoint)
{
    QString cmd = input.trimmed();
    bool ok = false;

    // 1. 상대 좌표 처리 (@)
    // 예: "@20,20" -> 현재 위치에서 x+20, y+20
    bool isRelative = false;
    if (cmd.startsWith("@")) {
        isRelative = true;
        cmd = cmd.mid(1); // '@' 제거
    }

    // 2. 극좌표 (<, d, D)
    // 예: "100<45" (거리 100, 각도 45도)
    if (cmd.contains('<') || cmd.contains('d') || cmd.contains('D')) {
        QChar sep = cmd.contains('<') ? '<' : (cmd.contains('d') ? 'd' : 'D');
        QStringList parts = cmd.split(sep);

        if (parts.size() == 2) {
            double dist = parts[0].toDouble(&ok);
            if (!ok) return false;
            dist *= m_unitScale; // [Phase 3] 거리만 환산, 각도는 그대로
            double angleDeg = parts[1].toDouble(&ok);
            if (!ok) return false;

            // [CAD 표준]
            // 0도 = 동쪽 (오른쪽)
            // 90도 = 북쪽 (위쪽) -> 시계 반대 방향(CCW)이 양수(+)

            double angleRad = qDegreesToRadians(angleDeg);
            double dx = dist * qCos(angleRad);
            // [주의] Qt 좌표계는 Y가 아래쪽이 (+)이므로, 위로 가려면 (-)를 해줘야 함
            double dy = dist * qSin(angleRad);

            // 극좌표는 기본적으로 기준점(basePoint)에서의 상대적 위치임
            resultPoint = basePoint + QPointF(dx, dy);
            return true;
        }
    }

    // 3. 절대/상대 좌표 (x,y)
    // 예: "20,30"
    if (cmd.contains(',')) {
        QStringList parts = cmd.split(',');
        if (parts.size() == 2) {
            double x = parts[0].toDouble(&ok);
            if (!ok) return false;
            double y = parts[1].toDouble(&ok);
            if (!ok) return false;

            // [Phase 3] 입력값을 모델(mm)로 환산. basePoint 는 이미 mm 이므로
            // 상대 입력은 델타(x,y)에만 배율을 적용한다.
            x *= m_unitScale;
            y *= m_unitScale;

            if (isRelative) {
                resultPoint = basePoint + QPointF(x, y);
            } else {
                resultPoint = QPointF(x, y);
            }
            return true;
        }
    }

    // 4. 거리 입력 (Direct Distance Entry)
    // 예: "500" (마우스 방향으로 500만큼)
    double dist = cmd.toDouble(&ok);
    if (ok) {
        dist *= m_unitScale; // [Phase 3] 입력 거리 → 모델(mm)

        // 기준점과 마우스 위치를 잇는 벡터 생성
        QLineF vectorLine(basePoint, mousePos);

        // 마우스가 기준점과 겹쳐있으면 (방향 불명) -> 0도(우측) 기본값
        if (vectorLine.length() < 1e-6) {
            vectorLine.setAngle(0);
        }

        // 직교 모드 (F8) 처리
        if (isOrtho) {
            double dx = qAbs(mousePos.x() - basePoint.x());
            double dy = qAbs(mousePos.y() - basePoint.y());

            // X축 이동량이 더 크면 -> 수평선 (Y 고정)
            if (dx > dy) {
                vectorLine.setPoints(basePoint, QPointF(mousePos.x(), basePoint.y()));
            }
            // Y축 이동량이 더 크면 -> 수직선 (X 고정)
            else {
                vectorLine.setPoints(basePoint, QPointF(basePoint.x(), mousePos.y()));
            }
        }

        // 방향은 유지하되, 길이는 입력받은 값으로 설정
        vectorLine.setLength(dist);
        resultPoint = vectorLine.p2();
        return true;
    }

    return false; // 좌표 형식이 아님
}
