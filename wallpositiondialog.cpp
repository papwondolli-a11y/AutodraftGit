#include "wallpositiondialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

WallPositionDialog::WallPositionDialog(Mode defaultMode, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("문 위치 선택"));
    setModal(true);

    QLabel *infoLabel = new QLabel(
        tr("입력한 문틀 두께가 벽 두께보다 작습니다.\n문 위치를 선택하세요."), this);

    m_radioInner  = new QRadioButton(tr("내벽 — 안쪽선에서 10mm 돌출"), this);
    m_radioCenter = new QRadioButton(tr("중앙 — 벽 중앙에 위치"),        this);
    m_radioOuter  = new QRadioButton(tr("외벽 — 바깥선에서 10mm 돌출"), this);

    switch (defaultMode) {
    case Center: m_radioCenter->setChecked(true); break;
    case Outer:  m_radioOuter->setChecked(true);  break;
    default:     m_radioInner->setChecked(true);  break;
    }

    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(infoLabel);
    main->addSpacing(8);
    main->addWidget(m_radioInner);
    main->addWidget(m_radioCenter);
    main->addWidget(m_radioOuter);
    main->addSpacing(8);
    main->addLayout(btnRow);

    connect(m_btnOk,     &QPushButton::clicked, this, &WallPositionDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &WallPositionDialog::onCancelClicked);
}

WallPositionDialog::Mode WallPositionDialog::mode() const
{
    if (m_radioCenter->isChecked()) return Center;
    if (m_radioOuter->isChecked())  return Outer;
    return Inner;
}

void WallPositionDialog::onOkClicked()     { accept(); }
void WallPositionDialog::onCancelClicked() { reject(); }
