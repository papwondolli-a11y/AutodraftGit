#include "inputmanager.h"
#include "abstracttool.h"
#include "appcontext.h"
#include "commanddock.h"
#include "drawsubwindow.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "subwindowfloatcontroller.h"

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsTextItem>
#include <QKeyEvent>

InputManager::InputManager(AppContext *context, QObject *parent)
    : QObject(parent), m_ctx(context) {}

bool InputManager::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::KeyPress) {
    return handleKeyPress(watched, static_cast<QKeyEvent *>(event));
  }
  return false;
}
bool InputManager::handleKeyPress(QObject *watched, QKeyEvent *keyEvent) {
  if (!m_ctx || m_isHandling)
    return false;

  m_isHandling = true;
  struct ReentrancyGuard {
    bool &flag;
    explicit ReentrancyGuard(bool &f) : flag(f) {}
    ~ReentrancyGuard() { flag = false; }
  } guard(m_isHandling);

  // [핵심] 키 이벤트가 발생한 위젯 계층을 추적하여 분리 창(Floating) / 부착 창 활성 상태 즉시 동기화
  if (watched) {
    QWidget *w = qobject_cast<QWidget *>(watched);
    while (w) {
      if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(w)) {
        if (m_ctx->floatController()) {
          if (m_ctx->floatController()->isDetached(dw) || dw->parentWidget() == nullptr) {
            m_ctx->floatController()->setActiveFloatingWindow(dw);
          } else {
            m_ctx->floatController()->clearActiveFloatingWindow();
          }
        }
        m_ctx->setLastActiveWindow(dw);
        break;
      }
      w = w->parentWidget();
    }
  }

  // 1. 도면에서 텍스트 입력 중인지 확인
  if (m_ctx->activeScene()) {
    QGraphicsItem *focusItem = m_ctx->activeScene()->focusItem();
    if (focusItem && (focusItem->type() == QGraphicsTextItem::Type ||
                      (focusItem->toGraphicsObject() &&
                       focusItem->toGraphicsObject()->property("noRedirect").toBool()))) {
      return false; // 텍스트 아이템이 처리하도록 통과 (ESC 등 포함)
    }
  }

  // [신규] 현재 이벤트를 받은 객체(watched)가 입력 컨트롤인 경우 리다이렉션
  // 방지
  if (watched) {
    // 모달 다이얼로그(QMessageBox, QFileDialog 등)가 활성화된 경우
    // 이벤트를 가로채지 않음 — 다이얼로그가 Enter를 직접 처리해야 함
    if (QApplication::activeModalWidget() != nullptr) {
      return false;
    }

    if (watched->inherits("QLineEdit") || watched->inherits("QTextEdit") ||
        watched->inherits("QPlainTextEdit") ||
        watched->inherits("QAbstractSpinBox") ||
        watched->property("noRedirect").toBool()) {
      return false;
    }
    // 부모가 noRedirect인 경우도 체크 (QComboBox의 lineEdit 등)
    if (watched->parent() &&
        watched->parent()->property("noRedirect").toBool()) {
      return false;
    }
  }

  // 0. ESC 키 전역 처리 (선택 해제 및 커맨드라인 초기화)
  if (keyEvent->key() == Qt::Key_Escape) {
    // 1. 선택 해제
    if (m_ctx->activeScene()) {
      m_ctx->activeScene()->clearSelection();
    }
    // 2. 커맨드라인 초기화
    CommandDock *cmdDock = m_ctx->commandDock();
    if (cmdDock) {
      if (cmdDock->inputWidget()) {
        cmdDock->inputWidget()->clear();
      }
      cmdDock->setPrompt(tr(" Command : "));
      cmdDock->setToolActive(false);
    }
    // Scene Overlay 정리 등을 위해 현재 툴에게도 ESC 전달
    DrawSubWindow *dw = m_ctx->activeDrawWindow();
    if (dw && dw->tool()) {
      dw->tool()->keyPress(keyEvent);
    }
    return true;
  }

  CommandDock *cmdDock = m_ctx->commandDock();

  // 2. 방향키 처리 (커맨드 히스토리 등)
  if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) {
    if (cmdDock && cmdDock->inputWidget()->hasFocus())
      return false;
    if (cmdDock) {
      cmdDock->injectKeyPress(keyEvent);
      return true;
    }
  }

  // 3. 기능키 (F3, F8, Alt)
  bool isFuncKey =
      (keyEvent->key() == Qt::Key_F3 || keyEvent->key() == Qt::Key_F8);
  bool isAltKey = (keyEvent->modifiers() & Qt::AltModifier);
  if (isFuncKey || isAltKey) {
    DrawSubWindow *dw = m_ctx->activeDrawWindow();
    if (dw && dw->tool()) {
      dw->tool()->keyPress(keyEvent);
      return true;
    }
  }

  // 4. 스페이스바 -> 엔터 변환
  if (keyEvent->key() == Qt::Key_Space &&
      keyEvent->modifiers() == Qt::NoModifier) {
    QWidget *focusWidget = QApplication::focusWidget();
    bool isCommandInput = (cmdDock && (focusWidget == cmdDock->inputWidget() || watched == cmdDock->inputWidget()));

    bool isSceneTextEditing = false;
    if (m_ctx->activeScene()) {
      QGraphicsItem *sceneFocusItem = m_ctx->activeScene()->focusItem();
      if (sceneFocusItem && sceneFocusItem->type() == QGraphicsTextItem::Type) {
        isSceneTextEditing = true;
      }
    }

    if (isCommandInput) {
      return false; // CommandInputLine 내부에서 Space->Enter 변환을 직접 처리
    }

    if (focusWidget &&
        (focusWidget->inherits("QLineEdit") ||
         focusWidget->inherits("QTextEdit") || isSceneTextEditing)) {
      return false;
    }

    // 도면 뷰 등 외부에 포커스가 있을 때, 커맨드 입력창에 텍스트가 있으면 Enter 주입
    if (cmdDock && (!cmdDock->inputWidget()->text().isEmpty() ||
                    static_cast<CommandInputLine *>(cmdDock->inputWidget())->isToolActive())) {
      QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      cmdDock->injectKeyPress(&enterEvent);
      return true;
    }

    // 그 외(대기 상태)에는 Enter와 동일하게 처리 → 직전 명령 반복
    // (sendEvent로 재전달하면 재진입 가드에 막혀 Enter 로직을 타지 못함)
    QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    if (handleEnterKey(watched, &enterEvent)) {
      return true;
    }
    if (focusWidget) {
      QApplication::sendEvent(focusWidget, &enterEvent);
    }
    return true;
  }

  // 5. 엔터키 처리
  if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
    if (handleEnterKey(watched, keyEvent)) {
      return true;
    }
    return false;
  }

  // 6. 자동 포커스 및 인쇄 문자
  if (cmdDock && cmdDock->inputWidget()->hasFocus())
    return false;

  QWidget *focusWidget = QApplication::focusWidget();
  if (focusWidget) {
    // QLineEdit, QTextEdit, QPlainTextEdit 또는 QComboBox의 자식(에디터)인 경우
    // 리다이렉션 방지
    if (focusWidget->inherits("QLineEdit") ||
        focusWidget->inherits("QTextEdit") ||
        focusWidget->inherits("QPlainTextEdit") ||
        focusWidget->inherits("QAbstractSpinBox") ||
        focusWidget->inherits(
            "QComboBox") || // QComboBox 자체 포커스 시에도 방지
        focusWidget->property("noRedirect").toBool() ||
        (focusWidget->parentWidget() &&
         (focusWidget->parentWidget()->inherits("QComboBox") ||
          focusWidget->parentWidget()->property("noRedirect").toBool()))) {
      return false;
    }
  }
  if (keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
    return false;
  if (keyEvent->key() >= Qt::Key_F1 && keyEvent->key() <= Qt::Key_F12)
    return false;

  QString text = keyEvent->text();
  if (!text.isEmpty() && text.at(0).isPrint()) {
    if (cmdDock) {
      if (!cmdDock->inputWidget()->hasFocus()) {
        cmdDock->inputWidget()->setFocus();
      }
      cmdDock->injectKeyPress(keyEvent);
      return true;
    }
  }

  return false;
}

