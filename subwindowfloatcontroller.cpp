#include "subwindowfloatcontroller.h"
#include "drawsubwindow.h"
#include "mygraphicsview.h"
#include "mygraphicsscene.h"
#include "abstracttool.h"
#include "appcontext.h"
#include <QMdiArea>
#include <QRect>
#include <QPoint>
#include <QSize>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <QEvent>
#include <QTimer>

SubWindowFloatController::SubWindowFloatController(QMdiArea *mdiArea, AppContext *appContext, QObject *parent)
    : QObject(parent), m_mdiArea(mdiArea), m_appContext(appContext), m_isActivating(false) {}

SubWindowFloatController::~SubWindowFloatController() {}

void SubWindowFloatController::detachSubWindow(DrawSubWindow *subWindow) {
  if (!subWindow || !m_mdiArea)
    return;

  if (isDetached(subWindow))
    return;

  m_isActivating = true;

  // 1. 현재 모니터(스크린)의 가용 영역을 기준으로 화면 비율의 2/3 크기 계산
  QScreen *screen = nullptr;
  if (subWindow->window() && subWindow->window()->windowHandle()) {
    screen = subWindow->window()->windowHandle()->screen();
  }
  if (!screen && m_mdiArea) {
    QWidget *top = m_mdiArea->window();
    if (top && top->windowHandle()) {
      screen = top->windowHandle()->screen();
    }
  }
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }

  QRect availGeo = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
  int targetW = qRound(availGeo.width() * 2.0 / 3.0);
  int targetH = qRound(availGeo.height() * 2.0 / 3.0);
  int targetX = availGeo.x() + (availGeo.width() - targetW) / 2;
  int targetY = availGeo.y() + (availGeo.height() - targetH) / 2;
  QRect floatGeo(targetX, targetY, targetW, targetH);

  // 2. MDI 영역 관리 목록에서 제거 및 분리창 목록/활성창에 최우선 등록 (활성화 이벤트 필터 오인 방지)
  m_mdiArea->removeSubWindow(subWindow);
  m_detachedList.append(subWindow);
  setActiveFloatingWindow(subWindow);

  // 3. 부모 해제 및 완전 독립 최상위 윈도우 플래그 부여 (시스템 타이틀바, 최소/최대/닫기 컨트롤 완벽 보장)
  subWindow->hide();
  subWindow->setParent(nullptr);
  subWindow->setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                           Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

  // [핵심] MDI 내부의 최대화 상태(Qt::WindowMaximized)를 해제하고 일반 창 상태로 초기화
  subWindow->setWindowState(Qt::WindowNoState);

  // 4. 활성화/포커스 감지 이벤트 필터 설치
  installSubWindowFilters(subWindow);

  // 5. 일반 창 모드로 표시하고 화면 2/3 크기 및 중앙 좌표로 확정
  subWindow->setGeometry(floatGeo);
  subWindow->resize(targetW, targetH);
  subWindow->showNormal();

  // 6. 처음 분리될 때 제일 위로 올라오도록 배치하고 활성화 (비활성 분리창도 대시보드 위 유지)
  raiseFloatingWindows(subWindow);

  if (m_appContext) {
    m_appContext->setLastActiveWindow(subWindow);
  }

  if (subWindow->view()) {
    subWindow->view()->recreateOpenGLViewport();
    subWindow->view()->zoomAll();
  }

  if (subWindow->scene() && subWindow->tool()) {
    subWindow->tool()->activate(subWindow->scene());
    subWindow->scene()->setTool(subWindow->tool());
  }

  // 7. 파괴 감지 시그널 연결 (멤버 함수 포인터 사용)
  connect(subWindow, &QObject::destroyed, this,
          &SubWindowFloatController::onDetachedWindowDestroyed);

  // 8. 탭바 메뉴 종료 후 분리창이 확실히 제일 위로 올라오도록 지연 raise 예약
  QTimer::singleShot(100, this, &SubWindowFloatController::onDetachRaiseTimeout);

  // 9. MDI 영역에 남아있는 고정창이 있다면 화면에 전면 표시 (회색 바탕 방지)
  if (m_mdiArea) {
    QList<QMdiSubWindow *> remaining = m_mdiArea->subWindowList();
    if (!remaining.isEmpty()) {
      QMdiSubWindow *nextActive = m_mdiArea->activeSubWindow();
      if (!nextActive || isDetached(qobject_cast<DrawSubWindow *>(nextActive))) {
        for (int i = remaining.size() - 1; i >= 0; --i) {
          DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(remaining.at(i));
          if (dw && !isDetached(dw)) {
            nextActive = dw;
            break;
          }
        }
      }
      if (nextActive) {
        nextActive->show();
        nextActive->showMaximized();
        nextActive->raise();
        if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(nextActive)) {
          if (dw->widget()) {
            dw->widget()->show();
          }
          if (dw->view()) {
            dw->view()->show();
            dw->view()->setCustomCrossCursor();
            if (dw->view()->viewport()) {
              dw->view()->viewport()->update();
            }
          }
        }
        m_mdiArea->setActiveSubWindow(nextActive);
      }
    }
  }

  m_isActivating = false;

  emit subWindowDetached(subWindow);
}

