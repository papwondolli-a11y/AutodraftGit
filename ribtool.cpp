#include "ribtool.h"
#include "stairplandialog.h"
#include "stairsymboldata.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainterPath>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 형상 계산
// ─────────────────────────────────────────────────────────────────────────────
int RibTool::countFor(double length, const RibParams &p)
{
    const double w = kRibWidth;   // 가로 크기는 고정
    if (w <= 1e-9 || length < w) return 0;
    const double step = w;   // 간격 없이 이어 붙임
    return int(std::floor((length - w) / step + 1e-9)) + 1;
}

QVector<QLineF> RibTool::buildLines(const QPointF &p1, const QPointF &p2, const RibParams &p)
{
    QVector<QLineF> out;
    const double len = QLineF(p1, p2).length();
    const int n = countFor(len, p);
    if (n <= 0) return out;

    const double ang  = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());
    const double c    = std::cos(ang);
    const double s    = std::sin(ang);
    const double w    = kRibWidth;   // 가로 크기는 고정
    const double sy   = p.height / kRibHeight;   // 세로 비율 = 입력 높이 / 원본 높이(20)
    const double step = w;   // 간격 없이 이어 붙임

    for (int i = 0; i < n; ++i) {
        const double d = i * step;                     // P1 에서 심볼 기준점까지 거리
        const QPointF ctr(p1.x() + d * c, p1.y() + d * s);
        for (int k = 0; k < kRibLinesCount; ++k) {
            const double x1 = kRibLines[k][0], y1 = kRibLines[k][1] * sy;
            const double x2 = kRibLines[k][2], y2 = kRibLines[k][3] * sy;
            out.append(QLineF(ctr.x() + x1 * c - y1 * s, ctr.y() + x1 * s + y1 * c,
                              ctr.x() + x2 * c - y2 * s, ctr.y() + x2 * s + y2 * c));
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// RibTool
// ─────────────────────────────────────────────────────────────────────────────
RibTool::RibTool(const RibParams &params, QObject *parent)
    : AbstractTool(parent), m_state(WaitP1), m_params(params)
{}

void RibTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitP1;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("Rib"), CommandDock::Command);
    }
    showPrompt();
}

void RibTool::deactivate()
{
    cancelDrawing();
    AbstractTool::deactivate();
}

void RibTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        m_state == WaitP1 ? tr(" 첫번째 점(P1)을 지정하세요 : ")
                          : tr(" 두번째 점(P2)을 지정하세요 : "), {});
}

void RibTool::cancelDrawing()
{
    m_state = WaitP1;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

bool RibTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->parser()) return false;
    const QPointF base = (m_state == WaitP2) ? m_p1 : QPointF(0, 0);
    QPointF result;
    if (ctx->parser()->parseCoordinate(command.trimmed(), base, base, false, result)) {
        mousePress(result, Qt::LeftButton);
        return true;
    }
    return false;
}

void RibTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        m_scene->setGhostPath(QPainterPath(), false);
        if (m_state == WaitP2) { m_state = WaitP1; showPrompt(); }
        else                   { cancelDrawing(); emit toolFinished(); }
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        const QPointF base = (m_state == WaitP2) ? m_p1 : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitP1) {
        m_p1 = snapped;
        m_state = WaitP2;
        showPrompt();
        return;
    }

    if (QLineF(m_p1, snapped).length() < 1e-6) return;
    m_p2 = snapped;
    m_scene->setGhostPath(QPainterPath(), false);
    if (!showSetupDialog()) {
        cancelDrawing();
        emit toolFinished();
        return;
    }
    finish();
}

void RibTool::mouseMove(QPointF pos)
{
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        const QPointF base = (m_state == WaitP2) ? m_p1 : QPointF();
        auto r = ctx->snapManager()->snapPoint(pos, m_scene, base);
        snapped = r.snappedPos;
        snapType = r.type;
    }
    if (m_state == WaitP2) {
        // 현재 설정(간격·배율)으로 심볼 배열 미리보기
        const QVector<QLineF> lines = buildLines(m_p1, snapped, m_params);
        QPainterPath path;
        path.moveTo(m_p1);
        path.lineTo(snapped);
        for (int i = 0; i < lines.size(); ++i) {
            path.moveTo(lines.at(i).p1());
            path.lineTo(lines.at(i).p2());
        }
        updateGhostPreview(path, m_params.layer);
    }
    m_scene->setSnapMarker(snapped, snapType);
}

void RibTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

