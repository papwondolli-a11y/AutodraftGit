#include "hatchpatternmanager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QPainter>
#include <QtMath>

HatchPatternManager::HatchPatternManager() : m_isLoaded(false) {
    // 생성 시점에서는 초기화만 진행합니다.
    // 실제 로드는 AppContext 등에서 loadPatterns()를 호출하여 수행합니다.
}

HatchPatternManager::~HatchPatternManager() {
    m_patterns.clear();
}

bool HatchPatternManager::loadPatterns(const QString& filePath) {
    if (m_isLoaded) return true; // 이미 로드됨

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open hatch pattern file:" << filePath;
        return false;
    }

    QTextStream in(&file);
    HatchPattern currentPattern;
    bool parsingPattern = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(";")) continue; // 빈 줄이거나 주석

        if (line.startsWith("*")) {
            // 새로운 패턴 시작
            if (parsingPattern && !currentPattern.name.isEmpty()) {
                m_patterns.insert(currentPattern.name.toUpper(), currentPattern);
            }
            currentPattern = HatchPattern();
            parsingPattern = true;

            QStringList parts = line.mid(1).split(",", Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                currentPattern.name = parts[0].trimmed();
                if (parts.size() > 1) {
                    currentPattern.description = parts[1].trimmed();
                }
            }
        } else if (parsingPattern) {
            // 패턴 데이터 (angle, x-origin, y-origin, delta-x, delta-y, dash-1, dash-2...)
            QStringList parts = line.split(",", Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                HatchLineDef def;
                bool ok = true;
                bool fieldOk = true;

                def.angle = parts[0].toDouble(&fieldOk); ok &= fieldOk;
                def.x0    = parts[1].toDouble(&fieldOk); ok &= fieldOk;
                def.y0    = parts[2].toDouble(&fieldOk); ok &= fieldOk;
                def.dx    = parts[3].toDouble(&fieldOk); ok &= fieldOk;
                def.dy    = parts[4].toDouble(&fieldOk); ok &= fieldOk;

                if (!ok) {
                    qWarning() << "[HatchPatternManager] Invalid numeric data in pattern line:" << line;
                    continue;
                }

                // 인덱스 기반 루프 사용 (C++11 detach 경고 방지)
                for (int i = 5; i < parts.size(); ++i) {
                    def.dashes.push_back(parts[i].toDouble());
                }
                currentPattern.lines.push_back(def);
            }
        }
    }

    // 마지막 패턴 저장
    if (parsingPattern && !currentPattern.name.isEmpty()) {
        m_patterns.insert(currentPattern.name.toUpper(), currentPattern);
    }

    file.close();
    m_isLoaded = true;
    return true;
}

QStringList HatchPatternManager::getPatternNames() const {
    QStringList names = m_patterns.keys();
    names.sort();
    if (!names.contains("SOLID")) names.prepend("SOLID");
    return names;
}

HatchPattern HatchPatternManager::getPattern(const QString& name) const {
    return m_patterns.value(name.toUpper());
}

QPixmap HatchPatternManager::generatePixmap(const QString& patternName, double scale, double angleOffset, int tileSize) const {
    QPixmap pixmap(tileSize, tileSize);
    pixmap.fill(Qt::white); // 배경 백색

    HatchPattern pat = getPattern(patternName);
    if (pat.name.isEmpty()) return pixmap;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(Qt::black);
    pen.setWidth(1);
    pen.setCosmetic(true);
    painter.setPen(pen);

    QPointF center(tileSize / 2.0, tileSize / 2.0);
    double diag = std::hypot(tileSize, tileSize);

    // 인덱스 기반 루프 사용 (C++11 detach 경고 방지)
    for (int i = 0; i < static_cast<int>(pat.lines.size()); ++i) {
        const HatchLineDef& def = pat.lines[i];

        // 회전 및 스케일 적용
        double radAngle = qDegreesToRadians(def.angle + angleOffset);
        double cosA = std::cos(radAngle);
        double sinA = std::sin(radAngle);

        // 원점 위치 보정
        double x0 = center.x() + (def.x0 * scale);
        double y0 = center.y() - (def.y0 * scale); // Qt는 +Y가 아래

        double dy = def.dy * scale;
        if (std::abs(dy) < 1e-5) dy = 10.0; // 최소 간격 보장 방어코드

        // 선의 방향 단위 벡터
        QPointF pDir(cosA, -sinA);

        // 선 사이 수직 벡터
        double pRad = radAngle + M_PI_2;
        QPointF pStep(std::cos(pRad), -std::sin(pRad));

        // 점선 속성 구성
        QVector<qreal> dashPattern;
        // 인덱스 기반 루프 사용
        for (int k = 0; k < static_cast<int>(def.dashes.size()); ++k) {
            double d = def.dashes[k] * scale;
            if (d > 0) dashPattern << d; // 선
            else if (d < 0) dashPattern << -d; // 공백
            else dashPattern << 0.5; // 점(dot)을 위한 최소크기
        }

        if (!dashPattern.isEmpty()) {
            // Qt 대쉬 패턴은 짝수개여야 함
            if (dashPattern.size() % 2 != 0) dashPattern << dashPattern.last();
            pen.setDashPattern(dashPattern);
            painter.setPen(pen);
        } else {
            pen.setStyle(Qt::SolidLine);
            painter.setPen(pen);
        }

        // 타일을 덮을 수 있는 충분한 범위
        int numLines = std::ceil(diag / std::abs(dy));
        for (int k = -numLines; k <= numLines; ++k) {
            QPointF start = QPointF(x0, y0) + pStep * (k * dy);
            // 선을 화면 크기만큼 양방향으로 연장
            QPointF p1 = start + pDir * diag;
            QPointF p2 = start - pDir * diag;
            painter.drawLine(p1, p2);
        }
    }

    return pixmap;
}