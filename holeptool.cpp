#include "holeptool.h"
#include "arcitem.h"
#include "circleitem.h"
#include "holedata.h"
#include "holepdialog.h"
#include "holeitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QKeyEvent>
#include <QLineF>
#include <QTimer>
#include <QUndoStack>
#include <QUuid>
#include <QtMath>
#include <algorithm>

namespace {
// 3-2: 도면 스코프 우선(ToolDefaults). 네임스페이스 "HolePTool" = 기존 그룹명.
void loadHoleLayerSettings(MyHole &data, MyGraphicsScene *scene) {
    ToolDefaults d("HolePTool", scene);
    data.baseLayer = d.getString("BaseLayer", QString());
    data.threadLayer = d.getString("ThreadLayer", QString());
    data.cbLayer = d.getString("CbLayer", QString());
    data.csLayer = d.getString("CsLayer", QString());
    data.showCenterLine = d.getBool("ShowCenterLine", true);
}

void saveHoleLayerSettings(const MyHole &data, MyGraphicsScene *scene) {
    ToolDefaults d("HolePTool", scene);
    d.put("BaseLayer", data.baseLayer);
    d.put("ThreadLayer", data.threadLayer);
    d.put("CbLayer", data.cbLayer);
    d.put("CsLayer", data.csLayer);
    d.put("ShowCenterLine", data.showCenterLine);
}
} // namespace

HolePTool::HolePTool(QObject *parent) : AbstractTool(parent) {}

void HolePTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // HoleTool과 동일한 이유: DrawSubWindow::setTool()이 myScene->setTool()(→ 이
    // activate() 호출) 완료 "후"에 toolFinished 연결을 맺으므로, 다이얼로그를 바로
    // 모달로 띄우면 취소/닫기(X)/ESC로 즉시 reject 될 때 emit toolFinished()가
    // 아직 연결되지 않아 유실되고 툴이 WaitPlacePoint로 남아 다음 클릭에 기본값
    // 구멍이 생성돼 버린다. 이벤트 루프가 한 바퀴 돈 뒤(연결 완료 후) 띄워 방지.
    QTimer::singleShot(0, this, [this, scene]() {
        if (m_scene != scene) return; // 대기 중 다른 도구로 전환된 경우 무시

        IAppContext *ctx = getContext();

        HolePDialog dlg(ctx ? ctx->layerManager() : nullptr);
        MyHole initData;
        loadHoleLayerSettings(initData, scene);
        dlg.setData(initData);

        if (dlg.exec() != QDialog::Accepted) {
            emit toolFinished();
            return;
        }

        m_pendingData = dlg.resultData();
        saveHoleLayerSettings(m_pendingData, scene);
        m_centerDistance = dlg.centerDistance();
        m_arrayRange = dlg.arrayRange();
        m_arrayCount = dlg.arrayCount();

        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->setToolActive(true);
            ctx->commandDock()->logMessage(tr("HOLE P (구멍 P)"), CommandDock::Command);
        }

        resetToStart();
    });
}

void HolePTool::deactivate() {
    if (m_scene) m_scene->clearOverlay();
    AbstractTool::deactivate();
}

void HolePTool::resetToStart() {
    m_state = WaitPlacePoint;
    m_firstRefCenter = MyVector();
    m_firstRefRadius = 0.0;
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection(); // 기준 원/호 하이라이트 해제
    }
    showStartPrompt();
}

void HolePTool::showStartPrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;

    switch (m_state) {
    case WaitPlacePoint:
        ctx->commandDock()->showInteractivePrompt(tr(" 구멍의 위치를 지정 하세요 <선택(S)> : "), {tr("선택(S)")});
        break;
    case WaitFirstRef:
        ctx->commandDock()->showInteractivePrompt(tr(" 기준 1 (원 또는 호)을 선택 하세요 : "), {});
        break;
    case WaitSecondRef:
        ctx->commandDock()->showInteractivePrompt(tr(" 기준 2 (원 또는 호)를 선택 하세요 : "), {});
        break;
    }
}

void HolePTool::cancelTool() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    emit toolFinished();
}

bool HolePTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    if (m_state == WaitPlacePoint && cmd == "s") {
        m_state = WaitFirstRef;
        if (m_scene) m_scene->clearOverlay(); // WaitPlacePoint에서 남은 고스트 미리보기 제거
        showStartPrompt();
        return true;
    }
    return false;
}

