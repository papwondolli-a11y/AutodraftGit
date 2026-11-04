#ifndef DXFSPLINEHANDLER_H
#define DXFSPLINEHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF SPLINE 엔티티 핸들러
// 제어점(splineControlPoints) → 피팅점(splineFitPoints) → 폴리라인 정점
// 순서로 데이터를 선택하여 SplineItem을 복원한다.
// 내보내기는 균등 노트 벡터를 자동으로 계산하여 출력한다.
// =============================================================================
class DxfSplineHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "SPLINE"; }

    /// DXF 데이터로부터 SplineItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 SplineItem인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// SplineItem을 DXF SPLINE 형식으로 스트림에 출력한다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFSPLINEHANDLER_H
