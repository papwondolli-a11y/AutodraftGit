#ifndef LAYERMANAGER_H
#define LAYERMANAGER_H

#include "layer.h"
#include "thememanager.h"
#include <QAbstractTableModel>
#include <QList>
#include <QString>

class IAppContext;

class LayerManager : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    Current = 0,
    Name,
    Color,
    Weight,
    Linetype,
    On,
    Plot,
    Lock,
    ColumnCount
  };

  explicit LayerManager(IAppContext *ctx = nullptr, QObject *parent = nullptr);
  ~LayerManager();

signals:
  void currentLayerChanged(const QString &name);
  void layerPropertyChanged(const QString &name);

public:
  // Model Overrides
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::EditRole) override;

  // Layer API
  void initDefault();
  bool addLayer(const Layer &layer);
  bool removeLayer(const QString &name);
  bool editLayer(const QString &name, const Layer &newLayer);
  bool renameLayer(const QString &oldName, const QString &newName);

  Layer *getLayer(const QString &name);
  QList<Layer> getAllLayers() const;
  Layer *getCurrentLayer();
  bool setCurrentLayer(const QString &name);

  void setContext(IAppContext *ctx) { m_context = ctx; }

private:
  int findRowByName(const QString &name) const;
  QList<Layer> m_layers;
  IAppContext *m_context = nullptr;

private slots:
  void onThemeChanged(ThemeManager::ThemeType type);
};

#endif // LAYERMANAGER_H
