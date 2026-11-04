#include "insulationtool.h"
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

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainterPath>
#include <QRadioButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 형상 계산
// ─────────────────────────────────────────────────────────────────────────────
double InsulationTool::unitWidth(int type)
{
    return type == 2 ? kIns2Width : kIns1Width;
}

int InsulationTool::countFor(double length, const InsulationParams &p)
{
    const double w = unitWidth(p.type) * p.height;   // 균등 확대: 가로도 height 배
    if (w <= 1e-9 || length < w) return 0;
    return int(std::floor(length / w + 1e-9));
}

QVector<InsulationArc> InsulationTool::buildArcs(const QPointF &p1, const QPointF &p2,
                                                 const InsulationParams &p)
{
    QVector<InsulationArc> out;
    const double len = QLineF(p1, p2).length();
    const int n = countFor(len, p);
    if (n <= 0) return out;

    const double angRad = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());
    const double angDeg = qRadiansToDegrees(angRad);
    const double c = std::cos(angRad);
    const double s = std::sin(angRad);
    const double sc = p.height / (p.type == 2 ? kIns2Height : kIns1Height);   // 원본 높이 1 → 입력 높이
    const double w  = unitWidth(p.type) * sc;

    const double (*data)[5] = (p.type == 2) ? kIns2Arcs : kIns1Arcs;
    const int count = (p.type == 2) ? kIns2ArcsCount : kIns1ArcsCount;

    for (int i = 0; i < n; ++i) {
        const double d = i * w;                        // P1 에서 심볼 기준점까지 거리
        const QPointF origin(p1.x() + d * c, p1.y() + d * s);
        for (int k = 0; k < count; ++k) {
            const double lx = data[k][0] * sc, ly = data[k][1] * sc;
            InsulationArc a;
            a.center   = QPointF(origin.x() + lx * c - ly * s, origin.y() + lx * s + ly * c);
            a.radius   = data[k][2] * sc;
            a.startDeg = data[k][3] + angDeg;
            a.spanDeg  = data[k][4];
            out.append(a);
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// InsulationTool
// ─────────────────────────────────────────────────────────────────────────────
InsulationTool::InsulationTool(const InsulationParams &params, QObject *parent)
    : AbstractTool(parent), m_state(WaitP1), m_params(params)
{}

void InsulationTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitP1;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("단열재"), CommandDock::Command);
    }
    showPrompt();
}

void InsulationTool::deactivate()
{
    cancelDrawing();
    AbstractTool::deactivate();
}

void InsulationTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        m_state == WaitP1 ? tr(" 첫번째 점(P1)을 지정하세요 : ")
                          : tr(" 두번째 점(P2)을 지정하세요 : "), {});
}

void InsulationTool::cancelDrawing()
{
    m_state = WaitP1;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

bool InsulationTool::processCommand(const QString &command)
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

void InsulationTool::mousePress(QPointF pos, Qt::MouseButton button)
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

void InsulationTool::mouseMove(QPointF pos)
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
        // 저장된 설정으로 배열 미리보기 (호는 점 샘플링 폴리라인으로 근사)
        const QVector<InsulationArc> arcs = buildArcs(m_p1, snapped, m_params);
        QPainterPath path;
        path.moveTo(m_p1);
        path.lineTo(snapped);
        for (int i = 0; i < arcs.size(); ++i) {
            const InsulationArc &a = arcs.at(i);
            const int seg = qMax(4, int(std::fabs(a.spanDeg) / 10.0));
            for (int j = 0; j <= seg; ++j) {
                const double t = qDegreesToRadians(a.startDeg + a.spanDeg * j / seg);
                const QPointF pt(a.center.x() + a.radius * std::cos(t),
                                 a.center.y() + a.radius * std::sin(t));
                if (j == 0) path.moveTo(pt);
                else        path.lineTo(pt);
            }
        }
        updateGhostPreview(path, m_params.layer);
    }
    m_scene->setSnapMarker(snapped, snapType);
}

void InsulationTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

bool InsulationTool::showSetupDialog()
{
    IAppContext *ctx = getContext();
    if (!ctx) return false;
    QWidget *parentWin = dynamic_cast<QWidget *>(ctx->commandDock());
    InsulationDialog dlg(m_params, QLineF(m_p1, m_p2).length(), ctx->layerManager(), parentWin);
    if (dlg.exec() != QDialog::Accepted) return false;
    m_params = dlg.params();
    saveParams(m_scene, m_params);
    return true;
}

void InsulationTool::ensureLayer(IAppContext *ctx, const QString &name)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, QColor(255, 255, 255), 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

