#include "dxfarchandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "arcitem.h"
#include "myarc.h"

// =============================================================================
// DxfArcHandler — ARC 엔티티 복원
// =============================================================================

CadItem* DxfArcHandler::restore(const DxfEntityData& data,
                                  EntityFactory& factory,
                                  MyGraphicsScene* /*scene*/,
                                  const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                  const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 기본 흰색 펜으로 호를 생성한다.
    QPen pen(Qt::white);

    // extZ(법선 벡터 Z) 가 음수이면 호가 미러 반전되어 있다.
    // 이 경우 시작·끝 각도를 180도 기준으로 뒤집어 보정한다.
    double actualStart = data.startAngle;
    double actualEnd   = data.endAngle;
    if (data.extZ < 0) {
        actualStart = 180.0 - data.endAngle;
        actualEnd   = 180.0 - data.startAngle;
    }

    // 호의 호각도(spanAngle)를 계산한다. 음수이면 360도를 더해 양수로 만든다.
    double span = actualEnd - actualStart;
    if (span < 0) span += 360.0;

    return dynamic_cast<CadItem*>(
        factory.createArc(QPointF(data.x1, data.y1), data.radius, actualStart, span, pen));
}

// =============================================================================
// DxfArcHandler — ARC 엔티티 내보내기
// =============================================================================

bool DxfArcHandler::canExport(CadItem* item) const
{
    // ArcItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<ArcItem*>(item) != nullptr;
}

void DxfArcHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    ArcItem* arc = dynamic_cast<ArcItem*>(item);
    MyArc a = arc->getArc();

    // DXF ARC는 항상 CCW(반시계방향)다. span < 0 인 CW 호(fillet 등)는
    // start/end를 교체하여 동일 경로를 CCW로 표현한다.
    double gcStart, gcEnd;
    if (a.spanAngle >= 0.0) {
        gcStart = a.startAngle;
        gcEnd   = a.startAngle + a.spanAngle;
    } else {
        gcStart = a.startAngle + a.spanAngle; // CW 호의 끝점 → DXF start
        gcEnd   = a.startAngle;               // CW 호의 시작점 → DXF end
    }
    while (gcStart >= 360.0) gcStart -= 360.0;
    while (gcStart <    0.0) gcStart += 360.0;
    while (gcEnd   >= 360.0) gcEnd   -= 360.0;
    while (gcEnd   <    0.0) gcEnd   += 360.0;

    // AC1015: ARC는 AcDbCircle(중심·반지름) + AcDbArc(각도) 두 서브클래스로 구성된다.
    manager->writeEntityAcDbHeader(out, "ARC", arc->layerName());
    writeGC(out, 100, QString("AcDbCircle"));
    writeGC(out, 10, a.center.x);
    writeGC(out, 20, a.center.y);
    writeGC(out, 30, 0.0);
    writeGC(out, 40, a.radius);
    writeGC(out, 100, QString("AcDbArc"));
    writeGC(out, 50, gcStart);
    writeGC(out, 51, gcEnd);
}
