#include "ellipsetool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "ellipseitem.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QLineF>
#include <QUndoStack>
#include <QtMath>
#include <cmath>

static const double kTwoPi = 6.283185307179586;

EllipseTool::EllipseTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitFirst), m_arcMode(false),
      m_startParam(0.0) {}

EllipseTool::~EllipseTool() { cancelDrawing(); }

void EllipseTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;

    AbstractTool::activate(scene);
    m_state = WaitFirst;
    m_arcMode = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("타원(ELLIPSE)"), CommandDock::Command);
    }
    showFirstPrompt();
}

void EllipseTool::deactivate() {
    cancelDrawing();
    AbstractTool::deactivate();
}

// ─────────────────────────────────────────────────────────────────────────
// 프롬프트
// ─────────────────────────────────────────────────────────────────────────
void EllipseTool::showFirstPrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    QStringList options;
    options << "Center(C)";
    if (!m_arcMode) options << "Arc(A)";
    ctx->commandDock()->showInteractivePrompt(
        m_arcMode ? tr(" 타원호 축의 첫 번째 끝점 지정 또는 [중심점(C)] : ")
                  : tr(" 타원의 축 끝점 지정 또는 [호(A)/중심점(C)] : "),
        options);
}

void EllipseTool::showSecondPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 축의 다른 끝점을 지정하세요 : "), {});
}

void EllipseTool::showCenterPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 타원의 중심점을 지정하세요 : "), {});
}

void EllipseTool::showCenterAxisPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 축의 끝점을 지정하세요 : "), {});
}

void EllipseTool::showOtherPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 다른 축의 반거리를 지정하세요 : "), {});
}

void EllipseTool::showStartAnglePrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 시작 각도를 지정하세요(도) : "), {});
}

void EllipseTool::showEndAnglePrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 끝 각도를 지정하세요(도) : "), {});
}

// ─────────────────────────────────────────────────────────────────────────
// 타원 계산
// ─────────────────────────────────────────────────────────────────────────
MyEllipse EllipseTool::buildEllipseByDistance(const QPointF &center,
                                              const QPointF &axisEnd,
                                              double otherDistance) {
    MyEllipse e;
    e.center = MyVector(center);
    MyVector major(axisEnd.x() - center.x(), axisEnd.y() - center.y());
    double a = major.magnitude();
    double d = std::fabs(otherDistance);
    if (a < 1.0e-9 || d < 1.0e-9) {
        e.majorAxis = major;
        e.ratio = 0.0; // 무효 (호출 측에서 검사)
        return e;
    }
    if (d <= a) {
        e.majorAxis = major;
        e.ratio = d / a;
    } else {
        // 입력한 반거리가 더 길면 그 방향이 장축이 된다 (ratio <= 1 유지)
        e.majorAxis = MyVector(-major.y / a * d, major.x / a * d);
        e.ratio = a / d;
    }
    return e;
}

MyEllipse EllipseTool::buildEllipse(const QPointF &center, const QPointF &axisEnd,
                                    const QPointF &otherRef) {
    // 장축선에 수직인 성분을 "다른 축 반거리"로 사용 → 타원이 커서 위치를 지나가게 된다.
    double ax = axisEnd.x() - center.x();
    double ay = axisEnd.y() - center.y();
    double a = std::sqrt(ax * ax + ay * ay);
    double d = 0.0;
    if (a > 1.0e-9) {
        double dx = otherRef.x() - center.x();
        double dy = otherRef.y() - center.y();
        d = std::fabs(-dx * ay / a + dy * ax / a);
    }
    return buildEllipseByDistance(center, axisEnd, d);
}

