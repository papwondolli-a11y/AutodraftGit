#ifndef SELECTIONMANAGER_H
#define SELECTIONMANAGER_H

#include <QList>
#include <QObject>
#include <QPointer>

class QGraphicsItem;

class SelectionManager : public QObject {
  Q_OBJECT
public:
  explicit SelectionManager(QObject *parent = nullptr);

  // 선택된 아이템 목록 반환
  QList<QGraphicsItem *> selectedItems() const;

  // 선택 상태 업데이트 (Scene에서 호출)
  void setSelectedItems(const QList<QGraphicsItem *> &items);

  // 선택 해제
  void clearSelection();

signals:
  // 선택 결과가 변경되었을 때 발생하는 시그널
  void selectionChanged(const QList<QGraphicsItem *> &items);

private:
  QList<QGraphicsItem *> m_selectedItems;
};

#endif // SELECTIONMANAGER_H
