#include "windowsettingsdialog.h"
#include "planwindowtool.h"

#include "commanddock.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

// ── 헬퍼: LayerManager → 콤보박스 (컬러 아이콘) ──────────────────────────────
void WindowSettingsDialog::populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                                              const QString &selected, bool withNone)
{
    combo->clear();
    if (withNone)
        combo->addItem(tr("(없음)"), QString());

    if (mgr) {
        for (const Layer &layer : mgr->getAllLayers()) {
            QPixmap px(16, 16);
            px.fill(layer.color());
            combo->addItem(QIcon(px), layer.name(), layer.name());
        }
    }

    const int idx = combo->findData(selected);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

// ── Win1~Win7 입력 페이지: 왼쪽 도해 + 오른쪽 입력 ───────────────────────────
QWidget *WindowSettingsDialog::buildWinPage(QDoubleSpinBox **spins, const double *vals, bool topOnly)
{
    QWidget *page = new QWidget(this);

    QLabel *img = new QLabel(page);
    QPixmap pm(QStringLiteral(":/icons/winfr3.png"));
    if (!pm.isNull())
        img->setPixmap(pm.scaledToWidth(230, Qt::SmoothTransformation));
    img->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    QFormLayout *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    for (int i = 0; i < 7; ++i) {
        spins[i] = new QDoubleSpinBox(page);
        spins[i]->setRange(0.1, 2000.0);
        spins[i]->setDecimals(1);
        spins[i]->setSingleStep(5.0);
        spins[i]->setSuffix(tr(" mm"));
        spins[i]->setValue(vals ? vals[i] : 0.0);
        form->addRow(QString("Win%1 :").arg(i + 1), spins[i]);

        // 상단부 전용: Win2(틀 두께)·Win3(하단 직사각형 높이)은 사용하지 않으므로 블라인드 처리
        if (topOnly && (i == 1 || i == 2)) {
            spins[i]->setEnabled(false);
            spins[i]->setStyleSheet(QStringLiteral(
                "QDoubleSpinBox:disabled { color: #d0d0d0; background: #f6f6f6; border: 1px solid #e4e4e4; }"));
            QWidget *lbl = form->labelForField(spins[i]);
            if (lbl)
                lbl->setStyleSheet(QStringLiteral("color: #d0d0d0;"));
        }
    }

    QHBoxLayout *row = new QHBoxLayout(page);
    row->addWidget(img);
    row->addLayout(form);
    return page;
}

WindowSettingsDialog::WindowSettingsDialog(LayerManager *layerMgr,
                                           const QString &frameLayer,
                                           const QString &otherLayer,
                                           const QString &trimExcludeLayer,
                                           const double *win, const double *winTop,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("창문설정"));
    setModal(true);

    // ── [Layer 설정] ─────────────────────────────────────────────────────────
    m_comboFrame       = new QComboBox(this);
    m_comboOther       = new QComboBox(this);
    m_comboTrimExclude = new QComboBox(this);
    populateLayerCombo(m_comboFrame,       layerMgr, frameLayer,       false);
    populateLayerCombo(m_comboOther,       layerMgr, otherLayer,       false);
    populateLayerCombo(m_comboTrimExclude, layerMgr, trimExcludeLayer, true);

    QFormLayout *layerForm = new QFormLayout;
    layerForm->setLabelAlignment(Qt::AlignRight);
    layerForm->addRow(tr("창문 프레임  :"),     m_comboFrame);
    layerForm->addRow(tr("창문 나머지 :"),      m_comboOther);
    layerForm->addRow(tr("Trim 제외 레이어 :"), m_comboTrimExclude);

    QGroupBox *layerGroup = new QGroupBox(tr("Layer 설정"), this);
    layerGroup->setLayout(layerForm);

    // ── [창 상세 치수] 탭: 미서기창 / 상단부 ─────────────────────────────────
    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(buildWinPage(m_spinWin,    win,    false), tr("미서기창"));
    tabs->addTab(buildWinPage(m_spinWinTop, winTop, true),  tr("상단부"));

    QGroupBox *winGroup = new QGroupBox(tr("창 상세 치수 (Win1 ~ Win7)"), this);
    QVBoxLayout *winLayout = new QVBoxLayout(winGroup);
    winLayout->addWidget(tabs);

    // ── 버튼 ────────────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(layerGroup);
    mainLayout->addWidget(winGroup);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    connect(m_btnOk,     &QPushButton::clicked, this, &QDialog::accept);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

QString WindowSettingsDialog::frameLayer() const { return m_comboFrame->currentData().toString(); }
QString WindowSettingsDialog::otherLayer() const { return m_comboOther->currentData().toString(); }
QString WindowSettingsDialog::trimExcludeLayer() const
{
    return m_comboTrimExclude->currentData().toString();
}
double WindowSettingsDialog::win(int i) const    { return (i >= 0 && i < 7) ? m_spinWin[i]->value()    : 0.0; }
double WindowSettingsDialog::winTop(int i) const { return (i >= 0 && i < 7) ? m_spinWinTop[i]->value() : 0.0; }

// ─────────────────────────────────────────────────────────────────────────────
// WindowSettingsCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *WindowSettingsCommand::createCommand(IAppContext *c)
{
    return new WindowSettingsCommand(c);
}

WindowSettingsCommand::WindowSettingsCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
{}

void WindowSettingsCommand::execute()
{
    if (!m_ctx) return;

    MyGraphicsScene *scene = m_ctx->activeScene();
    PlanWindowTool::loadSettings(scene);

    double win[7], winTop[7];
    for (int i = 0; i < 7; ++i) {
        win[i]    = PlanWindowTool::winParam(i);
        winTop[i] = PlanWindowTool::winParamTop(i);
    }

    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    WindowSettingsDialog dlg(m_ctx->layerManager(),
                             PlanWindowTool::frameLayer(),
                             PlanWindowTool::otherLayer(),
                             PlanWindowTool::trimExcludeLayer(),
                             win, winTop, parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        PlanWindowTool::setFrameLayer(dlg.frameLayer());
        PlanWindowTool::setOtherLayer(dlg.otherLayer());
        PlanWindowTool::setTrimExcludeLayer(dlg.trimExcludeLayer());
        for (int i = 0; i < 7; ++i) {
            PlanWindowTool::setWinParam(i, dlg.win(i));
            PlanWindowTool::setWinParamTop(i, dlg.winTop(i));
        }
        PlanWindowTool::saveSettings(scene);
    }
}
