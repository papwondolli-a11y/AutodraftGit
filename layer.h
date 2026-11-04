#ifndef LAYER_H
#define LAYER_H

#include <QColor>
#include <QString>


class Layer {
public:
  explicit Layer(const QString &name = "", const QColor &color = Qt::black,
                 double weight = 1.00, const QString &linetype = "Continuous",
                 bool current = false, bool layerOn = true, bool plotOn = true,
                 bool layerLock = false); // 기본 잠금은 해제가 자연스럽습니다.

  // Getters
  bool isCurrent() const;
  QString name() const;
  QColor color() const;
  double weight() const;
  QString linetype() const;
  bool isLayerOn() const;
  bool isPlotOn() const;
  bool isLayerLock() const;

  // [출력 스타일] 화면에 보이는 색(color)과 별개로, 인쇄에만 쓸 색상을 지정할 수 있다.
  // 지정하지 않으면(invalid QColor) 인쇄 시 화면 색상에 자동 규칙(밝은 색 -> 검정)을 적용한다.
  QColor printColor() const;
  bool hasPrintColorOverride() const;

  // Setters (QObject가 아니므로 일반 public 함수로 사용)
  void setCurrent(bool);
  void setName(const QString &);
  void setColor(const QColor &);
  void setWeight(double);
  void setLinetype(const QString &);
  void setLayerOn(bool);
  void setPlotOn(bool);
  void setLayerLock(bool);

  // [출력 스타일] 인쇄 전용 색상 지정/해제
  void setPrintColor(const QColor &color);
  void clearPrintColorOverride();

private:
  bool m_current;
  QString m_name;
  QColor m_color;
  double m_weight;
  QString m_linetype;
  bool m_layerOn;
  bool m_plotOn;
  bool m_layerLock;
  QColor m_printColor; // invalid(QColor()) = 오버라이드 없음(자동 규칙 사용)
};

#endif // LAYER_H
