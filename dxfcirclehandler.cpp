#include "dxfcirclehandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "circleitem.h"
#include "mycircle.h"

// =============================================================================
// DxfCircleHandler — CIRCLE 엔티티 복원
// =============================================================================

CadItem* DxfCircleHandler::restore(const DxfEntityData& data,
                                    EntityFactory& factory,
                                    MyGraphicsScene* /*scene*/,
                                    const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                    const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 기본 흰색 펜으로 원을 생성한다.
    QPen pen(Qt::white);
    // 중심점(x1, y1)과 반지름(radius)으로 CircleItem을 생성한다.
    return dynamic_cast<CadItem*>(
        factory.createCircle(QPointF(data.x1, data.y1), data.radius, pen));
}

// =============================================================================
// DxfCircleHandler — CIRCLE 엔티티 내보내기
// =============================================================================

bool DxfCircleHandler::canExport(CadItem* item) const
{
    // CircleItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<CircleItem*>(item) != nullptr;
}

void DxfCircleHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    CircleItem* circle = dynamic_cast<CircleItem*>(item);
    MyCircle c = circle->getCircle();

    manager->writeEntityAcDbHeader(out, "CIRCLE", circle->layerName());
    writeGC(out, 100, QString("AcDbCircle"));
    writeGC(out, 10, c.center.x);
    writeGC(out, 20, c.center.y);
    writeGC(out, 30, 0.0);
    writeGC(out, 40, c.radius);
}
