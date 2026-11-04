#ifndef LAYERTABLEVIEW_H
#define LAYERTABLEVIEW_H

#include "layer.h"
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

class AppContext; // 전방 선언
class LayerManager;

class LayerTableView : public QWidget {
  Q_OBJECT
public:
  // AppContext를 주입받도록 생성자 수정
  explicit LayerTableView(AppContext *context, QWidget *parent = nullptr);
  void initLayerTable();
  void setLayerManager(LayerManager *mgr);

signals:
  void dataChangedExternally();

private slots:
  void onCreatelayer();
  void onDeletelayer();
  void onSetlayer();
  void onItemSetLayer(const QModelIndex &index);
  void onItemTableChanged(const QModelIndex &index);

private:
  AppContext *m_appContext; // 주입받은 Context 저장용
  QTableView *view;

  QIcon createColorIcon(const QColor &color);
  QString getClosestColorName(const QColor &color);
};

#endif // LAYERTABLEVIEW_H
