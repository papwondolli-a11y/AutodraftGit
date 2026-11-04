#ifndef AIBRIDGESERVER_H
#define AIBRIDGESERVER_H

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>

class IAppContext;
class QGraphicsItem;
class QLocalServer;
class QLocalSocket;
class MyGraphicsScene;
class AbstractTool;
class Layer;
struct ElevatorParams;
struct ElevatorSpec;

// ─────────────────────────────────────────────────────────────────────────────
// AiBridgeServer — 외부 AI(MCP 브리지)가 AutoDraft 를 조작하기 위한 로컬 전용 서버
//
//   · QLocalServer("AutoDraftAI") 로 수신 — 이 PC 안에서만 접속 가능
//   · 프로토콜: 줄바꿈(\n) 구분 JSON
//       요청  {"id":1,"tool":"create_line","args":{...}}
//       응답  {"id":1,"ok":true,...}  /  {"id":1,"ok":false,"error":"이유와 수정 방법"}
//   · 모든 처리는 GUI(메인) 스레드에서 실행된다 (QLocalServer 가 메인 이벤트 루프에서 동작)
//   · 쓰기 도구 1회 호출 = undo 1단위
//   · 좌표는 도면 씬 좌표(mm). 위험 작업(삭제·덮어쓰기 저장)은 제공하지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
class AiBridgeServer : public QObject {
  Q_OBJECT
public:
  explicit AiBridgeServer(IAppContext *ctx, QObject *parent = nullptr);

  bool start(const QString &serverName = QStringLiteral("AutoDraftAI"));
  QString serverName() const { return m_serverName; }

signals:
  // new_drawing 도구 호출 시 — MainWindow 가 새 도면 창을 연다
  void newDrawingRequested();
  // open_drawing 으로 도면을 연 뒤 — MainWindow 가 작업 화면으로 전환한다
  void drawingOpened();

private slots:
  void onNewConnection();
  void onReadyRead();
  void onDisconnected();

private:
  QJsonObject handleRequest(const QJsonObject &req);
  QJsonObject dispatchTool(const QJsonObject &req);

  // ── 도구 ──
  QJsonObject toolPing();
  QJsonObject toolGetLayers();
  QJsonObject toolGetEntities(const QJsonObject &args);
  QJsonObject toolCreateLine(const QJsonObject &args);
  QJsonObject toolCreateCircle(const QJsonObject &args);
  QJsonObject toolCreateMultiline(const QJsonObject &args);
  QJsonObject toolMultilineCorner(const QJsonObject &args, char kind);
  QJsonObject toolCreateStair(const QJsonObject &args);
  QJsonObject toolCreateElevator(const QJsonObject &args);
  QJsonObject toolCreateDoor(const QJsonObject &args);
  QJsonObject toolCreateWindow(const QJsonObject &args);
  QJsonObject toolCreateRoom(const QJsonObject &args);
  QJsonObject toolScreenshot(const QJsonObject &args);
  QJsonObject toolListBlocks(const QJsonObject &args);
  QJsonObject toolCreateDimension(const QJsonObject &args);
  QJsonObject toolCreateCircleDimension(const QJsonObject &args);
  QJsonObject toolRunDimensionTool(const QJsonObject &args);
  QJsonObject toolMergeDimensions(const QJsonObject &args);
  QJsonObject toolSplitDimension(const QJsonObject &args);
  QJsonObject toolCreateGdt(const QJsonObject &args);
  QJsonObject toolCreateDatum(const QJsonObject &args);
  QJsonObject toolEditGdt(const QJsonObject &args);
  QJsonObject toolEditEntity(const QJsonObject &args);
  QJsonObject toolDimensionStyle(const QJsonObject &args);
  QJsonObject toolCreateBoxText(const QJsonObject &args);
  QJsonObject toolCreateHatch(const QJsonObject &args);
  QJsonObject toolCreateLayer(const QJsonObject &args);
  QJsonObject toolListHatchPatterns(const QJsonObject &args);
  QGraphicsItem *itemById(MyGraphicsScene *scene, int id) const;
  bool pickPointOnItem(MyGraphicsScene *scene, QGraphicsItem *item, QPointF &out) const;
  AbstractTool *createDimensionTool(const QString &name) const;
  bool normalizeSteps(MyGraphicsScene *scene, const QJsonArray &in, QJsonArray &out,
                      QString &error);
  QJsonObject driveTool(MyGraphicsScene *scene, AbstractTool *rawTool, const QJsonArray &steps,
                        const QString &label);
  Layer *resolveDimensionLayer(MyGraphicsScene *scene, const QJsonObject &args, bool *created,
                               QJsonObject &err);
  QJsonObject toolSetGroup(const QJsonObject &args);
  QJsonObject toolDeleteGroup(const QJsonObject &args);
  QJsonObject toolGetPlan(const QJsonObject &args);
  QJsonObject toolBuildPlan(const QJsonObject &spec);
  QJsonObject toolInsertBlock(const QJsonObject &args);
  QJsonObject toolCreateCore(const QJsonObject &args);
  QJsonObject toolSetDrawingScale(const QJsonObject &args);
  QJsonObject toolNewDrawing();
  QJsonObject toolOpenDrawing(const QJsonObject &args);
  QJsonObject toolUndo();

