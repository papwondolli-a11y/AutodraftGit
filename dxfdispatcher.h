#ifndef DXFDISPATCHER_H
#define DXFDISPATCHER_H

#include <QMap>
#include <QString>
#include <QList>
#include "dxfentityhandler.h"

class CadItem;
class EntityFactory;
class MyGraphicsScene;
class DxfManager;

/**
 * @brief DXF 엔티티 핸들러들을 관리하고 작업을 분배하는 디스패처 클래스
 */
class DxfDispatcher {
public:
    DxfDispatcher();
    ~DxfDispatcher();

    // 임포트 시 타입 문자열에 맞는 핸들러 호출
    CadItem* restore(const QString& type, const DxfEntityData& data, EntityFactory& factory, MyGraphicsScene* scene, 
                     const QMap<QString, DxfDimStyle>& dimStyles,
                     const QMap<QString, QList<CadItem*>>& blocks);

    // 익스포트 시 객체 타입에 맞는 핸들러 호출
    void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager);

private:
    void registerHandler(DxfEntityHandler* handler);

    // 임포트용 맵 (타입 문자열 -> 핸들러)
    QMap<QString, DxfEntityHandler*> m_importMap;
    // 익스포트용 리스트 (순차적으로 canExport 확인)
    QList<DxfEntityHandler*> m_handlers;
};

#endif // DXFDISPATCHER_H
