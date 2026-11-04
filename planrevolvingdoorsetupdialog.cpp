#include "planrevolvingdoorsetupdialog.h"
#include "spacefinishgroupbox.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanRevolvingDoorSetupDialog::PlanRevolvingDoorSetupDialog(
    double h, double v, bool useWallThickness, int wingCount,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("회전문 설정"));
    setModal(true);

    // ── [문 크기] 그룹 ───────────────────────────────────────────────────────
    m_spinH = new QDoubleSpinBox(this);
    m_spinH->setRange(1000.0, 8000.0);
    m_spinH->setDecimals(1);
    m_spinH->setSingleStep(50.0);
    m_spinH->setSuffix(tr(" mm"));
    m_spinH->setValue(h);

    m_spinV = new QDoubleSpinBox(this);
    m_spinV->setRange(50.0, 1000.0);
    m_spinV->setDecimals(1);
    m_spinV->setSingleStep(10.0);
    m_spinV->setSuffix(tr(" mm"));
    m_spinV->setValue(v);

    QWidget     *vModeWidget = new QWidget(this);
    QHBoxLayout *vModeRow    = new QHBoxLayout(vModeWidget);
    vModeRow->setContentsMargins(0, 0, 0, 0);
    m_radioVInput = new QRadioButton(tr("입력값"),    vModeWidget);
    m_radioVWall  = new QRadioButton(tr("벽 두께 값"), vModeWidget);
    vModeRow->addWidget(m_radioVInput);
    vModeRow->addWidget(m_radioVWall);
    vModeRow->addStretch();

    if (useWallThickness) {
        m_radioVWall->setChecked(true);
        m_spinV->setEnabled(false);
    } else {
        m_radioVInput->setChecked(true);
    }

    QFormLayout *sizeForm = new QFormLayout;
    sizeForm->setLabelAlignment(Qt::AlignRight);
    sizeForm->addRow(tr("H (문 크기, 개구부 전체 폭) :"), m_spinH);
    sizeForm->addRow(tr("V (문틀/벽 두께) :"),           m_spinV);
    sizeForm->addRow(tr(""),                              vModeWidget);

    QGroupBox *sizeGroup = new QGroupBox(tr("문 크기"), this);
    sizeGroup->setLayout(sizeForm);

    // ── [날개 개수] 그룹 ─────────────────────────────────────────────────────
    QButtonGroup *wingBtnGroup = new QButtonGroup(this);
    QHBoxLayout  *wingRow      = new QHBoxLayout;
    m_radioWing3 = new QRadioButton(tr("3개"), this);
    m_radioWing4 = new QRadioButton(tr("4개"), this);
    wingBtnGroup->addButton(m_radioWing3, 3);
    wingBtnGroup->addButton(m_radioWing4, 4);
    wingRow->addWidget(m_radioWing3);
    wingRow->addWidget(m_radioWing4);
    wingRow->addStretch();
    if (wingCount == 3) m_radioWing3->setChecked(true);
    else                m_radioWing4->setChecked(true);

    QGroupBox *wingGroup = new QGroupBox(tr("날개 개수"), this);
    wingGroup->setLayout(wingRow);

    // [공간벽 마감형태] 그룹 (swtype1~4, 초기 swtype1)
    m_spaceFinishGroup = new SpaceFinishGroupBox(this);

    // ── 버튼 ────────────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    // ── 메인 레이아웃 ────────────────────────────────────────────────────────
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(sizeGroup);
    mainLayout->addWidget(wingGroup);
    mainLayout->addWidget(m_spaceFinishGroup);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    // 코딩 규칙: 람다 금지 → 멤버 슬롯 함수로 연결
    connect(m_btnOk,     &QPushButton::clicked,
            this, &PlanRevolvingDoorSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked,
            this, &PlanRevolvingDoorSetupDialog::onCancelClicked);
    connect(m_radioVInput, &QRadioButton::toggled,
            this, &PlanRevolvingDoorSetupDialog::onVModeInputToggled);
}

// ── 접근자 ───────────────────────────────────────────────────────────────────
double  PlanRevolvingDoorSetupDialog::h()                const { return m_spinH->value(); }
double  PlanRevolvingDoorSetupDialog::v()                const { return m_spinV->value(); }
bool    PlanRevolvingDoorSetupDialog::useWallThickness() const { return m_radioVWall->isChecked(); }
int     PlanRevolvingDoorSetupDialog::wingCount()        const { return m_radioWing3->isChecked() ? 3 : 4; }
// ── 슬롯 ────────────────────────────────────────────────────────────────────
void PlanRevolvingDoorSetupDialog::onOkClicked()     { accept(); }
void PlanRevolvingDoorSetupDialog::onCancelClicked() { reject(); }

void PlanRevolvingDoorSetupDialog::onVModeInputToggled(bool checked)
{
    m_spinV->setEnabled(checked);
}

void PlanRevolvingDoorSetupDialog::setSpaceFinishType(int type) { m_spaceFinishGroup->setType(type); }
int  PlanRevolvingDoorSetupDialog::spaceFinishType() const         { return m_spaceFinishGroup->type(); }
