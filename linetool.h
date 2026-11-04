#ifndef LINETOOL_H
#define LINETOOL_H

#include "abstracttool.h"
#include <QList>
#include <QPointF>

class MyGraphicsScene;
class QGraphicsItem;

class LineTool : public AbstractTool {
  Q_OBJECT

public:
  explicit LineTool(QObject *parent = nullptr);
  ~LineTool() override;

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;

  // AbstractTool에서 상속받은 processCommand는 그대로 사용

  // [신규] 좌표 입력 처리 (AbstractTool 연동)
  bool processCommand(const QString &command) override;

signals:
  void lineCreated(); // [여기!] 구현 없이 선언만 합니다.

protected:
  void onFinish() override;
  void onCancel() override;

private:
  void finishLine();
  void cancelDrawing();
  bool undoLastSegment();
  void closeLoop();

private:
  bool m_isDrawing;
  QPointF m_startPoint; // 현재 선분의 시작점
  QPointF m_endPoint;   // 현재 선분의 끝점 (마우스 따라다님)
  QPointF m_firstPoint; // 최초 시작점 (Close용)
  QPointF m_currentMousePos;

  QList<QGraphicsItem *> m_addedItems; // Undo 관리를 위한 리스트
};

#endif // LINETOOL_H
