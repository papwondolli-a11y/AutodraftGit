#ifndef STRETCHTOOL_H
#define STRETCHTOOL_H

#include "abstracttool.h"
#include <QList>
#include <QMap>
#include <QPointF>


class CadItem;

class StretchTool : public AbstractTool {
  Q_OBJECT
public:
  explicit StretchTool(IAppContext *ctx, QObject *parent = nullptr);
  ~StretchTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void keyPress(QKeyEvent *event) override;

  bool processCommand(const QString &command) override;

protected:
  void onFinish() override;
  void onCancel() override;

private:
  enum State {
    State_Selecting,       // 신축할 객체 및 영역 선택
    State_WaitingForBase,  // 기준점 대기
    State_WaitingForSecond // 목적점 대기
  };

  State m_currentState;
  IAppContext *m_ctx;

  // 선택 결과 저장: 어떤 아이템의 어떤 정점들이 선택되었는가?
  QMap<CadItem *, QList<int>> m_stretchMap;

  QPointF m_firstCorner;
  bool m_isBoxSelecting;
  QPointF m_basePoint;
  QPointF m_currentMousePos;

  void handleSelectionPress(QPointF pos, Qt::MouseButton button);
  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p1, QPointF p2);
  void finishSelectionStep();
  void updatePrompt();
  void executeStretch(const QPointF &targetPoint);
  void clearOverlay();
};

#endif // STRETCHTOOL_H