void SubWindowFloatController::raiseFloatingWindows(DrawSubWindow *activeSubWindow) {
  if (!activeSubWindow)
    return;

  m_isActivating = true;

  // 1. 비활성 분리창들을 메인 윈도우(대시보드 화면) 위로 먼저 순차 배치 (대시보드 뒤로 숨지 않도록 보장)
  // C++11 range-loop detach 경고 방지를 위해 인덱스 기반 루프 사용
  for (int i = 0; i < m_detachedList.size(); ++i) {
    DrawSubWindow *dw = m_detachedList.at(i).data();
    if (dw && dw != activeSubWindow) {
      if (!dw->isMinimized()) {
        dw->show();
        dw->raise();
      }
    }
  }

  // 2. 활성 분리창을 최종 최상단으로 올리고 활성화 (창 순서: 활성창 최상단, 비활성창 중간, 대시보드창 최하단)
  if (activeSubWindow->isMinimized()) {
    activeSubWindow->showNormal();
  }
  activeSubWindow->show();
  activeSubWindow->raise();
  activeSubWindow->activateWindow();

  if (activeSubWindow->view()) {
    activeSubWindow->view()->setFocus();
    activeSubWindow->view()->setCustomCrossCursor();
  }

  m_isActivating = false;
}

void SubWindowFloatController::onDetachRaiseTimeout() {
  if (m_activeFloatingWindow) {
    raiseFloatingWindows(m_activeFloatingWindow.data());
  }
}

void SubWindowFloatController::onDelayedRaiseFloatingTimeout() {
  if (m_activeFloatingWindow) {
    raiseFloatingWindows(m_activeFloatingWindow.data());
  }
}

void SubWindowFloatController::attachSubWindow(DrawSubWindow *subWindow) {
  if (!subWindow || !m_mdiArea)
    return;

  if (!isDetached(subWindow))
    return;

  if (m_activeFloatingWindow.data() == subWindow) {
    clearActiveFloatingWindow();
  }

  m_isActivating = true;

  // 1. 관리 목록에서 제거 및 파괴 시그널 연결 해제 (공간 클릭 감지를 위해 이벤트 필터는 유지)
  // C++11 detach 경고 방지 역순 인덱스 루프로 확실하게 제거
  for (int i = m_detachedList.size() - 1; i >= 0; --i) {
    if (!m_detachedList.at(i) || m_detachedList.at(i).data() == subWindow) {
      m_detachedList.removeAt(i);
    }
  }
  disconnect(subWindow, &QObject::destroyed, this,
             &SubWindowFloatController::onDetachedWindowDestroyed);

  // 2. 다른 고정창을 억지로 hide()하지 않고, 도킹된 창을 최상단으로 raise하여 자연스럽게 전면 표시
  // (hide() 호출 시 다른 서브윈도우의 OpenGL 컨텍스트 및 레이아웃이 파괴되는 문제 방지)

  // 3. 타이틀바 데코레이션 잔재 제거를 위해 hide 및 내부 속성 설정
  subWindow->setProperty("isInternalChanging", true);
  subWindow->setProperty("isDockingAttached", true);
  subWindow->hide();
  subWindow->setAttribute(Qt::WA_StyledBackground, true);
  subWindow->setObjectName("DrawSubWindow");

  // 4. MDI 영역에 다시 서브윈도우로 추가 (플래그 지정으로 단일 설정)
  m_mdiArea->addSubWindow(subWindow, Qt::SubWindow | Qt::FramelessWindowHint);

  // 5. MDI 뷰포트 좌표(0, 0) 및 크기에 정확히 맞춘 후 최대화
  QRect mdiRect = m_mdiArea->viewport()->rect();
  if (mdiRect.isValid()) {
    subWindow->setGeometry(mdiRect);
  }

  // 6. 도킹된 창을 최상단으로 올리고 활성화
  if (m_mdiArea->window()) {
    m_mdiArea->window()->raise();
    m_mdiArea->window()->activateWindow();
  }

  m_mdiArea->setActiveSubWindow(subWindow);
  subWindow->show();
  subWindow->showMaximized();
  if (mdiRect.isValid()) {
    subWindow->setGeometry(mdiRect);
  }
  subWindow->raise();
  subWindow->activateWindow();

  if (subWindow->widget()) {
    subWindow->widget()->show();
  }

  if (m_appContext) {
    m_appContext->setLastActiveWindow(subWindow);
  }

  if (subWindow->view()) {
    subWindow->view()->show();
    subWindow->view()->recreateOpenGLViewport();
    subWindow->view()->setFocus();
    subWindow->view()->setCustomCrossCursor();
    subWindow->view()->zoomAll();
    subWindow->view()->update();
  }

  if (subWindow->scene()) {
    subWindow->scene()->update();
  }

  // 공간 클릭 감지 필터 유지/재확인
  installSubWindowFilters(subWindow);

  m_isActivating = false;
  subWindow->setProperty("isInternalChanging", false);

  emit subWindowAttached(subWindow);
}