// ─────────────────────────────────────────────────────────────────────────
// 입력 처리
// ─────────────────────────────────────────────────────────────────────────
bool EllipseTool::processCommand(const QString &command) {
    QString cmd = command.trimmed();
    IAppContext *ctx = getContext();
    if (!ctx) return false;
    QString lower = cmd.toLower();

    if (m_state == WaitFirst) {
        if (lower == "a" || lower == "arc") {
            m_arcMode = true;
            showFirstPrompt();
            return true;
        }
        if (lower == "c" || lower == "center") {
            m_state = WaitCenter;
            showCenterPrompt();
            return true;
        }
    }

    if (m_state == WaitOther) {
        bool ok = false;
        double val = cmd.toDouble(&ok);
        if (ok && val > 0.0) {
            finishOtherAxis(QPointF(), val);
            return true;
        }
    }

    if (m_state == WaitStartAngle || m_state == WaitEndAngle) {
        bool ok = false;
        double deg = cmd.toDouble(&ok);
        if (ok) {
            double param = qDegreesToRadians(deg);
            if (m_state == WaitStartAngle) {
                acceptStartParam(param);
            } else {
                MyEllipse e = m_baseEllipse;
                e.startParam = m_startParam;
                double span = std::fmod(param - m_startParam, kTwoPi);
                if (span < 0.0) span += kTwoPi;
                e.endParam = m_startParam + (span < 1.0e-9 ? kTwoPi : span);
                e.normalizeParams();
                finishEllipse(e);
            }
            return true;
        }
        return false;
    }

    // 좌표 입력(점 지정 단계)
    if (m_state == WaitFirst || m_state == WaitSecond || m_state == WaitCenter ||
        m_state == WaitCenterAxis || m_state == WaitOther) {
        if (ctx->parser()) {
            QPointF base = QPointF(0, 0);
            if (m_state == WaitSecond) base = m_p1;
            else if (m_state == WaitCenterAxis || m_state == WaitOther) base = m_center;
            QPointF resultPos;
            bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());
            if (ctx->parser()->parseCoordinate(lower, base, m_currentPos, isOrtho, resultPos)) {
                handlePoint(resultPos);
                return true;
            }
        }
    }
    return false;
}

void EllipseTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        cancelDrawing();
        emit toolFinished();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = QPointF();
        if (m_state == WaitSecond) base = m_p1;
        else if (m_state == WaitCenterAxis || m_state == WaitOther ||
                 m_state == WaitStartAngle || m_state == WaitEndAngle)
            base = m_center;
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }
    handlePoint(snappedPos);
}

void EllipseTool::handlePoint(const QPointF &pos) {
    switch (m_state) {
    case WaitFirst:
        m_p1 = pos;
        m_state = WaitSecond;
        showSecondPrompt();
        break;
    case WaitSecond:
        if (QLineF(m_p1, pos).length() < 1.0e-9) return;
        m_center = QPointF((m_p1.x() + pos.x()) / 2.0, (m_p1.y() + pos.y()) / 2.0);
        defineAxisEnd(pos, false);
        break;
    case WaitCenter:
        m_center = pos;
        m_state = WaitCenterAxis;
        showCenterAxisPrompt();
        break;
    case WaitCenterAxis:
        if (QLineF(m_center, pos).length() < 1.0e-9) return;
        defineAxisEnd(pos, true);
        break;
    case WaitOther:
        finishOtherAxis(pos);
        break;
    case WaitStartAngle:
        acceptStartParam(m_baseEllipse.paramAt(MyVector(pos)));
        break;
    case WaitEndAngle: {
        double endP = m_baseEllipse.paramAt(MyVector(pos));
        double span = std::fmod(endP - m_startParam, kTwoPi);
        if (span < 0.0) span += kTwoPi;
        MyEllipse e = m_baseEllipse;
        e.startParam = m_startParam;
        e.endParam = m_startParam + (span < 1.0e-9 ? kTwoPi : span);
        e.normalizeParams();
        finishEllipse(e);
        break;
    }
    }
}

void EllipseTool::defineAxisEnd(const QPointF &axisEnd, bool fromCenter) {
    Q_UNUSED(fromCenter);
    m_axisEnd = axisEnd;
    m_state = WaitOther;
    if (m_scene) m_scene->setGhostPath(QPainterPath(), true);
    showOtherPrompt();
}

