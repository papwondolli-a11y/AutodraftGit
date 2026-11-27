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
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUndoStack>
#include <QVBoxLayout>

// ── 설정창 ──
ExtrudeDialog::ExtrudeDialog(MyGraphicsScene *scene, const QList<ExtrudeTarget> &targets, bool cut, QWidget *parent)
    : QDialog(parent), m_scene(scene), m_targets(targets), m_cut(cut)
{
    setWindowTitle(cut ? tr("파내기") : tr("돌출"));
    QVBoxLayout *lay = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();
    // 대상 — 첫 항목이 자동 판단값
    m_target = new QComboBox(this);
    for (int i = 0; i < m_targets.size(); ++i)
        m_target->addItem(i == 0 ? tr("%1 (자동)").arg(m_targets.at(i).label()) : m_targets.at(i).label());
    m_direction = new QLabel(this);
    m_direction->setTextFormat(Qt::RichText);
    m_warning = new QLabel(tr("⚠ 고른 공간이 이 측면도와 떨어져 있습니다. 대상이 맞는지 확인하세요."), this);
    m_warning->setStyleSheet(QStringLiteral("color:#c06000;"));
    m_name = new QLineEdit(this);
    m_offsetLabel = new QLabel(this);
    m_offset = new QDoubleSpinBox(this);
    m_offset->setRange(cut ? 0.0 : -10000.0, 10000.0);
    m_offset->setDecimals(2);
    m_startNote = new QLabel(this);
    m_depth = new QDoubleSpinBox(this);
    m_depth->setRange(0.01, 10000.0);
    m_depth->setDecimals(2);
    m_depth->setValue(cut ? 5.0 : 10.0);
    m_merge = new QCheckBox(this);
    m_merge->setChecked(true);
    m_through = new QCheckBox(tr("관통 (반대쪽 면까지)"), this);
    form->addRow(tr("대상"), m_target);
    form->addRow(tr("방향"), m_direction);
    form->addRow(m_warning);
    form->addRow(tr("이름"), m_name);
    form->addRow(m_offsetLabel, m_offset);
    form->addRow(m_startNote);
    m_depthLabel = new QLabel(this);
    form->addRow(m_depthLabel, m_depth);
    lay->addLayout(form);
    lay->addWidget(m_merge);
    lay->addWidget(m_through);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    onTargetChanged(0);
    connect(m_target, &QComboBox::currentIndexChanged, this, &ExtrudeDialog::onTargetChanged);
    connect(m_offset, &QDoubleSpinBox::valueChanged, this, &ExtrudeDialog::onValueChanged);
    connect(m_depth, &QDoubleSpinBox::valueChanged, this, &ExtrudeDialog::onValueChanged);
    connect(m_through, &QCheckBox::toggled, this, &ExtrudeDialog::onValueChanged);
    connect(m_through, &QCheckBox::toggled, m_depth, &QWidget::setDisabled);
}

ExtrudeTarget ExtrudeDialog::target() const
{
    const int i = m_target->currentIndex();
    return (i >= 0 && i < m_targets.size()) ? m_targets.at(i) : ExtrudeTarget();
}

QString ExtrudeDialog::defaultName(const ExtrudeTarget &t) const
{
    if (m_cut)
        return tr("파내기");
    if (t.type == ExtrudeTarget::None)
        return tr("판");
    return t.type == ExtrudeTarget::PlateFace ? tr("돌기") : tr("받침대");
}

// 대상에 맞춰 입력 칸 전환: 회전체 = 시작 거리 + 합치기, 판의 면 = 깊이 + 합치기, 없음 = 시작 Z + 대칭
void ExtrudeDialog::onTargetChanged(int)
{
    const ExtrudeTarget t = target();
    const bool none = t.type == ExtrudeTarget::None;
    const bool side = t.type == ExtrudeTarget::RevolveSide;
    m_direction->setText(ExtrudeTargets::directionText(m_scene, t, m_cut));
    m_warning->setVisible(t.away);
    if (m_name->text().isEmpty() || m_name->text() == m_autoName) {   // 사용자가 고친 이름은 유지
        m_autoName = defaultName(t);
        m_name->setText(m_autoName);
    }
    if (int(t.type) != m_lastType) {                    // 시작 거리와 시작 Z 는 뜻이 달라 종류가 바뀌면 기본값
        m_offset->setValue(none ? t.startZ : 0.0);
        m_lastType = int(t.type);
    }
    m_offsetLabel->setText(none ? tr("시작 높이 (mm, 보는 쪽 +)") : tr("시작 거리 (끝면에서, mm)"));
    // 새 판(돌출)은 판 두께, 돌기·받침대·파내기는 들어가는/나오는 깊이
    m_depthLabel->setText(none && !m_cut ? tr("두께 (mm)") : tr("깊이 (mm)"));
    m_offsetLabel->setVisible(none || side);
    m_offset->setVisible(none || side);
    m_startNote->setText(t.startNote);
    m_startNote->setVisible(none && !t.startNote.isEmpty());
    m_merge->setText(tr("'%1'에 합치기 (한 덩어리)").arg(t.hostName));
    m_merge->setVisible(!none && !m_cut);
    m_through->setVisible(m_cut && !side);
    if (side && m_through->isChecked())
        m_through->setChecked(false);                   // 회전체 홈에는 관통이 없음 (깊이 칸도 다시 켜짐)
    adjustSize();
    emit previewChanged();
}

