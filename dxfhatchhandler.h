#ifndef DXFHATCHHANDLER_H
#define DXFHATCHHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF HATCH 엔티티 핸들러
// 복원(restore)은 DxfManager에서 별도 처리되므로 nullptr을 반환한다.
// 내보내기(exportDxf)는 HatchItem의 QPainterPath를 폴리곤으로 변환하여
// DXF HATCH 형식으로 출력한다.
// =============================================================================
class DxfHatchHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "HATCH"; }

    /// HATCH 복원은 DxfManager에서 처리하므로 nullptr을 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 HatchItem인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// HatchItem을 DXF HATCH 형식으로 스트림에 출력한다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFHATCHHANDLER_H
