#include "SideTitleBar.h"
#include <QStyle>
#include <QApplication>
#include <QHBoxLayout> // 혹은 QVBoxLayout

SideTitleBar::SideTitleBar(Qt::Orientation orient, QWidget *parent)
    : QWidget(parent), targetDock(nullptr), orientation(orient)
{
    setupUi();
}
void SideTitleBar::setTargetDock(QDockWidget *dock)
{
    targetDock = dock;
}

void SideTitleBar::setupUi()
{

    // [핵심 로직] 방향에 따라 레이아웃 결정
    QBoxLayout *layout;
    if (orientation == Qt::Vertical) {
        // [세로 모드] (왼쪽 타이틀바)
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 4, 2, 2); // 상하 여백
    } else {
        // [가로 모드] (하단 입력창 옆)
        layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 4, 0); // 좌우 여백 (입력창과의 간격 고려)
    }

    layout->setSpacing(8); // 버튼 사이 간격

    // 버튼 생성 (공통)
    btnClose = new QToolButton(this);
    btnClose->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    btnClose->setAutoRaise(true);
    btnClose->setFixedSize(12, 12);
    btnClose->setStyleSheet("border: none;");
    connect(btnClose, &QToolButton::clicked, this, &SideTitleBar::closeClicked);

    btnFloat = new QToolButton(this);
    btnFloat->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
    btnFloat->setAutoRaise(true);
    btnFloat->setFixedSize(12, 12);
    btnFloat->setStyleSheet("border: none;");
    connect(btnFloat, &QToolButton::clicked, this, &SideTitleBar::floatClicked);

    // [핵심 배치]
    if (orientation == Qt::Vertical) {
        // 세로일 땐: 위에서 아래로 배치 + 아래쪽 빈 공간(Stretch)
        layout->addWidget(btnClose);
        layout->addWidget(btnFloat);
        layout->addStretch();
    } else {
        // 가로일 땐: 왼쪽에서 오른쪽으로 배치
        // (입력창 왼쪽에 붙을 것이므로 Stretch 없이 버튼만 딱 들어가게)
        layout->addWidget(btnClose);
        layout->addWidget(btnFloat);
    }
}

// [핵심 기능] 마우스 클릭 이벤트 처리 (공간 판별)
void SideTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {

        // 1. [공간 판별] 클릭한 위치에 자식 위젯(버튼)이 있는지 확인
        // childAt은 해당 좌표에 있는 자식 위젯 포인터를 반환합니다.
        // 버튼이 있으면(nullptr이 아니면) 드래그 로직을 수행하지 않고 리턴합니다.
        if (childAt(event->position().toPoint()) != nullptr) {
            QWidget::mousePressEvent(event); // 버튼에게 클릭 이벤트 넘겨줌
            return;
        }

        // 2. 버튼이 없는 빈 공간을 클릭했을 때만 드래그 준비
        if (targetDock) {
            isPressed = true;
            dragStartPos = event->position().toPoint();
        }
    }
    QWidget::mousePressEvent(event);
}

void SideTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    // 버튼 위에서 드래그 시작된 경우는 isPressed가 false이므로 실행 안 됨 (안전)
    if (isPressed && (event->buttons() & Qt::LeftButton) && targetDock) {

        if ((event->position().toPoint() - dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
            return;
        }

        if (!targetDock->isFloating()) {
            targetDock->setFloating(true);
        }

        // 좌표 계산 (이전 답변의 보정된 로직)
        QPoint targetPos = event->globalPosition().toPoint() - dragStartPos;
        targetDock->move(targetPos);
    }
    QWidget::mouseMoveEvent(event);
}

void SideTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    isPressed = false;
    QWidget::mouseReleaseEvent(event);
}
