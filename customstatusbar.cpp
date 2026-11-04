#include "customstatusbar.h"
#include "snapmanager.h" // SnapManager의 정의가 필요함
#include <QDebug>
#include <QHBoxLayout>
#include <QWidget>

CustomStatusBar::CustomStatusBar(QWidget *parent) : QStatusBar(parent) {
  // 1. 좌표 라벨 생성
  coordLabel = new QLabel(this);
  coordLabel->setMinimumWidth(200);
  coordLabel->setStyleSheet("padding-left: 10px;"); // 여백 좀 주기
  coordLabel->setText("X: 0.0000 mm, Y: 0.0000 mm");

  // 2. 축척 라벨 생성
  scaleLabel = new QLabel(this);
  scaleLabel->setMinimumWidth(100);
  scaleLabel->setStyleSheet("padding-left: 10px; border-left: 1px solid #ccc;");
  scaleLabel->setText("Scale: None");

  // 3. 도면 크기 라벨 생성
  sizeLabel = new QLabel(this);
  sizeLabel->setMinimumWidth(100);
  sizeLabel->setStyleSheet("padding-left: 10px; border-left: 1px solid #ccc;");
  sizeLabel->setText("Size: None");

  // 3-b. 단위 인디케이터 라벨 생성
  unitLabel = new QLabel(this);
  unitLabel->setMinimumWidth(70);
  unitLabel->setStyleSheet("padding-left: 10px; border-left: 1px solid #ccc;");
  unitLabel->setText("Unit: mm");

  // 4. 상태바 맨 왼쪽에 좌표 라벨, 축척 라벨, 도면 크기 라벨, 단위 라벨 추가
  addWidget(coordLabel);
  addWidget(scaleLabel);
  addWidget(sizeLabel);
  addWidget(unitLabel);
}

QString CustomStatusBar::coordUnitTag() const {
  return (m_linearUnit == LinearUnit::Inch) ? QStringLiteral("\"")
                                            : QStringLiteral(" mm");
}

void CustomStatusBar::refreshCoordinate() {
  const QString tag = coordUnitTag();
  const QString text = QString("X: %1%3,  Y: %2%3")
                           .arg(UnitConvert::toDisplay(m_lastCoord.x(), m_linearUnit), 0, 'f', 4)
                           .arg(UnitConvert::toDisplay(m_lastCoord.y(), m_linearUnit), 0, 'f', 4)
                           .arg(tag);
  coordLabel->setText(text);
}

void CustomStatusBar::updateCoordinate(QPointF pos) {
  m_lastCoord = pos; // 항상 mm(모델 좌표)로 들어온다
  refreshCoordinate();
}

void CustomStatusBar::updateLinearUnit(LinearUnit u) {
  m_linearUnit = u;
  unitLabel->setText((u == LinearUnit::Inch) ? "Unit: inch" : "Unit: mm");
  refreshCoordinate();
}

void CustomStatusBar::updateScale(double scale) {
  if (scale <= 0) {
    scaleLabel->setText("Scale: None");
  } else {
    // 소수점 4자리까지 표시
    scaleLabel->setText(QString("Scale: 1 : %1").arg(scale, 0, 'f', 4));
  }
}

void CustomStatusBar::updateSize(const QString &size) {
  if (size.isEmpty() || size == "None") {
    sizeLabel->setText("Size: None");
  } else {
    sizeLabel->setText(QString("Size: %1").arg(size));
  }
}

