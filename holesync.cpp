#include "holesync.h"
#include "cadcommands.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "holedata.h"
#include "holesectiongenerator.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include <QMessageBox>
#include <QObject>
#include <QUndoStack>
#include <algorithm>

// PropertyDock에서 옮겨온 구멍 연동 로직 — 속성창과 구멍 콜아웃 편집이 함께 사용한다.

// [신규] ci가 HoleTool/HolePTool이 만든 구멍이고 연결된 중심선(HOLE_DATA_GROUP_ID로 태깅된
// 독립 LineItem)이 있으면, 그 중심선을 현재의 방향은 유지한 채 새 outerRadius()에 맞춰
// 중심점 기준으로 다시 늘이거나 줄인다. 태깅된 중심선이 없으면(예: 중심선 없이 생성했거나
// 이 기능이 생기기 전에 만들어진 구멍) 조용히 아무 것도 하지 않는다.
void HoleSync::syncCenterLines(IAppContext *ctx, CadItem *ci) {
    if (!ci) return;
    QString groupId = ci->data(HOLE_DATA_GROUP_ID).toString();
    if (groupId.isEmpty()) return;

    const MyHole *holeData = dynamic_cast<const MyHole *>(ci->getEntityData());
    if (!holeData) return;

    MyGraphicsScene *scene = dynamic_cast<MyGraphicsScene *>(ci->scene());
    if (!scene) return;

    double radius = holeData->outerRadius();
    double ext = std::max(2.0, radius * 0.1);
    double half = radius + ext;
    MyVector center = holeData->position;

    const QList<QGraphicsItem *> items = scene->items();
    for (QGraphicsItem *item : items) {
        if (item == ci) continue;
        if (item->data(HOLE_DATA_GROUP_ID).toString() != groupId) continue;
        LineItem *li = dynamic_cast<LineItem *>(item);
        if (!li) continue;

        MyLine current = li->getLine();
        MyVector dir = (current.end - current.start).normalized();
        if (dir.magnitude() < 0.5) continue; // 길이 0에 가까운 비정상 선은 방향을 알 수 없어 건너뜀

        MyVector newStart = center - dir * half;
        MyVector newEnd = center + dir * half;

        QVariant oldSX = li->getProperties().value("StartX");
        QVariant oldSY = li->getProperties().value("StartY");
        QVariant oldEX = li->getProperties().value("EndX");
        QVariant oldEY = li->getProperties().value("EndY");

        if (ctx && ctx->undoStack()) {
            ctx->undoStack()->push(new ChangePropertyCommand(li, "StartX", oldSX, newStart.x));
            ctx->undoStack()->push(new ChangePropertyCommand(li, "StartY", oldSY, newStart.y));
            ctx->undoStack()->push(new ChangePropertyCommand(li, "EndX", oldEX, newEnd.x));
            ctx->undoStack()->push(new ChangePropertyCommand(li, "EndY", oldEY, newEnd.y));
        } else {
            li->setProperty("StartX", newStart.x);
            li->setProperty("StartY", newStart.y);
            li->setProperty("EndX", newEnd.x);
            li->setProperty("EndY", newEnd.y);
        }
    }
}

// [신규] 탭가공된 구멍의 기본(드릴)지름이 바뀔 때(텍스트 편집/스핀박스 공용) 호출.
// onComboTextPropertyChanged의 ThreadSpec→Diameter 갱신(탭드릴 근사식: 드릴지름 = 호칭지름
// - 피치)을 역으로 적용해, 기존 규격의 피치는 유지한 채 새 호칭지름 = 새 드릴지름 + 피치로
// ThreadSpec 문자열을 다시 계산한다. 이렇게 하지 않으면 threadMajorRadius()가 그대로
// ThreadSpec 문자열에서 호칭지름을 읽어오므로, 드릴지름만 바뀌고 골지름선은 그대로 남는다.
void HoleSync::syncThreadedMajorDiameter(IAppContext *ctx, CadItem *ci, double newDrillDiameter) {
    if (!ci) return;
    const MyHole *holeData = dynamic_cast<const MyHole *>(ci->getEntityData());
    if (!holeData || !holeData->isThreaded) return;

    double majorDia = 0.0, pitch = 0.0;
    if (!MyHole::parseThreadSpec(holeData->threadSpec, majorDia, pitch)) return;

    double newMajorDia = newDrillDiameter + pitch;
    if (newDrillDiameter <= 0.0 || newMajorDia <= 0.0) return;

    QString newSpec = QStringLiteral("M%1x%2")
                           .arg(QString::number(newMajorDia, 'g', 6))
                           .arg(QString::number(pitch, 'g', 6));
    QVariant oldSpec = ci->getProperties().value("ThreadSpec");
    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new ChangePropertyCommand(ci, "ThreadSpec", oldSpec, newSpec));
    } else {
        ci->setProperty("ThreadSpec", newSpec);
    }
}

