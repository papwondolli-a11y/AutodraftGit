#include "dxfpolylinehandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "polylineitem.h"
#include "mypolyline.h"

// =============================================================================
// DxfPolylineHandler — LWPOLYLINE 엔티티 복원
// LWPOLYLINE의 복원은 DxfManager::decomposeLwPolyline에서 처리하므로
// 이 핸들러의 restore는 항상 nullptr을 반환한다.
// =============================================================================

CadItem* DxfPolylineHandler::restore(const DxfEntityData& /*data*/,
                                      EntityFactory& /*factory*/,
                                      MyGraphicsScene* /*scene*/,
                                      const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                      const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // DxfManager에서 직접 처리하므로 이 경로는 사용되지 않는다.
    return nullptr;
}

// =============================================================================
// DxfPolylineHandler — LWPOLYLINE 엔티티 내보내기
// =============================================================================

bool DxfPolylineHandler::canExport(CadItem* item) const
{
    // PolylineItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<PolylineItem*>(item) != nullptr;
}

void DxfPolylineHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    PolylineItem* poly = dynamic_cast<PolylineItem*>(item);
    MyPolyline p = poly->getPolyline();

    manager->writeEntityAcDbHeader(out, "LWPOLYLINE", poly->layerName());
    writeGC(out, 100, QString("AcDbPolyline"));
    writeGC(out, 90, static_cast<int>(p.vertices.size()));
    writeGC(out, 70, p.isClosed ? 1 : 0);

    for (size_t v = 0; v < p.vertices.size(); ++v) {
        writeGC(out, 10, p.vertices[v].x);
        writeGC(out, 20, p.vertices[v].y);
    }
}