void InsulationTool::finish()
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) { cancelDrawing(); emit toolFinished(); return; }

    const QVector<InsulationArc> arcs = buildArcs(m_p1, m_p2, m_params);
    if (arcs.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("P1-P2 길이(%1)가 심볼 가로 크기(%2)보다 짧아 배치할 수 없습니다.")
                    .arg(QLineF(m_p1, m_p2).length(), 0, 'f', 1)
                    .arg(unitWidth(m_params.type) * m_params.height, 0, 'f', 1),
                CommandDock::Info);
        cancelDrawing();
        emit toolFinished();
        return;
    }

    const QString layerName = AbstractTool::resolveLayerName(ctx, m_params.layer);
    ensureLayer(ctx, layerName);
    const QPen pen = AbstractTool::layerPenFor(ctx, layerName);

    if (ctx->undoStack()) ctx->undoStack()->beginMacro(tr("단열재"));
    for (int i = 0; i < arcs.size(); ++i) {
        const InsulationArc &a = arcs.at(i);
        QGraphicsItem *item = ctx->entityFactory()->createArc(a.center, a.radius,
                                                              a.startDeg, a.spanDeg, pen);
        if (!item) continue;
        if (CadItem *ci = dynamic_cast<CadItem *>(item)) ci->setLayerName(layerName);
        item->setParentItem(m_scene->getLayerRoot(layerName));
        if (item->scene() == m_scene) m_scene->removeItem(item);
        if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        else                  m_scene->addItem(item);
    }
    if (ctx->undoStack()) ctx->undoStack()->endMacro();

    if (ctx->commandDock()) {
        const int perSymbol = (m_params.type == 2) ? kIns2ArcsCount : kIns1ArcsCount;
        ctx->commandDock()->logMessage(
            tr("단열재 완료 — 심볼 %1개").arg(arcs.size() / perSymbol), CommandDock::Info);
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    }
    cancelDrawing();
    emit toolFinished();
}

InsulationParams InsulationTool::loadParams(MyGraphicsScene *scene)
{
    InsulationParams p;
    ToolDefaults d("InsulationTool", scene);
    p.type   = d.getInt("Type", p.type);
    p.height = d.getDouble("Height", p.height);
    p.layer  = d.getString("Layer", p.layer);
    return p;
}

void InsulationTool::saveParams(MyGraphicsScene *scene, const InsulationParams &p)
{
    ToolDefaults d("InsulationTool", scene);
    d.put("Type", p.type);
    d.put("Height", p.height);
    d.put("Layer", p.layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// InsulationDialog
// ─────────────────────────────────────────────────────────────────────────────
InsulationDialog::InsulationDialog(const InsulationParams &params, double length,
                                   LayerManager *layerMgr, QWidget *parent)
    : QDialog(parent), m_length(length)
{
    setWindowTitle(tr("단열재"));
    setModal(true);

    m_radioType1 = new QRadioButton(tr("타입 1"), this);
    m_radioType2 = new QRadioButton(tr("타입 2"), this);
    QButtonGroup *typeGroup = new QButtonGroup(this);
    typeGroup->addButton(m_radioType1);
    typeGroup->addButton(m_radioType2);
    (params.type == 2 ? m_radioType2 : m_radioType1)->setChecked(true);

    QWidget *typeRow = new QWidget(this);
    QHBoxLayout *typeLay = new QHBoxLayout(typeRow);
    typeLay->setContentsMargins(0, 0, 0, 0);
    typeLay->addWidget(m_radioType1);
    typeLay->addWidget(m_radioType2);
    typeLay->addStretch();

    m_spinHeight = new QDoubleSpinBox(this);
    m_spinHeight->setRange(0.1, 100000.0);
    m_spinHeight->setDecimals(1);
    m_spinHeight->setSingleStep(10.0);
    m_spinHeight->setValue(params.height);

    m_comboLayer = new QComboBox(this);
    QString sel = params.layer;
    if (sel.isEmpty() && layerMgr && layerMgr->getCurrentLayer())
        sel = layerMgr->getCurrentLayer()->name();
    StairPlanDialog::populateLayerCombo(m_comboLayer, layerMgr, sel);

    m_labelInfo = new QLabel(this);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("심볼"), typeRow);
    form->addRow(tr("심볼 높이 (Height)"), m_spinHeight);
    form->addRow(tr("레이어"), m_comboLayer);
    form->addRow(m_labelInfo);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QVBoxLayout *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(m_radioType2, &QRadioButton::toggled, this, &InsulationDialog::updateInfo);
    connect(m_spinHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &InsulationDialog::updateInfo);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateInfo();
}

void InsulationDialog::updateInfo()
{
    const InsulationParams p = params();
    m_labelInfo->setText(tr("P1-P2 길이 %1 / 심볼 크기 %2 x %3 → %4개 배치")
                             .arg(m_length, 0, 'f', 1)
                             .arg(InsulationTool::unitWidth(p.type) * p.height, 0, 'f', 1)
                             .arg(p.height, 0, 'f', 1)
                             .arg(InsulationTool::countFor(m_length, p)));
}

InsulationParams InsulationDialog::params() const
{
    InsulationParams p;
    p.type   = m_radioType2->isChecked() ? 2 : 1;
    p.height = m_spinHeight->value();
    p.layer  = m_comboLayer->currentData().toString();
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// InsulationCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *InsulationCommand::createCommand(IAppContext *c)
{
    return new InsulationCommand(c);
}

InsulationCommand::InsulationCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void InsulationCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new InsulationTool(InsulationTool::loadParams(m_ctx->activeScene())));
}
