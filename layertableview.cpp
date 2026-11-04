#include "layertableview.h"
#include "appcontext.h"
#include "createlayerdialog.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "linetypeselectdialog.h"
#include "resourcemanager.h"
#include "stylemanager.h"
#include "valueselectdialog.h"

#include <QColorDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

LayerTableView::LayerTableView(AppContext *context, QWidget *parent)
    : QWidget(parent), m_appContext(context), view(new QTableView(this)) {

  // [신규] ThemeManager에서 스타일을 타겟팅할 수 있도록 ObjectName 부여

  this->setObjectName("LayerTypeTableView");
  setAttribute(Qt::WA_StyledBackground, true);

  if (m_appContext && m_appContext->layerManager()) {
    view->setModel(m_appContext->layerManager());
      view -> setObjectName("LayerTableView");

    // 모델 데이터 변경 시 외부(TopToolBar 등)에 알림
    connect(m_appContext->layerManager(), &QAbstractItemModel::dataChanged,
            this, &LayerTableView::dataChangedExternally);
    connect(m_appContext->layerManager(), &QAbstractItemModel::rowsInserted,
            this, &LayerTableView::dataChangedExternally);
    connect(m_appContext->layerManager(), &QAbstractItemModel::rowsRemoved,
            this, &LayerTableView::dataChangedExternally);
  }

  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setEditTriggers(QAbstractItemView::DoubleClicked);
  view->setShowGrid(false);
  view->verticalHeader()->setVisible(false);
  // [수정] 하드코딩된 헤더 스타일시트 제거
  view->horizontalHeader()->setStretchLastSection(true);

  auto lay = new QVBoxLayout(this);
  lay->setContentsMargins(2, 2, 2, 2);
  lay->addWidget(view);

  QToolBar *toolbar = new QToolBar(this);
  // [신규] ThemeManager에서 스타일을 타겟팅할 수 있도록 ObjectName 부여
  toolbar->setObjectName("LayerToolBar");
  toolbar->setIconSize(QSize(16, 16));

  QToolButton *makeButton = new QToolButton(this);
  if (m_appContext && m_appContext->resourceManager()) {
    makeButton->setIcon(m_appContext->resourceManager()->getIcon(
        ResourceManager::IconType::MakeItem));
  } else {
    makeButton->setIcon(QIcon(":/icons/makeItem.png"));
  }
  makeButton->setToolTip(tr("Add new Layer"));
  toolbar->addWidget(makeButton);

  QToolButton *eraseButton = new QToolButton(this);
  if (m_appContext && m_appContext->resourceManager()) {
    eraseButton->setIcon(m_appContext->resourceManager()->getIcon(
        ResourceManager::IconType::EraseItem));
  } else {
    eraseButton->setIcon(QIcon(":/icons/eraseItem.png"));
  }
  eraseButton->setToolTip(tr("Delete selected Layer"));
  toolbar->addWidget(eraseButton);

  QToolButton *setButton = new QToolButton(this);
  if (m_appContext && m_appContext->resourceManager()) {
    setButton->setIcon(m_appContext->resourceManager()->getIcon(
        ResourceManager::IconType::LayerSet));
  } else {
    setButton->setIcon(QIcon(":/icons/layerSet.png"));
  }
  setButton->setToolTip(tr("Set Current Layer"));
  toolbar->addWidget(setButton);

  lay->addWidget(toolbar);

  // 시그널 연결 (기존대로 포인터 함수 사용)
  connect(makeButton, &QToolButton::clicked, this,
          &LayerTableView::onCreatelayer);
  connect(eraseButton, &QToolButton::clicked, this,
          &LayerTableView::onDeletelayer);
  connect(setButton, &QToolButton::clicked, this, &LayerTableView::onSetlayer);
  connect(view, &QTableView::doubleClicked, this,
          &LayerTableView::onItemSetLayer);
  connect(view, &QTableView::clicked, this,
          &LayerTableView::onItemTableChanged);

  // Setup column resizing
  view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  view->setColumnWidth(0, 30); // Current
  view->setColumnWidth(1, 60); // Name
  view->setColumnWidth(2, 60); // Color
  view->setColumnWidth(3, 80); // Weight
  view->setColumnWidth(4, 60); // Linetype
  view->setColumnWidth(5, 30); // On
  view->setColumnWidth(6, 30); // Plot
  view->setColumnWidth(7, 30); // Lock
}

void LayerTableView::setLayerManager(LayerManager *mgr) {
  if (!mgr || view->model() == mgr)
    return;

  // 기존 모델 시그널 해제
  if (view->model()) {
    disconnect(view->model(), nullptr, this, nullptr);
  }

  view->setModel(mgr);

  // 새 모델 시그널 연결
  connect(mgr, &QAbstractItemModel::dataChanged, this,
          &LayerTableView::dataChangedExternally);
  connect(mgr, &QAbstractItemModel::rowsInserted, this,
          &LayerTableView::dataChangedExternally);
  connect(mgr, &QAbstractItemModel::rowsRemoved, this,
          &LayerTableView::dataChangedExternally);

  emit dataChangedExternally();
}