void ExtrudeDialog::onValueChanged() { emit previewChanged(); }

ExtrudeValues ExtrudeDialog::values() const
{
    const ExtrudeTarget t = target();
    ExtrudeValues v;
    v.offset = (t.type == ExtrudeTarget::None || t.type == ExtrudeTarget::RevolveSide) ? m_offset->value() : 0.0;
    v.depth = m_depth->value();
    v.symmetric = false;    // 가운데 대칭 체크박스는 없앰 (시작 높이 = 밑면). 옛 대칭 기록은 resolve 가 그대로 읽음
    v.through = m_cut && t.type != ExtrudeTarget::RevolveSide && m_through->isChecked();
    v.cut = m_cut;
    return v;
}

QString ExtrudeDialog::partName() const
{
    const QString n = m_name->text().trimmed();
    return n.isEmpty() ? (m_cut ? tr("파내기") : tr("돌출")) : n;
}

bool ExtrudeDialog::merge() const
{
    return !m_cut && target().type != ExtrudeTarget::None && m_merge->isChecked();
}

namespace {
// 고른 공간 안쪽의 대표 점 — 첫 고른 점 (없을 일은 없지만 만일을 위해 공간 경계 사각 가운데)
QPointF regionProbe(const QList<QPointF> &picks, const QPainterPath &region)
{
    return picks.isEmpty() ? region.boundingRect().center() : picks.first();
}
} // namespace

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
    clearPreview();
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
        dock->showInteractivePrompt(m_cut ? tr(" 파낼 내부 공간을 지정하세요 (여러 곳 가능, 끝: 엔터) : ")
                                          : tr(" 돌출할 내부 공간을 지정하세요 (여러 곳 가능, 끝: 엔터) : "),
                                    {});
}

void ExtrudeTool::cancelTool()
{
    clearPreview();
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    emit toolFinished();
}

bool ExtrudeTool::processCommand(const QString &command)
{
    const QString c = command.trimmed().toUpper();
    if (m_state == AreaPick && c.isEmpty()) {
        if (m_picks.isEmpty())
            cancelTool();
        else
            finishArea();
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
            finishArea();
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
            finishArea();
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
    if (RevolveProfile::areaRegion(m_scene, m_box, one, true, false, false, QStringList(), true).isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("이 점을 둘러싼 닫힌 공간을 찾지 못했습니다."), CommandDock::Warning);
        return;
    }
    m_picks << pos;
    m_region = RevolveProfile::areaRegion(m_scene, m_box, m_picks, true, false, false, QStringList(), true);   // 중심선 레이어 제외
    m_scene->setGhostPath(m_region, true);
}

