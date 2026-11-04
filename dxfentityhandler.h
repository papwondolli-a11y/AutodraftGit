#ifndef DXFENTITYHANDLER_H
#define DXFENTITYHANDLER_H

#include "dxfcommon.h"
#include <QString>
#include <QTextStream>
#include <QMap>
#include <QList>

class CadItem;
class EntityFactory;
class MyGraphicsScene;
class DxfManager;

/**
 * @brief DXF 엔티티 처리를 위한 추상 인터페이스
 * 각 엔티티 타입별 독립된 클래스들이 이 인터페이스를 상속받아 구현합니다.
 */
class DxfEntityHandler {
public:
    virtual ~DxfEntityHandler() = default;
    
    // 이 핸들러가 처리하는 DXF 엔티티 타입 (예: "LINE")
    virtual QString entityType() const = 0;
    
    // DXF 데이터를 CadItem 객체로 복원
    virtual CadItem* restore(const DxfEntityData& data, EntityFactory& factory, MyGraphicsScene* scene, 
                             const QMap<QString, DxfDimStyle>& dimStyles,
                             const QMap<QString, QList<CadItem*>>& blocks) = 0;
    
    // CadItem 객체를 DXF 형식으로 내보내기
    virtual bool canExport(CadItem* item) const = 0;
    virtual void exportDxf(QTextStream& out, CadItem* item, DxfManager* manager) = 0;
};

#endif // DXFENTITYHANDLER_H
