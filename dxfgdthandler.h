#ifndef DXFGDTHANDLER_H
#define DXFGDTHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF TOLERANCE (GDT) 엔티티 핸들러
// %%v 구분자로 분리된 GDT 텍스트를 파싱하여
// 데이텀 기호이면 DatumItem, GDT 프레임이면 GdtItem으로 복원한다.
// 내보내기: GdtItem → TOLERANCE(AcDbFcf) 엔티티 출력.
//           지시선이 있으면 LEADER 엔티티도 함께 출력한다.
// =============================================================================
class DxfGdtHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "TOLERANCE"; }

    /// DXF 데이터로부터 GdtItem 또는 DatumItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// GdtItem이면 true를 반환한다.
    bool canExport(CadItem* item) const override;

    /// GdtItem을 TOLERANCE(AcDbFcf) 엔티티로 내보낸다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFGDTHANDLER_H