void ExtrudeTool::finishArea()
{
    IAppContext *ctx = getContext();
    QWidget *parent = QApplication::activeWindow();
    if (m_region.isEmpty()) {
        QMessageBox::warning(parent, tr("돌출"), tr("닫힌 공간을 찾지 못했습니다."));
        cancelTool();
        return;
    }
    // 대상 후보: 첫 항목 = 자동 판단값
    const QString title = m_cut ? tr("파내기") : tr("돌출");
    const QPointF probe = regionProbe(m_picks, m_region);
    const QList<ExtrudeTarget> targets = m_cut ? ExtrudeTargets::forCut(m_scene, m_region, probe)
                                               : ExtrudeTargets::forExtrude(m_scene, m_region, probe);
    if (targets.isEmpty()) {
        QMessageBox::warning(parent, title,
                             tr("파낼 부품을 찾지 못했습니다.\n판의 정면·측면도·보조 투상도, 또는 회전체 측면도 안에 닫힌 공간을 그리세요."));
        cancelTool();
        return;
    }
    ExtrudeDialog dlg(m_scene, targets, m_cut, parent);
    m_dialog = &dlg;
    connect(&dlg, &ExtrudeDialog::previewChanged, this, &ExtrudeTool::updatePreview);
    updatePreview();
    QStringList oldRecords;
    ExtrudeTarget t;
    bool ok = dlg.exec() == QDialog::Accepted;
    if (ok) {
        t = dlg.target();
        ok = askOldRecords(t, parent, oldRecords);
    }
    m_dialog = nullptr;
    clearPreview();
    if (!ok) {
        cancelTool();
        return;
    }
    const ExtrudeValues v = dlg.values();
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(title);
    QString err, uuid;
    if (t.type == ExtrudeTarget::PlateFace)
        uuid = ExtrudeFeature::registerPlateBoss(m_scene, undo, t.host, t.viewId, t.viewKind, m_box, m_picks, m_region,
                                                 dlg.partName(), v.depth, dlg.merge(), &err, m_cut, v.through);
    else if (t.type == ExtrudeTarget::PlateFront)
        uuid = ExtrudeFeature::registerPlateFrontCut(m_scene, undo, t.host, m_box, m_picks, m_region, dlg.partName(),
                                                     v.depth, v.through, &err);
    else if (t.type == ExtrudeTarget::RevolveSide)
        uuid = ExtrudeFeature::registerExtrude(m_scene, undo, t.host, t.viewId, t.viewKind, m_box, m_picks, m_region,
                                               dlg.partName(), v.offset, v.depth, dlg.merge(), &err, m_cut);
    else
        uuid = ExtrudeFeature::registerStandalone(m_scene, undo, m_box, m_picks, m_region, dlg.partName(), v.offset,
                                                  v.depth, v.symmetric, &err);
    // 새 돌출이 만들어진 뒤에만 옛 기록을 지움
    for (int i = 0; !uuid.isEmpty() && i < oldRecords.size(); ++i)
        ExtrudeTargets::removeRecord(ctx, m_scene, oldRecords.at(i));
    if (!uuid.isEmpty() && t.type != ExtrudeTarget::None) {
        if (t.type == ExtrudeTarget::RevolveSide)
            ProjectionView::ensureFrontView(ctx, m_scene, t.host);
        ProjectionView::regeneratePart(ctx, m_scene, t.host);   // 다른 투상도에 돌출·파낸 모서리
    }
    if (undo)
        undo->endMacro();
    if (uuid.isEmpty()) {
        QMessageBox::warning(parent, title, err);
    } else if (ctx && ctx->commandDock()) {
        QString msg;
        if (t.type == ExtrudeTarget::None)
            msg = tr("'%1' 단독 돌출 (두께 %2) — 3D 는 부품 3D 보기. 공간 안에 구멍R·구멍P 를 놓으면 구멍이 됩니다.")
                      .arg(dlg.partName())
                      .arg(v.depth);
        else if (m_cut)
            msg = tr("'%1' 파내기 → '%2' — 투상도 갱신. 3D 는 부품 3D 보기 [새로고침] (Blender 에서 빼기).")
                      .arg(dlg.partName(), t.label());
        else
            msg = tr("'%1' 돌출 → '%2' — 투상도 갱신. 3D 는 부품 3D 보기 [새로고침].").arg(dlg.partName(), t.label());
        ctx->commandDock()->logMessage(msg, CommandDock::Info);
    }
    cancelTool();
}

// 같은 공간을 다른 대상으로 돌출한 옛 기록: 지우기 / 둘 다 두기 / 취소
bool ExtrudeTool::askOldRecords(const ExtrudeTarget &t, QWidget *parent, QStringList &remove)
{
    const QStringList old = ExtrudeTargets::otherRecords(m_scene, m_region, t, m_cut);
    if (old.isEmpty())
        return true;
    QStringList lines;
    for (int i = 0; i < old.size(); ++i)
        lines << QStringLiteral("· ") + ExtrudeTargets::recordLabel(m_scene, old.at(i));
    QMessageBox box(QMessageBox::Question, m_cut ? tr("파내기") : tr("돌출"),
                    tr("같은 공간으로 만든 것이 이미 있습니다.\n%1\n\n옛것을 지우고 새로 만들까요?").arg(lines.join(QLatin1Char('\n'))),
                    QMessageBox::NoButton, parent);
    QPushButton *del = box.addButton(tr("옛것 지우기"), QMessageBox::AcceptRole);
    QPushButton *keep = box.addButton(tr("둘 다 두기"), QMessageBox::ActionRole);
    QPushButton *cancel = box.addButton(tr("취소"), QMessageBox::RejectRole);
    box.setDefaultButton(del);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == del)
        remove = old;
    return box.clickedButton() == del || box.clickedButton() == keep;
}

void ExtrudeTool::updatePreview()
{
    clearPreview();
    if (m_scene && m_dialog)
        m_preview = ExtrudeTargets::addPreview(m_scene, m_dialog->target(), m_region, m_dialog->values());
}

void ExtrudeTool::clearPreview()
{
    ExtrudeTargets::removePreview(m_scene, m_preview);
}
