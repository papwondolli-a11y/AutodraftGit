#ifndef CIRCLETOOL_H
#define CIRCLETOOL_H

#include "abstracttool.h"
#include <QGraphicsEllipseItem>
#include <QPointF>

class CircleTool : public AbstractTool {
  Q_OBJECT
public:
  explicit CircleTool(QObject *parent = nullptr);
  ~CircleTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  bool processCommand(const QString &command) override;
  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;

private:
  void finishCircle(qreal overrideRadius = -1.0);
  void cancelDrawing();
  void clearOverlay(); // [신규] 오버레이 초기화 헬퍼

  // 상태 변수
  bool m_isDrawing;
  bool m_isDiameterMode; // [신규] 지름 모드여부
  QPointF m_centerPoint;
  QPointF m_currentPos;
};

#endif // CIRCLETOOL_H
