#include "polygontool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "myvector.h"
#include "snapmanager.h"
#include <QUndoStack>

#include <QKeyEvent>
#include <QtMath>

static const int kDefaultSides = 6;
static const int kMinSides = 3;
static const int kMaxSides = 1024;

PolygonTool::PolygonTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSides), m_sides(kDefaultSides), m_mode(Mode::Inscribed)
{}

PolygonTool::~PolygonTool() { cancelDrawing(); }

void PolygonTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;

    AbstractTool::activate(scene);
    m_state = WaitSides;
    m_sides = kDefaultSides;
    m_mode  = Mode::Inscribed;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("다각형(POLYGON)"), CommandDock::Command);
    }
    showSidesPrompt();
}

void PolygonTool::deactivate() {
    cancelDrawing();
    AbstractTool::deactivate();
}

void PolygonTool::showSidesPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 변의 수 입력 <%1> : ").arg(m_sides), {});
    }
}

void PolygonTool::showCenterPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(tr(" 중심점을 지정하세요 : "), {});
    }
}

void PolygonTool::showRadiusPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        QStringList options;
        options << "Inscribed(I)" << "Circumscribed(C)";
        QString modeLabel = (m_mode == Mode::Inscribed) ? tr("내접")
                                                         : tr("외접");
        ctx->commandDock()->showInteractivePrompt(
            tr(" 원의 반지름 지정 또는 [내접(I)/외접(C)] <%1> : ").arg(modeLabel),
            options);
    }
}

bool PolygonTool::processCommand(const QString &command) {
    QString cmd = command.trimmed();
    IAppContext *ctx = getContext();
    if (!ctx) return false;

    if (m_state == WaitSides) {
        if (cmd.isEmpty()) {
            // Enter만 입력 → 기본값(마지막 사용값) 유지하고 다음 단계로
            m_state = WaitCenter;
            showCenterPrompt();
            return true;
        }
        bool ok = false;
        int n = cmd.toInt(&ok);
        if (ok && n >= kMinSides && n <= kMaxSides) {
            m_sides = n;
            m_state = WaitCenter;
            showCenterPrompt();
            return true;
        }
        return false;
    }

    if (m_state == WaitCenter) {
        if (ctx->parser()) {
            QPointF resultPos;
            if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), QPointF(0, 0), false, resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                return true;
            }
        }
        return false;
    }

    if (m_state == WaitRadius) {
        QString lower = cmd.toLower();
        if (lower == "i") {
            m_mode = Mode::Inscribed;
            showRadiusPrompt();
            return true;
        }
        if (lower == "c") {
            m_mode = Mode::Circumscribed;
            showRadiusPrompt();
            return true;
        }
        bool ok = false;
        double val = cmd.toDouble(&ok);
        if (ok && val > 0) {
            // 숫자로 직접 입력한 경우 마우스 방향 정보가 없으므로 0도(+X축) 기준으로 그린다.
            finishPolygon(m_center + QPointF(1, 0), val);
            return true;
        }
    }

    return false;
}

void PolygonTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitRadius) ? m_center : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitSides) {
        // 변의 수를 아직 정하지 않은 상태에서 클릭하면 기본값으로 확정하고 진행
        m_state = WaitCenter;
        showCenterPrompt();
        return;
    }

    if (m_state == WaitCenter) {
        m_center = snappedPos;
        m_currentPos = snappedPos;
        m_state = WaitRadius;
        if (m_scene) m_scene->setGhostPath(QPainterPath(), true);
        showRadiusPrompt();
        return;
    }

    // WaitRadius: 클릭 위치 → 반지름 + 회전각 확정
    finishPolygon(snappedPos);
}

void PolygonTool::mouseMove(QPointF pos) {
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitRadius) ? m_center : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }
    m_currentPos = snappedPos;

    if (m_state == WaitRadius) {
        updateGhost(snappedPos);
    }

    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        snapType = ctx->snapManager()->snapPoint(pos, m_scene, m_center).type;
    }
    m_scene->setSnapMarker(m_currentPos, snapType);
}

void PolygonTool::updateGhost(const QPointF &radiusRefPos) {
    double radius = QLineF(m_center, radiusRefPos).length();
    if (radius <= 0.001) return;
    double angle = qAtan2(radiusRefPos.y() - m_center.y(), radiusRefPos.x() - m_center.x());

    QVector<QPointF> verts = computeVertices(m_center, radius, m_sides, m_mode, angle);
    QPainterPath path;
    if (!verts.isEmpty()) {
        path.moveTo(verts.first());
        for (int i = 1; i < verts.size(); ++i) path.lineTo(verts[i]);
        path.closeSubpath();
    }
    updateGhostPreview(path, currentLayerName());
}

QVector<QPointF> PolygonTool::computeVertices(const QPointF &center, double radius,
                                              int sides, Mode mode, double rotationRad) {
    QVector<QPointF> verts;
    if (sides < kMinSides || radius <= 0.0) return verts;

    // 외접 모드는 입력 반지름이 "변의 중점까지 거리(어퍼셈)"이므로,
    // 실제 꼭짓점까지의 거리로 환산해야 한다: R = apothem / cos(π/N)
    double vertexRadius = (mode == PolygonTool::Mode::Circumscribed)
        ? radius / qCos(M_PI / sides)
        : radius;

    verts.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        double theta = rotationRad + (2.0 * M_PI * i) / sides;
        verts.append(QPointF(center.x() + vertexRadius * qCos(theta),
                             center.y() + vertexRadius * qSin(theta)));
    }
    return verts;
}

void PolygonTool::finishPolygon(const QPointF &radiusRefPos, double overrideRadius) {
    if (!m_scene) { cancelDrawing(); emit toolFinished(); return; }

    double radius = (overrideRadius > 0) ? overrideRadius
                                         : QLineF(m_center, radiusRefPos).length();
    double angle = qAtan2(radiusRefPos.y() - m_center.y(), radiusRefPos.x() - m_center.x());

    if (radius > 0.001) {
        QVector<QPointF> verts = computeVertices(m_center, radius, m_sides, m_mode, angle);
        IAppContext *ctx = getContext();
        if (verts.size() >= kMinSides && ctx && ctx->entityFactory() &&
            ctx->layerManager() && ctx->lineTypeManager()) {
            Layer *curLayer = ctx->layerManager()->getCurrentLayer();

            QPen pen(curLayer->color());
            pen.setWidthF(curLayer->weight());
            QString linetypeName = curLayer->linetype();
            if (linetypeName == "Continuous" || linetypeName == "Continous") {
                pen.setStyle(Qt::SolidLine);
            } else {
                pen.setStyle(Qt::CustomDashLine);
                QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(linetypeName);
                if (!pattern.isEmpty()) pen.setDashPattern(pattern);
                else pen.setStyle(Qt::SolidLine);
            }

            std::vector<MyVector> vertices;
            vertices.reserve(verts.size());
            for (const QPointF &p : verts) vertices.push_back(MyVector(p));

            QGraphicsItem *realItem = ctx->entityFactory()->createPolyline(vertices, true, pen);
            if (realItem) {
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
                    ctx->commandDock()->logMessage(tr("다각형 생성 완료."), CommandDock::Info);
                    ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
                }
            }
        }
    }

    cancelDrawing();
    emit toolFinished();
}

void PolygonTool::cancelDrawing() {
    m_state = WaitSides;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

void PolygonTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}
