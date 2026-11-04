#ifndef DXFCIRCLEHANDLER_H
#define DXFCIRCLEHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF CIRCLE 엔티티 핸들러
// DXF 파일에서 CIRCLE 엔티티를 읽어 CircleItem으로 복원하고,
// CircleItem을 DXF CIRCLE 엔티티로 내보낸다.
// =============================================================================
class DxfCircleHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "CIRCLE"; }

    /// DXF 데이터로부터 CircleItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 CircleItem인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// CircleItem을 DXF CIRCLE 형식으로 스트림에 출력한다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFCIRCLEHANDLER_H
