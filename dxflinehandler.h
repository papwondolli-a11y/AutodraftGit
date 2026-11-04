#ifndef DXFLINEHANDLER_H
#define DXFLINEHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF LINE 엔티티 핸들러
// DXF 파일에서 LINE 엔티티를 읽어 LineItem으로 복원하고,
// LineItem을 DXF LINE 엔티티로 내보낸다.
// =============================================================================
class DxfLineHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "LINE"; }

    /// DXF 데이터로부터 LineItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 LineItem인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// LineItem을 DXF LINE 형식으로 스트림에 출력한다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFLINEHANDLER_H
