#ifndef PRINTWINDOWTOOL_H
#define PRINTWINDOWTOOL_H

#include "abstracttool.h"
#include <QPointF>
#include <QRectF>

// "출력 윈도우 지정" 인터랙티브 도구 (ZoomWindowTool과 조작 방식은 동일).
// 사용자가 도면 위에서 사각형의 두 꼭짓점을 클릭하면 windowRectSelected 시그널로
// 결과 사각형(씬 좌표)을 알린다. 화면을 확대하는 대신 "이 영역만 인쇄"하는 데 쓰인다.
class PrintWindowTool : public AbstractTool {
  Q_OBJECT
public:
  explicit PrintWindowTool(IAppContext *ctx, QObject *parent = nullptr);

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

signals:
  void windowRectSelected(const QRectF &rect); // 사각형 지정 완료
  void cancelled();                            // ESC 등으로 취소됨 (AbstractTool::onCancel 경유)

protected:
  void onCancel() override;

private:
  enum State { State_WaitingForFirst, State_WaitingForSecond };
  State m_currentState = State_WaitingForFirst;
  QPointF m_firstCorner;
  bool m_isSelecting = false;

  void updatePrompt();
  void clearOverlay();
};

#endif // PRINTWINDOWTOOL_H
