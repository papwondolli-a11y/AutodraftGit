#include "zoomwindowtool.h"
#include "appcontext.h"
#include "commanddock.h"
#include "drawsubwindow.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"

ZoomWindowTool::ZoomWindowTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent) {
  setContext(ctx);
}

void ZoomWindowTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (button != Qt::LeftButton)
    return;

  if (m_currentState == State_WaitingForFirst) {
    m_firstCorner = pos;
    m_isSelecting = true;
    m_currentState = State_WaitingForSecond;
    updatePrompt();
  } else {
    // Second click
    QRectF rect = QRectF(m_firstCorner, pos).normalized();
    if (m_appContext && m_appContext->activeDrawWindow() &&
        m_appContext->activeDrawWindow()->view()) {
      m_appContext->activeDrawWindow()->view()->zoomWindow(rect);
    }
    m_currentState = State_WaitingForFirst;
    m_isSelecting = false;
    clearOverlay();
    updatePrompt();
    // Finish tool after one zoom
    if (m_appContext && m_appContext->activeDrawWindow()) {
      m_appContext->activeDrawWindow()->useDefaultTool();
    }
  }
}

void ZoomWindowTool::mouseMove(QPointF pos) {
  if (m_isSelecting && m_scene) {
    MyGraphicsScene::SelectionMode mode;
    if (pos.x() > m_firstCorner.x())
      mode = MyGraphicsScene::Mode_Window;
    else
      mode = MyGraphicsScene::Mode_Crossing;

    m_scene->setSelectionRect(m_firstCorner, pos, mode);
  }
}

void ZoomWindowTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
  Q_UNUSED(pos);
  Q_UNUSED(button);
}

void ZoomWindowTool::deactivate() {
  clearOverlay();
  AbstractTool::deactivate();
}

void ZoomWindowTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_currentState = State_WaitingForFirst;
  m_isSelecting = false;
  updatePrompt();
}

void ZoomWindowTool::updatePrompt() {
  if (!m_appContext || !m_appContext->commandDock())
    return;

  if (m_currentState == State_WaitingForFirst) {
    m_appContext->commandDock()->setPrompt(
        tr("Zoom Window: 첫 번째 구석 지정:"));
  } else {
    m_appContext->commandDock()->setPrompt(tr("반대쪽 구석 지정:"));
  }
}

void ZoomWindowTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}
