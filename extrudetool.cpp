#include "extrudetool.h"
#include "plateview.h"

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
ExtrudeDialog::ExtrudeDialog(const QString &hostName, const QString &viewName, QWidget *parent, bool plateBoss)
    : QDialog(parent)
{
    setWindowTitle(tr("돌출"));
    m_standalone = hostName.isEmpty();
    m_plateBoss = plateBoss && !m_standalone;
    QVBoxLayout *lay = new QVBoxLayout(this);
    if (m_plateBoss)
        lay->addWidget(new QLabel(tr("'%1'의 %2에 그린 공간을 그 면에서 바깥(보는 쪽)으로 돌출해 돌기를 붙입니다.\n"
                                     "시작 = 그 공간 아래 판 표면, 깊이 = 표면(비스듬하면 가장 앞)에서 나온 길이.")
                                      .arg(hostName, viewName),
                                  this));
    else if (m_standalone)
        lay->addWidget(new QLabel(tr("고른 공간을 도면에 수직(보는 쪽 +Z)으로 돌출해 판·블록 부품을 만듭니다.\n"
                                     "공간 안의 닫힌 모양은 관통 구멍, 공간 안의 구멍R·구멍P 는 앞면에서 파는 구멍이 됩니다.\n"
                                     "회전체·축과 맞출 때: 축선은 Z = 0 입니다."),
                                  this));
    else
        lay->addWidget(new QLabel(tr("'%1'의 %2에 그린 공간을 축 방향으로 돌출합니다.\n시작 = 이 측면 쪽 '%1' 끝면에서 안쪽으로.")
                                      .arg(hostName, viewName),
                                  this));
    QFormLayout *form = new QFormLayout();
    m_name = new QLineEdit(m_standalone ? tr("판") : (m_plateBoss ? tr("돌기") : tr("받침대")), this);
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
    m_symmetric = new QCheckBox(tr("시작 Z 를 가운데로 (앞뒤 대칭)"), this);
    form->addRow(tr("이름"), m_name);
    if (m_standalone) {
        form->addRow(tr("시작 Z (mm, 보는 쪽 +)"), m_offset);
        m_startNote = new QLabel(this);
        m_startNote->setVisible(false);
        form->addRow(QString(), m_startNote);
        form->addRow(tr("깊이 · 두께 (mm)"), m_depth);
    } else if (m_plateBoss) {
        form->addRow(tr("깊이 (면에서, mm)"), m_depth);
        m_offset->setVisible(false);
    } else {
        form->addRow(tr("시작 거리 (끝면에서, mm)"), m_offset);
        form->addRow(tr("깊이 (mm)"), m_depth);
    }
    lay->addLayout(form);
    lay->addWidget(m_standalone ? m_symmetric : m_merge);
    m_merge->setVisible(!m_standalone);
    m_symmetric->setVisible(m_standalone);
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
bool ExtrudeDialog::merge() const { return !m_standalone && m_merge->isChecked(); }
bool ExtrudeDialog::symmetric() const { return m_standalone && m_symmetric->isChecked(); }

void ExtrudeDialog::setStartZ(double z, const QString &note)
{
    m_offset->setValue(z);
    if (m_startNote) {
        m_startNote->setText(note);
        m_startNote->setVisible(!note.isEmpty());
    }
}

namespace {
// 고른 공간 안쪽의 대표 점 — 고른 점(공간 안)이 있으면 그것, 복합모양이면 공간 경계 사각 가운데
QPointF regionProbe(const QList<QPointF> &picks, const QPainterPath &region)
{
    return picks.isEmpty() ? region.boundingRect().center() : picks.first();
}
} // namespace

// ── 파내기 설정창 ──
CutDialog::CutDialog(Mode mode, const QString &hostName, const QString &viewName, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("파내기"));
    QVBoxLayout *lay = new QVBoxLayout(this);
    QString text;
    if (mode == PlateFace)
        text = tr("'%1'의 %2에 그린 공간을 그 면에서 안쪽으로 파냅니다.\n깊이 = 공간 아래 판 표면(비스듬하면 가장 깊은 곳)에서.")
                   .arg(hostName, viewName);
    else if (mode == PlateFront)
        text = tr("'%1' 정면 안에 그린 공간을 앞면에서 −Z 로 파냅니다 (포켓).").arg(hostName);
    else
        text = tr("'%1'의 %2에 그린 공간을 축 방향으로 파냅니다 (홈).\n시작 = 이 측면 쪽 끝면에서 안쪽으로.").arg(hostName, viewName);
    lay->addWidget(new QLabel(text, this));
    QFormLayout *form = new QFormLayout();
    m_name = new QLineEdit(tr("파내기"), this);
    m_offset = new QDoubleSpinBox(this);
    m_offset->setRange(0.0, 10000.0);
    m_offset->setDecimals(2);
    m_depth = new QDoubleSpinBox(this);
    m_depth->setRange(0.01, 10000.0);
    m_depth->setDecimals(2);
    m_depth->setValue(5.0);
    m_through = new QCheckBox(tr("관통 (반대쪽 면까지)"), this);
    form->addRow(tr("이름"), m_name);
    if (mode == RevolveSide)
        form->addRow(tr("시작 거리 (끝면에서, mm)"), m_offset);
    else
        m_offset->setVisible(false);
    form->addRow(tr("깊이 (mm)"), m_depth);
    lay->addLayout(form);
    lay->addWidget(m_through);
    m_through->setVisible(mode != RevolveSide);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_through, &QCheckBox::toggled, m_depth, &QWidget::setDisabled);
    lay->addWidget(bb);
}

