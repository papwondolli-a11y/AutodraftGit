#include "gripitem.h"
#include "caditem.h"
#include <QBrush>
#include <QCursor>
#include <QPainter>
#include <QPen>

static const double GRIP_SIZE = 6.0;

GripItem::GripItem(int index, CadItem *parent)
    : QGraphicsRectItem(parent), m_index(index), m_parent(parent) {
  // [핵심] 화면 줌에 관계없이 일정한 픽셀 크기를 유지하도록 설정
  setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  setRect(-GRIP_SIZE / 2, -GRIP_SIZE / 2, GRIP_SIZE, GRIP_SIZE);
  setBrush(QBrush(Qt::blue));
  setPen(QPen(Qt::black, 0));

  setFlag(QGraphicsItem::ItemIsSelectable, false);
  setFlag(QGraphicsItem::ItemIsMovable, false);
  setAcceptHoverEvents(true);
}

void GripItem::updatePosition(const QPointF &pos) { this->setPos(pos); }
void GripItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget) {
  // [핵심] 프린터 출력 시에는 그립(파란 사각형)을 그리지 않음
  if (painter->device()->devType() != QInternal::Widget) {
    return;
  }
  QGraphicsRectItem::paint(painter, option, widget);
}

void GripItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event) {
  setBrush(QBrush(Qt::red));
  QGraphicsRectItem::hoverEnterEvent(event);
}

void GripItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
  setBrush(QBrush(Qt::blue));
  QGraphicsRectItem::hoverLeaveEvent(event);
}
