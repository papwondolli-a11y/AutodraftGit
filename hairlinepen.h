#ifndef HAIRLINEPEN_H
#define HAIRLINEPEN_H

#include <QPen>
#include <QTransform>
#include <cmath>

// 선가중치 표시를 끈 상태에서 쓰는 "화면 1px 가는 선" 펜 설정.
// 코스메틱 펜은 래스터 엔진 전용 라이너를 타서 방향에 따라 AA 가 고르지 않고,
// 정확히 1px 이면 45° 부근 사선의 최고 밝기가 픽셀 위치에 따라 크게 출렁인다
// (측정 117~230). 화면 1.1px(168~220)의 일반 펜으로 그려 고르게 보이게 한다.
namespace HairlinePen {
constexpr qreal kScreenWidth = 1.1;

inline void apply(QPen &pen, const QTransform &painterTransform) {
  const qreal scale =
      std::hypot(painterTransform.m11(), painterTransform.m12());
  if (scale > 0.0) {
    pen.setCosmetic(false);
    pen.setWidthF(kScreenWidth / scale);
  } else {
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
  }
}

// 선택 강조(글로우, 코스메틱 펜)의 화면 픽셀 폭 = 선의 화면상 굵기 + 4px.
// 일반 펜은 폭이 장면 단위라 그대로 더하면 축소 시 글로우가 두꺼워지므로 배율을 곱해 px 로 환산한다.
inline qreal glowWidth(const QPen &pen, const QTransform &painterTransform) {
  const qreal scale =
      std::hypot(painterTransform.m11(), painterTransform.m12());
  const qreal px = pen.isCosmetic() ? pen.widthF() : pen.widthF() * scale;
  return px + 4.0;
}
} // namespace HairlinePen

#endif // HAIRLINEPEN_H