// [신규] ci(Hole)에 HOLE_SECTION_GROUP_ID로 연결된 단면 LineItem들을 찾아, 같은 배치
// (topCenter/direction)로 만들어진 것들끼리 묶어서 반환한다. HoleSectionTool로 같은 구멍의
// 단면을 여러 곳에 배치했을 수 있으므로(예: 정면도/측면도 각각) 배치별로 독립 처리해야 한다.
QList<QList<QGraphicsItem *>> HoleSync::findSectionInstances(CadItem *ci) {
    QList<QList<QGraphicsItem *>> result;
    if (!ci) return result;
    QString groupId = ci->data(HOLE_DATA_GROUP_ID).toString();
    if (groupId.isEmpty()) return result;

    MyGraphicsScene *scene = dynamic_cast<MyGraphicsScene *>(ci->scene());
    if (!scene) return result;

    QMap<QString, QList<QGraphicsItem *>> buckets;
    const QList<QGraphicsItem *> items = scene->items();
    for (QGraphicsItem *item : items) {
        if (item->data(HOLE_SECTION_GROUP_ID).toString() != groupId) continue;
        QString key = QString("%1|%2|%3|%4")
                          .arg(item->data(HOLE_SECTION_TOP_X).toDouble())
                          .arg(item->data(HOLE_SECTION_TOP_Y).toDouble())
                          .arg(item->data(HOLE_SECTION_DIR_X).toDouble())
                          .arg(item->data(HOLE_SECTION_DIR_Y).toDouble());
        buckets[key].append(item);
    }
    for (auto it = buckets.begin(); it != buckets.end(); ++it) {
        result.append(it.value());
    }
    return result;
}

void HoleSync::highlightSections(CadItem *ci) {
    QList<QList<QGraphicsItem *>> instances = findSectionInstances(ci);
    if (instances.isEmpty()) return;

    MyGraphicsScene *scene = dynamic_cast<MyGraphicsScene *>(ci->scene());
    if (!scene) return;

    scene->clearSelection();
    // for (const QList<QGraphicsItem *> &instance : instances) {
    //     for (QGraphicsItem *item : instance) {
    //         item->setSelected(true);
    //     }
    // }
    for (int i = 0; i < instances.size(); ++i) {
        const QList<QGraphicsItem *> &instance = instances.at(i);
        for (int j = 0; j < instance.size(); ++j) {
            instance.at(j)->setSelected(true);
        }
    }

}

// HoleSectionTool::layerNameForRole과 동일한 규칙 (역할별 레이어가 비어있으면 fallback 사용)
QString HoleSync::layerForSectionRole(const MyHole &hole, HoleSectionLineRole role,
                                          const QString &fallbackLayer) {
    QString layer;
    switch (role) {
    case HoleSectionLineRole::Base: layer = hole.baseLayer; break;
    case HoleSectionLineRole::Thread: layer = hole.threadLayer; break;
    case HoleSectionLineRole::Counterbore: layer = hole.cbLayer; break;
    case HoleSectionLineRole::Countersink: layer = hole.csLayer; break;
    case HoleSectionLineRole::Center: layer = DrawingSettingsCommand::s_centerLineLayer; break;
    }
    return !layer.isEmpty() ? layer : fallbackLayer;
}

