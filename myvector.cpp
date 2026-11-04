#include "myvector.h"

MyVector::MyVector(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}

MyVector::MyVector(const QPointF &p) : x(p.x()), y(p.y()), z(0.0) {}

MyVector MyVector::operator+(const MyVector &v) const {
  return MyVector(x + v.x, y + v.y, z + v.z);
}

MyVector MyVector::operator-(const MyVector &v) const {
  return MyVector(x - v.x, y - v.y, z - v.z);
}

MyVector MyVector::operator*(double s) const {
  return MyVector(x * s, y * s, z * s);
}

MyVector MyVector::operator/(double s) const {
  if (std::abs(s) < 1.0e-12)
    return *this; // 0 나누기 방지
  return MyVector(x / s, y / s, z / s);
}

MyVector &MyVector::operator+=(const MyVector &v) {
  x += v.x;
  y += v.y;
  z += v.z;
  return *this;
}

MyVector &MyVector::operator-=(const MyVector &v) {
  x -= v.x;
  y -= v.y;
  z -= v.z;
  return *this;
}

bool MyVector::operator==(const MyVector &other) const {
  // 단순 비교 (엄격한 오차 처리는 MyMath::isEqual 사용 권장)
  return (x == other.x && y == other.y && z == other.z);
}

bool MyVector::operator!=(const MyVector &other) const {
  return !(*this == other);
}

double MyVector::magnitude() const { return std::sqrt(x * x + y * y + z * z); }

double MyVector::distanceTo(const MyVector &v) const {
  return (*this - v).magnitude();
}

MyVector MyVector::normalized() const {
  double m = magnitude();
  if (m < 1.0e-12)
    return *this;
  return *this / m;
}

QPointF MyVector::toPointF() const { return QPointF(x, y); }
