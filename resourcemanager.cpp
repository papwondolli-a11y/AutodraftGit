#include "resourcemanager.h"

ResourceManager::ResourceManager(QObject *parent) : QObject(parent) {
  initIcons();
}

void ResourceManager::initIcons() {
  m_iconPaths[IconType::LayerOn] = ":/icons/layerON.png";
  m_iconPaths[IconType::LayerOff] = ":/icons/layerOFF.png";
  m_iconPaths[IconType::PlotOn] = ":/icons/plotON.png";
  m_iconPaths[IconType::PlotOff] = ":/icons/plotOFF.png";
  m_iconPaths[IconType::LayerLock] = ":/icons/layerLock.png";
  m_iconPaths[IconType::LayerUnlock] = ":/icons/layerUnlock.png";
  m_iconPaths[IconType::CurrentOn] = ":/icons/currentOn.png";
  m_iconPaths[IconType::CurrentOff] = ":/icons/currentOff.png";
  m_iconPaths[IconType::MakeItem] = ":/icons/makeItem.png";
  m_iconPaths[IconType::EraseItem] = ":/icons/eraseItem.png";
  m_iconPaths[IconType::LayerSet] = ":/icons/layerSet.png";
}

QIcon ResourceManager::getIcon(IconType type) const {
  return QIcon(getIconPath(type));
}

QString ResourceManager::getIconPath(IconType type) const {
  return m_iconPaths.value(type, QString());
}