  // ── 헬퍼 ──
  static QJsonObject fail(const QString &message);
  static bool readPoint(const QJsonValue &v, QPointF &out);
  MyGraphicsScene *requireScene(QJsonObject &errorOut) const;
  Layer *resolveLayer(const QJsonObject &args, QJsonObject &errorOut) const;
  QPen penForLayer(const Layer *layer) const;
  QJsonObject placeItem(MyGraphicsScene *scene, QGraphicsItem *item,
                        const Layer *layer);
  bool readWallOpening(const QJsonObject &args, QPointF &p1, QPointF &p2,
                       QPointF &p3, double &thickness, QString &sideUsed,
                       QJsonObject &errorOut) const;
  bool applyElevatorSpec(const QJsonObject &args, ElevatorParams &params,
                         ElevatorSpec *specOut, QJsonObject &errorOut) const;
  QJsonObject openingArgs(const QPointF &centerPt, const QPointF &run,
                          const QPointF &dir, double thickness,
                          double width) const;
  QString blocksRoot() const;
  QString resolveBlockCategory(const QString &category,
                               QStringList *allCategories) const;
  QStringList blockNames(const QString &category) const;
  Layer *ensureLayer(const QString &name, bool *created);
  bool placeText(MyGraphicsScene *scene, const QString &text,
                 const QPointF &center, double height);
  void drawWallChain(const QList<QPointF> &points, bool closed,
                     const QPointF *extFirst = nullptr,
                     const QPointF *extLast = nullptr);
  QSet<QGraphicsItem *> snapshotItems(MyGraphicsScene *scene) const;
  int tagNewItems(MyGraphicsScene *scene, const QSet<QGraphicsItem *> &before,
                  const QString &group);
  QList<QGraphicsItem *> collectGroupItems(MyGraphicsScene *scene,
                                           const QString &group) const;
  int removeGroupItems(MyGraphicsScene *scene, const QString &group);
  QRectF blockBounds(const QString &category, const QString &name) const;
  QJsonObject placeFurniture(const QJsonObject &f, double dx, double dy,
                             const QString &defaultLayer);
  QJsonObject newItemsResult(MyGraphicsScene *scene,
                             const QSet<QGraphicsItem *> &before,
                             const QString &emptyMessage);
  int idForItem(QGraphicsItem *item);
  QJsonObject describeItem(QGraphicsItem *item);

  IAppContext *m_ctx;
  QLocalServer *m_server;
  QString m_serverName;
  QHash<QLocalSocket *, QByteArray> m_buffers;

  // 세션 내 객체 핸들 — 객체의 QGraphicsItem::data 에 저장 (도면 파일에는 저장되지 않음)
  int m_nextId = 1;
  int m_shotCounter = 0;
  QString m_activeGroup;                 // set_group 으로 켠 자동 이름표
  QHash<QString, QJsonObject> m_plans;   // build_plan 그룹별 마지막 스펙 (메모리만)
};

#endif // AIBRIDGESERVER_H
