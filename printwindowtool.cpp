#include "printwindowtool.h"
#include "appcontext.h"
#include "commanddock.h"
#include "drawsubwindow.h"
#include "mygraphicsscene.h"

PrintWindowTool::PrintWindowTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent) {
  setContext(ctx);
}

void PrintWindowTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (button != Qt::LeftButton)
    return;

  if (m_currentState == State_WaitingForFirst) {
    m_firstCorner = pos;
    m_isSelecting = true;
    m_currentState = State_WaitingForSecond;
    updatePrompt();
  } else {
    // 두 번째 클릭: 사각형 확정
    QRectF rect = QRectF(m_firstCorner, pos).normalized();
    m_currentState = State_WaitingForFirst;
    m_isSelecting = false;
    clearOverlay();
    updatePrompt();

    emit windowRectSelected(rect);

    // Zoom Window와 동일하게, 한 번 지정하면 도구를 종료하고 기본(선택) 도구로 복귀
    if (m_appContext && m_appContext->activeDrawWindow()) {
      m_appContext->activeDrawWindow()->useDefaultTool();
    }
  }
}

void PrintWindowTool::mouseMove(QPointF pos) {
  if (m_isSelecting && m_scene) {
    m_scene->setSelectionRect(m_firstCorner, pos, MyGraphicsScene::Mode_Window);
  }
}

void PrintWindowTool::deactivate() {
  clearOverlay();
  AbstractTool::deactivate();
}

void PrintWindowTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_currentState = State_WaitingForFirst;
  m_isSelecting = false;
  updatePrompt();
}

void PrintWindowTool::onCancel() {
  clearOverlay();
  emit cancelled();
}

void PrintWindowTool::updatePrompt() {
  if (!m_appContext || !m_appContext->commandDock())
    return;

  if (m_currentState == State_WaitingForFirst) {
    m_appContext->commandDock()->setPrompt(tr("출력 윈도우: 첫 번째 구석 지정:"));
  } else {
    m_appContext->commandDock()->setPrompt(tr("반대쪽 구석 지정:"));
  }
}

void PrintWindowTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}
