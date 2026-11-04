#include "layermanager.h"
#include "iappcontext.h"
#include "resourcemanager.h"
#include "stylemanager.h"
#include <QIcon>
#include <QPixmap>

LayerManager::LayerManager(IAppContext *ctx, QObject *parent)
    : QAbstractTableModel(parent), m_context(ctx) {
  initDefault();

  if (m_context && m_context->themeManager()) {
    connect(m_context->themeManager(), &ThemeManager::themeChanged, this,
            &LayerManager::onThemeChanged);
    // 초기 테마에 맞게 색상 적용
    onThemeChanged(m_context->themeManager()->currentTheme());
  }
}

LayerManager::~LayerManager() {}

int LayerManager::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_layers.size();
}

int LayerManager::columnCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return ColumnCount;
}

QVariant LayerManager::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_layers.size())
    return QVariant();

  const Layer &layer = m_layers.at(index.row());

  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    switch (index.column()) {
    case Name:
      return layer.name();
    case Color: {
      if (m_context && m_context->styleManager()) {
        auto colors = m_context->styleManager()->getColors();
        for (const auto &sc : std::as_const(colors)) {
          if (sc.color == layer.color()) {
            return sc.name;
          }
        }
      }
      return layer.color().name();
    }
    case Weight:
      return QString::number(layer.weight(), 'f', 2) + " mm";
    case Linetype:
      return layer.linetype();
    default:
      return QVariant();
    }
  } else if (role == Qt::DecorationRole) {
    switch (index.column()) {
    case Current:
      if (m_context && m_context->resourceManager()) {
        auto rm = m_context->resourceManager();
        return rm->getIcon(layer.isCurrent()
                               ? ResourceManager::IconType::CurrentOn
                               : ResourceManager::IconType::CurrentOff);
      }
      return QIcon(layer.isCurrent() ? ":/icons/currentOn.png"
                                     : ":/icons/currentOff.png");
    case Color: {
      QPixmap pix(20, 20);
      pix.fill(layer.color());
      return QIcon(pix);
    }
    case Weight: {
      if (m_context && m_context->styleManager()) {
        auto weights = m_context->styleManager()->getLineWeights();
        for (const auto &lw : std::as_const(weights)) {
          if (qAbs(lw.weight - layer.weight()) < 0.001) {
            return QIcon(":/icons/" + lw.name + ".png");
          }
        }
      }
      return QVariant();
    }
    case Linetype:
      return QIcon(":/icons/" + layer.linetype() + ".png");
    case On:
      if (m_context && m_context->resourceManager()) {
        return m_context->resourceManager()->getIcon(
            layer.isLayerOn() ? ResourceManager::IconType::LayerOn
                              : ResourceManager::IconType::LayerOff);
      }
      return QIcon(layer.isLayerOn() ? ":/icons/layerON.png"
                                     : ":/icons/layerOFF.png");
    case Plot:
      if (m_context && m_context->resourceManager()) {
        return m_context->resourceManager()->getIcon(
            layer.isPlotOn() ? ResourceManager::IconType::PlotOn
                             : ResourceManager::IconType::PlotOff);
      }
      return QIcon(layer.isPlotOn() ? ":/icons/plotON.png"
                                    : ":/icons/plotOFF.png");
    case Lock:
      if (m_context && m_context->resourceManager()) {
        return m_context->resourceManager()->getIcon(
            layer.isLayerLock() ? ResourceManager::IconType::LayerLock
                                : ResourceManager::IconType::LayerUnlock);
      }
      return QIcon(layer.isLayerLock() ? ":/icons/layerLock.png"
                                       : ":/icons/layerUnlock.png");
    }
  } else if (role == Qt::TextAlignmentRole) {
    return Qt::AlignCenter;
  }

  return QVariant();
}

QVariant LayerManager::headerData(int section, Qt::Orientation orientation,
                                  int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
    case Current:
      return tr("현재");
    case Name:
      return tr("이름");
    case Color:
      return tr("색상");
    case Weight:
      return tr("두께");
    case Linetype:
      return tr("선종류");
    case On:
      return tr("켜기");
    case Plot:
      return tr("출력");
    case Lock:
      return tr("잠그기");
    }
  }
  return QVariant();
}

Qt::ItemFlags LayerManager::flags(const QModelIndex &index) const {
  if (!index.isValid() || index.row() >= m_layers.size())
    return Qt::NoItemFlags;

  Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (index.column() == Name) {
    // Default 레이어는 이름을 변경할 수 없도록 수정
    if (m_layers[index.row()].name() != "Default") {
      f |= Qt::ItemIsEditable;
    }
  }
  return f;
}

bool LayerManager::setData(const QModelIndex &index, const QVariant &value,
                           int role) {
  if (!index.isValid() || index.row() >= m_layers.size())
    return false;

  Layer &layer = m_layers[index.row()];
  bool changed = false;

  if (role == Qt::EditRole && index.column() == Name) {
    QString newName = value.toString();
    // Default 레이어 보호 및 중복 이름 체크
    if (layer.name() != "Default" && !newName.isEmpty() &&
        newName != layer.name()) {
      if (findRowByName(newName) == -1) {
        layer.setName(newName);
        changed = true;
      }
    }
  } else if (role == Qt::EditRole) {
    if (index.column() == Color) {
      layer.setColor(value.value<QColor>());
      changed = true;
    } else if (index.column() == Weight) {
      layer.setWeight(value.toDouble());
      changed = true;
    } else if (index.column() == Linetype) {
      layer.setLinetype(value.toString());
      changed = true;
    }

    if (changed) {
      emit layerPropertyChanged(layer.name());
    }
  }

  if (changed) {
    emit dataChanged(index, index, {role});
    return true;
  }
  return false;
}

void LayerManager::initDefault() {
  if (m_layers.isEmpty()) {
    Layer def("Default", Qt::black, 1.0, "Continuous", true, true, true, false);
    addLayer(def);
  }
}

bool LayerManager::addLayer(const Layer &layer) {
  if (findRowByName(layer.name()) != -1)
    return false;

  Layer newLayer = layer;
  // 기존에 활성화된 레이어가 이미 있다면, 새로 추가되는 레이어는 비활성 상태로
  // 강제
  if (getCurrentLayer() != nullptr) {
    newLayer.setCurrent(false);
  }

  beginInsertRows(QModelIndex(), m_layers.size(), m_layers.size());
  m_layers.append(newLayer);
  endInsertRows();
  emit layerPropertyChanged(newLayer.name());
  return true;
}

bool LayerManager::removeLayer(const QString &name) {
  if (name == "Default")
    return false;

  int row = findRowByName(name);
  if (row == -1)
    return false;

  beginRemoveRows(QModelIndex(), row, row);
  m_layers.removeAt(row);
  endRemoveRows();
  emit layerPropertyChanged(name); // Scene에서 캐시 제거 등을 위해 신호 발생
  return true;
}

bool LayerManager::editLayer(const QString &name, const Layer &newLayer) {
  int row = findRowByName(name);
  if (row == -1)
    return false;

  m_layers[row] = newLayer;
  emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
  emit layerPropertyChanged(name);
  return true;
}

bool LayerManager::renameLayer(const QString &oldName, const QString &newName) {
  int row = findRowByName(oldName);
  if (row == -1 || findRowByName(newName) != -1)
    return false;

  QString old = oldName;
  m_layers[row].setName(newName);
  emit dataChanged(index(row, Name), index(row, Name));
  emit layerPropertyChanged(newName); // 새 이름으로 스타일 동기화 유도
  return true;
}

Layer *LayerManager::getLayer(const QString &name) {
  int row = findRowByName(name);
  if (row == -1)
    return nullptr;
  return &m_layers[row];
}

QList<Layer> LayerManager::getAllLayers() const { return m_layers; }

Layer *LayerManager::getCurrentLayer() {
  for (int i = 0; i < m_layers.size(); ++i) {
    if (m_layers[i].isCurrent())
      return &m_layers[i];
  }
  return getLayer("Default");
}

bool LayerManager::setCurrentLayer(const QString &name) {
  bool found = false;
  for (int i = 0; i < m_layers.size(); ++i) {
    bool isTarget = (m_layers[i].name() == name);
    if (m_layers[i].isCurrent() != isTarget) {
      m_layers[i].setCurrent(isTarget);
    }
    if (isTarget)
      found = true;
  }

  if (found) {
    emit dataChanged(index(0, Current), index(m_layers.size() - 1, Current));
    emit currentLayerChanged(name);
    return true;
  }
  return false;
}

int LayerManager::findRowByName(const QString &name) const {
  for (int i = 0; i < m_layers.size(); ++i) {
    if (m_layers[i].name() == name)
      return i;
  }
  return -1;
}

void LayerManager::onThemeChanged(ThemeManager::ThemeType type) {
  int row = findRowByName("Default");
  if (row != -1) {
    QColor newColor =
        (type == ThemeManager::ThemeType::Light || type == ThemeManager::ThemeType::CADClassic ) ? Qt::black : Qt::white;
    if (m_layers[row].color() != newColor) {
      m_layers[row].setColor(newColor);
      emit dataChanged(index(row, Color), index(row, Color),
                       {Qt::DisplayRole, Qt::DecorationRole});
      emit layerPropertyChanged("Default"); // 씬 레이어 스타일 캐시 갱신
    }
  }
}
