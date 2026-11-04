#include "walltypedialog.h"
#include "commanddock.h"
#include "mygraphicsscene.h"
#include "twolinetool.h"
#include "layer.h"
#include "layermanager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QIcon>
#include <QToolButton>
#include <QVBoxLayout>

static const int kWallIconSize  = 96;

WallTypeDialog::WallTypeDialog(int firstIcon, int count, const QString &title,
                               int columns, QWidget *parent)
    : QDialog(parent)
    , m_group(new QButtonGroup(this))
    , m_selected(0)
{
    setWindowTitle(title);

    QGridLayout *grid = new QGridLayout;
    grid->setSpacing(8);

    // wall-<firstIcon> ~ wall-<firstIcon+count-1>, columns열 배치
    for (int k = 0; k < count; ++k) {
        const int i = firstIcon + k;
        QToolButton *btn = new QToolButton(this);
        btn->setIcon(QIcon(QStringLiteral(":/icons/wall-%1.png").arg(i)));
        btn->setIconSize(QSize(kWallIconSize, kWallIconSize));
        btn->setFixedSize(kWallIconSize + 16, kWallIconSize + 16);
        btn->setCheckable(true);
        btn->setAutoRaise(false);
        m_group->addButton(btn, i);
        grid->addWidget(btn, k / columns, k % columns);
    }

    connect(m_group, &QButtonGroup::idClicked, this, &WallTypeDialog::onTypeClicked);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(grid);
    lay->addWidget(box);
}

int WallTypeDialog::selectedType() const
{
    return m_selected;
}

void WallTypeDialog::onTypeClicked(int id)
{
    m_selected = id;
    accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// WallSettingsDialog
// ─────────────────────────────────────────────────────────────────────────────
WallSettingsDialog::WallSettingsDialog(bool centerLineEnabled,
                                       const QString &wallLayerName,
                                       const QString &mortarLayerName,
                                       LayerManager *layerMgr,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("벽체설정"));

    m_centerLineCheck = new QCheckBox(tr("중심선"), this);
    m_centerLineCheck->setChecked(centerLineEnabled);

    m_wallLayerCombo = new QComboBox(this);
    populateCombo(m_wallLayerCombo, layerMgr, wallLayerName);

    m_mortarLayerCombo = new QComboBox(this);
    populateCombo(m_mortarLayerCombo, layerMgr, mortarLayerName);

    QFormLayout *form = new QFormLayout;
    form->addRow(QString(), m_centerLineCheck);
    form->addRow(tr("벽체선 레이어 :"), m_wallLayerCombo);
    form->addRow(tr("몰타르선 레이어 :"), m_mortarLayerCombo);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(box);
}

