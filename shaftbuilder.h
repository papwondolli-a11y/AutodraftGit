#ifndef SHAFTBUILDER_H
#define SHAFTBUILDER_H

#include "mydimension.h"
#include "myvector.h"
#include "shaftdata.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QStringList>
#include <vector>

// 축 한 개를 그리는 데 필요한 도형 묶음 (씬과 무관한 순수 데이터 → MCP 등에서도 재사용 가능)
// 키홈 한 개의 외곽 (열린 폴리라인 + 꼭짓점별 bulge)
struct ShaftKeywayShape {
    std::vector<MyVector> pts;
    std::vector<double>   bulges;
};

struct ShaftGeometry {
    std::vector<MyVector> outline;        // 외형 실루엣 (닫힌 폴리라인)
    QList<QLineF>         edgeLines;      // 모따기·단 경계의 전체 높이 수직선
    QList<QLineF>         threadLines;    // 나사 골지름 표시선 (가는 실선)
    QList<ShaftKeywayShape> keyways;      // 키홈 외곽
    QList<ShaftKeywayShape> breakLines;   // 단면 표시의 파단선 (열린 폴리라인)
    QList<QPainterPath>   hatchPaths;     // 단면 해칭 영역
    bool                  hasCenterLine = false;
    QLineF                centerLine;
    QList<MyDimension>    diameterDims;
    QList<MyDimension>    lengthDims;
};

// ─────────────────────────────────────────────────────────────────────────
// ShaftBuilder — ShaftParams → ShaftGeometry
//   origin = 축 왼쪽 끝면 중심. 축선은 수평, 위쪽이 +Y.
//   지름 치수: 왼쪽 절반 구간은 왼쪽에, 오른쪽 절반 구간은 오른쪽에 쌓는다
//              (축 끝에 가까운 구간이 도형에 가장 가깝다).
//   길이 치수: 도형 아래에 1단=구간별, 2단=전체 길이.
// ─────────────────────────────────────────────────────────────────────────
class ShaftBuilder {
public:
    static ShaftGeometry build(const ShaftParams &params, const QPointF &origin);

    // 입력 문제 목록 (비어 있으면 정상)
    static QStringList validate(const ShaftParams &params);

    // 고스트 미리보기용 경로 (외형 + 모따기선 + 중심선)
    static QPainterPath buildPath(const ShaftParams &params, const QPointF &origin);

private:
    static std::vector<MyVector> buildOutline(const ShaftParams &params, const QPointF &origin,
                                              QList<QLineF> &edgeLines);
    static ShaftParams withThreadChamfers(const ShaftParams &params);
    static void buildEnds(const ShaftParams &params, const QPointF &origin, ShaftGeometry &g);
    static void buildDiameterDims(const ShaftParams &params, const QPointF &origin,
                                  QList<MyDimension> &out);
    static void buildLengthDims(const ShaftParams &params, const QPointF &origin,
                                QList<MyDimension> &out);
};

#endif // SHAFTBUILDER_H
