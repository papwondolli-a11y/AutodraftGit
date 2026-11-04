#ifndef DXFLEADERHANDLER_H
#define DXFLEADERHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF LEADER 엔티티 핸들러
// DXF 파일에서 LEADER 엔티티를 읽어 LeaderItem으로 복원한다.
// associatedHandle이 있을 경우 연관 핸들을 아이템 데이터에 저장한다.
// 현재 익스포트는 미지원이다.
// =============================================================================
class DxfLeaderHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "LEADER"; }

    /// DXF 데이터로부터 LeaderItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// LEADER 내보내기 미지원 — 항상 false를 반환한다.
    bool canExport(CadItem* item) const override;

    /// 내보내기 미구현 — 아무 동작도 하지 않는다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFLEADERHANDLER_H