bool HolePTool::pickCircleOrArc(QPointF pos, MyVector &outCenter, double &outRadius) {
    if (!m_scene) return false;

    auto tryPick = [&](QGraphicsItem *item) -> QGraphicsItem * {
        if (CircleItem *ci = dynamic_cast<CircleItem *>(item)) {
            outCenter = ci->getCircle().center;
            outRadius = ci->getCircle().radius;
            return ci;
        }
        if (ArcItem *ai = dynamic_cast<ArcItem *>(item)) {
            outCenter = ai->getArc().center;
            outRadius = ai->getArc().radius;
            return ai;
        }
        return nullptr;
    };

    QGraphicsItem *hit = tryPick(m_scene->itemAt(pos, QTransform()));
    if (!hit) {
        const QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
        for (int i = 0; i < items.size() && !hit; ++i) {
            hit = tryPick(items[i]);
        }
    }
    if (!hit) return false;

    hit->setSelected(true); // 선택된 기준 원/호 하이라이트 (구멍 생성/취소 시 clearSelection으로 해제)
    return true;
}

void HolePTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void HolePTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_state == WaitPlacePoint) {
        IAppContext *ctx = getContext();
        QPointF snappedPos = pos;
        if (ctx && ctx->snapManager()) {
            snappedPos = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
        }
        placeArrayAndReset(MyVector(snappedPos), m_centerDistance);
        return;
    }

    if (m_state == WaitFirstRef) {
        if (pickCircleOrArc(pos, m_firstRefCenter, m_firstRefRadius)) {
            m_state = WaitSecondRef;
            showStartPrompt();
        }
        return;
    }

    // WaitSecondRef: 기준점 = 두 중심점의 정중앙, 거리값 = 두 반지름의 평균
    // (작은 반지름 + (큰 반지름 - 작은 반지름)/2) — 범위/갯수 배열은 동일하게 적용
    MyVector secondCenter;
    double secondRadius = 0.0;
    if (!pickCircleOrArc(pos, secondCenter, secondRadius)) return;

    MyVector anchor = (m_firstRefCenter + secondCenter) * 0.5;
    double smallR = std::min(m_firstRefRadius, secondRadius);
    double bigR = std::max(m_firstRefRadius, secondRadius);
    double distance = smallR + (bigR - smallR) / 2.0;

    placeArrayAndReset(anchor, distance);
}

void HolePTool::mouseMove(QPointF pos) {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene);
        finalPos = res.snappedPos;
        snapType = res.type;
    }
    m_scene->setSnapMarker(finalPos, snapType);

    if (m_state != WaitPlacePoint) return;

    MyHole tmp = m_pendingData;
    tmp.position = MyVector(finalPos);
    HoleItem tmpItem(tmp);
    updateGhostPreview(tmpItem.geometryPath(), currentLayerName());
}

// anchor(기준점)에서 distance만큼 떨어진 위치에 0도부터 시작해서 count개의 구멍을
// 원형 배열. 갯수가 2개 이상이면 range 양 끝점에 첫/마지막 구멍이 오도록
// step = range/(count-1)로 배치(예: 범위 90·갯수 2 → 0도, 90도).
// 단, range가 360도(전체 원)이면 마지막 구멍이 첫 구멍과 겹치지 않도록 예외적으로
// step = range/count 사용(예: 360도·갯수 4 → 0,90,180,270도, 볼트서클 패턴).
// count=1이면 range와 무관하게 0도 방향(거리 0이면 anchor 그 자체) 한 곳에만 생성.
// - 클릭 배치(WaitPlacePoint): distance = 다이얼로그의 "중심으로부터 거리" 입력값
// - [선택(S)] 배치(WaitSecondRef): distance = 선택된 두 원/호 반지름의 평균
//   (작은 반지름 + (큰 반지름 - 작은 반지름)/2)
void HolePTool::placeArrayAndReset(const MyVector &anchor, double distance) {
    IAppContext *ctx = getContext();
    int count = std::max(1, m_arrayCount);
    double step = 0.0;
    if (count > 1) {
        step = (std::abs(m_arrayRange) >= 360.0) ? m_arrayRange / count
                                                 : m_arrayRange / (count - 1);
    }

    auto placeAll = [&]() {
        for (int i = 0; i < count; ++i) {
            double rad = qDegreesToRadians(i * step);
            MyVector pos = anchor + MyVector(distance * qCos(rad), distance * qSin(rad));
            createHoleAt(pos, anchor);
        }
    };

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->beginMacro(QObject::tr("구멍 생성 (P)"));
        placeAll();
        ctx->undoStack()->endMacro();
    } else {
        placeAll();
    }

    // 연속 배치: 같은 스펙으로 다음 배치를 계속 지정 가능 (ESC/우클릭으로 종료)
    resetToStart();
}

