#ifndef DXFTEXTHANDLER_H
#define DXFTEXTHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF TEXT / MTEXT 엔티티 핸들러
// DXF 파일에서 TEXT 또는 MTEXT 엔티티를 읽어 TextItem으로 복원하고,
// TextItem을 DXF TEXT 형식으로 내보낸다.
// MTEXT는 DxfDispatcher에서 이 핸들러에 별칭으로 등록된다.
// =============================================================================
class DxfTextHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 기본 DXF 엔티티 타입 문자열을 반환한다.
    /// (MTEXT는 Dispatcher에서 별칭으로 추가 등록)
    QString entityType() const override { return "TEXT"; }

    /// DXF 데이터로부터 TextItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 TextItem인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// TextItem을 DXF TEXT 형식으로 스트림에 출력한다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFTEXTHANDLER_H
