#include "mdicontroller.h"
#include "subwindowcontrols.h"
#include "drawsubwindow.h"
#include "mygraphicsview.h"
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

MdiController::MdiController(QMdiArea *mdiArea, SubWindowControls *controls,
                             QObject *parent)
    : QObject(parent), m_mdiArea(mdiArea), m_subControls(controls) {}

bool MdiController::handleWindowStateChange(QObject *watched, QEvent *event) {
  Q_UNUSED(event)
  if (!m_mdiArea)
    return false;

  QMdiSubWindow *sub = qobject_cast<QMdiSubWindow *>(watched);
  if (!sub)
    return false;

  // [핵심] 분리된 독립 창(Floating Window)은 MDI 내부 창 스타일(프레임리스) 조작 대상에서 완전히 제외
  if (!sub->parentWidget() || sub->isWindow())
    return false;

  if (sub->property("isInternalChanging").toBool())
    return false;

  Qt::WindowStates state = sub->windowState();
  Qt::WindowFlags flags = sub->windowFlags();

  bool isMaximized = (state & Qt::WindowMaximized);
  bool isMinimized = (state & Qt::WindowMinimized);
  bool isFrameless = (flags & Qt::FramelessWindowHint);

  // 이전에 최소화 상태였던 도면 창이 이번 이벤트에서 최대화 상태로 바뀌었다면
  // "최소화 후 최대화 복구" 시나리오이므로, 뷰가 도면 전체를 보여주도록
  // zoomAll()을 예약한다. (레이아웃/뷰포트 크기가 확정된 뒤 호출되도록 지연)
  bool wasMinimized = sub->property("mdiWasMinimized").toBool();
  if (wasMinimized && isMaximized) {
    m_zoomAllPendingSub = sub;
    QTimer::singleShot(150, this, &MdiController::onZoomAllRestoreTimeout);
  }
  sub->setProperty("mdiWasMinimized", isMinimized);

  // [핵심 복원] 새 창 생성 직후 등에서 최대화 상태와 프레임리스 플래그가 불일치할 때
  // 도킹 중인 창(isDockingAttached)은 FBO 보호를 위해 건너뛰고, 새 창일 때만
  // hide() -> setWindowFlags() -> showMaximized()를 통해 네이티브 창 스타일 및 마우스 리시버 완벽 동기화
  if (isMaximized != isFrameless && !sub->property("isDockingAttached").toBool()) {
    m_stylePendingSub = sub;
    m_stylePendingMaximized = isMaximized;
    QTimer::singleShot(10, this, &MdiController::onFrameStyleSyncTimeout);
  }

  QTimer::singleShot(50, this, &MdiController::updateSubWindowControls);
  return false;
}

void MdiController::onFrameStyleSyncTimeout() {
  if (!m_stylePendingSub)
    return;

  QMdiSubWindow *pSub = m_stylePendingSub.data();
  if (!pSub)
    return;

  bool isMaximized = m_stylePendingMaximized;
  if (pSub->isMaximized() == isMaximized &&
      (bool)(pSub->windowFlags() & Qt::FramelessWindowHint) == isMaximized)
    return;

  if (pSub->property("isDockingAttached").toBool())
    return;

  pSub->setProperty("isInternalChanging", true);
  pSub->hide();
  if (isMaximized)
    pSub->setWindowFlags(Qt::SubWindow | Qt::FramelessWindowHint);
  else
    pSub->setWindowFlags(Qt::SubWindow);

  if (isMaximized)
    pSub->showMaximized();
  else if (pSub->isMinimized())
    pSub->showMinimized();
  else
    pSub->showNormal();

  QTimer::singleShot(50, this, &MdiController::onFrameStyleFinalizeTimeout);
}

void MdiController::onFrameStyleFinalizeTimeout() {
  if (!m_stylePendingSub)
    return;

  QMdiSubWindow *pSub = m_stylePendingSub.data();
  m_stylePendingSub.clear();
  if (!pSub)
    return;

  pSub->setProperty("isInternalChanging", false);
  updateSubWindowControls();

  // [버그 수정] 이 타임아웃은 프레임 스타일(프레임리스<->일반) 동기화가 끝난
  // 뒤 pSub를 활성창으로 재지정하는데, pSub가 "최소화"로 전환된 경우까지
  // 무조건 활성화해버리면 minimize 핸들러(MainWindow::onSubWindowMinimize)가
  // 방금 다른 창을 활성화해 놓은 것을 곧바로 되돌려 버린다. 그 결과 화면에는
  // 다른 창이 보이는데 탭바는 방금 최소화된 창의 탭을 계속 활성색으로
  // 표시하는 불일치가 발생했다. 최소화된 창은 활성화 대상에서 제외한다.
  if (pSub->isMinimized())
    return;

  if (m_mdiArea) {
    m_mdiArea->setActiveSubWindow(pSub);
  }
  pSub->activateWindow();
  if (QWidget *topWin = m_mdiArea ? m_mdiArea->window() : nullptr) {
    topWin->activateWindow();
  }
  if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(pSub)) {
    if (dw->view()) {
      dw->view()->setFocus();
      if (dw->view()->viewport()) {
        dw->view()->viewport()->setFocus();
      }
    }
  }
}