void EllipseTool::finishOtherAxis(const QPointF &otherRef, double overrideDistance) {
    MyEllipse e = (overrideDistance > 0.0)
                      ? buildEllipseByDistance(m_center, m_axisEnd, overrideDistance)
                      : buildEllipse(m_center, m_axisEnd, otherRef);
    if (e.ratio < 1.0e-6 || e.majorRadius() < 1.0e-9) return; // 무효 입력은 무시하고 재입력 대기

    if (m_arcMode) {
        m_baseEllipse = e;
        m_state = WaitStartAngle;
        if (m_scene) m_scene->setGhostPath(QPainterPath(), true);
        showStartAnglePrompt();
        return;
    }
    finishEllipse(e);
}

void EllipseTool::acceptStartParam(double param) {
    m_startParam = param;
    m_state = WaitEndAngle;
    showEndAnglePrompt();
}

void EllipseTool::finishEllipse(const MyEllipse &ellipse) {
    IAppContext *ctx = getContext();
    if (m_scene && ctx && ctx->entityFactory() && ctx->layerManager()) {
        Layer *curLayer = ctx->layerManager()->getCurrentLayer();
        QPen pen = currentLayerPen();

        QGraphicsItem *realItem = ctx->entityFactory()->createEllipse(ellipse, pen);
        if (realItem && curLayer) {
            if (CadItem *ci = dynamic_cast<CadItem *>(realItem)) {
                ci->setLayerName(curLayer->name());
            }
            QGraphicsItem *layerFolder = m_scene->getLayerRoot(curLayer->name());
            realItem->setParentItem(layerFolder);
            if (realItem->scene() == m_scene) m_scene->removeItem(realItem);

            if (ctx->undoStack()) {
                ctx->undoStack()->push(new AddEntityCommand(m_scene, realItem));
            } else {
                m_scene->addItem(realItem);
            }

            if (ctx->commandDock()) {
                ctx->commandDock()->logMessage(tr("타원 생성 완료."), CommandDock::Info);
                ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
            }
        } else if (realItem) {
            delete realItem;
        }
    }
    cancelDrawing();
    emit toolFinished();
}

// ─────────────────────────────────────────────────────────────────────────
// 마우스 이동 / 미리보기
// ─────────────────────────────────────────────────────────────────────────
void EllipseTool::mouseMove(QPointF pos) {
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF base = QPointF();
    if (m_state == WaitSecond) base = m_p1;
    else if (m_state == WaitCenterAxis || m_state == WaitOther ||
             m_state == WaitStartAngle || m_state == WaitEndAngle)
        base = m_center;

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        auto result = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = result.snappedPos;
        snapType = result.type;
    }
    m_currentPos = finalPos;

    updateGhost(finalPos);
    m_scene->setSnapMarker(finalPos, snapType);
}

void EllipseTool::updateGhost(const QPointF &pos) {
    switch (m_state) {
    case WaitSecond:
        updateGhostLinePreview(QLineF(m_p1, pos), currentLayerName());
        break;
    case WaitCenterAxis:
        updateGhostLinePreview(QLineF(m_center, pos), currentLayerName());
        break;
    case WaitOther: {
        MyEllipse e = buildEllipse(m_center, m_axisEnd, pos);
        if (e.ratio < 1.0e-6 || e.majorRadius() < 1.0e-9) return;
        updateGhostPreview(EllipseItem::createQtPath(e), currentLayerName());
        break;
    }
    case WaitStartAngle: {
        QPainterPath path = EllipseItem::createQtPath(m_baseEllipse);
        path.moveTo(m_center);
        path.lineTo(pos);
        updateGhostPreview(path, currentLayerName());
        break;
    }
    case WaitEndAngle: {
        MyEllipse e = m_baseEllipse;
        double endP = m_baseEllipse.paramAt(MyVector(pos));
        double span = std::fmod(endP - m_startParam, kTwoPi);
        if (span < 0.0) span += kTwoPi;
        e.startParam = m_startParam;
        e.endParam = m_startParam + (span < 1.0e-9 ? kTwoPi : span);
        e.normalizeParams();
        updateGhostPreview(EllipseItem::createQtPath(e), currentLayerName());
        break;
    }
    default:
        break;
    }
}

void EllipseTool::cancelDrawing() {
    m_state = WaitFirst;
    m_arcMode = false;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

void EllipseTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}
