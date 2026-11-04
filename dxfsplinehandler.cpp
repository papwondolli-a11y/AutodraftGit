#include "dxfsplinehandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "splineitem.h"
#include "myspline.h"

// =============================================================================
// DxfSplineHandler — SPLINE 엔티티 복원
// 데이터 우선순위: 제어점(splineControlPoints) > 피팅점(splineFitPoints) > 폴리 정점
// =============================================================================

CadItem* DxfSplineHandler::restore(const DxfEntityData& data,
                                    EntityFactory& factory,
                                    MyGraphicsScene* /*scene*/,
                                    const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                    const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 사용 가능한 점 집합을 우선순위에 따라 선택한다.
    std::vector<MyVector> pts;
    if (!data.splineControlPoints.empty()) {
        pts = data.splineControlPoints; // 1순위: 제어점
    } else if (!data.splineFitPoints.empty()) {
        pts = data.splineFitPoints;     // 2순위: 피팅점
    } else {
        pts = data.polyVertices;        // 3순위: 폴리라인 정점
    }

    // 점이 2개 미만이면 유효한 스플라인을 만들 수 없다.
    if (pts.size() < 2) return nullptr;

    MySpline splineData(pts, data.splineDegree, data.splineClosed);
    if (!data.splineKnots.empty()) {
        splineData.knots = data.splineKnots;
    }
    QPen pen(Qt::white);
    return dynamic_cast<CadItem*>(factory.createSpline(splineData, pen));
}

// =============================================================================
// DxfSplineHandler — SPLINE 엔티티 내보내기
// 균등 노트 벡터(clamp B-spline)를 자동 계산하여 출력한다.
// 출력 순서: 플래그 → 차수 → 제어점 수 → 제어점 좌표 → 노트 수 → 노트 값
// (원본 코드의 출력 순서를 그대로 유지한다)
// =============================================================================

bool DxfSplineHandler::canExport(CadItem* item) const
{
    // SplineItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<SplineItem*>(item) != nullptr;
}

void DxfSplineHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    SplineItem* spline = dynamic_cast<SplineItem*>(item);
    const MySpline* s = dynamic_cast<const MySpline*>(spline->getEntityData());
    if (!s) return;

    int n = static_cast<int>(s->controlPoints.size());
    if (n < 2) return; // 제어점이 2개 미만이면 유효한 스플라인 불가

    // degree는 반드시 1 이상, n-1 이하여야 한다
    int k = (s->degree >= 1) ? qMin(s->degree, n - 1) : qMin(3, n - 1);
    int numK = n + k + 1;

    // AC1015 순서: 100 AcDbSpline → 법선벡터 → 플래그 → 차수 → 노트수/제어점수 → 노트값 → 제어점
    manager->writeEntityAcDbHeader(out, "SPLINE", spline->layerName());
    writeGC(out, 100, QString("AcDbSpline"));
    writeGC(out, 210, 0.0); // 법선 벡터 X
    writeGC(out, 220, 0.0); // 법선 벡터 Y
    writeGC(out, 230, 1.0); // 법선 벡터 Z (2D이므로 항상 1)
    writeGC(out, 70, s->isClosed ? 1 : 0); // 플래그
    writeGC(out, 71, k);    // 차수
    writeGC(out, 72, numK); // 노트 수
    writeGC(out, 73, n);    // 제어점 수
    writeGC(out, 74, 0);    // 피팅점 수

    // 균등 클램프 B-스플라인 노트 벡터
    for (int i = 0; i < numK; ++i) {
        double knot;
        if (i < k) {
            knot = 0.0;
        } else if (i >= numK - k) {
            knot = static_cast<double>(n - k);
        } else {
            knot = static_cast<double>(i - k);
        }
        writeGC(out, 40, knot);
    }

    // 제어점 좌표 (노트 벡터 다음에 출력 — AC1015 규정 순서)
    for (size_t i = 0; i < s->controlPoints.size(); ++i) {
        writeGC(out, 10, s->controlPoints[i].x);
        writeGC(out, 20, s->controlPoints[i].y);
        writeGC(out, 30, 0.0);
    }
}