bool RibTool::showSetupDialog()
{
    IAppContext *ctx = getContext();
    if (!ctx) return false;
    QWidget *parentWin = dynamic_cast<QWidget *>(ctx->commandDock());
    RibDialog dlg(m_params, QLineF(m_p1, m_p2).length(), ctx->layerManager(), parentWin);
    if (dlg.exec() != QDialog::Accepted) return false;
    m_params = dlg.params();
    saveParams(m_scene, m_params);
    return true;
}

void RibTool::ensureLayer(IAppContext *ctx, const QString &name)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, QColor(255, 255, 255), 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

void RibTool::finish()
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) { cancelDrawing(); emit toolFinished(); return; }

    const QVector<QLineF> lines = buildLines(m_p1, m_p2, m_params);
    if (lines.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("P1-P2 길이(%1)가 심볼 가로 크기(%2)보다 짧아 배치할 수 없습니다.")
                    .arg(QLineF(m_p1, m_p2).length(), 0, 'f', 1)
                    .arg(kRibWidth, 0, 'f', 1),
                CommandDock::Info);
        cancelDrawing();
        emit toolFinished();
        return;
    }

    const QString layerName = AbstractTool::resolveLayerName(ctx, m_params.layer);
    ensureLayer(ctx, layerName);
    const QPen pen = AbstractTool::layerPenFor(ctx, layerName);

    if (ctx->undoStack()) ctx->undoStack()->beginMacro(tr("Rib"));
    for (int i = 0; i < lines.size(); ++i) {
        QGraphicsItem *item = ctx->entityFactory()->createLine(lines.at(i), pen);
        if (!item) continue;
        if (CadItem *ci = dynamic_cast<CadItem *>(item)) ci->setLayerName(layerName);
        item->setParentItem(m_scene->getLayerRoot(layerName));
        if (item->scene() == m_scene) m_scene->removeItem(item);
        if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        else                  m_scene->addItem(item);
    }
    if (ctx->undoStack()) ctx->undoStack()->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("Rib 완료 — 심볼 %1개").arg(lines.size() / kRibLinesCount), CommandDock::Info);
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    }
    cancelDrawing();
    emit toolFinished();
}

RibParams RibTool::loadParams(MyGraphicsScene *scene)
{
    RibParams p;
    ToolDefaults d("RibTool", scene);
    p.height = d.getDouble("Height", p.height);
    p.layer = d.getString("Layer", p.layer);
    return p;
}

void RibTool::saveParams(MyGraphicsScene *scene, const RibParams &p)
{
    ToolDefaults d("RibTool", scene);
    d.put("Height", p.height);
    d.put("Layer", p.layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// RibDialog
// ─────────────────────────────────────────────────────────────────────────────
RibDialog::RibDialog(const RibParams &params, double length, LayerManager *layerMgr,
                           QWidget *parent)
    : QDialog(parent), m_length(length)
{
    setWindowTitle(tr("Rib"));
    setModal(true);

    m_spinHeight = new QDoubleSpinBox(this);
    m_spinHeight->setRange(1.0, 100000.0);
    m_spinHeight->setDecimals(1);
    m_spinHeight->setSingleStep(1.0);
    m_spinHeight->setValue(params.height);

    m_comboLayer = new QComboBox(this);
    QString sel = params.layer;
    if (sel.isEmpty() && layerMgr && layerMgr->getCurrentLayer())
        sel = layerMgr->getCurrentLayer()->name();
    StairPlanDialog::populateLayerCombo(m_comboLayer, layerMgr, sel);

    m_labelInfo = new QLabel(this);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("심볼 높이 (Height)"), m_spinHeight);
    form->addRow(tr("레이어"), m_comboLayer);
    form->addRow(m_labelInfo);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QVBoxLayout *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(m_spinHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &RibDialog::updateInfo);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateInfo();
}

void RibDialog::updateInfo()
{
    const RibParams p = params();
    m_labelInfo->setText(tr("P1-P2 길이 %1 / 심볼 크기 %2 x %3 → %4개 배치")
                             .arg(m_length, 0, 'f', 1)
                             .arg(kRibWidth, 0, 'f', 1)
                             .arg(p.height, 0, 'f', 1)
                             .arg(RibTool::countFor(m_length, p)));
}

RibParams RibDialog::params() const
{
    RibParams p;
    p.height = m_spinHeight->value();
    p.layer = m_comboLayer->currentData().toString();
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// RibCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *RibCommand::createCommand(IAppContext *c)
{
    return new RibCommand(c);
}

RibCommand::RibCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void RibCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new RibTool(RibTool::loadParams(m_ctx->activeScene())));
}
