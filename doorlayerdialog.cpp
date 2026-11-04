#include "doorlayerdialog.h"
#include "doorlayersettings.h"

#include "commanddock.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

// ── 헬퍼: LayerManager → 콤보박스 (컬러 아이콘) ──────────────────────────────
void DoorLayerDialog::populateLayerCombo(QComboBox *combo, LayerManager *mgr,
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

DoorLayerDialog::DoorLayerDialog(LayerManager *layerMgr,
                                 const QString &frameLayer,
                                 const QString &otherLayer,
                                 const QString &trimExcludeLayer,
                                 double jambOuter, double stopWidth, double stepHeight,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("도어 레이어설정"));
    setModal(true);

    m_comboFrame       = new QComboBox(this);
    m_comboOther       = new QComboBox(this);
    m_comboTrimExclude = new QComboBox(this);
    populateLayerCombo(m_comboFrame,       layerMgr, frameLayer,       false);
    populateLayerCombo(m_comboOther,       layerMgr, otherLayer,       false);
    populateLayerCombo(m_comboTrimExclude, layerMgr, trimExcludeLayer, true);

    QFormLayout *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("문틀  :"),            m_comboFrame);
    form->addRow(tr("나머지 :"),           m_comboOther);
    form->addRow(tr("Trim 제외 레이어 :"), m_comboTrimExclude);

    QGroupBox *group = new QGroupBox(tr("Layer"), this);
    group->setLayout(form);

    // ── [문틀 상세치수] ──────────────────────────────────────────────────────
    m_spinJamb = new QDoubleSpinBox(this);
    m_spinStop = new QDoubleSpinBox(this);
    m_spinStep = new QDoubleSpinBox(this);
    QDoubleSpinBox *spins[3] = { m_spinJamb, m_spinStop, m_spinStep };
    const double    vals[3]  = { jambOuter, stopWidth, stepHeight };
    for (int i = 0; i < 3; ++i) {
        spins[i]->setRange(i == 2 ? 10.0 : 1.0, i == 2 ? 50.0 : 500.0);   // K: 10~50
        spins[i]->setDecimals(1);
        spins[i]->setSingleStep(1.0);
        spins[i]->setSuffix(tr(" mm"));
        spins[i]->setValue(vals[i]);
    }

    QFormLayout *dimForm = new QFormLayout;
    dimForm->setLabelAlignment(Qt::AlignRight);
    dimForm->addRow(tr("J (문틀 외곽 폭) :"),   m_spinJamb);
    dimForm->addRow(tr("S (스톱 폭) :"),        m_spinStop);
    dimForm->addRow(tr("K (단 높이) :"),        m_spinStep);

    QGroupBox *dimGroup = new QGroupBox(tr("문틀 상세치수"), this);
    QLabel *dimIcon = new QLabel(this);
    dimIcon->setPixmap(QPixmap(QStringLiteral(":/icons/doorframeset.png")));
    dimIcon->setAlignment(Qt::AlignCenter);

    QHBoxLayout *dimRow = new QHBoxLayout;
    dimRow->addWidget(dimIcon);
    dimRow->addLayout(dimForm, 1);
    dimGroup->setLayout(dimRow);

    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(group);
    mainLayout->addWidget(dimGroup);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    connect(m_btnOk,     &QPushButton::clicked, this, &DoorLayerDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void DoorLayerDialog::onOkClicked()
{
    if (m_spinJamb->value() - m_spinStop->value() <= 10.0) {
        QMessageBox::warning(this, tr("문틀 상세치수"),
                             tr("비정상 입력입니다.\nJ - S 의 값이 10mm 이하입니다. "
                                "J 는 S 보다 10mm 보다 크게 입력하세요."));
        return;
    }
    accept();
}

QString DoorLayerDialog::frameLayer() const { return m_comboFrame->currentData().toString(); }
QString DoorLayerDialog::otherLayer() const { return m_comboOther->currentData().toString(); }
QString DoorLayerDialog::trimExcludeLayer() const
{
    return m_comboTrimExclude->currentData().toString();
}

double DoorLayerDialog::jambOuter() const  { return m_spinJamb->value(); }
double DoorLayerDialog::stopWidth() const  { return m_spinStop->value(); }
double DoorLayerDialog::stepHeight() const { return m_spinStep->value(); }

// ─────────────────────────────────────────────────────────────────────────────
// DoorLayerCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *DoorLayerCommand::createCommand(IAppContext *c)
{
    return new DoorLayerCommand(c);
}

DoorLayerCommand::DoorLayerCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent)
    , m_ctx(ctx)
{}

void DoorLayerCommand::execute()
{
    if (!m_ctx) return;

    MyGraphicsScene *scene = m_ctx->activeScene();
    DoorLayerSettings::load(scene);

    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    DoorLayerDialog dlg(m_ctx->layerManager(),
                        DoorLayerSettings::frameLayer(),
                        DoorLayerSettings::otherLayer(),
                        DoorLayerSettings::trimExcludeLayer(),
                        DoorLayerSettings::jambOuter(),
                        DoorLayerSettings::stopWidth(),
                        DoorLayerSettings::stepHeight(),
                        parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        DoorLayerSettings::setFrameLayer(dlg.frameLayer());
        DoorLayerSettings::setOtherLayer(dlg.otherLayer());
        DoorLayerSettings::setTrimExcludeLayer(dlg.trimExcludeLayer());
        DoorLayerSettings::setJambOuter(dlg.jambOuter());
        DoorLayerSettings::setStopWidth(dlg.stopWidth());
        DoorLayerSettings::setStepHeight(dlg.stepHeight());
        DoorLayerSettings::save(scene);
    }
}
