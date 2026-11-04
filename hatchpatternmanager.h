#ifndef HATCHPATTERNMANAGER_H
#define HATCHPATTERNMANAGER_H

#include <QString>
#include <QMap>
#include <QStringList>
#include <QPixmap>
#include <vector>

struct HatchLineDef {
    double angle;
    double x0;
    double y0;
    double dx;
    double dy;
    std::vector<double> dashes;
};

struct HatchPattern {
    QString name;
    QString description;
    std::vector<HatchLineDef> lines;
};

class HatchPatternManager {
public:
    HatchPatternManager();
    ~HatchPatternManager();

    // 패턴 파일을 읽어들이는 함수
    bool loadPatterns(const QString& filePath);

    // 로드된 패턴 이름 목록 반환 (알파벳 정렬, SOLID 포함)
    QStringList getPatternNames() const;

    // 특정 패턴 데이터 반환
    HatchPattern getPattern(const QString& name) const;

    // 다이얼로그 썸네일 등을 위한 미리보기 픽스맵 생성
    QPixmap generatePixmap(const QString& patternName, double scale, double angleOffset, int tileSize) const;

private:
    QMap<QString, HatchPattern> m_patterns;
    bool m_isLoaded;
};

#endif // HATCHPATTERNMANAGER_H