#include "plandoorsimplesetupdialog.h"
#include "spacefinishgroupbox.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

PlanDoorSimpleSetupDialog::PlanDoorSimpleSetupDialog(
    double h, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("단순문 설정"));
    setModal(true);

    // ── [문 크기] ────────────────────────────────────────────────────────────
    m_spinH = new QDoubleSpinBox(this);
    m_spinH->setRange(100.0, 5000.0);
    m_spinH->setDecimals(1);
    m_spinH->setSingleStep(10.0);
    m_spinH->setSuffix(tr(" mm"));
    m_spinH->setValue(h);

    QFormLayout *sizeForm = new QFormLayout;
    sizeForm->setLabelAlignment(Qt::AlignRight);
    sizeForm->addRow(tr("W  (문 너비) :"), m_spinH);

    QGroupBox *sizeGroup = new QGroupBox(tr("문 크기"), this);
    sizeGroup->setLayout(sizeForm);

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

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(sizeGroup);
    mainLayout->addWidget(m_spaceFinishGroup);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    connect(m_btnOk,     &QPushButton::clicked, this, &QDialog::accept);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

double  PlanDoorSimpleSetupDialog::h()         const { return m_spinH->value(); }
void PlanDoorSimpleSetupDialog::setSpaceFinishType(int type) { m_spaceFinishGroup->setType(type); }
int  PlanDoorSimpleSetupDialog::spaceFinishType() const         { return m_spaceFinishGroup->type(); }
