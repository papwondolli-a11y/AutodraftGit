#ifndef ABSTRACTTOOL_H
#define ABSTRACTTOOL_H

#include "iappcontext.h"
#include "CadPicker.h"

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <Qt> // Qt::MouseButton, Qt::Key 등을 사용하기 위함

#include <QList>

// 전방 선언 (헤더 의존성 최소화)
class MyGraphicsScene;
class QKeyEvent;
class QGraphicsItem;
class MyDimension;
// class AppContext;

class AbstractTool : public QObject {
  Q_OBJECT

public:
  AbstractTool(QObject *parent = nullptr);
  virtual ~AbstractTool();

  // 도구가 활성화될 때 호출 (Scene 연결)
  virtual void activate(MyGraphicsScene *scene);

  // 도구가 비활성화될 때 호출 (정리 작업)
  virtual void deactivate();
  // 공유 도구가 이 씬에 바인딩되어 있을 때만 해제 (씬 소멸 전 dangling 방지)
  void detachFromScene(MyGraphicsScene *scene);

  // [신규] 선택된 객체의 그립을 숨기는 헬퍼 (공통 사용)
  void hideGripsOfSelectedItems();

  // [신규] 선택된 아이템 목록에서 최상위 부모(BlockItem 등)만 추려내는 헬퍼
  QList<class CadItem *>
  getTopLevelCadItems(const QList<class QGraphicsItem *> &items) const;

  // [성능] 씬 사각형에 걸치는 후보를 "유일한 최상위 CadItem" 목록으로 즉시
  // 축약한다. m_scene->items()는 블록의 자식 수천 개까지 전부 돌려주는데,
  // 그 각각에 대해 geometryPath()/shape() 기하 검사를 하면 확정 순간이
  // 도면 개수에 비례해 급격히 느려진다. 기하 검사 전에 먼저 블록 단위로 접는다.
  QList<class CadItem *> topLevelCandidatesInRect(const QRectF &sceneRect) const;

  // =========================================================
  // 입력 이벤트 인터페이스 (자식 클래스에서 필요한 것만 재정의)
  // =========================================================

  // 마우스 버튼을 눌렀을 때 (좌클릭/우클릭 구분)
  virtual void mousePress(QPointF pos, Qt::MouseButton button);

  // 마우스를 움직일 때
  virtual void mouseMove(QPointF pos);

  // 마우스 버튼을 뗐을 때
  virtual void mouseRelease(QPointF pos, Qt::MouseButton button);

  // 마우스 버튼을 더블 클릭했을 때
  virtual void mouseDoubleClick(QPointF pos, Qt::MouseButton button);

  // 키보드 키를 눌렀을 때 (ESC 취소, Delete 삭제 등)
  virtual void keyPress(QKeyEvent *event);

  // [신규 추가] IAppContext를 명시적으로 주입받는 함수
  void setContext(IAppContext *ctx);

  virtual bool processCommand(const QString &command);

  void blockEnterKeyTemporarily(int msec = 200);

  // [신규추가] InputManager 등에서 Enter 키 차단 상태를 확인할 수 있도록 함
  bool isEnterKeyBlocked() const { return m_blockEnterKey; }

  // [신규] 고스트 미리보기 업데이트 (레이어 스타일 반영)
  void updateGhostPreview(const QPainterPath &path,
                          const QString &layerName = "");
  void updateGhostLinePreview(const QLineF &line,
                              const QString &layerName = "");

  // [신규] 활성 문서의 현재 레이어 이름을 가져오는 헬퍼
  QString currentLayerName() const;
  QPen currentLayerPen() const;
  QPen
  getLayerPen(const QString &layerName) const; // [신규] 특정 레이어의 펜 제공
  // [신규] static 버전: ctx만으로 레이어 펜(색·두께·선종류) 생성.
  //  레이어가 없거나 이름이 비면 현재 레이어 → 흰색 순으로 대체 (static buildGeometry 등에서 사용)
  static QPen layerPenFor(IAppContext *ctx, const QString &layerName);
  // [신규] 레이어 이름 검증: 도면에 존재하면 그대로, 비었거나 없으면 현재 레이어 이름 반환
  static QString resolveLayerName(IAppContext *ctx, const QString &layerName);

  // [신규] 선택된 아이템들의 공통 레이어 이름을 반환하는 헬퍼
  QString getCommonLayerName(const QList<QGraphicsItem *> &items) const;

  // [신규] 상세도 영역 자동 적용:
  //  치수의 정의점(start/end, 각도는 end2까지)이 모두 같은 상세도 경계원 안에 있으면
  //  dim.measureScale = 1 / (해당 영역 축척) 로 설정해 확대 배율을 상쇄한다.
  //  (DetailViewTool이 배치한 경계원에 DETAIL_REGION_SCALE 데이터가 태깅돼 있어야 함)
  //  반환값: 영역을 찾아 적용했으면 true.
  bool applyDetailRegionScale(MyDimension &dim) const;

  // 특정 레이어의 가시성(On/Off)을 모든 씬에 업데이트하는 함수

signals:
  // [추가] 도구가 작업을 마쳤음을 알리는 시그널
  void toolFinished();

private slots:
  void unblockEnterKey();

protected:
  // 자식 도구들이 그림을 그릴 대상 Scene
  QPointer<MyGraphicsScene> m_scene;

  // [신규 추가] IAppContext 포인터 저장소
  IAppContext *m_appContext;

  IAppContext *getContext() const;

  // [신규] 픽박스(Pickbox) 기반 점 클릭 선택 헬퍼
  // 커서 중앙 네모박스 범위(약 5px 반경) 내에서 가장 가까운 객체를 반환
  class QGraphicsItem *itemAtPos(const QPointF &pos);

  /**
   * @brief Window 선택 (L->R): SelectTool과 동일한 기든으로 CadItem을 수집합니다.
   *  - geometryPath BoundingRect 기준 완전 포함 판정
   *  - 수직/수평 선분 두께 보정 (±0.5)
   *  - getTopLevelCadItems 자동 적용
   */
  QList<CadItem *> collectItemsInWindowRect(const QPointF &p1,
                                            const QPointF &p2) const;

  /**
   * @brief Crossing 선택 (R->L): 선택 영역에 교차하는 CadItem을 수집합니다.
   *  - getTopLevelCadItems 자동 적용
   */
  QList<CadItem *> collectItemsInCrossingRect(const QPointF &p1,
                                              const QPointF &p2) const;

  virtual void onFinish() {}
  virtual void onCancel() {}

  bool m_blockEnterKey; // 엔터 차단 플래그 (공통 변수)

private:
  // [신규] scene 포인터를 주입받은 CadPicker 인스턴스
  CadPicker m_picker;
};

#endif // ABSTRACTTOOL_H