void WallSettingsDialog::populateCombo(QComboBox *combo, LayerManager *mgr,
                                       const QString &selectedName)
{
    combo->addItem(tr("기본값 (현재 레이어)"), QString());
    if (mgr) {
        for (const Layer &layer : mgr->getAllLayers()) {
            QPixmap px(16, 16);
            px.fill(layer.color());
            combo->addItem(QIcon(px), layer.name(), layer.name());
        }
    }
    const int idx = selectedName.isEmpty() ? 0 : combo->findData(selectedName);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

bool WallSettingsDialog::centerLineEnabled() const
{
    return m_centerLineCheck->isChecked();
}

QString WallSettingsDialog::wallLayerName() const
{
    return m_wallLayerCombo->currentData().toString();
}

QString WallSettingsDialog::mortarLayerName() const
{
    return m_mortarLayerCombo->currentData().toString();
}

// ─────────────────────────────────────────────────────────────────────────────
// WallSettingsCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *WallSettingsCommand::createCommand(IAppContext *c)
{
    return new WallSettingsCommand(c);
}

WallSettingsCommand::WallSettingsCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
{}

void WallSettingsCommand::execute()
{
    if (!m_ctx) return;

    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    WallSettingsDialog dlg(TwoLineTool::globalCenterLineEnabled(),
                           TwoLineTool::globalLayerName(),
                           TwoLineTool::globalMortarLayerName(),
                           m_ctx->layerManager(), parentWin);
    if (dlg.exec() != QDialog::Accepted)
        return;

    TwoLineTool::setGlobalCenterLineEnabled(dlg.centerLineEnabled());
    TwoLineTool::setGlobalLayerName(dlg.wallLayerName());
    TwoLineTool::setGlobalMortarLayerName(dlg.mortarLayerName());
    TwoLineTool::saveDocDefaults(m_ctx->activeScene());
}

// ─────────────────────────────────────────────────────────────────────────────
// WallTypeCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *WallTypeCommand::createCommand(IAppContext *c)
{
    return new WallTypeCommand(c, Plain);
}

CommandBase *WallTypeCommand::createMortarCommand(IAppContext *c)
{
    return new WallTypeCommand(c, Mortar);
}

CommandBase *WallTypeCommand::createSpaceMortarCommand(IAppContext *c)
{
    return new WallTypeCommand(c, SpaceMortar);
}

WallTypeCommand::WallTypeCommand(IAppContext *ctx, Kind kind, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
    , m_kind(kind)
{}

void WallTypeCommand::execute()
{
    if (!m_ctx) return;

    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    if (m_kind == SpaceMortar)
        executeSpaceMortar(parentWin);
    else if (m_kind == Mortar)
        executeMortar(parentWin);
    else
        executePlain(parentWin);
}

// 벽+몰탈: wall-5~7 → 편심4-Line
void WallTypeCommand::executeMortar(QWidget *parentWin)
{
    WallTypeDialog dlg(5, 3, tr("벽+몰탈"), 2, parentWin);
    if (dlg.exec() != QDialog::Accepted)
        return;

    double spacing      = 100.0;
    double centerPos    = 50.0;
    bool   outerMortar  = true;
    switch (dlg.selectedType()) {
    case 5: spacing = 100.0; centerPos = 50.0;  break;
    case 6: spacing = 200.0; centerPos = 100.0; break;
    case 7: spacing = 200.0; centerPos = 100.0; outerMortar = false; break;
    default: return;
    }

    // 편심4-Line 모드 값으로 저장 (모드별 따로 보관됨)
    TwoLineTool::setGlobalSpacing(spacing);
    const int modeE4 = TwoLineTool::modeKey(true, false, true);
    TwoLineTool::setGlobalEccentricOffset(modeE4, centerPos);
    TwoLineTool::setGlobalInnerMortarEnabled(modeE4, true);
    TwoLineTool::setGlobalOuterMortarEnabled(modeE4, outerMortar);
    TwoLineTool::saveDocDefaults(m_ctx->activeScene());

    m_ctx->startTool(new EccentricFourLineTool());
}

// 벽+공간+몰탈: wall-8~14 → 편심6-Line (안쪽/공간/바깥쪽/중심선 위치/외벽 몰타르)
void WallTypeCommand::executeSpaceMortar(QWidget *parentWin)
{
    WallTypeDialog dlg(8, 7, tr("벽+공간+몰탈"), 3, parentWin);
    if (dlg.exec() != QDialog::Accepted)
        return;

    struct Preset { int id; double inner, space, outer, center; bool outerMortar; };
    static const Preset kPresets[] = {
        { 8,  100.0, 50.0, 100.0, 125.0, true  },
        { 9,  100.0, 50.0, 100.0, 125.0, false },
        { 10, 200.0, 50.0, 100.0, 175.0, false },
        { 11, 200.0, 50.0, 100.0, 100.0, false },
        { 12, 100.0, 50.0, 100.0,  75.0, false },
        { 13, 100.0, 50.0, 200.0, 175.0, false },
        { 14, 200.0, 50.0, 100.0, 225.0, false },
    };

    const Preset *sel = nullptr;
    for (const Preset &p : kPresets) {
        if (p.id == dlg.selectedType()) { sel = &p; break; }
    }
    if (!sel) return;

    // 편심6-Line 모드 값으로 저장 (모드별 따로 보관됨)
    const int modeE6 = TwoLineTool::modeKey(true, true, true);
    TwoLineTool::setGlobalInnerWallThickness(sel->inner);
    TwoLineTool::setGlobalSpaceThickness(sel->space);
    TwoLineTool::setGlobalOuterWallThickness(sel->outer);
    TwoLineTool::setGlobalEccentricOffset(modeE6, sel->center);
    TwoLineTool::setGlobalInnerMortarEnabled(modeE6, true);
    TwoLineTool::setGlobalOuterMortarEnabled(modeE6, sel->outerMortar);
    TwoLineTool::saveDocDefaults(m_ctx->activeScene());

    m_ctx->startTool(new EccentricSixLineTool());
}

// 벽체: wall-1~4 → 2-Line / 편심2-Line
void WallTypeCommand::executePlain(QWidget *parentWin)
{
    WallTypeDialog dlg(1, 4, tr("벽체"), 2, parentWin);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // 벽체 종류별 (간격, 편심 여부, 중심선 위치)
    double spacing   = 100.0;
    bool   eccentric = false;
    double centerPos = 175.0;
    switch (dlg.selectedType()) {
    case 1: spacing = 100.0; break;
    case 2: spacing = 200.0; break;
    case 3: spacing = 200.0; eccentric = true; centerPos = 175.0; break;
    case 4: spacing = 250.0; eccentric = true; centerPos = 175.0; break;
    default: return;
    }

    // TwoLineTool::activate()가 도면 Setup 값을 다시 읽으므로 도면 스코프에도 저장해 둔다.
    TwoLineTool::setGlobalSpacing(spacing);
    if (eccentric)
        TwoLineTool::setGlobalEccentricOffset(TwoLineTool::modeKey(false, false, true), centerPos);
    TwoLineTool::saveDocDefaults(m_ctx->activeScene());

    if (eccentric)
        m_ctx->startTool(new EccentricTwoLineTool());
    else
        m_ctx->startTool(new TwoLineTool());
}
