#include "dxflinehandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "myline.h"

// =============================================================================
// DxfLineHandler — LINE 엔티티 복원
// =============================================================================

CadItem* DxfLineHandler::restore(const DxfEntityData& data,
                                  EntityFactory& factory,
                                  MyGraphicsScene* /*scene*/,
                                  const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                  const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 기본 흰색 펜으로 선을 생성한다. 레이어 색상은 씬에서 별도 적용된다.
    QPen pen(Qt::white);
    // (x1,y1) → (x2,y2) 좌표로 LineItem을 생성한다.
    return dynamic_cast<CadItem*>(
        factory.createLine(QLineF(data.x1, data.y1, data.x2, data.y2), pen));
}

// =============================================================================
// DxfLineHandler — LINE 엔티티 내보내기
// =============================================================================

bool DxfLineHandler::canExport(CadItem* item) const
{
    // LineItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<LineItem*>(item) != nullptr;
}

void DxfLineHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    LineItem* line = dynamic_cast<LineItem*>(item);
    MyLine l = line->getLine();

    manager->writeEntityAcDbHeader(out, "LINE", line->layerName());
    writeGC(out, 100, QString("AcDbLine"));
    writeGC(out, 10, l.start.x);
    writeGC(out, 20, l.start.y);
    writeGC(out, 30, 0.0);
    writeGC(out, 11, l.end.x);
    writeGC(out, 21, l.end.y);
    writeGC(out, 31, 0.0);

    // 다중선 그룹 태그가 있으면 AUTODRAFT XDATA 로 기록
    // 재임포트 시 L-Corner 툴이 그룹 ID / 역할을 인식할 수 있도록 한다.
    const QString mlGid     = line->data(ML_DATA_GROUP_ID).toString();
    const QString mlRole    = line->data(ML_DATA_ROLE).toString();
    const double  mlSpacing = line->data(ML_DATA_SPACING).toDouble();
    if (!mlGid.isEmpty() && !mlRole.isEmpty()) {
        writeGC(out, 1001, QString("AUTODRAFT"));
        writeGC(out, 1000, QString("ML_GROUP_ID:") + mlGid);
        writeGC(out, 1000, QString("ML_ROLE:")     + mlRole);
        if (mlSpacing > 0.0)
            writeGC(out, 1000, QString("ML_SPACING:%1").arg(mlSpacing, 0, 'f', 4));
    }
}
