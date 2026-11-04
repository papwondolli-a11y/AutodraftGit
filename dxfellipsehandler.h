#ifndef DXFELLIPSEHANDLER_H
#define DXFELLIPSEHANDLER_H

#include "dxfentityhandler.h"

// =============================================================================
// DXF ELLIPSE 엔티티 핸들러
// 임포트: gc10/20 중심, gc11/21 장축 끝점(중심 기준 벡터), gc40 단축비,
//         gc41/42 시작·끝 매개각(라디안), gc230 법선 Z(음수면 매개각 미러 보정)
// 내보내기: EllipseItem → AC1015 AcDbEllipse 구조
// =============================================================================
class DxfEllipseHandler : public DxfEntityHandler {
public:
    /// 이 핸들러가 처리하는 DXF 엔티티 타입 문자열을 반환한다.
    QString entityType() const override { return "ELLIPSE"; }

    /// DXF 데이터로부터 EllipseItem을 생성해 반환한다.
    CadItem* restore(const DxfEntityData& data,
                     EntityFactory& factory,
                     MyGraphicsScene* scene,
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks) override;

    /// 주어진 아이템이 EllipseItem인지 확인한다.
    bool canExport(CadItem* item) const override;

    /// EllipseItem을 DXF ELLIPSE 형식으로 스트림에 출력한다.
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) override;
};

#endif // DXFELLIPSEHANDLER_H
