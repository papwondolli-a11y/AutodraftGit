#ifndef CHAMFERTOOL_H
#define CHAMFERTOOL_H

#include "abstracttool.h"
#include <QPointF>

class CadItem;

class ChamferTool : public AbstractTool {
  Q_OBJECT

public:
  enum State {
    State_WaitingForFirst,
    State_WaitingForSecond,
    State_BoxSelecting,
    State_WaitingForDist1,
    State_WaitingForDist2
  };

  explicit ChamferTool(QObject *parent = nullptr);
  virtual ~ChamferTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;
  bool processCommand(const QString &command) override;

private:
  void updatePrompt();
  void clearOverlay();
  void acceptDefaultDistance();

  // Selection Box
  void handleSelectionPress(QPointF pos, Qt::MouseButton button);
  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p2);

  // Chamfer Logic

  void tryChamfer(CadItem *item1, CadItem *item2, const QPointF &p1,
                  const QPointF &p2, bool trimLines = true);
  void executeChamfer(CadItem *item1, CadItem *item2, const QPointF &p1,
                      const QPointF &p2, bool trimLines);

  void updateHoverPreview(const QPointF &pos);
  void clearHoverPreview();

  State m_currentState;
  CadItem *m_firstItem;
  QPointF m_firstPickPos;

  static double s_lastDist1;
  static double s_lastDist2;
  double m_dist1;
  double m_dist2;

  // Box Selection
  bool m_isBoxSelecting;
  QPointF m_firstCorner;
};

#endif // CHAMFERTOOL_H
