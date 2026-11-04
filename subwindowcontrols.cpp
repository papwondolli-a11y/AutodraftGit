#include "subwindowcontrols.h"

SubWindowControls::SubWindowControls(QWidget *parent) : QWidget(parent) {
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // 하드코딩된 스타일시트 제거 및 QPushButton으로 변경

  // 0. 창 분리/도킹 버튼
  m_btnFloat = new QPushButton(QStringLiteral("⧉"), this);
  m_btnFloat->setObjectName("SubWinFloatButton");
  m_btnFloat->setToolTip(tr("창 분리 / 도킹"));
  m_btnFloat->setFixedSize(24, 24);
  connect(m_btnFloat, &QPushButton::clicked, this,
          &SubWindowControls::floatClicked);

  // 1. 최소화 버튼
  m_btnMin = new QPushButton("_", this);
  m_btnMin->setObjectName("SubWinMinButton"); // ThemeManager 타겟팅용
  m_btnMin->setFixedSize(24, 24);
  connect(m_btnMin, &QPushButton::clicked, this,
          &SubWindowControls::minimizeClicked);

  // 2. 닫기 버튼
  m_btnClose = new QPushButton("X", this);
  m_btnClose->setObjectName("SubWinCloseButton");
  m_btnClose->setFixedSize(24, 24);
  connect(m_btnClose, &QPushButton::clicked, this,
          &SubWindowControls::closeClicked);

  layout->addWidget(m_btnFloat);
  layout->addWidget(m_btnMin);
  layout->addWidget(m_btnClose);

  // 위젯 자체 배경은 투명하게 설정
  setAttribute(Qt::WA_TranslucentBackground);

  // 초기 상태에서는 보이지 않도록 설정
  this->hide();
}