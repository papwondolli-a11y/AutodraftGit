#ifndef MOVETOOL_H
#define MOVETOOL_H

#include "abstracttool.h"
#include "myvector.h"

class MoveTool : public AbstractTool {
  Q_OBJECT

public:
  enum State {
    State_Idle,
    State_Selecting,       // 객체 선택 중
    State_WaitingForBase,  // 기준점 대기
    State_WaitingForSecond // 목적점 대기
  };

  explicit MoveTool(bool isCopyMode = false, QObject *parent = nullptr);
  //    explicit MoveTool(QObject* parent = nullptr);

  ~MoveTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  // mouseRelease는 클릭-클릭 방식에서 거의 사용 안 함
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;
  bool processCommand(const QString &command) override;

private:
  void updatePrompt();
  void clearOverlay();
  void executeMoveOrCopy(const MyVector &delta);

  // [신규] 선택된 아이템들의 그립을 강제로 숨기는 헬퍼
  void hideGripsOfSelectedItems();

  // 내부 로직
  void handleSelectionPress(QPointF pos, Qt::MouseButton button);
  void handleSelectionMove(QPointF pos);
  // handleSelectionRelease 삭제됨 (Press에서 처리)

  void finishSelectionStep();
  void processBoxSelection(QPointF p2); // 박스 선택 확정

  State m_currentState;
  bool m_isCopyMode;
  QPointF m_basePoint;

  // 박스 선택용
  bool m_isBoxSelecting; // true면 첫 번째 점을 찍은 상태
  QPointF m_firstCorner;
  QPointF m_currentMousePos;
};

#endif // MOVETOOL_H
