#include "continuedimensiontool.h"
#include "lineardimensionitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "dimmergetool.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QtMath>

ContinuedimensionTool::ContinuedimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitInitialOption), m_isDrawing(false),
    m_useSelectionMode(false), m_fixedAngle(0.0), m_useFixedAngle(false), m_isHvMode(true) {}

ContinuedimensionTool::~ContinuedimensionTool() { cancelDrawing(); }

void ContinuedimensionTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitInitialOption;
    m_isDrawing = false;
    m_nextOrigins.clear();
    m_useSelectionMode = false;
    m_isHvMode = true; // [추가] 항상 수평/수직 모드로 시작
    m_baseDimData = MyDimension(); // 속성 초기화

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DIMCONTINUE", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫번째 치수보조선 점을 지정 하세요 [Selection/Point/Aligned/HV]<HV> : "), {"s", "p", "a", "hv"});
    }
}

void ContinuedimensionTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

void ContinuedimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        if (m_state == WaitNextOrigins && !m_nextOrigins.isEmpty()) {
            if (m_useSelectionMode) {
                finalizeDimensions(m_selectedDimLine.toPointF());
            } else {
                m_state = WaitDimLinePoint;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->showInteractivePrompt(tr(" 치수선 놓일 위치를 지정 하세요 : "), {});
                }
            }
            return;
        } else {
            onCancel();
            return;
        }
    }

    if (button != Qt::LeftButton) return;

    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitInitialOption && m_state != WaitFirstOrigin) ? m_firstOrigin.toPointF() : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitInitialOption || m_state == WaitFirstOrigin) {
        m_firstOrigin = MyVector(snappedPos);
        m_state = WaitNextOrigins;
        m_isDrawing = true;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 다음 치수 보조선 점을 지정 하세요 : "), {});
        }
    } else if (m_state == WaitBaseDimension) {
        selectBaseDimension(pos);
    } else if (m_state == WaitNextOrigins) {
        m_nextOrigins.append(MyVector(snappedPos));
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                tr(" 다음 치수 보조선 점을 지정 하세요 : (지정 종료:엔터,스페이스,마우스우측) "), {});
        }
    } else if (m_state == WaitDimLinePoint) {
        finalizeDimensions(snappedPos);
    }
}

void ContinuedimensionTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitNextOrigins || m_state == WaitDimLinePoint) ? m_firstOrigin.toPointF() : QPointF();
        SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = result.snappedPos;
        snapType = result.type;
    }

    if (m_scene) {
        m_scene->setSnapMarker(finalPos, snapType);
    }

    if (m_isDrawing) {
        updateGhostPreview();
    }
}

void ContinuedimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void ContinuedimensionTool::onFinish() {
    if (m_state == WaitDimLinePoint) {
        finalizeDimensions(m_currentMousePos);
    }
}

void ContinuedimensionTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void ContinuedimensionTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitInitialOption;
    m_nextOrigins.clear();
    m_baseDimData = MyDimension(); // 속성 초기화
    m_chainItems.clear();
    if (m_scene) {
        m_scene->clearOverlay();
    }
}

void ContinuedimensionTool::selectBaseDimension(const QPointF &pos) {
    if (!m_scene) return;

    QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
    LinearDimensionItem *dim = dynamic_cast<LinearDimensionItem *>(item);
    if (!dim) {
        QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
        for (int i = 0; i < items.size(); ++i) {
            if ((dim = dynamic_cast<LinearDimensionItem *>(items[i]))) break;
        }
    }

    if (dim) {
        const MyDimension &data = dynamic_cast<const MyDimension&>(*dim->getEntityData());

        // [핵심] 선택한 기준 치수선의 공차, 기호, 텍스트 속성을 저장해둡니다.
        m_baseDimData = data;

        m_firstOrigin = data.end;
        m_allPoints.clear();
        m_allPoints.append(data.start);
        m_allPoints.append(data.end);

        m_selectedDimLine = data.dimLine;
        m_state = WaitNextOrigins;
        m_isDrawing = true;

        MyVector v = data.end - data.start;
        double len = v.magnitude();
        if (len > 1e-6) {
            m_useFixedAngle = true;
            m_fixedAngle = qRadiansToDegrees(qAtan2(v.y, v.x));
        } else {
            m_useFixedAngle = false;
        }

        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 다음 치수보조선 점을 지정 하세요 : "), {});
        }
    }
}

