#ifndef QUADMIRRORTOOL_H
#define QUADMIRRORTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointF>
#include <vector>

class CadItem;

// [신규] "4거울상" 도구.
// 객체를 선택한 뒤, 2x2 미리보기 다이얼로그(QuadMirrorDialog)로 원본/세로축
// 대칭/가로축 대칭/180도 회전 결과를 보여주고, 사용자가 고른 모양을
// 기준점 -> 목적점 방식(이동 도구와 동일한 방식)으로 배치한다.
// 이동 모드: 원본을 삭제하고 변환된 복제본만 남긴다.
// 복사 모드: 원본은 그대로 두고 변환된 복제본을 새 위치에 추가한다.
class QuadMirrorTool : public AbstractTool {
  Q_OBJECT
public:
  enum State {
    State_SelectingObjects,              // 객체 선택 대기
    State_SelectingObjects_SecondCorner, // 박스 선택 두 번째 점 대기
    State_PickingBasePoint,              // 변환된 복제본의 기준점 대기
    State_PickingDestinationPoint        // 기준점 기준 목적점(배치 위치) 대기
  };

  explicit QuadMirrorTool(IAppContext *ctx, QObject *parent = nullptr);
  virtual ~QuadMirrorTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void keyPress(QKeyEvent *event) override;

private:
  void updatePrompt();
  void clearOverlay();

  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p2);

  // 다이얼로그를 띄워 방향/배치 방식을 확정받고, 변환된 복제본(m_pendingClones)을
  // 미리 만들어 기준점 입력 대기 상태로 전환한다.
  // 다이얼로그가 취소되었거나 대상이 없으면 false를 반환한다.
  bool showDialogAndPrepareClones();

  // 기준점->목적점 이동량(delta)만큼 대기 중인 복제본들을 배치하고,
  // 이동 모드면 원본을 삭제한다.
  void finalizePlacement(const MyVector &delta);

  // 아직 씬에 추가되지 않은 대기 중인 복제본들을 정리(delete)한다.
  // (Esc 등으로 배치 전에 취소된 경우, 소유권이 이 도구에 있으므로 직접 해제)
  void discardPendingClones();

  State m_currentState;
  QPointF m_firstCorner;

  std::vector<CadItem *> m_selectedItems; // 변환 대상 원본 객체
  std::vector<CadItem *> m_pendingClones; // 변환은 끝났지만 아직 배치 전인 복제본
  bool m_isMoveMode;                      // true=이동(원본삭제) false=복사(원본유지)
  QPointF m_basePoint;                    // 사용자가 지정한 기준점
};

#endif // QUADMIRRORTOOL_H
