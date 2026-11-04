#include "parallelcentersetupdialog.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ParallelCenterSetupDialog::ParallelCenterSetupDialog(double offset,
                                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("평행 중심선 설정"));
    setModal(true);

    // ── 중심선 위치 ────────────────────────────────────────────────────────────
    m_spinOffset = new QDoubleSpinBox(this);
    m_spinOffset->setRange(0.0, 999999.0);
    m_spinOffset->setDecimals(2);
    m_spinOffset->setSingleStep(1.0);
    m_spinOffset->setSpecialValueText(tr("자동 (두 선의 중심)"));
    m_spinOffset->setValue(offset);

    QLabel *offsetHint = new QLabel(
        tr("첫 번째 선에서 중심선까지의 거리\n값이 0 이면 두 선의 정중앙에 생성됩니다."), this);
    offsetHint->setWordWrap(true);
    QFont hintFont = offsetHint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.85);
    offsetHint->setFont(hintFont);

    QFormLayout *posForm = new QFormLayout;
    posForm->setLabelAlignment(Qt::AlignRight);
    posForm->addRow(tr("거리 :"), m_spinOffset);
    posForm->addRow(QString(), offsetHint);

    QGroupBox *posGroup = new QGroupBox(tr("중심선 위치"), this);
    posGroup->setLayout(posForm);

    // ── 버튼 ───────────────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);

    // ── 메인 레이아웃 ──────────────────────────────────────────────────────────
    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(posGroup);
    main->addSpacing(8);
    main->addLayout(btnRow);

    connect(m_btnOk,     &QPushButton::clicked, this, &ParallelCenterSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &ParallelCenterSetupDialog::onCancelClicked);
}

double ParallelCenterSetupDialog::offset() const { return m_spinOffset->value(); }

void ParallelCenterSetupDialog::onOkClicked()     { accept(); }
void ParallelCenterSetupDialog::onCancelClicked() { reject(); }