QString CutDialog::partName() const
{
    const QString n = m_name->text().trimmed();
    return n.isEmpty() ? tr("파내기") : n;
}
double CutDialog::offset() const { return m_offset->value(); }
double CutDialog::depth() const { return m_depth->value(); }
bool CutDialog::through() const { return m_through->isVisible() && m_through->isChecked(); }

// ── 도구 ──
ExtrudeTool::ExtrudeTool(QObject *parent) : AbstractTool(parent) {}

CutTool::CutTool(QObject *parent) : ExtrudeTool(parent)
{
    m_cut = true;
}

void ExtrudeTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_state = AreaBox1;
    m_picks.clear();
    m_region = QPainterPath();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(m_cut ? tr("CUT3D (파내기)") : tr("EXT3D (돌출)"), CommandDock::Command);
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
        dock->showInteractivePrompt(m_cut ? tr(" 파낼 공간: 경계가 될 객체를 고를 사각상자의 첫 점을 지정하세요 : ")
                                          : tr(" 돌출할 공간: 경계가 될 객체를 고를 사각상자의 첫 점을 지정하세요 : "),
                                    {});
    else if (m_state == AreaBox2)
        dock->showInteractivePrompt(tr(" 사각상자의 두번째 점을 지정하세요 : "), {});
    else
        dock->showInteractivePrompt(m_cut ? tr(" 파낼 내부 공간을 지정하세요 (여러 곳 가능, 끝: 엔터) [복합모양(B)] : ")
                                          : tr(" 돌출할 내부 공간을 지정하세요 (여러 곳 가능, 끝: 엔터) [복합모양(B)] : "),
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
    if (m_cut) {
        finishCut();
        return;
    }
    // 판의 측면도·보조 투상도 안 → 그 면에 붙는 돌기
    {
        QString plate;
        PlateView::FaceView fv;
        if (PlateView::viewAt(m_scene, m_region.boundingRect().center(), plate, fv)) {
            const QString plateName = m_scene->symbolRegistry()->symbolInfo(plate).name;
            ExtrudeDialog bdlg(plateName, ProjectionView::kindName(fv.kind), parent, true);
            if (bdlg.exec() != QDialog::Accepted) {
                cancelTool();
                return;
            }
            QUndoStack *bundo = ctx ? ctx->undoStack() : nullptr;
            if (bundo)
                bundo->beginMacro(tr("돌출"));
            QString berr;
            const QString buuid = ExtrudeFeature::registerPlateBoss(m_scene, bundo, plate, fv.viewId, fv.kind, m_box,
                                                                    m_picks, m_region, bdlg.partName(), bdlg.depth(),
                                                                    bdlg.merge(), &berr);
            if (!buuid.isEmpty())
                ProjectionView::regeneratePart(ctx, m_scene, plate);   // 다른 투상도에 돌기 외형, 정면 기록 자동
            if (bundo)
                bundo->endMacro();
            if (buuid.isEmpty())
                QMessageBox::warning(parent, tr("돌출"), berr);
            else if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("'%1' 돌기 → '%2' 의 면 — 투상도 갱신. 3D 는 부품 3D 보기 [새로고침].").arg(bdlg.partName(), plateName),
                    CommandDock::Info);
            cancelTool();
            return;
        }
    }
    QString host, viewId;
    int viewKind = ProjectionView::Left;
    if (!ExtrudeFeature::findHostView(m_scene, m_region, host, viewId, viewKind)) {
        // 회전체 측면도 근처가 아니면 단독 돌출 (판·블록)
        ExtrudeDialog sdlg(QString(), QString(), parent);
        // 이미 돌출된 판 위에 그린 공간이면 시작 Z = 그 판(겹치면 가장 앞)의 윗면 — 판 위에 올라가도록 자동
        const QString below = PlateView::plateAt(m_scene, regionProbe(m_picks, m_region), true);
        ExtrudeInfo bi;
        if (!below.isEmpty() && ExtrudeFeature::resolve(m_scene, below, bi))
            sdlg.setStartZ(bi.x1, tr("※ 아래 판 '%1' 윗면 Z = %2 자동 적용").arg(bi.name).arg(bi.x1));
        if (sdlg.exec() != QDialog::Accepted) {
            cancelTool();
            return;
        }
        QUndoStack *sundo = ctx ? ctx->undoStack() : nullptr;
        if (sundo)
            sundo->beginMacro(tr("돌출"));
        QString serr;
        const QString suuid = ExtrudeFeature::registerStandalone(m_scene, sundo, m_box, m_picks, m_region,
                                                                 sdlg.partName(), sdlg.offset(), sdlg.depth(),
                                                                 sdlg.symmetric(), &serr);
        if (sundo)
            sundo->endMacro();
        if (suuid.isEmpty())
            QMessageBox::warning(parent, tr("돌출"), serr);
        else if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("'%1' 단독 돌출 (두께 %2) — 3D 는 부품 3D 보기. 공간 안에 구멍R·구멍P 를 놓으면 구멍이 됩니다.")
                    .arg(sdlg.partName())
                    .arg(sdlg.depth()),
                CommandDock::Info);
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

