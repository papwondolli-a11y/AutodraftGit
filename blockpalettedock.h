#ifndef BLOCKPALETTEDOCK_H
#define BLOCKPALETTEDOCK_H

#include <QDockWidget>
#include <QListWidget>
#include <QMap>
#include <QPointer>
#include "layermanager.h"

class IAppContext;
class QComboBox;
class QLabel;
class QPushButton;

class BlockPaletteDock : public QDockWidget {
  Q_OBJECT
public:
  explicit BlockPaletteDock(IAppContext *ctx, QWidget *parent = nullptr);

  void refresh();
  void activateFolder(const QString &path);
  void refreshLayers();
  void setLayerManager(class LayerManager *mgr);

  QString selectedLayerName() const;

signals:
  void blockClicked(const QString &filePath);
  void blockDoubleClicked(const QString &filePath);

private slots:
  void onItemClicked(QListWidgetItem *item);
  void onItemDoubleClicked(QListWidgetItem *item);
  void onBrowseButtonClicked();
  void onClearHistoryClicked();
  void onFolderChanged(int index);
  void onLayerComboChanged(int index);
  void onSourceChanged();
  void onViewModeChanged();

private:
  QIcon renderBlockIcon(class BlockItem *block);
  void saveFolderHistory();
  void loadFolderHistory();
  void addFolderToHistory(const QString &path);

  void saveFolderLayerMap();
  void loadFolderLayerMap();
  void applyFolderLayer(const QString &folderPath);

  IAppContext *m_ctx;
  QListWidget *m_listWidget;
  QComboBox *m_folderCombo;
  QPushButton *m_browseButton;
  QPushButton *m_clearHistoryButton;

  class QRadioButton *m_radioLibrary;
  class QRadioButton *m_radioCurrent;
  class QComboBox *m_viewModeCombo;

  QComboBox    *m_layerCombo;
  QLabel       *m_layerLabel;
  // 활성 도면창이 닫히면 LayerManager가 함께 삭제되므로 QPointer로 보관
  QPointer<LayerManager> m_layerManager;

  QString              m_currentFolderPath;
  QMap<QString,QString> m_folderLayerMap;   // folderPath → layerName
};

#endif // BLOCKPALETTEDOCK_H
