#include "stairplandialog.h"
#include "layer.h"
#include "layermanager.h"
#include "linetypemanager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QIcon>
#include <QPixmap>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

StairPlanDialog::StairPlanDialog(const StairPlanParams &params, LayerManager *layerMgr,
                                 LineTypeManager *, double wallLength, QWidget *parent)
    : QDialog(parent), m_landType1(params.land), m_wallLength(wallLength)
{
    setWindowTitle(tr("계단평면"));
    setModal(true);

    // ── 타입 이미지 (타입 1 / 타입 2 에 따라 교체) ─────────────────────────
    m_labelImage = new QLabel(this);
    m_labelImage->setAlignment(Qt::AlignCenter);
    m_labelImage->setFixedHeight(290);

    // ── 계단 수 ───────────────────────────────────────────────────────────
    m_spinUp = new QSpinBox(this);
    m_spinUp->setRange(0, 200);
    m_spinUp->setValue(params.up);
    m_spinDown = new QSpinBox(this);
    m_spinDown->setRange(0, 200);
    m_spinDown->setValue(params.down);

    QGroupBox *grpCount = new QGroupBox(tr("계단 수 입력"), this);
    QFormLayout *countForm = new QFormLayout(grpCount);
    countForm->addRow(tr("UP"), m_spinUp);
    countForm->addRow(tr("DN"), m_spinDown);
    QLabel *hint = new QLabel(tr("한쪽이 0 이면 직진 단일 계단, 둘 다 1 이상이면 꺾임(U자) 계단"), grpCount);
    hint->setWordWrap(true);
    countForm->addRow(hint);

    // ── 치수 ──────────────────────────────────────────────────────────────
    m_spinTread = new QDoubleSpinBox(this);
    m_spinTread->setRange(10.0, 5000.0);
    m_spinTread->setDecimals(1);
    m_spinTread->setValue(params.tread);
    m_spinTreadLen = new QDoubleSpinBox(this);
    m_spinTreadLen->setRange(10.0, 20000.0);
    m_spinTreadLen->setDecimals(1);
    m_spinTreadLen->setValue(params.treadLen > 0.0 ? params.treadLen : 800.0);
    m_spinTreadLen->setToolTip(tr("꺾임 계단의 디딤판 가로 길이. 벽선 지정 후 길이x2+200 이 벽선보다 길면 (벽선길이-200)/2 로 자동 조정. 중앙 사각형 폭 = 벽선길이 - 길이x2 (최소 200)"));
    m_spinLand = new QDoubleSpinBox(this);
    m_spinLand->setRange(0.0, 20000.0);
    m_spinLand->setDecimals(1);
    m_spinLand->setValue(params.land);

    // ── 옵션 ──────────────────────────────────────────────────────────────
    m_radioNumYes  = new QRadioButton(tr("Yes"), this);
    m_radioNumNo   = new QRadioButton(tr("No"), this);
    m_radioSlipYes = new QRadioButton(tr("Yes"), this);
    m_radioSlipNo  = new QRadioButton(tr("No"), this);
    // 같은 부모의 라디오버튼은 자동 배타이므로 쌍마다 QButtonGroup 으로 분리
    QButtonGroup *numGroup = new QButtonGroup(this);
    numGroup->addButton(m_radioNumYes);
    numGroup->addButton(m_radioNumNo);
    QButtonGroup *slipGroup = new QButtonGroup(this);
    slipGroup->addButton(m_radioSlipYes);
    slipGroup->addButton(m_radioSlipNo);
    (params.numbering ? m_radioNumYes : m_radioNumNo)->setChecked(true);
    (params.nonslip ? m_radioSlipYes : m_radioSlipNo)->setChecked(true);

    QWidget *numRow = new QWidget(this);
    QHBoxLayout *numLay = new QHBoxLayout(numRow);
    numLay->setContentsMargins(0, 0, 0, 0);
    numLay->addWidget(m_radioNumYes);
    numLay->addWidget(m_radioNumNo);
    numLay->addStretch();

    QWidget *slipRow = new QWidget(this);
    QHBoxLayout *slipLay = new QHBoxLayout(slipRow);
    slipLay->setContentsMargins(0, 0, 0, 0);
    slipLay->addWidget(m_radioSlipYes);
    slipLay->addWidget(m_radioSlipNo);
    slipLay->addStretch();

    // ── 타입 ──────────────────────────────────────────────────────────────
    m_radioType1 = new QRadioButton(tr("타입 1"), this);
    m_radioType2 = new QRadioButton(tr("타입 2 (계단참에 중간계단)"), this);
    QButtonGroup *typeGroup = new QButtonGroup(this);
    typeGroup->addButton(m_radioType1);
    typeGroup->addButton(m_radioType2);
    (params.type == 2 ? m_radioType2 : m_radioType1)->setChecked(true);
    m_spinMid = new QSpinBox(this);
    m_spinMid->setRange(1, 100);
    m_spinMid->setValue(params.midSteps);

    QGroupBox *grpOpt = new QGroupBox(tr("계단 설정"), this);
    QFormLayout *optForm = new QFormLayout(grpOpt);
    QWidget *typeRow = new QWidget(this);
    QHBoxLayout *typeLay = new QHBoxLayout(typeRow);
    typeLay->setContentsMargins(0, 0, 0, 0);
    typeLay->addWidget(m_radioType1);
    typeLay->addWidget(m_radioType2);
    typeLay->addStretch();
    optForm->addRow(typeRow);
    optForm->addRow(tr("중간계단 수"), m_spinMid);
    optForm->addRow(tr("디딤판 폭 (세로)"), m_spinTread);
    optForm->addRow(tr("디딤판 길이 (가로)"), m_spinTreadLen);
    optForm->addRow(tr("계단참 길이 (Landing length)"), m_spinLand);
    optForm->addRow(tr("계단 번호 (Stair Number)"), numRow);
    optForm->addRow(tr("미끄럼방지 (Nonslip Drawing)"), slipRow);

    // ── 레이어 ────────────────────────────────────────────────────────────
    m_comboLayer = new QComboBox(this);
    m_comboNonslipLayer = new QComboBox(this);
    populateLayerCombo(m_comboLayer, layerMgr, params.layer);
    populateLayerCombo(m_comboNonslipLayer, layerMgr, params.nonslipLayer);

    QGroupBox *grpLayer = new QGroupBox(tr("레이어"), this);
    QFormLayout *layerForm = new QFormLayout(grpLayer);
    layerForm->addRow(tr("계단레이어"), m_comboLayer);
    layerForm->addRow(tr("미끄럼방지선"), m_comboNonslipLayer);
    QLabel *layerHint = new QLabel(tr("UP/DN·번호의 원과 글자는 글자 레이어를 따릅니다."), grpLayer);
    layerHint->setWordWrap(true);
    layerForm->addRow(layerHint);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addWidget(m_labelImage);
    root->addWidget(grpCount);
    root->addWidget(grpOpt);
    root->addWidget(grpLayer);
    root->addWidget(buttons);

    connect(m_radioType2, &QRadioButton::toggled, this, &StairPlanDialog::onTypeToggled);
    if (m_wallLength > 0.0) {   // 초기 표시값도 현재 타입 기준으로
        const double v = m_radioType2->isChecked() ? m_wallLength / 3.0
                                                    : (m_wallLength - 200.0) / 2.0;
        m_spinTreadLen->setValue(qMax(10.0, v));
    }
    connect(m_spinMid, QOverload<int>::of(&QSpinBox::valueChanged), this, &StairPlanDialog::onTypeChanged);
    connect(m_spinTread, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StairPlanDialog::onTypeChanged);
    connect(m_spinLand, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StairPlanDialog::onLandEdited);
    onTypeChanged();
    connect(buttons, &QDialogButtonBox::accepted, this, &StairPlanDialog::onOkClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void StairPlanDialog::populateLayerCombo(QComboBox *combo, LayerManager *mgr, const QString &selected)
{
    combo->clear();
    if (mgr) {
        const QList<Layer> layers = mgr->getAllLayers();
        for (int i = 0; i < layers.size(); ++i) {
            QPixmap px(16, 16);
            px.fill(layers.at(i).color());
            combo->addItem(QIcon(px), layers.at(i).name(), layers.at(i).name());
        }
    }
    int idx = combo->findData(selected);
    if (idx < 0 && !selected.isEmpty()) {   // 도면에 아직 없는 기본 레이어 (그릴 때 생성됨)
        combo->addItem(selected, selected);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

// 타입2: 중간계단 수 입력 활성화, 계단참 길이 = 중간계단수 x 디딤판폭 + 200 (자동 표시)
// 타입 전환 시 디딤판 길이 기본값: 타입1 = (벽선-200)/2, 타입2 = 벽선/3
void StairPlanDialog::onTypeToggled()
{
    if (m_wallLength > 0.0) {
        const double v = m_radioType2->isChecked() ? m_wallLength / 3.0
                                                    : (m_wallLength - 200.0) / 2.0;
        m_spinTreadLen->setValue(qMax(10.0, v));
    }
    onTypeChanged();
}

void StairPlanDialog::onTypeChanged()
{
    const bool t2 = m_radioType2->isChecked();
    const QPixmap pm(t2 ? QStringLiteral(":/icons/stairtype2.png")
                        : QStringLiteral(":/icons/stairtype1.png"));
    if (!pm.isNull())
        m_labelImage->setPixmap(pm.scaledToHeight(280, Qt::SmoothTransformation));
    m_spinMid->setEnabled(t2);
    m_spinLand->setEnabled(!t2);
    if (t2) {
        m_spinLand->setValue(m_spinMid->value() * m_spinTread->value() + 200.0);
    } else {
        m_spinLand->setValue(m_landType1);
    }
}

void StairPlanDialog::onLandEdited(double v)
{
    if (m_spinLand->isEnabled()) m_landType1 = v;
}

void StairPlanDialog::onOkClicked()
{
    if (m_spinUp->value() == 0 && m_spinDown->value() == 0) {
        QMessageBox::warning(this, tr("계단평면"), tr("UP 과 DN 이 모두 0 입니다."));
        return;
    }
    accept();
}

StairPlanParams StairPlanDialog::params() const
{
    StairPlanParams p;
    p.up        = m_spinUp->value();
    p.down      = m_spinDown->value();
    p.tread     = m_spinTread->value();
    p.land      = m_spinLand->value();
    p.type      = m_radioType2->isChecked() ? 2 : 1;
    p.midSteps  = m_spinMid->value();
    p.treadLen  = m_spinTreadLen->value();
    p.numbering = m_radioNumYes->isChecked();
    p.nonslip   = m_radioSlipYes->isChecked();
    p.layer        = m_comboLayer->currentData().toString();
    p.nonslipLayer = m_comboNonslipLayer->currentData().toString();
    return p;
}
