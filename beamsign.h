#ifndef BEAMSIGN_H
#define BEAMSIGN_H

#include <QLineF>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────────
// 보 부호 (blocks/etc/g.cdb 참조)
//   [층 구분][종류][번호]  예) 1G1 = 층 구분 "1" + 종류 "G" + 번호 "1"
//   도형 : 타원형 외곽(높이 6, 반원 반지름 3) + 문자 3개. 종류 글자는 크게(3), 앞뒤는 작게(2).
//   모든 길이는 축척 1, 중심이 (0,0) 인 블록 로컬 좌표이며 삽입 시 도면 축척이 곱해진다.
// ─────────────────────────────────────────────────────────────────────────────
struct BeamMarkParts {
    QString prefix;   // 층 구분 (1, 2, R, F, B1 …) — 비어 있어도 됨
    QString type;     // 종류 (G, B, CG, SG …)
    QString number;   // 번호 (1, 2, 10 …)

    QString compose() const { return prefix + type + number; }
    bool isValid() const { return !type.trimmed().isEmpty() && !number.trimmed().isEmpty(); }
};

// 문자 크기 (축척 1 기준 글자 높이)
struct BeamSignSizes {
    double prefix = 2.0;   // 층 구분
    double type   = 3.0;   // 종류
    double number = 2.0;   // 번호
};

// 문자 1개의 폭 정보 (글자 높이 기준 도면 단위). 글자 사이 간격을 "실제 획" 기준으로 맞추기 위해 사용
struct BeamSignMetrics {
    double advance = 0.0;   // 글자 진행 폭 (좌우 여백 포함)
    double inkLeft = 0.0;   // 진행 폭의 시작점에서 실제 획 왼쪽 끝까지
    double inkRight = 0.0;  // 진행 폭의 시작점에서 실제 획 오른쪽 끝까지
};

namespace BeamSignText {
    // 부호 문자열 분해 : 끝의 숫자 = 번호, 그 앞의 종류(알려진 종류 중 가장 긴 것, 없으면 글자 하나), 나머지 = 층 구분
    BeamMarkParts parse(const QString &name, const QStringList &knownTypes);

    // 종류 / 층 구분 기본 목록 (./data/beam_sign.json 이 있으면 그 값, 없으면 내장 기본값)
    QStringList defaultTypes();
    QStringList defaultPrefixes();

    // 같은 층 구분 + 종류에서 names 중 가장 큰 번호 + 1 (없으면 1)
    int nextNumber(const QStringList &names, const QString &prefix, const QString &type,
                   const QStringList &knownTypes);
}

namespace BeamSignReader {
    // 구조도 .cdb 파일(SQLite)에서 BEAMSIGN 블록을 읽어 {부호 → 도면 수량}. 도면을 열지 않고 파일만 읽는다.
    // 읽기 실패 시 빈 결과 + error
    QMap<QString, int> readCdb(const QString &path, QString *error = nullptr);

    // 1G2 < 1G10 처럼 숫자를 값으로 비교하는 자연 정렬
    QStringList naturalSorted(const QStringList &names);
}

// ─────────────────────────────────────────────────────────────────────────────
// 부호 도형 (블록 로컬 좌표, 축척 1)
// ─────────────────────────────────────────────────────────────────────────────
struct BeamSignArc {
    QPointF center;
    double  radius = 0.0;
    double  startAngle = 0.0;   // 도(deg), CCW
    double  spanAngle  = 0.0;
};

struct BeamSignLabel {
    QString text;
    QString tag;       // 속성 태그 (PREFIX / TYPE / NO)
    QString prompt;    // 속성 입력창 안내문
    QPointF pos;       // 베이스라인 위치 (가운데 정렬)
    double  size = 0.0;
};

struct BeamSignGeom {
    QVector<QLineF>       lines;
    QVector<BeamSignArc>  arcs;
    QVector<BeamSignLabel> texts;
    double                width  = 0.0;   // 외곽 전체 폭
    double                height = 6.0;
};

namespace BeamSignBuilder {
    extern const QString kBlockName;   // "BEAMSIGN"
    extern const QString kTagPrefix;   // "PREFIX"
    extern const QString kTagType;     // "TYPE"
    extern const QString kTagNumber;   // "NO"
    extern const QString kOutLayer;    // "out"
    extern const QString kTextLayer;   // "signT"

    // 문자 폭은 호출 측이 폰트로 계산해서 넘긴다 (도형 계산을 GUI 와 분리)
    BeamSignGeom build(const BeamMarkParts &parts, const BeamSignSizes &sizes,
                       const BeamSignMetrics &prefix, const BeamSignMetrics &type,
                       const BeamSignMetrics &number);
}

#endif // BEAMSIGN_H
