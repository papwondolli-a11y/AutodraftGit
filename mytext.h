#ifndef MYTEXT_H
#define MYTEXT_H

#include "myentity.h"
#include <QString>

struct MyText : public MyEntity {
  MyVector pos;
  QString text;
  QString fontFamily;
  double fontSize;
  double rotation;

  int alignH = 0; // 0=left, 1=center, 2=right

  // 글자 서식 (Word 의 굵게/기울임/밑줄)
  bool bold = false;
  bool italic = false;
  bool underline = false;

  bool isAttribute = false;
  QString tag;
  QString prompt;

  MyText(const MyVector &p = MyVector(), const QString &t = "",
         const QString &ff = "Arial", double fs = 12.0, double r = 0.0,
         int ah = 0, bool isAttr = false, const QString &tg = "", const QString &pr = "")
      : pos(p), text(t), fontFamily(ff), fontSize(fs), rotation(r), alignH(ah),
        isAttribute(isAttr), tag(tg), prompt(pr) {}

  EntityType type() const override { return EntityType::Text; }

  std::vector<MyVector> intersect(const MyEntity *other) const override {
      Q_UNUSED(other)
    // 텍스트는 보통 다른 객체와의 교차점 계산에서 제외하거나
    // 바운딩 박스로 계산합니다. 여기서는 일단 빈 결과를 반환합니다.
    return {};
  }

  MyVector getNearestPoint(const MyVector &p) const override
  {
      Q_UNUSED(p)
    return pos; // 단순화를 위해 삽입점 반환
  }
};

#endif // MYTEXT_H
