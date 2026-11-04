#ifndef ARCCENTERTOOL_H
#define ARCCENTERTOOL_H

#include "abstracttool.h"
#include <QPointF>

class ArcCenterTool : public AbstractTool {
  Q_OBJECT
public:
  explicit ArcCenterTool(QObject *parent = nullptr);
  ~ArcCenterTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  bool processCommand(const QString &cmd) override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;

private:
  void finishArc();
  void cancelDrawing();
  void clearOverlay();

  // 입력 단계: 0(준비), 1(중심점 완료), 2(시작점 완료/호 그리는 중)
  int m_step;

  QPointF m_center;          // 1단계 입력값
  QPointF m_startPoint;      // 2단계 입력값
  QPointF m_endPoint;        // 3단계 입력값
  QPointF m_currentMousePos; // 실시간 마우스 위치 (방향 계산용)
};

#endif // ARCCENTERTOOL_H