DrawSubWindow *SubWindowFloatController::findDrawSubWindow(QObject *watched) const {
  if (!watched)
    return nullptr;

  DrawSubWindow *target = qobject_cast<DrawSubWindow *>(watched);
  if (target)
    return target;

  QWidget *w = qobject_cast<QWidget *>(watched);
  while (w) {
    if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(w)) {
      return dw;
    }
    w = w->parentWidget();
  }
  return nullptr;
}

void SubWindowFloatController::installSubWindowFilters(DrawSubWindow *subWindow) {
  if (!subWindow)
    return;

  subWindow->installEventFilter(this);
  if (subWindow->widget()) {
    subWindow->widget()->installEventFilter(this);
  }
  if (subWindow->view()) {
    subWindow->view()->installEventFilter(this);
    if (subWindow->view()->viewport()) {
      subWindow->view()->viewport()->installEventFilter(this);
    }
  }
}

void SubWindowFloatController::removeSubWindowFilters(DrawSubWindow *subWindow) {
  if (!subWindow)
    return;

  subWindow->removeEventFilter(this);
  if (subWindow->widget()) {
    subWindow->widget()->removeEventFilter(this);
  }
  if (subWindow->view()) {
    subWindow->view()->removeEventFilter(this);
    if (subWindow->view()->viewport()) {
      subWindow->view()->viewport()->removeEventFilter(this);
    }
  }
}

void SubWindowFloatController::registerSubWindow(DrawSubWindow *subWindow) {
  if (!subWindow)
    return;

  // C++11 detach 경고 방지 인덱스 루프
  for (int i = 0; i < m_registeredList.size(); ++i) {
    if (m_registeredList.at(i).data() == subWindow) {
      installSubWindowFilters(subWindow);
      return;
    }
  }

  installSubWindowFilters(subWindow);
  connect(subWindow, &QObject::destroyed, this,
          &SubWindowFloatController::onRegisteredWindowDestroyed);
  m_registeredList.append(subWindow);
}

void SubWindowFloatController::unregisterSubWindow(DrawSubWindow *subWindow) {
  if (!subWindow)
    return;

  removeSubWindowFilters(subWindow);
  disconnect(subWindow, &QObject::destroyed, this,
             &SubWindowFloatController::onRegisteredWindowDestroyed);

  // C++11 detach 경고 방지 역순 인덱스 루프
  for (int i = m_registeredList.size() - 1; i >= 0; --i) {
    if (!m_registeredList.at(i) || m_registeredList.at(i).data() == subWindow) {
      m_registeredList.removeAt(i);
    }
  }
}

void SubWindowFloatController::onRegisteredWindowDestroyed(QObject *obj) {
  for (int i = m_registeredList.size() - 1; i >= 0; --i) {
    if (!m_registeredList.at(i) || m_registeredList.at(i).data() == obj) {
      m_registeredList.removeAt(i);
    }
  }
}

void SubWindowFloatController::handleSubWindowActivated(DrawSubWindow *subWindow) {
  if (!subWindow || m_isActivating)
    return;

  bool detached = isDetached(subWindow);

  if (detached) {
    bool alreadyActive = (m_activeFloatingWindow.data() == subWindow &&
                          m_appContext && m_appContext->activeDrawWindow() == subWindow);
    if (!alreadyActive) {
      setActiveFloatingWindow(subWindow);
      if (m_appContext) {
        m_appContext->setLastActiveWindow(subWindow);
      }
      if (subWindow->scene() && subWindow->tool()) {
        subWindow->tool()->activate(subWindow->scene());
        subWindow->scene()->setTool(subWindow->tool());
      }
      raiseFloatingWindows(subWindow);
      emit subWindowActivated(subWindow);
    }
  } else {
    bool alreadyActive = (m_activeFloatingWindow.isNull() &&
                          m_mdiArea && m_mdiArea->activeSubWindow() == subWindow &&
                          m_appContext && m_appContext->activeDrawWindow() == subWindow);
    if (!alreadyActive) {
      m_isActivating = true;
      clearActiveFloatingWindow();
      if (m_appContext) {
        m_appContext->setLastActiveWindow(subWindow);
      }
      if (m_mdiArea && m_mdiArea->window()) {
        m_mdiArea->window()->raise();
        m_mdiArea->window()->activateWindow();
      }

      if (m_mdiArea && m_mdiArea->activeSubWindow() != subWindow) {
        m_mdiArea->setActiveSubWindow(subWindow);
      }

      subWindow->showMaximized();
      subWindow->raise();
      subWindow->activateWindow();

      if (subWindow->widget()) {
        subWindow->widget()->show();
      }

      if (subWindow->scene() && subWindow->tool()) {
        subWindow->tool()->activate(subWindow->scene());
        subWindow->scene()->setTool(subWindow->tool());
      }
      if (subWindow->view()) {
        subWindow->view()->show();
        subWindow->view()->setFocus();
        subWindow->view()->setCustomCrossCursor();
        if (subWindow->view()->viewport()) {
          subWindow->view()->viewport()->update();
        }
      }
      emit subWindowActivated(subWindow);
      m_isActivating = false;
    }
  }
}

