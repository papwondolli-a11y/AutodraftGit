#include "holetool.h"
#include "holedata.h"
#include "holedialog.h"
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
#include "mymath.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QKeyEvent>
#include <QLineF>
#include <QTimer>
#include <QUndoStack>
#include <QUuid>
#include <algorithm>

namespace {
// 두 기준선(무한 연장)의 교차점을 구하고, 각 선의 세그먼트가 실제로 뻗어있는
// 방향(교차점에서 더 먼 쪽 끝점 방향)으로 offsetX/offsetY만큼 안쪽으로 이동한 위치를 반환.
MyVector computeCornerOffsetPosition(const MyLine &a, const MyLine &b,
                                     double offsetX, double offsetY, bool &ok) {
    std::vector<MyVector> pts = MyMath::intersectLineLineInfinite(a, b);
    if (pts.empty()) {
        ok = false;
        return MyVector();
    }
    MyVector corner = pts.front();

    MyVector farA = (a.start.distanceTo(corner) > a.end.distanceTo(corner)) ? a.start : a.end;
    MyVector farB = (b.start.distanceTo(corner) > b.end.distanceTo(corner)) ? b.start : b.end;
    MyVector dirA = (farA - corner).normalized();
    MyVector dirB = (farB - corner).normalized();

    ok = true;
    return corner + dirA * offsetX + dirB * offsetY;
}

// 3-2: 도면 스코프 우선(ToolDefaults). 네임스페이스 "HoleTool" = 기존 그룹명.
void loadHoleLayerSettings(MyHole &data, MyGraphicsScene *scene) {
    ToolDefaults d("HoleTool", scene);
    data.baseLayer = d.getString("BaseLayer", QString());
    data.threadLayer = d.getString("ThreadLayer", QString());
    data.cbLayer = d.getString("CbLayer", QString());
    data.csLayer = d.getString("CsLayer", QString());
    data.showCenterLine = d.getBool("ShowCenterLine", true);
}

void saveHoleLayerSettings(const MyHole &data, MyGraphicsScene *scene) {
    ToolDefaults d("HoleTool", scene);
    d.put("BaseLayer", data.baseLayer);
    d.put("ThreadLayer", data.threadLayer);
    d.put("CbLayer", data.cbLayer);
    d.put("CsLayer", data.csLayer);
    d.put("ShowCenterLine", data.showCenterLine);
}
} // namespace

HoleTool::HoleTool(QObject *parent) : AbstractTool(parent) {}

void HoleTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // DrawSubWindow::setTool()은 myScene->setTool()(→ 이 activate() 호출)이 끝난
    // "다음에" toolFinished 시그널을 연결한다. 다이얼로그를 여기서 바로 모달로 띄우면
    // 취소/닫기(X)/ESC로 즉시 reject 되어 emit toolFinished()가 아직 연결되지 않은
    // 시점에 발생 → 신호가 유실되어 툴이 WaitPlacePoint 상태로 남고, 다음 캔버스
    // 클릭에서 기본값(MyHole())으로 구멍이 생성돼 버린다. InsertTool과 동일하게
    // 이벤트 루프가 한 바퀴 돈 뒤(연결이 완료된 뒤) 다이얼로그를 띄워 이를 방지한다.
    QTimer::singleShot(0, this, [this, scene]() {
        if (m_scene != scene) return; // 대기 중 다른 도구로 전환된 경우 무시

        IAppContext *ctx = getContext();

        HoleDialog dlg(ctx ? ctx->layerManager() : nullptr);
        MyHole initData;
        loadHoleLayerSettings(initData, scene);
        dlg.setData(initData);

        if (dlg.exec() != QDialog::Accepted) {
            emit toolFinished();
            return;
        }

        m_pendingData = dlg.resultData();
        saveHoleLayerSettings(m_pendingData, scene);
        m_offsetX = dlg.offsetX();
        m_offsetY = dlg.offsetY();
        m_arrayCountX = dlg.arrayCountX();
        m_arraySpacingX = dlg.arraySpacingX();
        m_arrayCountY = dlg.arrayCountY();
        m_arraySpacingY = dlg.arraySpacingY();

        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->setToolActive(true);
            ctx->commandDock()->logMessage(tr("HOLE (구멍)"), CommandDock::Command);
        }

        resetToStart();
    });
}

void HoleTool::deactivate() {
    if (m_scene) m_scene->clearOverlay();
    AbstractTool::deactivate();
}

// 오프셋이 0,0이면 일반 클릭 배치(WaitPlacePoint), 아니면 두 기준선을 선택해
// 그 교차점에서 오프셋만큼 안쪽으로 이동한 위치에 배치(WaitFirstLine부터 시작)
void HoleTool::resetToStart() {
    m_state = (m_offsetX != 0.0 || m_offsetY != 0.0) ? WaitFirstLine : WaitPlacePoint;
    m_firstLine = MyLine();
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection(); // 구멍 생성 완료(또는 재시작) 시 기준선 하이라이트 해제
    }
    showStartPrompt();
}

