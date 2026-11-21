#include "extrudetool.h"

#include "commanddock.h"
#include "extrudefeature.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "projectionview.h"
#include "revolveprofile.h"
#include "scenesymbolregistry.h"
#include "snapmanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QUndoStack>
#include <QVBoxLayout>

// ── 설정창 ──
ExtrudeDialog::ExtrudeDialog(const QString &hostName, const QString &viewName, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("돌출"));
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("'%1'의 %2에 그린 공간을 축 방향으로 돌출합니다.\n시작 = 이 측면 쪽 '%1' 끝면에서 안쪽으로.")
                                  .arg(hostName, viewName),
                              this));
    QFormLayout *form = new QFormLayout();
    m_name = new QLineEdit(tr("받침대"), this);
    m_offset = new QDoubleSpinBox(this);
    m_offset->setRange(-10000.0, 10000.0);
    m_offset->setDecimals(2);
    m_offset->setValue(0.0);
    m_depth = new QDoubleSpinBox(this);
    m_depth->setRange(0.01, 10000.0);
    m_depth->setDecimals(2);
    m_depth->setValue(10.0);
    m_merge = new QCheckBox(tr("'%1'에 합치기 (한 덩어리)").arg(hostName), this);
    m_merge->setChecked(true);
    form->addRow(tr("이름"), m_name);
    form->addRow(tr("시작 거리 (끝면에서, mm)"), m_offset);
    form->addRow(tr("깊이 (mm)"), m_depth);
    lay->addLayout(form);
    lay->addWidget(m_merge);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

QString ExtrudeDialog::partName() const
{
    const QString n = m_name->text().trimmed();
    return n.isEmpty() ? tr("돌출") : n;
}
double ExtrudeDialog::offset() const { return m_offset->value(); }
double ExtrudeDialog::depth() const { return m_depth->value(); }
bool ExtrudeDialog::merge() const { return m_merge->isChecked(); }

// ── 도구 ──
ExtrudeTool::ExtrudeTool(QObject *parent) : AbstractTool(parent) {}

void ExtrudeTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_state = AreaBox1;
    m_picks.clear();
    m_region = QPainterPath();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("EXT3D (돌출)"), CommandDock::Command);
    }
    showPrompt();
}

void ExtrudeTool::deactivate()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

void ExtrudeTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    CommandDock *dock = ctx->commandDock();
    if (m_state == AreaBox1)
        dock->showInteractivePrompt(tr(" 돌출할 공간: 경계가 될 객체를 고를 사각상자의 첫 점을 지정하세요 : "), {});
    else if (m_state == AreaBox2)
        dock->showInteractivePrompt(tr(" 사각상자의 두번째 점을 지정하세요 : "), {});
    else
        dock->showInteractivePrompt(tr(" 돌출할 내부 공간을 지정하세요 (여러 곳 가능, 끝: 엔터) [복합모양(B)] : "),
                                    {tr("복합모양(B)")});
}

void ExtrudeTool::cancelTool()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    emit toolFinished();
}

bool ExtrudeTool::processCommand(const QString &command)
{
    const QString c = command.trimmed().toUpper();
    if (m_state == AreaPick && (c == QLatin1String("B") || c.contains(QLatin1String("(B)")))) {
        finishArea(true);
        return true;
    }
    if (m_state == AreaPick && c.isEmpty()) {
        if (m_picks.isEmpty())
            cancelTool();
        else
            finishArea(false);
        return true;
    }
    return false;
}

void ExtrudeTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    if (m_state == AreaPick &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        if (m_picks.isEmpty())
            cancelTool();
        else
            finishArea(false);
        return;
    }
    AbstractTool::keyPress(event);
}

void ExtrudeTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;
    // 해치 명령과 같은 방식: 스냅 표시 + 두번째 점까지 가상 사각 박스
    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        const SnapManager::SnapResult r = ctx->snapManager()->snapPoint(pos, m_scene, m_state == AreaBox2 ? m_box1 : QPointF());
        snapped = r.snappedPos;
        snapType = r.type;
    }
    if (m_state == AreaBox2) {
        QPainterPath p;
        p.addRect(QRectF(m_box1, snapped).normalized());
        updateGhostPreview(p, QString());
    }
    m_scene->setSnapMarker(snapped, snapType);
}

void ExtrudeTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    IAppContext *ctx = getContext();
    if (button == Qt::RightButton) {
        if (m_state == AreaPick && !m_picks.isEmpty())
            finishArea(false);
        else
            cancelTool();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    QPointF snapped = pos;
    if (ctx && ctx->snapManager() && m_state != AreaPick)
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, m_state == AreaBox2 ? m_box1 : QPointF()).snappedPos;

    if (m_state == AreaBox1) {
        m_box1 = snapped;
        m_state = AreaBox2;
        showPrompt();
        return;
    }
    if (m_state == AreaBox2) {
        m_box = QRectF(m_box1, snapped).normalized();
        m_scene->setGhostPath(QPainterPath(), false);
        if (m_box.width() < 1e-5 || m_box.height() < 1e-5) {
            m_state = AreaBox1;
            showPrompt();
            return;
        }
        m_picks.clear();
        m_region = QPainterPath();
        m_state = AreaPick;
        showPrompt();
        return;
    }
    // AreaPick
    QList<QPointF> one;
    one << pos;
    if (RevolveProfile::areaRegion(m_scene, m_box, one, true).isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("이 점을 둘러싼 닫힌 공간을 찾지 못했습니다."), CommandDock::Warning);
        return;
    }
    m_picks << pos;
    m_region = RevolveProfile::areaRegion(m_scene, m_box, m_picks, true);
    m_scene->setGhostPath(m_region, true);
}

void ExtrudeTool::finishArea(bool complexShape)
{
    IAppContext *ctx = getContext();
    if (complexShape) {
        m_picks.clear();
        m_region = RevolveProfile::areaRegion(m_scene, m_box, m_picks, true);
    }
    QWidget *parent = QApplication::activeWindow();
    if (m_region.isEmpty()) {
        QMessageBox::warning(parent, tr("돌출"), tr("닫힌 공간을 찾지 못했습니다."));
        cancelTool();
        return;
    }
    QString host, viewId;
    int viewKind = ProjectionView::Left;
    if (!ExtrudeFeature::findHostView(m_scene, m_region, host, viewId, viewKind)) {
        QMessageBox::warning(parent, tr("돌출"),
                             tr("고른 공간 근처에 단면 회전체의 측면도(투상도 만들기로 만든 좌·우측면도)가 없습니다."));
        cancelTool();
        return;
    }
    const QString hostName = m_scene->symbolRegistry()->symbolInfo(host).name;
    ExtrudeDialog dlg(hostName, ProjectionView::kindName(viewKind), parent);
    if (dlg.exec() != QDialog::Accepted) {
        cancelTool();
        return;
    }
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(tr("돌출"));
    QString err;
    const QString uuid = ExtrudeFeature::registerExtrude(m_scene, undo, host, viewId, viewKind, m_box, m_picks,
                                                         m_region, dlg.partName(), dlg.offset(), dlg.depth(),
                                                         dlg.merge(), &err);
    if (!uuid.isEmpty()) {
        ProjectionView::ensureFrontView(ctx, m_scene, host);
        ProjectionView::regeneratePart(ctx, m_scene, host);
    }
    if (undo)
        undo->endMacro();
    if (uuid.isEmpty())
        QMessageBox::warning(parent, tr("돌출"), err);
    else if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("'%1' 돌출 — 투상도 갱신. 3D 는 부품 3D 보기 [새로고침].").arg(dlg.partName()),
                                       CommandDock::Info);
    cancelTool();
}