bool SubWindowFloatController::eventFilter(QObject *watched, QEvent *event) {
  if (m_isActivating) {
    return QObject::eventFilter(watched, event);
  }

  // 사용자가 마우스로 창 공간을 직접 클릭했을 때 활성화 및 최상단 배치
  if (event->type() == QEvent::MouseButtonPress) {
    DrawSubWindow *target = findDrawSubWindow(watched);
    if (target) {
      handleSubWindowActivated(target);
    }
  } else if (event->type() == QEvent::WindowActivate) {
    // OS 창 전환(Alt+Tab 또는 시스템 타이틀바 클릭 등)으로 창이 이미 활성화된 경우
    // 다시 activateWindow()/raise()를 호출하면 무한 루프가 발생하므로, 내부 상태만 동기화
    DrawSubWindow *target = findDrawSubWindow(watched);
    if (target && isDetached(target)) {
      if (m_activeFloatingWindow.data() != target ||
          (m_appContext && m_appContext->activeDrawWindow() != target)) {
        m_isActivating = true;
        setActiveFloatingWindow(target);
        if (m_appContext) {
          m_appContext->setLastActiveWindow(target);
        }
        if (target->scene() && target->tool()) {
          target->tool()->activate(target->scene());
          target->scene()->setTool(target->tool());
        }
        emit subWindowActivated(target);
        m_isActivating = false;
      }
    }
  }

  return QObject::eventFilter(watched, event);
}

void SubWindowFloatController::toggleFloat(DrawSubWindow *subWindow) {
  if (!subWindow)
    return;

  if (isDetached(subWindow)) {
    attachSubWindow(subWindow);
  } else {
    detachSubWindow(subWindow);
  }
}

bool SubWindowFloatController::isDetached(DrawSubWindow *subWindow) const {
  if (!subWindow)
    return false;

  // 1. 관리 목록에 등록되어 있으면 확실한 분리창
  for (int i = 0; i < m_detachedList.size(); ++i) {
    if (m_detachedList.at(i).data() == subWindow) {
      return true;
    }
  }

  // 2. 부모가 아예 없으면 분리창
  if (!subWindow->parentWidget()) {
    return true;
  }

  // 3. 그 외 부모가 존재하면 MDI 도킹창
  return false;
}

QList<DrawSubWindow *> SubWindowFloatController::detachedWindows() const {
  QList<DrawSubWindow *> result;
  // C++11 detach 경고 방지를 위해 인덱스 기반 루프 사용
  for (int i = 0; i < m_detachedList.size(); ++i) {
    if (m_detachedList.at(i)) {
      result.append(m_detachedList.at(i).data());
    }
  }
  return result;
}

void SubWindowFloatController::onDetachedWindowDestroyed(QObject *obj) {
  if (m_activeFloatingWindow.data() == obj) {
    clearActiveFloatingWindow();
  }

  // C++11 detach 경고 방지를 위해 역순 인덱스 기반 루프 사용
  for (int i = m_detachedList.size() - 1; i >= 0; --i) {
    if (!m_detachedList.at(i) || m_detachedList.at(i).data() == obj) {
      m_detachedList.removeAt(i);
    }
  }
}

DrawSubWindow *SubWindowFloatController::activeFloatingWindow() const {
  return m_activeFloatingWindow.data();
}

void SubWindowFloatController::setActiveFloatingWindow(DrawSubWindow *w) {
  if (m_activeFloatingWindow.data() != w) {
    m_activeFloatingWindow = w;
  }
  if (w && m_appContext) {
    m_appContext->setLastActiveWindow(w);
  }
}

void SubWindowFloatController::clearActiveFloatingWindow() {
  m_activeFloatingWindow.clear();
}