void MdiController::onZoomAllRestoreTimeout() {
  // 예약해둔 서브윈도우가 이미 사라졌으면 아무것도 하지 않는다
  if (!m_zoomAllPendingSub)
    return;

  // DrawSubWindow(도면 창)가 맞는지 확인 후, 뷰를 전체 도면이 보이도록 맞춘다
  DrawSubWindow *drawWindow =
      qobject_cast<DrawSubWindow *>(m_zoomAllPendingSub.data());
  m_zoomAllPendingSub.clear();

  if (drawWindow && drawWindow->view())
    drawWindow->view()->zoomAll();
}

void MdiController::syncAllSubWindowStyles() {
  if (!m_mdiArea)
    return;
  // [핵심] DrawSubWindow는 생성자 및 도킹 시 프레임리스로 고정되므로,
  // 런타임에 setWindowFlags()를 호출하여 OpenGL FBO 및 네이티브 핸들을 파괴하지 않도록
  // 불필요한 플래그 변경을 배제하고 컨트롤 버튼만 동기화한다.
  updateSubWindowControls();
}

void MdiController::updateSubWindowControls() {
  if (!m_mdiArea || !m_subControls)
    return;

  // 창 상태가 바뀌거나 작업 영역이 활성화될 때 약간의 지연 후 체크 (사용자 규칙: 람다 대신 포인터 슬롯)
  QTimer::singleShot(50, this, &MdiController::onUpdateControlsTimeout);
}

void MdiController::onUpdateControlsTimeout() {
  if (!m_mdiArea || !m_subControls)
    return;

  QMdiSubWindow *activeSub = m_mdiArea->activeSubWindow();
  // [수정] 활성 창이 없거나 최대화 상태가 아니면 숨김 처리
  bool shouldShow = (activeSub != nullptr && activeSub->isMaximized());
  m_subControls->setVisible(shouldShow);
}

void MdiController::setupNewSubWindowActivation(QMdiSubWindow *subWindow) {
  if (!subWindow)
    return;

  m_pendingNewSub = subWindow;

  // 1. 혹시 남아있을 수 있는 마우스 그래버 및 OS 캡처 즉시 해제
  if (QWidget::mouseGrabber()) {
    QWidget::mouseGrabber()->releaseMouse();
  }

#ifdef Q_OS_WIN
  if (::GetCapture()) {
    ::ReleaseCapture();
  }
  if (m_mdiArea && m_mdiArea->window()) {
    HWND hwndMain = reinterpret_cast<HWND>(m_mdiArea->window()->winId());
    if (hwndMain) {
      ::SetForegroundWindow(hwndMain);
      ::SetActiveWindow(hwndMain);
    }
  }
#endif

  if (m_mdiArea) {
    m_mdiArea->setActiveSubWindow(subWindow);
  }
  subWindow->raise();
  subWindow->activateWindow();

  // 2. 창 레이아웃 및 OS 이벤트 큐 완료 후 확실한 포커스/마우스 동기화 수행 (사용자 규칙: 람다 금지)
  QTimer::singleShot(20, this, &MdiController::onNewSubWindowActivateTimeout);
}

void MdiController::scheduleSubWindowActivation(QMdiSubWindow *subWindow, int delayMs) {
  if (!subWindow)
    return;

  m_pendingNewSub = subWindow;
  QTimer::singleShot(delayMs, this, &MdiController::onNewSubWindowActivateTimeout);
}

void MdiController::onNewSubWindowActivateTimeout() {
  if (!m_pendingNewSub)
    return;

  QMdiSubWindow *sub = m_pendingNewSub.data();
  m_pendingNewSub.clear();
  if (!sub)
    return;

  // 마우스 그래버 최종 확인 및 해제
  if (QWidget::mouseGrabber()) {
    QWidget::mouseGrabber()->releaseMouse();
  }

#ifdef Q_OS_WIN
  if (::GetCapture()) {
    ::ReleaseCapture();
  }
  if (m_mdiArea && m_mdiArea->window()) {
    HWND hwndMain = reinterpret_cast<HWND>(m_mdiArea->window()->winId());
    if (hwndMain) {
      ::SetForegroundWindow(hwndMain);
      ::SetActiveWindow(hwndMain);
    }
  }
#endif

  if (m_mdiArea) {
    m_mdiArea->setActiveSubWindow(sub);
  }
  sub->activateWindow();
  if (m_mdiArea && m_mdiArea->window()) {
    m_mdiArea->window()->activateWindow();
  }

  DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(sub);
  if (drawSub && drawSub->view()) {
    drawSub->view()->setFocus(Qt::OtherFocusReason);
    if (drawSub->view()->viewport()) {
      drawSub->view()->viewport()->setFocus(Qt::OtherFocusReason);
      if (drawSub->view()->viewport()->underMouse()) {
        drawSub->view()->setCustomCrossCursor();
      } else {
        drawSub->view()->applyArrowCursorOutside();
      }
    }
  }

#ifdef Q_OS_WIN
  // [핵심] 현재 마우스 위치 아래의 네이티브 윈도우에 WM_SETCURSOR를 강제 통지하여
  // 사용자가 클릭하지 않아도 OS 커서 파이프라인이 즉시 정상 작동하도록 보장
  POINT pt;
  if (::GetCursorPos(&pt)) {
    HWND hwndUnder = ::WindowFromPoint(pt);
    if (hwndUnder) {
      ::SendMessage(hwndUnder, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwndUnder),
                    MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }
  }
#endif
}
