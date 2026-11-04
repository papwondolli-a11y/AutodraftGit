#ifndef MYVECTOR_H
#define MYVECTOR_H

#include <QPointF> // View 변환용
#include <cmath>


class MyVector {
public:
  double x;
  double y;
  double z; // 3D 확장을 위해 유지

  // 생성자
  MyVector(double _x = 0.0, double _y = 0.0, double _z = 0.0);
  MyVector(const QPointF &p); // Qt 호환

  // 연산자 오버로딩
  MyVector operator+(const MyVector &v) const;
  MyVector operator-(const MyVector &v) const;
  MyVector operator*(double s) const;
  MyVector operator/(double s) const;

  MyVector &operator+=(const MyVector &v);
  MyVector &operator-=(const MyVector &v);

  // 비교 (오차 범위 고려 안 함, Map 키 등으로 쓸 때 필요)
  bool operator==(const MyVector &other) const;
  bool operator!=(const MyVector &other) const;

  // 유틸리티
  double magnitude() const;
  double distanceTo(const MyVector &v) const;
  MyVector normalized() const;

  // Qt 변환 (그릴 때만 사용)
  QPointF toPointF() const;
};

#endif // MYVECTOR_H