// Enter 키 처리 (Space 키도 대기 상태에서 이 경로를 공유)
// 반환값: true = 이벤트 소비됨, false = 기본 처리로 넘김
bool InputManager::handleEnterKey(QObject *watched, QKeyEvent *keyEvent) {
  CommandDock *cmdDock = m_ctx->commandDock();
  {
    if (cmdDock && (cmdDock->inputWidget()->hasFocus() || watched == cmdDock->inputWidget()))
      return false;
    if (cmdDock && (!cmdDock->inputWidget()->text().isEmpty() || static_cast<CommandInputLine*>(cmdDock->inputWidget())->isToolActive())) {
      cmdDock->injectKeyPress(keyEvent);
      return true;
    }

    DrawSubWindow *dw = m_ctx->activeDrawWindow();
    if (dw) {
      bool isCommandRunning =
          (dw->tool() &&
           QString(dw->tool()->metaObject()->className()) != "SelectTool");
      if (isCommandRunning) {
        if (dw->tool()->isEnterKeyBlocked()) {
          return true; // 새로 시작된 도구의 Enter 키 차단 보호
        }
        dw->tool()->keyPress(keyEvent);
      } else {
        // [수정] 기본 툴(SelectTool)이 활성화된 직후에 눌려진 Enter 키가
        // 뒤늦게 처리되어 이전 명령을 반복 실행하는 문제를 방지합니다.
        if (dw->tool() && dw->tool()->isEnterKeyBlocked()) {
          return true;
        }

        // 뷰(View)가 실제로 포커스를 가지고 있을 때만 마지막 명령 반복
        // 다이얼로그 확인 버튼 클릭 등이 명령 반복으로 이어지는 것을 방지
        if (dw->view() && dw->view()->hasFocus()) {
          emit m_ctx->commandDock()->commandSignal("");
        }
      }
      return true;
    }
  }
  return false;
}
