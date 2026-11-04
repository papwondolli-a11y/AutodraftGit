#ifndef DXFDIMENSIONHANDLER_H
#define DXFDIMENSIONHANDLER_H

#include "dxfentityhandler.h"

// MyDimension은 .cpp에서만 완전 정의가 필요하므로 전방 선언으로 충분하다.
class MyDimension;

// =============================================================================
// DXF DIMENSION 엔티티 핸들러
// dimType 비트 필드를 분석하여 선형·반치수·각도·지름·반지름 치수를
// 적절한 DimensionType으로 복원한다.
// DxfDimStyle 및 DxfEntityData의 오버라이드 값을 병합(mergeProperties)한다.
// 익스포트는 현재 미구현 상태이다.
// =============================================================================
class DxfDimensionHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "DIMENSION"; }

    /// DXF 데이터로부터 치수 아이템(BaseDimensionItem 파생)을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 BaseDimensionItem 파생 타입인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// 치수 DXF 내보내기 (현재 미구현)
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;

private:
    /// DxfDimStyle과 엔티티별 오버라이드 값을 target MyDimension에 병합한다.
    /// sceneDimScale: DXF $DIMSCALE / DWG DIMSCALE 헤더값. DimStyle 레코드에
    /// 개별 DIMSCALE(gc40)이 없을 때 도면 전체 치수 축척으로 사용한다.
    void mergeProperties(MyDimension& target,
                         const DxfEntityData& source,
                         double measurement,
                         const QMap<QString, DxfDimStyle>& dimStyles,
                         double sceneDimScale = 0.0);
};

#endif // DXFDIMENSIONHANDLER_H