void ExtrudeTool::finishCut()
{
    IAppContext *ctx = getContext();
    QWidget *parent = QApplication::activeWindow();
    // 공간 안쪽 대표 점 (오목한 공간이면 경계 사각 가운데가 공간 밖일 수 있어 고른 점 우선)
    const QPointF center = regionProbe(m_picks, m_region);
    QString host, viewId, viewName;
    int viewKind = ProjectionView::Left;
    int mode = -1;
    PlateView::FaceView fv;
    if (PlateView::viewAt(m_scene, center, host, fv)) {
        mode = CutDialog::PlateFace;
        viewId = fv.viewId;
        viewKind = fv.kind;
        viewName = ProjectionView::kindName(fv.kind);
    } else {
        // 판이 겹쳐 있으면 가장 앞(윗면 Z 가 큰) 판 → 그 판 윗면에서 파 들어감 (PlateView::plateAt)
        host = PlateView::plateAt(m_scene, center, true);
        if (!host.isEmpty()) {
            mode = CutDialog::PlateFront;
            viewName = tr("정면");
        } else if (ExtrudeFeature::findHostView(m_scene, m_region, host, viewId, viewKind)) {
            mode = CutDialog::RevolveSide;
            viewName = ProjectionView::kindName(viewKind);
        }
    }
    if (mode < 0) {
        QMessageBox::warning(parent, tr("파내기"),
                             tr("파낼 부품을 찾지 못했습니다.\n판의 정면·측면도·보조 투상도, 또는 회전체 측면도 안에 닫힌 공간을 그리세요."));
        cancelTool();
        return;
    }
    const QString hostName = m_scene->symbolRegistry()->symbolInfo(host).name;
    CutDialog dlg(CutDialog::Mode(mode), hostName, viewName, parent);
    if (dlg.exec() != QDialog::Accepted) {
        cancelTool();
        return;
    }
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(tr("파내기"));
    QString err, uuid;
    if (mode == CutDialog::PlateFace)
        uuid = ExtrudeFeature::registerPlateBoss(m_scene, undo, host, viewId, viewKind, m_box, m_picks, m_region,
                                                 dlg.partName(), dlg.depth(), false, &err, true, dlg.through());
    else if (mode == CutDialog::PlateFront)
        uuid = ExtrudeFeature::registerPlateFrontCut(m_scene, undo, host, m_box, m_picks, m_region, dlg.partName(),
                                                     dlg.depth(), dlg.through(), &err);
    else
        uuid = ExtrudeFeature::registerExtrude(m_scene, undo, host, viewId, viewKind, m_box, m_picks, m_region,
                                               dlg.partName(), dlg.offset(), dlg.depth(), false, &err, true);
    if (!uuid.isEmpty()) {
        if (mode == CutDialog::RevolveSide)
            ProjectionView::ensureFrontView(ctx, m_scene, host);
        ProjectionView::regeneratePart(ctx, m_scene, host);
    }
    if (undo)
        undo->endMacro();
    if (uuid.isEmpty())
        QMessageBox::warning(parent, tr("파내기"), err);
    else if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("'%1' 파내기 → '%2' — 투상도 갱신. 3D 는 부품 3D 보기 [새로고침] (Blender 에서 빼기).")
                                           .arg(dlg.partName(), hostName),
                                       CommandDock::Info);
    cancelTool();
}
