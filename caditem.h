#ifndef CADITEM_H
#define CADITEM_H

#include "myentity.h" // MyEntity 타입 사용
#include "myvector.h"
#include "snaptypes.h" //  여기에 정의된 SnapPoint(MyVector 버전)를 사용해야 함
#include <QGraphicsItem>
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QString>


class CadItem : public QGraphicsItem {
public:
  enum { Type = UserType + 1 };
  int type() const override { return Type; }

  CadItem(QGraphicsItem *parent = nullptr) : QGraphicsItem(parent) {
    setFlags(QGraphicsItem::ItemIsSelectable);
  }

  //  스냅 포인트 제공
  // 이제 SnapPoint는 snaptypes.h에 있는 'MyVector' 버전을 사용합니다.
  virtual QList<SnapPoint> getSnapPoints() const = 0;

  //  그립 편집
  virtual QList<QPointF> getGripPoints() const = 0;
  virtual void applyGripMove(int index,
                             const MyVector &pos) = 0; // MyVector 사용
  virtual QPainterPath getGhostShape(int index, const MyVector &pos) const = 0;

  //  선택을 위한 거리 계산
  virtual double distanceTo(const MyVector &pos) const = 0;
  
  // [신규] 그립 편집 시 직교(Ortho) 모드의 기준점을 제공하는 인터페이스
  virtual bool getOrthoBasePoint(int index, MyVector &outBasePoint) const {
      Q_UNUSED(index);
      Q_UNUSED(outBasePoint);
      return false;
  }

  //  Nearest / Intersection 스냅을 위한 필수 함수
  virtual SnapPoint getNearestSnap(const MyVector &pos) const = 0;
  virtual const MyEntity *
  getEntityData() const = 0; // 교차점 계산용 데이터 접근
  virtual void move(const MyVector &delta) = 0;
  virtual void mirror(const MyVector &p1, const MyVector &p2) = 0;
  // virtual CadItem* clone() const = 0;

  //  "delta만큼 이동했을 때의 외형 Path를 달라"는 순수 가상 함수
  virtual QPainterPath getMovedPath(const MyVector &delta) const = 0;

  //  대칭 변환된 외형 Path를 반환하는 순수 가상 함수
  virtual QPainterPath getMirroredPath(const MyVector &p1,
                                       const MyVector &p2) const = 0;

  //  회전 변환 및 회전된 외형 Path를 반환하는 순수 가상 함수
  virtual void rotate(const MyVector &base, double angle) = 0;
  virtual QPainterPath getRotatedPath(const MyVector &base,
                                      double angle) const = 0;

  //  축척(Scale) 변환 및 축척된 외형 Path를 반환하는 순수 가상 함수
  virtual void scale(const MyVector &base, double scaleFactor) = 0;
  virtual QPainterPath getScaledPath(const MyVector &base,
                                     double scaleFactor) const = 0;

  //  신축(Stretch) 변환 및 신축된 외형 Path를 반환하는 순수 가상 함수
  virtual void stretch(const QList<int> &indices, const MyVector &delta) = 0;
  virtual QPainterPath getStretchedPath(const QList<int> &indices,
                                        const MyVector &delta) const = 0;

  // [신규] 연장을 위해 끝점을 업데이트하는 함수
  virtual void updateEndpoint(const QPointF &newPos, bool isFirstEnd) {
    Q_UNUSED(newPos);
    Q_UNUSED(isFirstEnd);
  }

  //  속성 복제 및 다형성 복사를 위한 패턴
  virtual CadItem *clone() const = 0;

  // 레이어 이름을 지정하고 가져오는 함수 (가상 함수로 변경하여 블록 등에서 전파
  // 가능)
  virtual void setLayerName(const QString &name) { m_layerName = name; }
  QString layerName() const { return m_layerName; }

  //  펜(선 속성) 중앙 관리
  virtual void setPen(const QPen &pen) {
    m_pen = pen;
    update();
  }
  virtual QPen pen() const { return m_pen; }

  //  속성창(Property Dock) 연동을 위한 인터페이스
  // 레이어, 색상, 라인타입 외의 기하학적 속성이나 개별 특성을 다룸
  virtual QMap<QString, QVariant> getProperties() const = 0;
  virtual bool setProperty(const QString &key, const QVariant &value) = 0;

  // [신규] 그립 표시 여부 플래그 (도구에서 하이라이트만 하고 싶을 때 사용)
  virtual void setGripsEnabled(bool enabled) { m_gripsEnabled = enabled; }
  virtual bool gripsEnabled() const { return m_gripsEnabled; }

  virtual void setGripsVisible(bool visible) = 0;

  // [신규] 두께(Stroke)를 포함하지 않는 순수 기하학적 경로 반환
  virtual QPainterPath geometryPath() const = 0;

  // [신규] 외부에서 영역 갱신을 트리거할 수 있는 인터페이스
  void updateGeometry() { prepareGeometryChange(); }

  virtual QList<CadItem *> explode() const {
      return QList<CadItem *>();
  }

protected:
  QString m_layerName; //  소속 레이어를 기억하는 명찰
  QPen m_pen;          //  공통 선 속성
  bool m_gripsEnabled = true;
};

#endif // CADITEM_H
