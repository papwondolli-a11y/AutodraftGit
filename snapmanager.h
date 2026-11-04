#ifndef SNAPMANAGER_H
#define SNAPMANAGER_H

#include <QList>
#include <QObject>
#include <QPointF>

// #include "caditem.h" // [필수] SnapType, SnapPoint 정의 사용
#include "snaptypes.h"

class QGraphicsScene;
class QGraphicsItem;

class SnapManager : public QObject {
  Q_OBJECT
public:
  // 스냅 계산 결과를 반환하기 위한 구조체
  struct SnapResult {
    QPointF snappedPos; // 보정된 최종 좌표
    SnapType type;      // 적용된 스냅 종류 (None, Endpoint, Grid...)
    bool isValid;       // 스냅 성공 여부
  };

  explicit SnapManager(QObject *parent = nullptr);

  // [신규] 모두 끄기 기능 ([None] 버튼용)
  void clearAllSnaps();
  /**
   * @brief 마우스 좌표를 입력받아 스냅된 좌표를 반환하는 핵심 함수
   * @param mousePos 현재 마우스 위치 (Scene 좌표)
   * @param scene 검색할 씬 포인터
   * @param basePoint (선택) Ortho 계산을 위한 기준점. 없는 경우 QPointF()
   * @param useOrtho (선택) 강제로 직교 모드를 적용할지 여부
   */
  SnapResult snapPoint(const QPointF &mousePos, QGraphicsScene *scene,
                       const QPointF &basePoint = QPointF(),
                       bool useOrtho = false);

  // =========================================================
  // 상태 조회 (Getter) - UI 업데이트용
  // =========================================================
  bool isOSnap() const { return m_osnapOn; }
  // AI(MCP) 호출처럼 정확한 좌표가 필요할 때 객체/그리드/직교 보정을 모두 건너뛴다
  void setSuspended(bool on) { m_suspended = on; }
  bool isSuspended() const { return m_suspended; }
  bool isOrtho() const { return m_orthoOn; }
  bool isOrthoActive() const; // [신규] Shift 키 등을 고려한 현재 실제 직교상태 반환
  bool isGridSnap() const { return m_gridSnapOn; }

  bool isSnapEndpoint() const { return m_snapEndpoint; }
  bool isSnapMidpoint() const { return m_snapMidpoint; }
  bool isSnapCenter() const { return m_snapCenter; }
  bool isSnapIntersection() const { return m_snapIntersection; }
  bool isSnapQuadrant() const { return m_snapQuadrant; }
  bool isSnapNearest() const { return m_snapNearest; }
  bool isSnapPerpendicular() const { return m_snapPerpendicular; }

  // 설정값 접근
  void setGridSize(double size) { m_gridSize = size; }
  void setSnapDistance(double dist) { m_snapDistance = dist; }

  // =========================================================
  // 설정 제어 (Toggle Functions)
  // =========================================================
  // 전체 모드 제어
public slots:

  void toggleOSnap();
  void toggleOrtho();

  // 개별 스냅 모드 제어
  void toggleSnapEndpoint();
  void toggleSnapMidpoint();
  void toggleSnapCenter();
  void toggleSnapIntersection();
  void toggleSnapQuadrant();
  void toggleSnapNearest();
  void toggleSnapPerpendicular();

signals:
  // [핵심] 상태가 변경되면 UI에 알리는 신호
  void osnapChanged(bool isOn);
  void orthoChanged(bool isOn);
  void snapEndpointChanged(bool isOn);
  void snapMidpointChanged(bool isOn);
  void snapCenterChanged(bool isOn);
  void snapIntersectionChanged(bool isOn);
  void snapQuadrantChanged(bool isOn);
  void snapNearestChanged(bool isOn);
  void snapPerpendicularChanged(bool isOn);

private:
  // 내부 로직 함수들
  QPointF applyGrid(const QPointF &pos);
  QPointF applyOrtho(const QPointF &pos, const QPointF &base);
  SnapResult findObjectSnap(const QPointF &pos, QGraphicsScene *scene,
                            const QPointF &basePoint = QPointF());
  bool isModeEnabled(SnapType type) const;
  int getSnapPriority(SnapType type) const;

  // ----------------------------------------
  // 멤버 변수 (설정값)
  // ----------------------------------------
  // 1. 메인 스위치
  bool m_suspended = false;
  bool m_osnapOn = true;     // [수정] 기본값 On
  bool m_orthoOn = false;    // F8 (직교 모드)
  bool m_gridSnapOn = false; // 그리드 스냅

  // 2. 세부 스냅 스위치 [수정] 주요 스냅 기본값 On
  bool m_snapEndpoint = true;
  bool m_snapMidpoint = false;
  bool m_snapCenter = true;
  bool m_snapIntersection = true;
  bool m_snapQuadrant = false;
  bool m_snapNearest = false;
  bool m_snapPerpendicular = false;

  // 3. 수치 설정
  double m_gridSize = 10.0;     // 그리드 간격
  double m_snapDistance = 15.0; // 자석 감지 거리 (픽셀 단위)
};

#endif // SNAPMANAGER_H