// [신규] ci(Hole) 편집 후 호출. 연결된 단면이 있으면 하이라이트 + 확인 다이얼로그를 띄우고,
// "예"를 누른 경우에만 각 배치를 지우고 같은 위치/방향/재료두께로 최신 스펙으로 다시 만든다.
void HoleSync::confirmAndSyncSections(IAppContext *ctx, CadItem *ci, QWidget *parent) {
    if (!ci) return;
    const MyHole *holeData = dynamic_cast<const MyHole *>(ci->getEntityData());
    if (!holeData) return;

    QList<QList<QGraphicsItem *>> instances = findSectionInstances(ci);
    if (instances.isEmpty()) return;

    highlightSections(ci);

    QMessageBox::StandardButton reply = QMessageBox::question(
        parent, QObject::tr("구멍 단면 변경"),
        QObject::tr("이 구멍에 연결된 단면이 있습니다.\n단면도 함께 변경하시겠습니까?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (reply != QMessageBox::Yes) return;

    MyGraphicsScene *scene = dynamic_cast<MyGraphicsScene *>(ci->scene());
    if (!scene || !ctx || !ctx->entityFactory()) return;

    QString groupId = ci->data(HOLE_DATA_GROUP_ID).toString();

    for (int i = 0; i < instances.size(); ++i) {
        const QList<QGraphicsItem *> &instance = instances.at(i);
        if (instance.isEmpty()) continue;

    // for (const QList<QGraphicsItem *> &instance : instances) {
    //     if (instance.isEmpty()) continue;

        QGraphicsItem *anchor = instance.first();
        double topX = anchor->data(HOLE_SECTION_TOP_X).toDouble();
        double topY = anchor->data(HOLE_SECTION_TOP_Y).toDouble();
        double dirX = anchor->data(HOLE_SECTION_DIR_X).toDouble();
        double dirY = anchor->data(HOLE_SECTION_DIR_Y).toDouble();
        double thickness = anchor->data(HOLE_SECTION_MATERIAL_THICKNESS).toDouble();

        // 기존 단면 선 제거
        for (QGraphicsItem *old : instance) {
            if (ctx->undoStack()) {
                ctx->undoStack()->push(new RemoveEntityCommand(scene, old));
            } else {
                scene->removeItem(old);
                delete old;
            }
        }

        // 최신 구멍 스펙으로 같은 위치/방향에 다시 생성
        QList<HoleSectionLine> lines = HoleSectionGenerator::buildLines(
            *holeData, QPointF(topX, topY), thickness, QPointF(dirX, dirY));

        for (int k = 0; k < lines.size(); ++k) {
            const HoleSectionLine &sectionLine = lines.at(k);
            QString layerName = layerForSectionRole(*holeData, sectionLine.role, ci->layerName());

        // for (const HoleSectionLine &sectionLine : lines) {
        //     QString layerName = layerForSectionRole(*holeData, sectionLine.role, ci->layerName());

            // [수정] 색상/두께만 반영하고 라인타입(점선/일점쇄선 등)을 빼먹으면 중심선
            // 레이어여도 항상 실선으로 보인다(레이어를 바꿨다 되돌리면 그제서야 정상 표시되는
            // 것도 이 라인타입 누락 때문). AbstractTool::getLayerPen()/HoleTool::addCenterLines와
            // 동일하게 레이어의 linetype을 조회해서 대시 패턴까지 적용해야 한다.
            QPen pen(Qt::black, 0);
            QGraphicsItem *layerRoot = nullptr;
            if (ctx->layerManager()) {
                Layer *targetLayer = ctx->layerManager()->getLayer(layerName);
                if (targetLayer) {
                    pen = QPen(targetLayer->color());
                    pen.setWidthF(targetLayer->weight());
                    layerRoot = scene->getLayerRoot(targetLayer->name());

                    if (ctx->lineTypeManager()) {
                        QVector<qreal> pattern =
                            ctx->lineTypeManager()->getQtDashPattern(targetLayer->linetype());
                        if (!pattern.isEmpty()) {
                            pen.setStyle(Qt::CustomDashLine);
                            pen.setDashPattern(pattern);
                        } else {
                            pen.setStyle(Qt::SolidLine);
                        }
                    }
                }
            }
            pen.setCosmetic(true);

            QGraphicsItem *newLine = ctx->entityFactory()->createLine(sectionLine.line, pen);
            if (!newLine) continue;

            if (CadItem *nci = dynamic_cast<CadItem *>(newLine)) {
                nci->setLayerName(layerName);
            }
            newLine->setData(HOLE_SECTION_GROUP_ID, groupId);
            newLine->setData(HOLE_SECTION_TOP_X, topX);
            newLine->setData(HOLE_SECTION_TOP_Y, topY);
            newLine->setData(HOLE_SECTION_DIR_X, dirX);
            newLine->setData(HOLE_SECTION_DIR_Y, dirY);
            newLine->setData(HOLE_SECTION_MATERIAL_THICKNESS, thickness);

            if (layerRoot) newLine->setParentItem(layerRoot);
            if (newLine->scene() == scene) scene->removeItem(newLine);

            if (ctx->undoStack()) {
                ctx->undoStack()->push(new AddEntityCommand(scene, newLine));
            } else {
                scene->addItem(newLine);
            }
        }
    }
}
