#ifndef SELECTTOOL_H
#define SELECTTOOL_H

#include "abstracttool.h"
#include <QElapsedTimer>
#include <QList>
#include <QPoint>

class GripItem;
class QGraphicsItem;
class CadItem; // [핵심] 전방 선언

class SelectTool : public AbstractTool {
  Q_OBJECT
public:
  explicit SelectTool(QObject *parent = nullptr);
  ~SelectTool();

  // 툴 활성화/비활성화 (상태 초기화 보강)
  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  // 마우스 이벤트
  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseDoubleClick(QPointF pos, Qt::MouseButton button) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;

  // 키보드 이벤트 (취소, 삭제)
  void keyPress(QKeyEvent *event) override;

private:
  // =========================================================
  // 상태 변수
  // =========================================================

  // [1] 그립 편집 모드
  GripItem *m_activeGrip;
  QPointF m_lastPos;

  // [2] 박스 선택 모드
  bool m_boxSelectPending;
  QPointF m_firstCorner;

  // [성능] 박스 선택 드래그 중 과도한 재그리기를 억제하기 위한 상태
  bool m_fastDragRender = false;    // 드래그 중 저품질(안티앨리어싱 off) 렌더링 여부
  bool m_hasLastBoxViewPos = false; // 스로틀용 직전 뷰포트 좌표 유효 여부
  QPoint m_lastBoxViewPos;          // 직전에 선택 사각형을 갱신한 뷰포트 좌표

  // =========================================================
  // 내부 헬퍼 함수
  // =========================================================

  // 선택 토글
  void toggleSelection(QGraphicsItem *item);

  // 박스 선택 확정
  void extracted(QList<QGraphicsItem *> &items);
  void finishBoxSelection(const QPointF &secondCorner);

  // 커서 중앙 픽박스 범위 내에서 가장 가까운 그립을 반환 (없으면 nullptr)
  GripItem *findGripAt(const QPointF &pos) const;

  // 오버레이 정리
  void clearOverlay();

  // [성능] 박스 선택 드래그 동안 뷰의 안티앨리어싱을 끄고, 끝나면 복원한다.
  void beginFastDragRender();
  void endFastDragRender();
  // 박스 선택 상태(그립/사각형/저품질 렌더링)를 한 번에 초기화한다.
  void cancelBoxSelection();
};

#endif // SELECTTOOL_H
