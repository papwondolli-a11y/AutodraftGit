#include "dxfdispatcher.h"
// 각 엔티티 핸들러를 독립 모듈로 포함한다.
#include "dxflinehandler.h"
#include "dxfcirclehandler.h"
#include "dxfarchandler.h"
#include "dxftexthandler.h"
#include "dxfdimensionhandler.h"
#include "dxfpolylinehandler.h"
#include "dxfsplinehandler.h"
#include "dxfellipsehandler.h"
#include "dxfleaderhandler.h"
#include "dxfmultileaderhandler.h"
#include "dxfgdthandler.h"
#include "dxfhatchhandler.h"

DxfDispatcher::DxfDispatcher() {
    // 각 엔티티 타입별 독립 핸들러를 등록한다.
    // 등록 순서는 importMap 키 충돌이 없으므로 중요하지 않지만,
    // exportDxf의 canExport 검색 순서에 영향을 준다.
    registerHandler(new DxfLineHandler());
    registerHandler(new DxfCircleHandler());
    registerHandler(new DxfArcHandler());
    registerHandler(new DxfTextHandler());       // MTEXT 별칭도 아래에서 추가 등록됨
    registerHandler(new DxfDimensionHandler());
    registerHandler(new DxfPolylineHandler());
    registerHandler(new DxfSplineHandler());
    registerHandler(new DxfEllipseHandler());
    registerHandler(new DxfLeaderHandler());
    registerHandler(new DxfMultiLeaderHandler());
    registerHandler(new DxfGdtHandler());
    registerHandler(new DxfHatchHandler());
}

DxfDispatcher::~DxfDispatcher() {
    qDeleteAll(m_handlers);
    m_handlers.clear();
    m_importMap.clear();
}

void DxfDispatcher::registerHandler(DxfEntityHandler* handler) {
    m_handlers.append(handler);
    m_importMap.insert(handler->entityType(), handler);
    // MTEXT와 같이 한 핸들러가 여러 타입을 처리하는 경우 추가 등록 가능
    if (handler->entityType() == "TEXT") {
        m_importMap.insert("MTEXT", handler);
        m_importMap.insert("ATTDEF", handler);
        m_importMap.insert("ATTRIB", handler);
    }
}

CadItem* DxfDispatcher::restore(const QString& type, const DxfEntityData& data, EntityFactory& factory, MyGraphicsScene* scene, 
                               const QMap<QString, DxfDimStyle>& dimStyles,
                               const QMap<QString, QList<CadItem*>>& blocks) {
    if (m_importMap.contains(type)) {
        return m_importMap[type]->restore(data, factory, scene, dimStyles, blocks);
    }
    return nullptr;
}

void DxfDispatcher::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) {
    if (!item) return;
    // 익스포트 시에는 어떤 핸들러가 처리 가능한지 순차적으로 확인 (Strategy 패턴)
    for (int i = 0; i < m_handlers.size(); ++i) {
        if (m_handlers[i]->canExport(item)) {
            m_handlers[i]->exportDxf(out, item, manager);
            return;
        }
    }
}
