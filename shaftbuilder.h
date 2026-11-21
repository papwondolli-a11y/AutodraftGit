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
    bool                  closed = false;  // 막힌 키홈(양끝 반원)은 닫힌 폴리라인
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
    // 키홈 하나의 입력 문제 (index = 구간 번호 0~, segCount = 구간 수) — 키홈 설정창에서도 사용
    //   chamferLeft/Right = 그 구간의 좌/우 모따기 (왼쪽/오른쪽 면 기준 막힌 키홈은 모따기만큼 안쪽에서 잰다)
    static QStringList validateKeyway(const ShaftKeyway &k, int index, int segCount,
                                      double diameter, double segLength,
                                      double chamferLeft = 0.0, double chamferRight = 0.0);
    // 기준면 모따기 때문에 키홈이 안쪽으로 밀리는 양 (막힘 + 왼쪽/오른쪽 면 기준일 때만, 그 외 0)
    static double keywayChamferShift(const ShaftKeyway &k, double chamferLeft, double chamferRight);

    // 구간 index 의 키홈 x 범위 [sa, sb] (축 좌표: 왼쪽 끝면 = 0). openL/openR = 축 끝면으로 열림.
    // 키홈이 없으면 false. 2D·3D 공용.
    static bool keywaySpan(const ShaftParams &params, int index, double &sa, double &sb,
                           bool &openL, bool &openR);

    // 고스트 미리보기용 경로 (외형 + 모따기선 + 중심선)
    static QPainterPath buildPath(const ShaftParams &params, const QPointF &origin);

    // 나사부 끝에 나사 높이만큼 모따기를 넣은 실제 외형 파라미터 (2D 외형·3D 회전체 공용)
    static ShaftParams withThreadChamfers(const ShaftParams &params);

private:
    static std::vector<MyVector> buildOutline(const ShaftParams &params, const QPointF &origin,
                                              QList<QLineF> &edgeLines);
    static void buildEnds(const ShaftParams &params, const QPointF &origin, ShaftGeometry &g);
    static void buildDiameterDims(const ShaftParams &params, const QPointF &origin,
                                  QList<MyDimension> &out);
    static void buildLengthDims(const ShaftParams &params, const QPointF &origin,
                                QList<MyDimension> &out);
};

#endif // SHAFTBUILDER_H
