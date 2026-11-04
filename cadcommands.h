#ifndef CADCOMMANDS_H
#define CADCOMMANDS_H

#include "mydimension.h"
#include "myvector.h"
#include <QPointF>
#include <QPointer>
#include <QUndoCommand>

#include <QGraphicsItem>
#include <QPen>
#include <QVariant>

class CadItem;
class MyGraphicsScene;
class IAppContext;
#include "commandbase.h"
#include "myvector.h"

class AddEntityCommand : public QUndoCommand {
public:
  AddEntityCommand(MyGraphicsScene *scene, QGraphicsItem *item,
                   QUndoCommand *parent = nullptr);
  ~AddEntityCommand();

  void undo() override;
  void redo() override;

private:
  MyGraphicsScene *m_scene;
  QGraphicsItem *m_item;
  bool m_ownsItem; // [핵심] 씬에 없을 때 메모리 관리용
  QString m_layerName;
};

class RemoveEntityCommand : public QUndoCommand {
public:
  RemoveEntityCommand(MyGraphicsScene *scene, QGraphicsItem *item,
                      QUndoCommand *parent = nullptr);
  ~RemoveEntityCommand();

  void undo() override;
  void redo() override;

private:
  MyGraphicsScene *m_scene;
  QGraphicsItem *m_item;
  bool m_ownsItem;
  QString m_layerName;
};

class MoveEntityCommand : public QUndoCommand {
public:
  MoveEntityCommand(CadItem *item, const MyVector &delta,
                    QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  MyVector m_delta;
};

class RotateEntityCommand : public QUndoCommand {
public:
  RotateEntityCommand(CadItem *item, const MyVector &basePoint, double angle,
                      QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  MyVector m_basePoint;
  double m_angle;
};

class ScaleEntityCommand : public QUndoCommand {
public:
  ScaleEntityCommand(CadItem *item, const MyVector &basePoint,
                     double scaleFactor, QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  MyVector m_basePoint;
  double m_scaleFactor;
};

class ExtendEntityCommand : public QUndoCommand {
public:
  ExtendEntityCommand(CadItem *item, const QPointF &oldPos,
                      const QPointF &newPos, bool isFirstEnd,
                      QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  QPointF m_oldPos;
  QPointF m_newPos;
  bool m_isFirstEnd;
};

class StretchEntityCommand : public QUndoCommand {
public:
  StretchEntityCommand(CadItem *item, const QList<int> &indices,
                       const MyVector &delta, QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  QList<int> m_indices;
  MyVector m_delta;
};

class ChangePropertyCommand : public QUndoCommand {
public:
  ChangePropertyCommand(CadItem *item, const QString &key,
                        const QVariant &oldValue, const QVariant &newValue,
                        QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  QString m_key;
  QVariant m_oldValue;
  QVariant m_newValue;
};

class ChangeLayerCommand : public QUndoCommand {
public:
  ChangeLayerCommand(MyGraphicsScene *scene, CadItem *item,
                     const QString &oldLayer, const QString &newLayer,
                     const QPen &oldPen, const QPen &newPen,
                     QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  MyGraphicsScene *m_scene;
  CadItem *m_item;
  QString m_oldLayer;
  QString m_newLayer;
  QPen m_oldPen;
  QPen m_newPen;
};

class GlobalUndoCommand : public CommandBase {
  Q_OBJECT
public:
  explicit GlobalUndoCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class GlobalRedoCommand : public CommandBase {
  Q_OBJECT
public:
  explicit GlobalRedoCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class ZoomAllCommand : public CommandBase {
  Q_OBJECT
public:
  explicit ZoomAllCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class ZoomExtendCommand : public CommandBase {
  Q_OBJECT
public:
  explicit ZoomExtendCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class MirrorEntityCommand : public QUndoCommand {
public:
  MirrorEntityCommand(CadItem *item, const MyVector &p1, const MyVector &p2,
                      QUndoCommand *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  CadItem *m_item;
  MyVector m_p1;
  MyVector m_p2;
};

class InsertBlockCommand : public QUndoCommand {
public:
  InsertBlockCommand(MyGraphicsScene *scene, const QList<CadItem *> &items,
                     QUndoCommand *parent = nullptr);
  ~InsertBlockCommand();

  void undo() override;
  void redo() override;

private:
  MyGraphicsScene *m_scene;
  QList<CadItem *> m_items;
  bool m_ownsItems;
};

class MirrorCommand : public CommandBase {
  Q_OBJECT
public:
  explicit MirrorCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

// [신규] "4거울상" 명령: 선택 객체를 상/하/좌/우 4방향 미리보기 다이얼로그로
// 보여주고, 클릭한 방향으로 미러 복제본을 생성하는 QuadMirrorTool을 실행한다.
class QuadMirrorCommand : public CommandBase {
  Q_OBJECT
public:
  explicit QuadMirrorCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class RotateCommand : public CommandBase {
  Q_OBJECT
public:
  explicit RotateCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class ScaleCommand : public CommandBase {
  Q_OBJECT
public:
  explicit ScaleCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class StretchCommand : public CommandBase {
  Q_OBJECT
public:
  explicit StretchCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class ZoomWindowCommand : public CommandBase {
  Q_OBJECT
public:
  explicit ZoomWindowCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class InsertCallCommand : public CommandBase {
  Q_OBJECT
public:
  explicit InsertCallCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class SplineCommand : public CommandBase {
  Q_OBJECT
public:
  explicit SplineCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class BlockLibraryCommand : public CommandBase {
  Q_OBJECT
public:
  explicit BlockLibraryCommand(IAppContext *ctx, const QString &subFolder,
                               QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
  QString m_subFolder;
};

// ─────────────────────────────────────────────────────────────────────────────
// ChangeDimStyleCommand
//   DimStyleDialog OK 시 치수 스타일 변경을 Undo/Redo 지원하는 커맨드
// ─────────────────────────────────────────────────────────────────────────────
class ChangeDimStyleCommand : public QUndoCommand {
public:
  ChangeDimStyleCommand(MyGraphicsScene       *scene,
                        const DimensionStyle  &oldStyle,
                        const DimensionStyle  &newStyle,
                        QUndoCommand          *parent = nullptr);

  void undo() override;
  void redo() override;

private:
  MyGraphicsScene *m_scene;
  DimensionStyle   m_oldStyle;
  DimensionStyle   m_newStyle;
};

#endif // CADCOMMANDS_H
