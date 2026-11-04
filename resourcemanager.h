#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

#include <QIcon>
#include <QMap>
#include <QObject>
#include <QString>


class ResourceManager : public QObject {
  Q_OBJECT
public:
  enum class IconType {
    LayerOn,
    LayerOff,
    PlotOn,
    PlotOff,
    LayerLock,
    LayerUnlock,
    CurrentOn,
    CurrentOff,
    MakeItem,
    EraseItem,
    LayerSet,
    // Add more as needed
  };

  explicit ResourceManager(QObject *parent = nullptr);

  QIcon getIcon(IconType type) const;
  QString getIconPath(IconType type) const;

private:
  void initIcons();
  QMap<IconType, QString> m_iconPaths;
};

#endif // RESOURCEMANAGER_H
