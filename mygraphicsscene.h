#ifndef MYGRAPHICSSCENE_H
#define MYGRAPHICSSCENE_H

#include "documentsettings.h"
#include "mydimension.h"
#include "unitconvert.h"
#include "scenesymbolregistry.h"
#include "snaptypes.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QList>
#include <QMap>
#include <QPainterPath>


class AbstractTool; // 전방 선언
class CadItem;
class OverlayManager;
class IAppContext;
class ThemeManager;

struct LayerStyle {
  QColor color;
  double weight = 1.0;
  QVector<qreal> dashPattern;
};

class MyGraphicsScene : public QGraphicsScene {
  Q_OBJECT
public:
  explicit MyGraphicsScene(QObject *parent = nullptr);
  ~MyGraphicsScene();

  // 현재 도구를 설정하는 함수
  void setTool(AbstractTool *tool);
  // [신규] 레이어 이름으로 폴더(Root 아이템)를 가져오는 함수
  QGraphicsItem *getLayerRoot(const QString &layerName);

  // [신규] 아이템의 레이어 폴더를 변경(재배치)하는 함수
  void reparentItemToLayer(QGraphicsItem *item, const QString &layerName);
  void clearAll(); // [신규] 씬과 레이어 루트 일괄 삭제

  // [신규] 폴더 전체를 숨기거나 보여주는 함수
  void setLayerVisible(const QString &layerName, bool visible);
  void updateLayerItems(const QString &layerName, const QColor &color,
                        double weight, const QString &linetype,
                        const QVector<qreal> &dashPattern = {});

  // =========================================================
  // [추가] AppContext 접근용 Getter / Setter
  // =========================================================
  void setContext(IAppContext *ctx);
  IAppContext *context() const;
  void updateOverlayTheme(ThemeManager *tm);

  SceneSymbolRegistry *symbolRegistry() const;
  QColor defaultGhostColor() const { return m_defaultGhostColor; }

  // [3-1] 이 도면에 귀속되는 도구/문서 기본값 저장소.
  //   ToolDefaults 가 조회 1순위로 사용하고, FileManager 가 .cdb 에 직렬화한다.
  DocumentSettings *documentSettings() { return &m_docSettings; }
  const DocumentSettings *documentSettings() const { return &m_docSettings; }

  enum SelectionMode { Mode_None, Mode_Window, Mode_Crossing };

  // 오버레이(고스트, 선택박스, 스냅) 제어 API
  void setGhostLine(const QLineF &sceneLine, bool visible,
                    const QColor &color = Qt::gray,
                    const QVector<qreal> &dashPattern = {});
  void setGhostPath(const QPainterPath &scenePath, bool visible = true,
                    const QColor &color = Qt::gray, double width = 1.0,
                    const QVector<qreal> &dashPattern = {});
  void setGhostPixmap(const QPixmap &pixmap, const QPointF &scenePos,
                      const QRectF &blockRect, double scale = 1.0,
                      double rotation = 0.0, bool visible = true);
  void setSnapMarker(const QPointF &scenePos, SnapType type);
  void setSelectionRect(const QPointF &p1, const QPointF &p2,
                        SelectionMode mode);
  void setDashRect(const QPointF &p1, const QPointF &p2, const QColor &color);
  void clearOverlay();

  void setShowLineWeight(bool show);
  bool showLineWeight() const;
  void setShowOrigin(bool show);
  bool showOrigin() const { return m_showOrigin; }

  // [신규] 드로잉 사이즈(Limits) 및 스케일 제어
  void setDrawingLimits(const QRectF &limits);
  QRectF drawingLimits() const { return m_drawingLimits; }
  void setDrawingScale(double scale);
  double drawingScale() const { return m_drawingScale; }
  void setDrawingSizeName(const QString &name);
  QString drawingSizeName() const { return m_drawingSizeName; }

  // [신규] 치수 스타일 제어
  void setDimensionStyle(const DimensionStyle &style);
  DimensionStyle dimensionStyle() const { return m_dimStyle; }

  // [Phase 2] 도면 길이 단위(mm / inch).
  //   저장 위치: m_docSettings 키 "units/linear" ("mm" | "inch").
  //   키가 없으면 mm. 좌표·기하는 항상 mm 로 저장하며, 이 값은 좌표 입력과
  //   화면·치수 표시 환산에만 쓴다(도면 축척 drawingScale 과는 무관).
  LinearUnit linearUnit() const;
  void setLinearUnit(LinearUnit u);

  LayerStyle getLayerStyle(const QString &layerName) const;
  void renameLayerStyle(const QString &oldName, const QString &newName);
  void removeLayerStyle(const QString &layerName);

  AbstractTool *currentTool() const;

  // 이전 선택 세트 저장/복원 (P 옵션)
  void savePreviousSelection();
  bool applyPreviousSelection();

  // [성능] 대량 선택 변경(박스 선택 확정, move/copy 재선택 등) 구간을 감싼다.
  // 이 구간에서는 selectionChanged→onSelectionChanged 처리를 억제하고
  // endBulkSelectionChange()에서 한 번만 실행한다. 중첩 호출 안전(depth 카운트).
  void beginBulkSelectionChange();
  void endBulkSelectionChange();
  bool isBulkSelectionChange() const { return m_bulkSelectionDepth > 0; }

signals:
  void drawingSettingsChanged();

public slots:
  void onSelectionChanged();

private slots:
  // onSelectionChanged가 한 이벤트 루프 턴에 몰린 신호를 모아 한 번만 실행
  void flushSelectionChanged();

protected:
  // Scene 차원의 마우스 이벤트 재정의
  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *e) override;
  void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
  OverlayManager *m_overlayManager;
  AbstractTool *m_currentTool;
  // 레이어 이름과 해당 폴더 아이템을 매칭하는 맵
  QMap<QString, QGraphicsItem *> m_layerRoots;
  bool m_showLineWeight = false;
  bool m_showOrigin = true;
  QMap<QString, LayerStyle> m_layerStyleCache;

  // [신규] 드로잉 설정 값
  QRectF m_drawingLimits;
  double m_drawingScale = 1.0;
  QString m_drawingSizeName = "None";
  DimensionStyle m_dimStyle;
  DocumentSettings m_docSettings; // [3-1] 도면 스코프 도구 기본값
  IAppContext *m_context = nullptr;
  QColor m_defaultGhostColor = QColor(60, 60, 60, 200);
  QList<CadItem *> m_previousSelection;
  SceneSymbolRegistry *m_symbolRegistry = nullptr;
  int m_bulkSelectionDepth = 0;         // >0이면 대량 선택 구간
  bool m_selectionFlushPending = false; // flushSelectionChanged 예약됨 여부
};

#endif // MYGRAPHICSSCENE_H
