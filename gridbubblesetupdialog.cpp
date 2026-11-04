#include "gridbubblesetupdialog.h"

#include "layer.h"
#include "layermanager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

// ── 헬퍼: LayerManager → 콤보박스 (컬러 아이콘) ──────────────────────────────
void GridBubbleSetupDialog::populateLayerCombo(QComboBox    *combo,
                                               LayerManager *mgr,
                                               const QString &selectedName)
{
    combo->blockSignals(true);
    combo->clear();

    if (mgr) {
        for (const Layer &layer : mgr->getAllLayers()) {
            QPixmap px(16, 16);
            px.fill(layer.color());
            combo->addItem(QIcon(px), layer.name(), layer.name());
        }
    }

    int idx = combo->findData(selectedName);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    combo->blockSignals(false);
}

// ─────────────────────────────────────────────────────────────────────────────
GridBubbleSetupDialog::GridBubbleSetupDialog(LabelType      labelType,
                                             LayerManager  *layerMgr,
                                             const QString &currentLayer,
                                             const QString &currentLabelLayer,
                                             bool           tiltApply,
                                             double         labelSize,
                                             double         diameter,
                                             QWidget       *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("주열번호 설정"));
    setModal(true);
    setMinimumWidth(300);

    // ── 라벨 선택 그룹 ───────────────────────────────────────────────────────
    QGroupBox *grpLabel = new QGroupBox(tr("라벨 선택"), this);

    m_radioX   = new QRadioButton(tr("X  (X1, X2, X3 ...)"), grpLabel);
    m_radioY   = new QRadioButton(tr("Y  (Y1, Y2, Y3 ...)"), grpLabel);
    m_radioA   = new QRadioButton(tr("A  (A, B, C ...)"),    grpLabel);
    m_radioNum = new QRadioButton(tr("1  (1, 2, 3 ...)"),    grpLabel);

    m_btnGroup = new QButtonGroup(this);
    m_btnGroup->addButton(m_radioX,   TypeX);
    m_btnGroup->addButton(m_radioY,   TypeY);
    m_btnGroup->addButton(m_radioA,   TypeA);
    m_btnGroup->addButton(m_radioNum, TypeNum);

    switch (labelType) {
    case TypeX:   m_radioX->setChecked(true);   break;
    case TypeY:   m_radioY->setChecked(true);   break;
    case TypeA:   m_radioA->setChecked(true);   break;
    case TypeNum: m_radioNum->setChecked(true); break;
    }

    // 라벨 크기 스핀박스 (기본값, 스케일 미적용)
    m_spinLabelSize = new QDoubleSpinBox(grpLabel);
    m_spinLabelSize->setRange(0.01, 100000.0);
    m_spinLabelSize->setDecimals(2);
    m_spinLabelSize->setSingleStep(1.0);
    m_spinLabelSize->setValue(labelSize);

    // 라벨레이어 콤보 (컬러 아이콘)
    m_comboLabelLayer = new QComboBox(grpLabel);
    populateLayerCombo(m_comboLabelLayer, layerMgr, currentLabelLayer);

    // 기울기 적용 체크박스
    m_chkTilt = new QCheckBox(tr("기울기 적용"), grpLabel);
    m_chkTilt->setChecked(tiltApply);

    QFormLayout *labelForm = new QFormLayout;
    labelForm->addRow(tr("라벨 크기 :"),  m_spinLabelSize);
    labelForm->addRow(tr("라벨레이어 :"), m_comboLabelLayer);

    QVBoxLayout *labelLayout = new QVBoxLayout(grpLabel);
    labelLayout->addWidget(m_radioX);
    labelLayout->addWidget(m_radioY);
    labelLayout->addWidget(m_radioA);
    labelLayout->addWidget(m_radioNum);
    labelLayout->addLayout(labelForm);
    labelLayout->addWidget(m_chkTilt);

    // ── 원 그룹 ─────────────────────────────────────────────────────────────
    QGroupBox *grpCircle = new QGroupBox(tr("원"), this);

    // 원 지름 스핀박스 (기본값, 스케일 미적용)
    m_spinDiameter = new QDoubleSpinBox(grpCircle);
    m_spinDiameter->setRange(0.01, 1000000.0);
    m_spinDiameter->setDecimals(2);
    m_spinDiameter->setSingleStep(1.0);
    m_spinDiameter->setValue(diameter);

    m_comboLayer = new QComboBox(grpCircle);
    populateLayerCombo(m_comboLayer, layerMgr, currentLayer);

    QFormLayout *circleForm = new QFormLayout(grpCircle);
    circleForm->addRow(tr("원 지름 :"),   m_spinDiameter);
    circleForm->addRow(tr("원 레이어 :"), m_comboLayer);

    // ── OK / Cancel ─────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    // ── 최종 레이아웃 ────────────────────────────────────────────────────────
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(grpLabel);
    mainLayout->addWidget(grpCircle);
    mainLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    connect(m_btnOk,     &QPushButton::clicked, this, &GridBubbleSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &GridBubbleSetupDialog::onCancelClicked);
}

// ── 접근자 ────────────────────────────────────────────────────────────────────

GridBubbleSetupDialog::LabelType GridBubbleSetupDialog::labelType() const
{
    return static_cast<LabelType>(m_btnGroup->checkedId());
}

QString GridBubbleSetupDialog::layerName() const
{
    return m_comboLayer->currentData().toString();
}

QString GridBubbleSetupDialog::labelLayerName() const
{
    return m_comboLabelLayer->currentData().toString();
}

bool GridBubbleSetupDialog::tiltEnabled() const
{
    return m_chkTilt->isChecked();
}

double GridBubbleSetupDialog::labelSize() const
{
    return m_spinLabelSize->value();
}

double GridBubbleSetupDialog::diameter() const
{
    return m_spinDiameter->value();
}

void GridBubbleSetupDialog::onOkClicked()
{
    accept();
}

void GridBubbleSetupDialog::onCancelClicked()
{
    reject();
}
