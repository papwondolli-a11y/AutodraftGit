#include "dxftexthandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "mygraphicsscene.h"
#include "cadutils.h"
#include "textitem.h"

// =============================================================================
// DxfTextHandler — TEXT / MTEXT 엔티티 복원
// =============================================================================

CadItem* DxfTextHandler::restore(const DxfEntityData& data,
                                  EntityFactory& factory,
                                  MyGraphicsScene* scene,
                                  const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                  const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 씬이 있으면 레이어 스타일(색상 등)을 가져온다. 없으면 기본값을 사용한다.
    LayerStyle style = scene ? scene->getLayerStyle(data.layer) : LayerStyle();

    // DXF fontSize가 유효한 경우 사용하고, 0 이하이면 기본값 10pt를 사용한다.
    QFont font;
    font.setPointSizeF(data.fontSize > 0 ? data.fontSize : 10.0);

    // DXF 특수 코드(%%c, %%d 등)를 실제 문자로 변환한다.
    QString processedText = CadUtils::processText(data.text);

    CadItem* item = dynamic_cast<CadItem*>(
        factory.createText(processedText,
                           QPointF(data.x1, data.y1), // 삽입점 / 정렬점
                           font,
                           style.color,               // 레이어 색상
                           data.rotation,             // 회전 각도 (도)
                           data.textAlignH));         // 0=left 1=center 2=right

    TextItem* textItem = dynamic_cast<TextItem*>(item);
    if (textItem && data.isAttribute) {
        textItem->setProperty("isAttribute", true);
        textItem->setProperty("tag", data.attrTag);
        textItem->setProperty("prompt", data.attrPrompt);
    }
    return item;
}

// =============================================================================
// DxfTextHandler — TEXT 엔티티 내보내기
// =============================================================================

bool DxfTextHandler::canExport(CadItem* item) const
{
    // TextItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<TextItem*>(item) != nullptr;
}

void DxfTextHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    TextItem* text = dynamic_cast<TextItem*>(item);
    QMap<QString, QVariant> props = text->getProperties();

    // AC1015: TEXT 서브클래스 마커는 두 번 출력해야 한다 (첫 번째: 좌표·높이·내용, 두 번째: 정렬).
    manager->writeEntityAcDbHeader(out, "TEXT", text->layerName());
    writeGC(out, 100, QString("AcDbText"));
    writeGC(out, 10, props.value("PosX",     0.0).toDouble());
    writeGC(out, 20, props.value("PosY",     0.0).toDouble());
    writeGC(out, 30, 0.0);
    writeGC(out, 40, props.value("FontSize", 10.0).toDouble());
    writeGC(out,  1, props.value("Content",  "").toString());
    writeGC(out, 50, props.value("Rotation", 0.0).toDouble());
    writeGC(out, 100, QString("AcDbText")); // AC1015 규격 — 두 번째 AcDbText 필수
}
