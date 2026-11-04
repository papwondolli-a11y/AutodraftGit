#ifndef DXFMULTILEADERHANDLER_H
#define DXFMULTILEADERHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF MULTILEADER 엔티티 핸들러
// mlContentPos, mlDoglegPos, mlArrowTip 데이터를 분석하여
// 짧은 텍스트(≤2자)이면 DatumItem, 그 외에는 LeaderItem으로 복원한다.
// 현재 익스포트는 미지원이다.
// =============================================================================
class DxfMultiLeaderHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "MULTILEADER"; }

    /// DXF 데이터로부터 LeaderItem 또는 DatumItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// MULTILEADER 내보내기 미지원 — 항상 false를 반환한다.
    bool canExport(CadItem* item) const override;

    /// 내보내기 미구현 — 아무 동작도 하지 않는다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFMULTILEADERHANDLER_H
