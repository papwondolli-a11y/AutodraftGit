#ifndef INSERTTOOL_H
#define INSERTTOOL_H

#include "abstracttool.h"
#include <QList>
#include <QPointF>
#include <QString>

class CadItem;

class InsertTool : public AbstractTool {
  Q_OBJECT
public:
  explicit InsertTool(QObject *parent = nullptr);
  ~InsertTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void setBlockPath(const QString &path) { m_filePath = path; }
  void setBlockItems(const QList<CadItem *> &items, const QString &name,
                     const QString &originalLayer);
  void setTargetLayer(const QString &layer) { m_targetLayer = layer; }

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void keyPress(QKeyEvent *event) override;
  bool processCommand(const QString &command) override;

private:
  void reset();
  void clearItems();
  void updateGhost(const QPointF &pos);
  void applyTransformations();
  void createAndInsertBlock();

  enum State {
    PickingFile,
    PickingPoint,
    PickingScale,
    PickingRotation,
    Finished
  };

  State m_state = PickingFile;
  QList<CadItem *> m_items; // 블록 원본 객체 (0,0 기준)
  QPixmap m_thumbnail;
  QPointF m_thumbnailOffset;
  QSizeF m_thumbnailSize; // 블록의 실제 크기 (Scene Units)
  QPointF m_insertionPoint;
  double m_scale = 1.0;
  double m_rotation = 0.0;
  QString m_filePath;
  QString m_originalLayerName;
  QString m_targetLayer;
  bool m_inDialog = false;
};

#endif // INSERTTOOL_H
