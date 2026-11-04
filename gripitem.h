#ifndef GRIPITEM_H
#define GRIPITEM_H

#include <QGraphicsRectItem>

class CadItem; // 전방 선언

class GripItem : public QGraphicsRectItem
{
public:
    // [수정] 부모를 CadItem으로 받음, index로 그립 구분
    GripItem(int index, CadItem* parent);

    int gripIndex() const { return m_index; }
    void updatePosition(const QPointF& pos);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    int m_index;
    CadItem* m_parent;
};

#endif // GRIPITEM_H
