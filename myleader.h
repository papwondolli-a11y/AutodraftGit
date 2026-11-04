#ifndef MYLEADER_H
#define MYLEADER_H

#include "myentity.h"
#include "myline.h"
#include <QList>
#include <QString>
#include <QStringList>

struct MyLeader : public MyEntity {
  QList<MyVector> points;
  QString text;
  double textSize = 2.5;
  double landingLength = 5.0;
  bool hideArrow = false;
  // 구멍 콜아웃(HoleCalloutTool)이 가리키는 구멍들의 HOLE_DATA_GROUP_ID.
  // 비어있으면 일반 지시선. 연결돼 있으면 text는 구멍 데이터에서 다시 조립된다(HoleCalloutLink).
  QStringList holeIds;

  MyLeader() {}
  virtual ~MyLeader() {}

  EntityType type() const override { return EntityType::Leader; }

  std::vector<MyVector> intersect(const MyEntity *other) const override
  {
      Q_UNUSED(other)
      // Basic intersection: check each segment
      // Simplified for now, can be improved
      return std::vector<MyVector>();
  }

  MyVector getNearestPoint(const MyVector &pos) const override
  {
      Q_UNUSED(pos)
      // Handled by LeaderItem::getNearestSnap for now
      return MyVector();
  }
};

#endif // MYLEADER_H