void LayerTableView::onCreatelayer() {
  CreateLayerDialog dlg(m_appContext, this);
  if (dlg.exec() != QDialog::Accepted)
    return;

  Layer newLayer(dlg.layerName(), dlg.layerColor(),
                 dlg.lineWeight().split(' ').first().toDouble(), dlg.lineType(),
                 false, true, true);

  if (m_appContext && m_appContext->layerManager()) {
    m_appContext->layerManager()->addLayer(newLayer);
  }
  emit dataChangedExternally();
}

void LayerTableView::onDeletelayer() {
  QModelIndex index = view->currentIndex();
  if (!index.isValid())
    return;

  QString layerName =
      index.siblingAtColumn(LayerManager::Name).data().toString();
  if (layerName == "Default") {
    QMessageBox::warning(this, tr("삭제불가 레이어"),
                         tr("Default 레이어는 삭제 할 수 없습니다."));
    return;
  }

  if (m_appContext && m_appContext->layerManager()) {
    Layer *checkObj = m_appContext->layerManager()->getLayer(layerName);
    if (checkObj && checkObj->isCurrent()) {
      QMessageBox::warning(this, tr("삭제불가 레이어"),
                           tr("현재 사용 중인 레이어는 삭제 할 수 없습니다."));
      return;
    }
    m_appContext->layerManager()->removeLayer(layerName);
  }
  emit dataChangedExternally();
}

void LayerTableView::onSetlayer() {
  QModelIndex index = view->currentIndex();
  if (!index.isValid())
    return;

  QString layerName =
      index.siblingAtColumn(LayerManager::Name).data().toString();
  if (m_appContext && m_appContext->layerManager()) {
    m_appContext->layerManager()->setCurrentLayer(layerName);
  }
  emit dataChangedExternally();
}

void LayerTableView::onItemSetLayer(const QModelIndex &index) {
  if (index.column() != LayerManager::Current)
    return;

  QString layerName =
      index.siblingAtColumn(LayerManager::Name).data().toString();
  if (m_appContext && m_appContext->layerManager()) {
    m_appContext->layerManager()->setCurrentLayer(layerName);
  }
  emit dataChangedExternally();
}

void LayerTableView::onItemTableChanged(const QModelIndex &index) {
  if (!m_appContext || !m_appContext->layerManager() || !index.isValid())
    return;

  int col = index.column();
  QString layerName =
      index.siblingAtColumn(LayerManager::Name).data().toString();
  auto mgr = m_appContext->layerManager();
  Layer *obj = mgr->getLayer(layerName);
  if (!obj)
    return;

  if (col == LayerManager::Color) {
    QColor newColor =
        QColorDialog::getColor(obj->color(), this, tr("Select color"));
    if (newColor.isValid()) {
      mgr->setData(index, newColor, Qt::EditRole);
    }
  } else if (col == LayerManager::Weight) {
    if (!m_appContext->styleManager())
      return;
    auto weights = m_appContext->styleManager()->getLineWeights();

    ValueSelectDialog<LineWeight> dlg(
        weights, QString::number(obj->weight(), 'f', 2) + " mm",
        [](const LineWeight &t) {
          return QIcon(QString(":/icons/%1.png").arg(t.name));
        },
        [](const LineWeight &t) {
          return QString::number(t.weight, 'f', 2) + " mm";
        });

    if (dlg.exec() == QDialog::Accepted && dlg.hasValidSelection()) {
      mgr->setData(index, dlg.selectedItem().weight, Qt::EditRole);
    }
  } else if (col == LayerManager::Linetype) {
    if (!m_appContext->lineTypeManager())
      return;

    // 그룹 라디오(AutoDraft / AutoCAD호환(S) / (ISO))가 달린 전용 다이얼로그.
    // 라디오는 "새 레이어 만들기"와 LineTypeManager::currentGroup() 을 공유한다.
    LineTypeSelectDialog dlg(m_appContext, obj->linetype(), this);
    if (dlg.exec() == QDialog::Accepted) {
      const QString chosen = dlg.selectedLinetype();
      if (!chosen.isEmpty())
        mgr->setData(index, chosen, Qt::EditRole);
    }
  } else if (col == LayerManager::On) {
    bool next = !obj->isLayerOn();
    obj->setLayerOn(next);
    m_appContext->updateLayerVisibility(layerName, next);
    emit mgr->dataChanged(index, index);
  } else if (col == LayerManager::Plot) {
    obj->setPlotOn(!obj->isPlotOn());
    emit mgr->dataChanged(index, index);
  } else if (col == LayerManager::Lock) {
    obj->setLayerLock(!obj->isLayerLock());
    emit mgr->dataChanged(index, index);
  }
}

QIcon LayerTableView::createColorIcon(const QColor &color) {
  if (!color.isValid())
    return QIcon();
  QPixmap pix(20, 20);
  pix.fill(color);
  return QIcon(pix);
}

QString LayerTableView::getClosestColorName(const QColor &color) {
  QString closestName;
  int minDistance = INT_MAX;

  // [수정] C++11 range-loop detach 경고 방지를 위해 인덱스 기반 루프로 변경
  QStringList colorNames = QColor::colorNames();
  for (int i = 0; i < colorNames.size(); ++i) {
    QString name = colorNames.at(i);
    QColor namedColor(name);
    int distance = std::pow(color.red() - namedColor.red(), 2) +
                   std::pow(color.green() - namedColor.green(), 2) +
                   std::pow(color.blue() - namedColor.blue(), 2);
    if (distance < minDistance) {
      minDistance = distance;
      closestName = name;
    }
  }
  return closestName;
}