// ---------------------------------------------------------
// [핵심] 스냅 버튼 생성 및 SnapManager 연결
// ---------------------------------------------------------
void CustomStatusBar::setupSnapControls(SnapManager *manager) {
  if (!manager)
    return;

  // 1. 버튼들을 담을 컨테이너 위젯 생성
  QWidget *container = new QWidget(this);
  QHBoxLayout *layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 10, 0); // 오른쪽 여백 10
  layout->setSpacing(2);

  // 2. 버튼 생성 (헬퍼 함수 사용)
  QPushButton *btnSnap = createSnapBtn("Snap", container);
  QPushButton *btnOrtho = createSnapBtn("Ortho", container);
  QPushButton *btnNone =
      createSnapBtn("None", container, false); // Checkable false

  // 구분선
  QLabel *spacer = new QLabel("|", container);
  spacer->setStyleSheet("color: #aaa; margin: 0 5px;");
  layout->addWidget(spacer);

  QPushButton *btnEnd = createSnapBtn("End", container);
  QPushButton *btnMid = createSnapBtn("Mid", container);
  QPushButton *btnCen = createSnapBtn("Cen", container);
  QPushButton *btnInt = createSnapBtn("Int", container);
  QPushButton *btnQua = createSnapBtn("Qua", container);
  QPushButton *btnNea = createSnapBtn("Nea", container);
  QPushButton *btnPer = createSnapBtn("Per", container); // [신규]

  // 3. 상태바 오른쪽에 영구 위젯으로 추가
  addPermanentWidget(container);

  // ---------------------------------------------------------
  // 4. 로직 연결 (동기화)
  // ---------------------------------------------------------

  // A. 초기 상태 반영
  btnSnap->setChecked(manager->isOSnap());
  btnOrtho->setChecked(manager->isOrtho());
  btnEnd->setChecked(manager->isSnapEndpoint());
  btnMid->setChecked(manager->isSnapMidpoint());
  btnCen->setChecked(manager->isSnapCenter());
  btnInt->setChecked(manager->isSnapIntersection());
  btnQua->setChecked(manager->isSnapQuadrant());
  btnNea->setChecked(manager->isSnapNearest());
  btnPer->setChecked(manager->isSnapPerpendicular()); // [신규]

  // B. [UI -> Logic] 버튼 클릭 시 매니저 토글
  connect(btnSnap, &QPushButton::clicked, manager, &SnapManager::toggleOSnap);
  connect(btnOrtho, &QPushButton::clicked, manager, &SnapManager::toggleOrtho);
  connect(btnEnd, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapEndpoint);
  connect(btnMid, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapMidpoint);
  connect(btnCen, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapCenter);
  connect(btnInt, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapIntersection);
  connect(btnQua, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapQuadrant);
  connect(btnNea, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapNearest);
  connect(btnPer, &QPushButton::clicked, manager,
          &SnapManager::toggleSnapPerpendicular); // [신규]
  connect(btnNone, &QPushButton::clicked, manager, &SnapManager::clearAllSnaps);

  // C. [Logic -> UI] 매니저 상태 변경 시 버튼 업데이트 (키보드 단축키 등)
  connect(manager, &SnapManager::osnapChanged, btnSnap,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::orthoChanged, btnOrtho,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapEndpointChanged, btnEnd,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapMidpointChanged, btnMid,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapCenterChanged, btnCen,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapIntersectionChanged, btnInt,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapQuadrantChanged, btnQua,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapNearestChanged, btnNea,
          &QPushButton::setChecked);
  connect(manager, &SnapManager::snapPerpendicularChanged, btnPer,
          &QPushButton::setChecked); // [신규]
}

// 버튼 생성 헬퍼
QPushButton *CustomStatusBar::createSnapBtn(const QString &text,
                                            QWidget *parent, bool checkable) {
  QPushButton *btn = new QPushButton(text, parent);
  btn->setCheckable(checkable);
  btn->setFlat(true);
  btn->setFixedSize(45, 24); // 높이를 상태바에 맞게 조절

  // 스타일시트: 평소엔 회색, 체크되면 파란색+굵게
  btn->setStyleSheet(
      "QPushButton { "
      "   border: none; background: transparent; color: #333; font-size: 11px; "
      "}"
      "QPushButton:hover { "
      "   background-color: #e0e0e0; border-radius: 3px; "
      "}"
      "QPushButton:checked { "
      "   color: #0066ff; font-weight: bold; background-color: #d0e0ff; "
      "border-radius: 3px;"
      "}");

  parent->layout()->addWidget(btn);
  return btn;
}