void ContinuedimensionTool::finalizeDimensions(const QPointF &lastPos) {
    if (!m_scene || m_nextOrigins.isEmpty()) return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager()) return;

    Layer *curLayer = ctx->layerManager()->getCurrentLayer();
    DimensionStyle style = m_scene->dimensionStyle();

    // 새 치수 생성 시 전역 스타일의 제로 억제 설정을 기본으로 상속 (선택 모드 아닐 때)
    if (!m_useSelectionMode) {
        m_baseDimData.precision = style.precision;
        m_baseDimData.suppressTrailingZeros = style.suppressTrailingZeros;
        m_baseDimData.suppressTrailingZerosTol = style.suppressTrailingZerosTol;
    }

    QString targetLayerName = style.dimLineLayer;
    if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
        targetLayerName = curLayer->name();
    }

    Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

    QPen pen(targetLayer->color());
    pen.setWidthF(targetLayer->weight());

    MyVector currentStart = m_firstOrigin;
    m_dimLinePosition = MyVector(lastPos);

    if (!m_useSelectionMode) {
        m_allPoints.clear();
        m_allPoints.append(m_firstOrigin);
    }

    for (int i = 0; i < m_nextOrigins.size(); ++i) {
        MyVector p2 = m_nextOrigins[i];
        m_allPoints.append(p2);
        MyVector v = p2 - currentStart;
        double len = v.magnitude();
        if (len < 1e-6) continue;

        MyVector dir;
        if (m_isHvMode || (m_useSelectionMode && m_useFixedAngle)) {
            if (i == 0 && m_isHvMode && !m_useSelectionMode) {
                 // 첫 번째 치수에서 수평/수직 판단
                 m_fixedAngle = MyMath::linearDimAngle(currentStart, p2, m_dimLinePosition);
                 m_useFixedAngle = true;
            }
            double rad = qDegreesToRadians(m_fixedAngle);
            dir = MyVector(qCos(rad), qSin(rad));
        } else if (i == 0) {
            m_useFixedAngle = true;
            m_fixedAngle = qRadiansToDegrees(qAtan2(v.y, v.x));
            dir = v / len;
        } else {
            double rad = qDegreesToRadians(m_fixedAngle);
            dir = MyVector(qCos(rad), qSin(rad));
        }

        MyVector normal(-dir.y, dir.x);
        double h = MyMath::dotProduct(m_dimLinePosition - currentStart, normal);

        // [핵심] 빈 구조체가 아닌, 저장해둔 기준 치수선의 공차/기호 속성을 그대로 복사합니다!
        MyDimension data = m_baseDimData;
        data.text = ""; // 텍스트 재지정(Override) 내용은 지우고 실제 측정값이 나오도록 리셋
        data.dimensionType = DimensionType::Linear;
        data.start = currentStart;
        data.end = p2;
        data.dimLine = currentStart + normal * h;
        data.useFixedAngle = true;
        data.fixedAngle = m_fixedAngle;
        data.hideExt1 = (i > 0 || m_useSelectionMode);
        data.hideExt2 = false;

        LinearDimensionItem *item = new LinearDimensionItem(data);
        item->setLayerName(targetLayer->name());
        item->setPen(pen);
        item->setGripsVisible(false);

        QGraphicsItem *layerRoot = m_scene->getLayerRoot(targetLayer->name());
        item->setParentItem(layerRoot);

        if (ctx->undoStack()) {
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        } else {
            m_scene->addItem(item);
        }
        m_chainItems.append(item);

        currentStart = p2;
    }

    if (ctx && ctx->commandDock()) {
        m_state = WaitTotalDimPrompt;
        ctx->commandDock()->showInteractivePrompt(tr(" 전체 치수선을 그리겠습니까? (Y/N) <Y> : "), {"Y", "N"});
    } else {
        onCancel();
    }
}

void ContinuedimensionTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;

    QPainterPath combinedPath;
    MyVector pStart = m_firstOrigin;
     MyVector currentPos = MyVector(m_currentMousePos);
    double liveAngle = m_fixedAngle;
    bool useFixed = m_useFixedAngle;

    // 1. [핵심] 기준 각도 결정 (전체 체인에 동일하게 적용)
    if (m_isHvMode && !m_useSelectionMode) {
        // 첫 번째 분절 혹은 마우스 위치를 기반으로 실시간 결정
        MyVector refPoint = m_nextOrigins.isEmpty() ? currentPos : m_nextOrigins.first();
        if (refPoint != pStart) {
            liveAngle = MyMath::linearDimAngle(pStart, refPoint, currentPos);
            useFixed = true;
        }
    } else if (!m_isHvMode && !m_useSelectionMode) {
        // Aligned 모드: 첫 번째 분절의 실제 기울기 사용
        MyVector refPoint = m_nextOrigins.isEmpty() ? currentPos : m_nextOrigins.first();
        MyVector v = refPoint - pStart;
        double len = v.magnitude();
        if (len > 1e-6) {
            liveAngle = qRadiansToDegrees(qAtan2(v.y, v.x));
            useFixed = true;
        }
    }

    double rad = qDegreesToRadians(liveAngle);
    MyVector dir(qCos(rad), qSin(rad));
    MyVector normal(-dir.y, dir.x);

    // 2. 단일 루프로 모든 분절 미리보기 생성
    MyVector tempStart = pStart;
    for (int i = 0; i <= m_nextOrigins.size(); ++i) {
        MyVector p2 = (i < m_nextOrigins.size()) ? m_nextOrigins[i] : currentPos;
        if (p2.distanceTo(tempStart) < 1e-6) continue;

        double h = MyMath::dotProduct(currentPos - tempStart, normal);
        MyDimension d = m_baseDimData;
        d.dimensionType = DimensionType::Linear;
        d.start = tempStart; 
        d.end = p2; 
        d.dimLine = tempStart + normal * h;
        d.useFixedAngle = useFixed;
        d.fixedAngle = liveAngle;
        d.hideExt1 = (i > 0 || m_useSelectionMode);
        d.hideExt2 = false;

        LinearDimensionItem di(d);
        combinedPath.addPath(di.geometryPath());
        tempStart = p2;
    }

    m_scene->setGhostPath(combinedPath, true, Qt::yellow, 1.0);
}