// 지정된 위치 하나에 구멍(+ 중심선) 하나를 생성. undo 매크로 경계는 호출부(placeArrayAndReset)에서 관리.
// anchor: 이 구멍이 배치된 기준점 — 중심선을 기준점 방향(반경 방향)으로 축정렬하는 데 사용.
void HolePTool::createHoleAt(const MyVector &pos, const MyVector &anchor) {
    MyHole data = m_pendingData;
    data.position = pos;
    HoleItem *item = new HoleItem(data);

    IAppContext *ctx = getContext();
    QString targetLayerName = !data.baseLayer.isEmpty() ? data.baseLayer : currentLayerName();
    QPen itemPen = currentLayerPen();
    QGraphicsItem *layerRoot = nullptr;

    if (ctx && ctx->layerManager() && m_scene) {
        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);
        if (targetLayer) {
            layerRoot = m_scene->getLayerRoot(targetLayer->name());
            itemPen = QPen(targetLayer->color());
            itemPen.setWidthF(targetLayer->weight());
        }
    }

    item->setLayerName(targetLayerName);
    item->setPen(itemPen);
    if (layerRoot) item->setParentItem(layerRoot);

    // [추가] 중심선(독립 LineItem)과 세션 내에서 짝지을 수 있도록 그룹 ID를 매긴다.
    // 속성 dock에서 지름/나사규격 등이 바뀔 때 이 ID로 중심선을 찾아 길이를 다시 맞춘다.
    QString groupId = QUuid::createUuid().toString();
    item->setData(HOLE_DATA_GROUP_ID, groupId);

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    } else if (m_scene) {
        m_scene->addItem(item);
    }

    if (data.showCenterLine) addCenterLines(pos, data.outerRadius() * 2.0, anchor, groupId);
}

// CircleCenterTool::addCenterLines를 기반으로 하되, 중심선을 항상 수평/수직이 아니라
// 기준점(anchor) → 구멍중심(center) 방향(반경 방향)으로 축정렬해서 생성.
// anchor와 center가 같은 점이면(거리 0) 방향을 정할 수 없으므로 수평/수직으로 대체.
void HolePTool::addCenterLines(const MyVector &center, double diameter, const MyVector &anchor, const QString &groupId) {
    if (!m_scene || diameter <= 0.0) return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory()) return;

    double radius = diameter / 2.0;

    Layer *layer = nullptr;
    if (ctx->layerManager()) {
        const QString &globalLayer = DrawingSettingsCommand::s_centerLineLayer;
        if (!globalLayer.isEmpty())
            layer = ctx->layerManager()->getLayer(globalLayer);
        if (!layer)
            layer = ctx->layerManager()->getCurrentLayer();
    }
    if (!layer) return;

    QPen pen(layer->color());
    pen.setWidthF(layer->weight());
    QString ltName = layer->linetype();
    if (ltName != "Continuous" && ltName != "Continous" && ctx->lineTypeManager()) {
        pen.setStyle(Qt::CustomDashLine);
        QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(ltName);
        if (!pattern.isEmpty())
            pen.setDashPattern(pattern);
        else
            pen.setStyle(Qt::SolidLine);
    }

    auto makeLineItem = [&](const QLineF &line) -> QGraphicsItem * {
        QGraphicsItem *lineGfx = ctx->entityFactory()->createLine(line, pen);
        if (!lineGfx) return nullptr;
        if (LineItem *li = dynamic_cast<LineItem *>(lineGfx))
            li->setLayerName(layer->name());
        lineGfx->setData(HOLE_DATA_GROUP_ID, groupId);
        QGraphicsItem *folder = m_scene->getLayerRoot(layer->name());
        lineGfx->setParentItem(folder);
        if (lineGfx->scene() == m_scene) m_scene->removeItem(lineGfx);
        return lineGfx;
    };

    double ext = std::max(2.0, radius * 0.1);
    double half = radius + ext;

    MyVector radial = center - anchor;
    double radialLen = radial.magnitude();
    MyVector dirRadial = (radialLen > 1e-6) ? radial.normalized() : MyVector(1, 0, 0);
    MyVector dirTangent(-dirRadial.y, dirRadial.x, 0); // 반경 방향에 수직

    QGraphicsItem *hLine = makeLineItem(QLineF((center - dirRadial * half).toPointF(),
                                               (center + dirRadial * half).toPointF()));
    QGraphicsItem *vLine = makeLineItem(QLineF((center - dirTangent * half).toPointF(),
                                               (center + dirTangent * half).toPointF()));

    if (ctx->undoStack()) {
        if (hLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, hLine));
        if (vLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, vLine));
    } else {
        if (hLine) m_scene->addItem(hLine);
        if (vLine) m_scene->addItem(vLine);
    }
}
