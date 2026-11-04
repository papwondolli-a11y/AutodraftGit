#include "selectionmanager.h"
#include <QGraphicsItem>

SelectionManager::SelectionManager(QObject *parent) : QObject(parent) {}

QList<QGraphicsItem *> SelectionManager::selectedItems() const {
  return m_selectedItems;
}

void SelectionManager::setSelectedItems(const QList<QGraphicsItem *> &items) {
  m_selectedItems.clear();
  for (auto *item : items) {
    m_selectedItems.append(item);
  }
  emit selectionChanged(items);
}

void SelectionManager::clearSelection() {
  if (m_selectedItems.isEmpty())
    return;

  m_selectedItems.clear();
  emit selectionChanged(QList<QGraphicsItem *>());
}