void HoleTool::showStartPrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;

    switch (m_state) {
    case WaitPlacePoint:
        ctx->commandDock()->showInteractivePrompt(tr(" 구멍 위치를 지정 하세요 : "), {});
        break;
    case WaitFirstLine:
        ctx->commandDock()->showInteractivePrompt(tr(" 기준선 1 (X축 방향)을 선택 하세요 : "), {});
        break;
    case WaitSecondLine:
        ctx->commandDock()->showInteractivePrompt(tr(" 기준선 2 (Y축 방향)를 선택 하세요 : "), {});
        break;
    }
}

void HoleTool::cancelTool() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    emit toolFinished();
}

bool HoleTool::pickLine(QPointF pos, MyLine &outLine) {
    if (!m_scene) return false;

    QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
    LineItem *li = dynamic_cast<LineItem *>(item);
    if (!li) {
        const QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
        for (int i = 0; i < items.size(); ++i) {
            if ((li = dynamic_cast<LineItem *>(items[i]))) break;
        }
    }
    if (!li) return false;

    outLine = li->getLine();
    li->setSelected(true); // 선택된 기준선 하이라이트 (구멍 생성/취소 시 clearSelection으로 해제)
    return true;
}

void HoleTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void HoleTool::mousePress(QPointF pos, Qt::MouseButton button) {
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
        placeItemAndReset(MyVector(snappedPos));
        return;
    }

    if (m_state == WaitFirstLine) {
        if (pickLine(pos, m_firstLine)) {
            m_state = WaitSecondLine;
            showStartPrompt();
        }
        return;
    }

    // WaitSecondLine
    MyLine secondLine;
    if (!pickLine(pos, secondLine)) return;

    bool ok = false;
    MyVector cornerPos = computeCornerOffsetPosition(m_firstLine, secondLine, m_offsetX, m_offsetY, ok);

    if (!ok) {
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("두 기준선이 평행해서 교차점을 찾을 수 없습니다."), CommandDock::Info);
        resetToStart();
        return;
    }

    placeItemAndReset(cornerPos);
}

void HoleTool::mouseMove(QPointF pos) {
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

// pos를 기준(첫 번째 구멍 위치)으로, X/Y 배열 갯수·간격이 1보다 크면 격자 형태로
// 여러 구멍을 한 번에 생성. 갯수가 둘 다 1이면 단일 구멍 생성과 동일.
void HoleTool::placeItemAndReset(const MyVector &pos) {
    IAppContext *ctx = getContext();
    int countX = std::max(1, m_arrayCountX);
    int countY = std::max(1, m_arrayCountY);

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->beginMacro(QObject::tr("구멍 생성"));
        for (int j = 0; j < countY; ++j) {
            for (int i = 0; i < countX; ++i) {
                createHoleAt(pos + MyVector(i * m_arraySpacingX, j * m_arraySpacingY));
            }
        }
        ctx->undoStack()->endMacro();
    } else {
        for (int j = 0; j < countY; ++j) {
            for (int i = 0; i < countX; ++i) {
                createHoleAt(pos + MyVector(i * m_arraySpacingX, j * m_arraySpacingY));
            }
        }
    }

    // 연속 배치: 같은 스펙으로 다음 위치를 계속 지정 가능 (ESC/우클릭으로 종료)
    resetToStart();
}

// 지정된 위치 하나에 구멍(+ 중심선) 하나를 생성. undo 매크로 경계는 호출부(placeItemAndReset)에서 관리.
void HoleTool::createHoleAt(const MyVector &pos) {
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

    if (data.showCenterLine) addCenterLines(pos, data.outerRadius() * 2.0, groupId);
}

// CircleCenterTool::addCenterLines와 동일한 방식 — 중심선을 HoleItem 자체 도형이
// 아니라 전용 중심선 레이어의 독립된 LineItem 2개(수평/수직)로 생성
void HoleTool::addCenterLines(const MyVector &center, double diameter, const QString &groupId) {
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

    double cx = center.x, cy = center.y;
    double ext = std::max(2.0, radius * 0.1);
    QGraphicsItem *hLine = makeLineItem(QLineF(cx - radius - ext, cy, cx + radius + ext, cy));
    QGraphicsItem *vLine = makeLineItem(QLineF(cx, cy - radius - ext, cx, cy + radius + ext));

    if (ctx->undoStack()) {
        if (hLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, hLine));
        if (vLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, vLine));
    } else {
        if (hLine) m_scene->addItem(hLine);
        if (vLine) m_scene->addItem(vLine);
    }
}