bool ContinuedimensionTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    IAppContext *ctx = getContext();

    if (m_state == WaitInitialOption) {
        if (cmd == "s") {
            m_state = WaitBaseDimension;
            m_useSelectionMode = true;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->showInteractivePrompt(tr(" 연결할 치수선을 선택 하세요 : "), {});
            }
            return true;
        } else if (cmd == "p") {
            m_state = WaitFirstOrigin;
            m_useSelectionMode = false;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->showInteractivePrompt(tr(" 다음 치수보조선 점을 지정 하세요 : "), {});
            }
            return true;
        } else if (cmd == "a") {
            m_isHvMode = false;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->logMessage("Mode set to: Aligned", CommandDock::Command);
            }
            return true;
        } else if (cmd == "hv" || cmd == "h" || cmd.isEmpty()) {
            m_isHvMode = true;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->logMessage("Mode set to: Horizontal/Vertical", CommandDock::Command);
            }
            // default mode stays, wait for actual origin
            if (cmd.isEmpty()) {
                 m_state = WaitFirstOrigin;
                 m_useSelectionMode = false;
                 if (ctx && ctx->commandDock()) {
                     ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선을 지정 하세요 :(종료:스페이스,엔터,마우스우측) "), {});
                 }
            }
            return true;
        }
    } else if (m_state == WaitTotalDimPrompt) {
        if (cmd == "y" || cmd.isEmpty()) {
            if (m_allPoints.size() >= 2) {
                DimensionStyle style = m_scene->dimensionStyle();
                MyVector pTotalStart = m_allPoints.first();
                MyVector pTotalEnd = m_allPoints.last();

                double rad = qDegreesToRadians(m_fixedAngle);
                MyVector dir(qCos(rad), qSin(rad));
                MyVector normal(-dir.y, dir.x);

                double h_chain = MyMath::dotProduct(m_dimLinePosition - pTotalStart, normal);
                double spacing = style.baselineSpacing * m_scene->drawingScale();
                double h_total = (h_chain >= 0) ? (h_chain + spacing) : (h_chain - spacing);

                MyDimension totalData = m_baseDimData; // 전체 치수에도 속성 복사
                totalData.text = "";
                totalData.dimensionType = DimensionType::Linear;
                totalData.start = pTotalStart;
                totalData.end = pTotalEnd;
                totalData.dimLine = pTotalStart + normal * h_total;
                totalData.useFixedAngle = true;
                totalData.fixedAngle = m_fixedAngle;
                totalData.hideExt1 = false;
                totalData.hideExt2 = false;

                Layer *curLayer = ctx->layerManager()->getCurrentLayer();

                QString targetLayerName = style.dimLineLayer;
                if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
                    targetLayerName = curLayer->name();
                }

                Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

                // 펜 속성도 타겟 레이어의 속성을 따라갑니다.
                QPen pen(targetLayer->color());
                pen.setWidthF(targetLayer->weight());

                LinearDimensionItem *item = new LinearDimensionItem(totalData);
                item->setLayerName(targetLayer->name());
                item->setPen(pen);
                item->setGripsVisible(false);

                item->setParentItem(m_scene->getLayerRoot(targetLayer->name()));

                // 전체 치수보조선과 겹치는 기존 치수보조선은 숨김
                if (ctx->undoStack()) ctx->undoStack()->beginMacro(tr("전체 치수선"));
                DimMergeTool::hideCoincidentExtLines(ctx, m_chainItems, pTotalStart, pTotalEnd);
                if (ctx->undoStack()) {
                    ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
                    ctx->undoStack()->endMacro();
                } else {
                    m_scene->addItem(item);
                }
            }
            onCancel();
            return true;
        } else if (cmd == "n") {
            onCancel();
            return true;
        }
    } else if (m_state == WaitFirstOrigin || m_state == WaitNextOrigins) {
        if (cmd.isEmpty()) {
            // [엔터/스페이스] 우클릭과 동일하게 지정 종료 로직 수행
            if (m_state == WaitNextOrigins && !m_nextOrigins.isEmpty()) {
                if (m_useSelectionMode) {
                    finalizeDimensions(m_selectedDimLine.toPointF());
                } else {
                    m_state = WaitDimLinePoint;
                    if (ctx && ctx->commandDock()) {
                        ctx->commandDock()->showInteractivePrompt(tr(" 치수선 놓일 위치를 지정 하세요 : "), {});
                    }
                }
            } else {
                onCancel();
            }
            return true;
        }
    }
    return false;
}