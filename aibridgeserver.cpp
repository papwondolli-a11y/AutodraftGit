#include "aibridgeserver.h"
#include "blockitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "sceneprinter.h"
#include "twolinetool.h"
#include "multilineraycastlcornertool.h"
#include "multilineraycastcornertool.h"
#include "multilineccornertool.h"
#include "myline.h"
#include "stairplantool.h"
#include "elevatortool.h"
#include "drawsubwindow.h"
#include "filecommands.h"
#include "filemanager.h"
#include "lineardimensionitem.h"
#include "radialdimensionitem.h"
#include "circleitem.h"
#include "arcitem.h"
#include "abstracttool.h"
#include "dimlineartool.h"
#include "lineardimensiontool.h"
#include "radialdimensiontool.h"
#include "angulardimensiontool.h"
#include "angulardimensiontoolA.h"
#include "baselinedimensiontool.h"
#include "continuedimensiontool.h"
#include "autocontinuousdimtool.h"
#include "leadertool.h"
#include "holecallouttool.h"
#include "halfdimensiontool.h"
#include "areadimensiontool.h"
#include "dimleadertool.h"
#include "dimmovetexttool.h"
#include "dimmergetool.h"
#include "dimsplittool.h"
#include "gdtitem.h"
#include "datumitem.h"
#include "mymath.h"
#include "textitem.h"
#include "boxtextutil.h"
#include "tooldefaults.h"
#include "cadutils.h"
#include "hatchitem.h"
#include "hatchpatternmanager.h"
#include "advancedboundaryfinder.h"
#include "stylemanager.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "plandoortool.h"
#include "planwindowtool.h"
#include "plandoordoubletool.h"
#include "plandoorbiswingtool.h"
#include "plandoordoublebiswingtool.h"
#include "planrevolvingdoortool.h"
#include "plandoorsimpletool.h"

#include <QCollator>
#include <algorithm>
#include <utility>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPainter>
#include <QGraphicsItem>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPen>
#include <QPointer>
#include <QSet>
#include <QUndoStack>

namespace {
const int kAiHandleDataKey = 0x41490001; // QGraphicsItem::data 키 (객체 핸들 ID)
const int kGroupDataKey = 0x41490002;    // QGraphicsItem::data 키 (그룹 이름표)
const int kDefaultEntityLimit = 200;
const int kMaxEntityLimit = 1000;

// [x, y] 점 하나를 JSON 배열에 추가한다 (다중선 points 만들기)
void addPt(QJsonArray &arr, double x, double y) {
  QJsonArray p;
  p.append(x);
  p.append(y);
  arr.append(p);
}

// 코너 도구 인자(clickA, clickB) 만들기
QJsonObject cornerArgs(double ax, double ay, double bx, double by) {
  QJsonArray a;
  a.append(ax);
  a.append(ay);
  QJsonArray b;
  b.append(bx);
  b.append(by);
  QJsonObject o;
  o.insert(QStringLiteral("clickA"), a);
  o.insert(QStringLiteral("clickB"), b);
  return o;
}

// 이름 정렬: ft2 < ft10 처럼 숫자 크기 순서로 비교
bool naturalLess(const QString &a, const QString &b) {
  static QCollator collator;
  static bool ready = false;
  if (!ready) {
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    ready = true;
  }
  return collator.compare(a, b) < 0;
}

// 호출 동안만 스냅(객체/그리드/직교 보정)을 꺼서 준 좌표가 그대로 쓰이게 한다
struct SnapSuspender {
  SnapManager *m_snap;
  bool m_prev;
  explicit SnapSuspender(SnapManager *snap) : m_snap(snap), m_prev(false) {
    if (m_snap) {
      m_prev = m_snap->isSuspended();
      m_snap->setSuspended(true);
    }
  }
  ~SnapSuspender() {
    if (m_snap)
      m_snap->setSuspended(m_prev);
  }
};

// 호출 동안만 화면 색상 그대로 그리게 한다 (QImage 렌더링에서 밝은 색이 검정으로 바뀌는 인쇄 규칙 해제)
struct ScreenCaptureGuard {
  ScreenCaptureGuard() { ScenePrinter::setScreenCaptureMode(true); }
  ~ScreenCaptureGuard() { ScenePrinter::setScreenCaptureMode(false); }
};
} // namespace

AiBridgeServer::AiBridgeServer(IAppContext *ctx, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_server(new QLocalServer(this)) {
  connect(m_server, &QLocalServer::newConnection, this,
          &AiBridgeServer::onNewConnection);
}

bool AiBridgeServer::start(const QString &serverName) {
  m_serverName = serverName;
  // 이전 실행이 비정상 종료되어 남은 소켓 정리
  QLocalServer::removeServer(serverName);
  m_server->setSocketOptions(QLocalServer::UserAccessOption);
  return m_server->listen(serverName);
}

void AiBridgeServer::stop() {
  if (m_server->isListening())
    m_server->close();

  // 이미 연결된 클라이언트를 끊는다. disconnected 시그널이 m_buffers 를 정리하므로
  // 키 목록을 복사해서 순회한다.
  const QList<QLocalSocket *> sockets = m_buffers.keys();
  for (int i = 0; i < sockets.size(); ++i)
    sockets.at(i)->disconnectFromServer();
}

bool AiBridgeServer::isListening() const { return m_server->isListening(); }

void AiBridgeServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    QLocalSocket *socket = m_server->nextPendingConnection();
    m_buffers.insert(socket, QByteArray());
    connect(socket, &QLocalSocket::readyRead, this,
            &AiBridgeServer::onReadyRead);
    connect(socket, &QLocalSocket::disconnected, this,
            &AiBridgeServer::onDisconnected);
  }
}

void AiBridgeServer::onDisconnected() {
  QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
  if (!socket)
    return;
  m_buffers.remove(socket);
  socket->deleteLater();
}

void AiBridgeServer::onReadyRead() {
  QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
  if (!socket || !m_buffers.contains(socket))
    return;

  QByteArray &buffer = m_buffers[socket];
  buffer.append(socket->readAll());

  int newline = buffer.indexOf('\n');
  while (newline >= 0) {
    const QByteArray line = buffer.left(newline).trimmed();
    buffer.remove(0, newline + 1);

    if (!line.isEmpty()) {
      QJsonParseError parseError;
      const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
      QJsonObject response;
      if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        response = fail(QStringLiteral("요청이 올바른 JSON 객체가 아닙니다: %1")
                            .arg(parseError.errorString()));
      } else {
        response = handleRequest(doc.object());
        if (doc.object().contains(QStringLiteral("id")))
          response.insert(QStringLiteral("id"),
                          doc.object().value(QStringLiteral("id")));
      }
      socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) +
                    '\n');
      socket->flush();
    }
    newline = buffer.indexOf('\n');
  }
}

// 요청 처리: create_* / insert_block 은 set_group 으로 켠 그룹 이름표를 자동으로 붙인다
QJsonObject AiBridgeServer::handleRequest(const QJsonObject &req) {
  const QString tool = req.value(QStringLiteral("tool")).toString();
  MyGraphicsScene *scene = m_ctx ? m_ctx->activeScene() : nullptr;
  const bool taggable = !m_activeGroup.isEmpty() && scene &&
                        (tool.startsWith(QLatin1String("create_")) ||
                         tool == QLatin1String("insert_block"));
  QSet<QGraphicsItem *> before;
  if (taggable)
    before = snapshotItems(scene);
  const QJsonObject result = dispatchTool(req);
  if (taggable && result.value(QStringLiteral("ok")).toBool())
    tagNewItems(scene, before, m_activeGroup);
  return result;
}

QJsonObject AiBridgeServer::dispatchTool(const QJsonObject &req) {
  const QString tool = req.value(QStringLiteral("tool")).toString();
  const QJsonObject args = req.value(QStringLiteral("args")).toObject();

  if (tool == QLatin1String("ping"))
    return toolPing();
  if (tool == QLatin1String("get_layers"))
    return toolGetLayers();
  if (tool == QLatin1String("get_entities"))
    return toolGetEntities(args);
  if (tool == QLatin1String("create_line"))
    return toolCreateLine(args);
  if (tool == QLatin1String("create_circle"))
    return toolCreateCircle(args);
  if (tool == QLatin1String("create_multiline"))
    return toolCreateMultiline(args);
  if (tool == QLatin1String("multiline_lcorner"))
    return toolMultilineCorner(args, 'L');
  if (tool == QLatin1String("multiline_tcorner"))
    return toolMultilineCorner(args, 'T');
  if (tool == QLatin1String("multiline_ccorner"))
    return toolMultilineCorner(args, 'C');
  if (tool == QLatin1String("create_stair"))
    return toolCreateStair(args);
  if (tool == QLatin1String("create_elevator"))
    return toolCreateElevator(args);
  if (tool == QLatin1String("create_door"))
    return toolCreateDoor(args);
  if (tool == QLatin1String("create_window"))
    return toolCreateWindow(args);
  if (tool == QLatin1String("create_room"))
    return toolCreateRoom(args);
  if (tool == QLatin1String("create_core"))
    return toolCreateCore(args);
  if (tool == QLatin1String("set_drawing_scale"))
    return toolSetDrawingScale(args);
  if (tool == QLatin1String("create_dimension"))
    return toolCreateDimension(args);
  if (tool == QLatin1String("create_circle_dimension"))
    return toolCreateCircleDimension(args);
  if (tool == QLatin1String("run_dimension_tool"))
    return toolRunDimensionTool(args);
  if (tool == QLatin1String("merge_dimensions"))
    return toolMergeDimensions(args);
  if (tool == QLatin1String("split_dimension"))
    return toolSplitDimension(args);
  if (tool == QLatin1String("create_gdt"))
    return toolCreateGdt(args);
  if (tool == QLatin1String("create_datum"))
    return toolCreateDatum(args);
  if (tool == QLatin1String("edit_gdt"))
    return toolEditGdt(args);
  if (tool == QLatin1String("edit_entity"))
    return toolEditEntity(args);
  if (tool == QLatin1String("dimension_style"))
    return toolDimensionStyle(args);
  if (tool == QLatin1String("create_box_text"))
    return toolCreateBoxText(args);
  if (tool == QLatin1String("create_hatch"))
    return toolCreateHatch(args);
  if (tool == QLatin1String("create_layer"))
    return toolCreateLayer(args);
  if (tool == QLatin1String("list_hatch_patterns"))
    return toolListHatchPatterns(args);
  if (tool == QLatin1String("list_blocks"))
    return toolListBlocks(args);
  if (tool == QLatin1String("insert_block"))
    return toolInsertBlock(args);
  if (tool == QLatin1String("screenshot"))
    return toolScreenshot(args);
  if (tool == QLatin1String("build_plan"))
    return toolBuildPlan(args);
  if (tool == QLatin1String("set_group"))
    return toolSetGroup(args);
  if (tool == QLatin1String("delete_group"))
    return toolDeleteGroup(args);
  if (tool == QLatin1String("get_plan"))
    return toolGetPlan(args);
  if (tool == QLatin1String("open_drawing"))
    return toolOpenDrawing(args);
  if (tool == QLatin1String("new_drawing"))
    return toolNewDrawing();
  if (tool == QLatin1String("undo"))
    return toolUndo();

  return fail(QStringLiteral("알 수 없는 도구입니다: '%1'. 사용 가능: ping, "
                             "get_layers, get_entities, create_line, "
                             "create_circle, create_multiline, multiline_lcorner, multiline_tcorner, multiline_ccorner, create_stair, create_elevator, create_door, create_window, create_room, create_core, set_drawing_scale, list_blocks, insert_block, create_dimension, build_plan, set_group, delete_group, get_plan, screenshot, new_drawing, open_drawing, undo")
                  .arg(tool));
}

// ─────────────────────────────────────────────────────────────────────────────
// 도구
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject AiBridgeServer::toolPing() {
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("app"), QStringLiteral("AutoDraft"));
  MyGraphicsScene *scene = m_ctx ? m_ctx->activeScene() : nullptr;
  result.insert(QStringLiteral("hasActiveDrawing"), scene != nullptr);
  if (scene)
    result.insert(QStringLiteral("itemCount"), scene->items().size());
  if (scene)
    result.insert(QStringLiteral("drawingScale"), scene->drawingScale());
  return result;
}

QJsonObject AiBridgeServer::toolGetLayers() {
  if (!m_ctx || !m_ctx->layerManager())
    return fail(QStringLiteral("레이어 관리자를 사용할 수 없습니다."));

  QJsonArray layers;
  const QList<Layer> all = m_ctx->layerManager()->getAllLayers();
  for (int i = 0; i < all.size(); ++i) {
    const Layer &layer = all.at(i);
    QJsonObject o;
    o.insert(QStringLiteral("name"), layer.name());
    o.insert(QStringLiteral("color"), layer.color().name());
    o.insert(QStringLiteral("weight"), layer.weight());
    o.insert(QStringLiteral("linetype"), layer.linetype());
    o.insert(QStringLiteral("on"), layer.isLayerOn());
    o.insert(QStringLiteral("locked"), layer.isLayerLock());
    o.insert(QStringLiteral("current"), layer.isCurrent());
    layers.append(o);
  }
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("layers"), layers);
  return result;
}

QJsonObject AiBridgeServer::toolGetEntities(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  const QString layerFilter = args.value(QStringLiteral("layer")).toString();
  const QString typeFilter = args.value(QStringLiteral("type")).toString();
  const QString groupFilter = args.value(QStringLiteral("group")).toString();
  int limit = args.value(QStringLiteral("limit")).toInt(kDefaultEntityLimit);
  if (limit < 1)
    limit = 1;
  if (limit > kMaxEntityLimit)
    limit = kMaxEntityLimit;
  const int offset = qMax(0, args.value(QStringLiteral("offset")).toInt(0));

  QJsonArray entities;
  int matched = 0;
  const QList<QGraphicsItem *> items = scene->items();
  for (int i = 0; i < items.size(); ++i) {
    QGraphicsItem *item = items.at(i);
    if (dynamic_cast<BlockItem *>(item->parentItem()) != nullptr)
      continue;
    CadItem *cad = dynamic_cast<CadItem *>(item);
    if (!cad)
      continue;
    if (!layerFilter.isEmpty() && cad->layerName() != layerFilter)
      continue;
    if (!groupFilter.isEmpty() && item->data(kGroupDataKey).toString() != groupFilter)
      continue;
    if (!typeFilter.isEmpty() &&
        cad->getProperties().value(QStringLiteral("Type")).toString().compare(
            typeFilter, Qt::CaseInsensitive) != 0)
      continue;

    if (matched >= offset && entities.size() < limit)
      entities.append(describeItem(item));
    ++matched;
  }

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("total"), matched);
  result.insert(QStringLiteral("returned"), entities.size());
  result.insert(QStringLiteral("offset"), offset);
  result.insert(QStringLiteral("entities"), entities);
  if (matched > offset + entities.size())
    result.insert(QStringLiteral("note"),
                  QStringLiteral("결과가 더 있습니다. offset 을 %1 로 올려 다시 "
                                 "호출하거나 layer/type 으로 좁히세요.")
                      .arg(offset + entities.size()));
  return result;
}

QJsonObject AiBridgeServer::toolCreateLine(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF p1;
  QPointF p2;
  if (!readPoint(args.value(QStringLiteral("p1")), p1) ||
      !readPoint(args.value(QStringLiteral("p2")), p2))
    return fail(QStringLiteral("p1, p2 는 [x, y] 형식의 숫자 배열이어야 합니다. "
                               "예: {\"p1\":[0,0],\"p2\":[100,50]}"));
  if (QLineF(p1, p2).length() < 0.001)
    return fail(QStringLiteral("p1 과 p2 가 같은 점입니다. 서로 다른 두 점을 "
                               "지정하세요."));

  Layer *layer = resolveLayer(args, err);
  if (!layer)
    return err;

  QGraphicsItem *item =
      m_ctx->entityFactory()->createLine(QLineF(p1, p2), penForLayer(layer));
  return placeItem(scene, item, layer);
}

QJsonObject AiBridgeServer::toolCreateCircle(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF center;
  if (!readPoint(args.value(QStringLiteral("center")), center))
    return fail(QStringLiteral("center 는 [x, y] 형식의 숫자 배열이어야 합니다. "
                               "예: {\"center\":[0,0],\"radius\":50}"));
  const double radius = args.value(QStringLiteral("radius")).toDouble(0.0);
  if (radius <= 0.0)
    return fail(QStringLiteral("radius 는 0 보다 큰 숫자(mm)여야 합니다."));

  Layer *layer = resolveLayer(args, err);
  if (!layer)
    return err;

  QGraphicsItem *item = m_ctx->entityFactory()->createCircle(
      center, radius, penForLayer(layer));
  return placeItem(scene, item, layer);
}

QSet<QGraphicsItem *> AiBridgeServer::snapshotItems(MyGraphicsScene *scene) const {
  QSet<QGraphicsItem *> set;
  const QList<QGraphicsItem *> items = scene->items();
  for (int i = 0; i < items.size(); ++i)
    set.insert(items.at(i));
  return set;
}

// snapshotItems 이후 새로 생긴 CadItem 을 모아 응답 형태로 만든다
QJsonObject AiBridgeServer::newItemsResult(MyGraphicsScene *scene,
                                           const QSet<QGraphicsItem *> &before,
                                           const QString &emptyMessage) {
  QJsonArray created;
  int createdCount = 0;
  QMap<QString, int> perLayer;
  const QList<QGraphicsItem *> after = scene->items();
  for (int i = 0; i < after.size(); ++i) {
    QGraphicsItem *item = after.at(i);
    CadItem *cad = dynamic_cast<CadItem *>(item);
    if (before.contains(item) || !cad)
      continue;
    ++createdCount;
    perLayer[cad->layerName()] += 1;
    if (created.size() < 40)
      created.append(describeItem(item));
  }
  if (createdCount == 0)
    return fail(emptyMessage);

  QJsonObject layers;
  for (QMap<QString, int>::const_iterator it = perLayer.constBegin();
       it != perLayer.constEnd(); ++it)
    layers.insert(it.key(), it.value());

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("createdCount"), createdCount);
  result.insert(QStringLiteral("countByLayer"), layers);
  result.insert(QStringLiteral("entities"), created);
  if (createdCount > created.size())
    result.insert(QStringLiteral("note"),
                  QStringLiteral("생성 객체가 많아 앞 %1개만 표시했습니다. 전체는 "
                                 "get_entities 로 조회하세요.")
                      .arg(created.size()));
  return result;
}

// 계단평면(StairPlanTool). 설정창 없이 도면에 저장된 기본값을 불러와 넘긴 값만 덮어쓴다.
//   p1(UP), p2(DN) = 계단참 벽선의 두 모서리, toward = 계단이 뻗어 나갈 쪽의 한 점
QJsonObject AiBridgeServer::toolCreateStair(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF p1;
  QPointF p2;
  QPointF toward;
  if (!readPoint(args.value(QStringLiteral("p1")), p1) ||
      !readPoint(args.value(QStringLiteral("p2")), p2) ||
      !readPoint(args.value(QStringLiteral("toward")), toward))
    return fail(QStringLiteral("p1, p2, toward 는 모두 [x, y] 형식이어야 합니다. "
                               "p1/p2 는 계단참 벽선의 두 모서리(UP, DN), toward 는 "
                               "계단이 뻗어 나갈 쪽의 점입니다. 예: "
                               "{\"p1\":[0,0],\"p2\":[2400,0],\"toward\":[1200,-500]}"));
  const QLineF wall(p1, p2);
  if (wall.length() < 1.0)
    return fail(QStringLiteral("p1 과 p2 가 같은 점입니다. 계단참 벽선의 두 모서리를 "
                               "서로 다르게 지정하세요."));
  // toward 가 벽선 위에 놓이면 방향을 정할 수 없다
  const double side = (p2.x() - p1.x()) * (toward.y() - p1.y()) -
                      (p2.y() - p1.y()) * (toward.x() - p1.x());
  if (qAbs(side) / wall.length() < 1.0)
    return fail(QStringLiteral("toward 가 p1-p2 벽선 위에 있어 계단 방향을 정할 수 "
                               "없습니다. 벽선에서 떨어진 쪽의 점을 지정하세요."));

  StairPlanParams params = StairPlanTool::loadParams(scene);
  if (args.contains(QStringLiteral("type")))
    params.type = args.value(QStringLiteral("type")).toInt();
  if (args.contains(QStringLiteral("up")))
    params.up = args.value(QStringLiteral("up")).toInt();
  if (args.contains(QStringLiteral("down")))
    params.down = args.value(QStringLiteral("down")).toInt();
  if (args.contains(QStringLiteral("midSteps")))
    params.midSteps = args.value(QStringLiteral("midSteps")).toInt();
  if (args.contains(QStringLiteral("tread")))
    params.tread = args.value(QStringLiteral("tread")).toDouble();
  if (args.contains(QStringLiteral("treadLen")))
    params.treadLen = args.value(QStringLiteral("treadLen")).toDouble();
  if (args.contains(QStringLiteral("land")))
    params.land = args.value(QStringLiteral("land")).toDouble();
  if (args.contains(QStringLiteral("numbering")))
    params.numbering = args.value(QStringLiteral("numbering")).toBool();
  if (args.contains(QStringLiteral("nonslip")))
    params.nonslip = args.value(QStringLiteral("nonslip")).toBool();
  if (args.contains(QStringLiteral("layer")))
    params.layer = args.value(QStringLiteral("layer")).toString();
  if (args.contains(QStringLiteral("nonslipLayer")))
    params.nonslipLayer = args.value(QStringLiteral("nonslipLayer")).toString();

  if (params.type != 1 && params.type != 2)
    return fail(QStringLiteral("type 은 1(기본) 또는 2(계단참에도 중간계단) 입니다."));
  if (params.up < 0 || params.down < 0 || (params.up == 0 && params.down == 0))
    return fail(QStringLiteral("up/down(계단 수)은 0 이상이고 둘 다 0 일 수는 "
                               "없습니다. 한쪽만 0 이면 직진 단일 계단, 둘 다 1 이상이면 "
                               "꺾임(U자) 계단입니다."));
  if (params.tread <= 0.0 || params.land < 0.0)
    return fail(QStringLiteral("tread(디딤판 폭)는 0 보다 커야 하고 land(계단참 "
                               "길이)는 0 이상이어야 합니다."));
  if (params.layer.isEmpty())
    params.layer = QStringLiteral("st");
  if (params.nonslipLayer.isEmpty())
    params.nonslipLayer = params.layer;

  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  {
    SnapSuspender guard(m_ctx->snapManager());
    StairPlanTool *tool = new StairPlanTool(params);
    tool->setHeadless(true);
    m_ctx->startTool(tool);
    tool->mousePress(p1, Qt::LeftButton);
    tool->mousePress(p2, Qt::LeftButton);
    tool->mousePress(toward, Qt::LeftButton);
  }

  QJsonObject result = newItemsResult(
      scene, before,
      QStringLiteral("계단이 만들어지지 않았습니다. 계단 수·벽선 길이를 확인하세요."));
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  QJsonObject used;
  used.insert(QStringLiteral("type"), params.type);
  used.insert(QStringLiteral("up"), params.up);
  used.insert(QStringLiteral("down"), params.down);
  used.insert(QStringLiteral("midSteps"), params.midSteps);
  used.insert(QStringLiteral("tread"), params.tread);
  used.insert(QStringLiteral("treadLen"), params.treadLen);
  used.insert(QStringLiteral("land"), params.land);
  used.insert(QStringLiteral("numbering"), params.numbering);
  used.insert(QStringLiteral("nonslip"), params.nonslip);
  used.insert(QStringLiteral("layer"), params.layer);
  used.insert(QStringLiteral("nonslipLayer"), params.nonslipLayer);
  result.insert(QStringLiteral("paramsUsed"), used);
  return result;
}

// 엘리베이터 평면(ElevatorTool). 설정창 없이 저장된 기본값 + 인승 규격(JSON) 또는 직접 치수.
//   p1 = 승강로 문 쪽 모서리, p2 = 대각 모서리
QJsonObject AiBridgeServer::toolCreateElevator(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF p1;
  QPointF p2;
  if (!readPoint(args.value(QStringLiteral("p1")), p1) ||
      !readPoint(args.value(QStringLiteral("p2")), p2))
    return fail(QStringLiteral("p1, p2 는 승강로의 대각 모서리 [x, y] 입니다. p1 이 있는 "
                               "쪽 벽에 승강기 문이 생깁니다. 예: "
                               "{\"p1\":[0,0],\"p2\":[2100,2300]}"));
  const double shaftW = qAbs(p2.x() - p1.x());
  const double shaftD = qAbs(p2.y() - p1.y());
  if (shaftW < 1.0 || shaftD < 1.0)
    return fail(QStringLiteral("p1 과 p2 는 x, y 가 모두 달라야 합니다(직사각형 "
                               "승강로)."));

  ElevatorParams params = ElevatorTool::loadParams(scene);

  // 1) 인승 규격이 주어지면 규격 JSON 에서 카 치수를 가져온다
  if (!applyElevatorSpec(args, params, nullptr, err))
    return err;

  // 2) 직접 지정한 값이 있으면 덮어쓴다
  if (args.contains(QStringLiteral("carWidth")))
    params.carWidth = args.value(QStringLiteral("carWidth")).toDouble();
  if (args.contains(QStringLiteral("carDepth")))
    params.carDepth = args.value(QStringLiteral("carDepth")).toDouble();
  if (args.contains(QStringLiteral("doorSize")))
    params.doorSize = args.value(QStringLiteral("doorSize")).toDouble();
  if (args.contains(QStringLiteral("drawWalls")))
    params.drawWalls = args.value(QStringLiteral("drawWalls")).toBool();
  if (args.contains(QStringLiteral("drawDims")))
    params.drawDims = args.value(QStringLiteral("drawDims")).toBool();

  if (params.carWidth <= 0.0 || params.carDepth <= 0.0 || params.doorSize <= 0.0)
    return fail(QStringLiteral("carWidth, carDepth, doorSize 는 0 보다 커야 합니다."));

  // 승강로 안에 들어가는지 먼저 검사한다 (도구는 경고만 하고 그려 버리므로)
  const QString fit = ElevatorTool::checkFit(shaftW, shaftD, params);
  if (!fit.isEmpty())
    return fail(QStringLiteral("승강로에 맞지 않습니다: %1 승강로(%2 x %3)를 키우거나 "
                               "더 작은 인승/치수를 선택하세요.")
                    .arg(fit)
                    .arg(shaftW, 0, 'f', 0)
                    .arg(shaftD, 0, 'f', 0));

  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  {
    SnapSuspender guard(m_ctx->snapManager());
    ElevatorTool *tool = new ElevatorTool(params);
    tool->setHeadless(true);
    m_ctx->startTool(tool);
    tool->mousePress(p1, Qt::LeftButton);
    tool->mousePress(p2, Qt::LeftButton);
  }

  QJsonObject result = newItemsResult(
      scene, before,
      QStringLiteral("엘리베이터가 만들어지지 않았습니다. p1, p2 를 확인하세요."));
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  QJsonObject used;
  used.insert(QStringLiteral("maker"), params.maker);
  used.insert(QStringLiteral("carWidth"), params.carWidth);
  used.insert(QStringLiteral("carDepth"), params.carDepth);
  used.insert(QStringLiteral("doorSize"), params.doorSize);
  used.insert(QStringLiteral("compactCounterweight"), params.compactCw);
  used.insert(QStringLiteral("drawWalls"), params.drawWalls);
  used.insert(QStringLiteral("drawDims"), params.drawDims);
  used.insert(QStringLiteral("shaftWidth"), shaftW);
  used.insert(QStringLiteral("shaftDepth"), shaftD);
  result.insert(QStringLiteral("paramsUsed"), used);
  return result;
}

// 문/창문 공통 입력 해석.
//   hinge  = 벽면 위 시작점(P1, 문은 경첩 쪽 끝)
//   toward = 문/창이 진행하는 방향의 점 (hinge 에서 toward 쪽으로 뻗는다)
//   side   = "left"(진행 방향 기준 왼쪽, 기본) | "right" — 벽 두께와 문이 열리는 쪽
//   wallThickness = 벽 두께(mm). 생략하면 다중선 기본 간격
// 도구는 P1(벽면 위) → P2(반대쪽 벽면, 두께+열림 방향) → P3(진행 방향) 세 점을 받는다.
bool AiBridgeServer::readWallOpening(const QJsonObject &args, QPointF &p1,
                                     QPointF &p2, QPointF &p3,
                                     double &thickness, QString &sideUsed,
                                     QJsonObject &errorOut) const {
  QPointF hinge;
  QPointF toward;
  if (!readPoint(args.value(QStringLiteral("hinge")), hinge) ||
      !readPoint(args.value(QStringLiteral("toward")), toward)) {
    errorOut = fail(QStringLiteral(
        "hinge, toward 는 [x, y] 형식이어야 합니다. hinge 는 벽면 위의 시작점, "
        "toward 는 벽을 따라 진행하는 방향의 점입니다. 예: "
        "{\"hinge\":[1000,100],\"toward\":[2000,100]}"));
    return false;
  }
  const QLineF run(hinge, toward);
  if (run.length() < 1.0) {
    errorOut = fail(QStringLiteral("hinge 와 toward 가 같은 점입니다. 벽을 따라 "
                                   "떨어진 점을 지정하세요."));
    return false;
  }
  sideUsed = args.value(QStringLiteral("side")).toString(QStringLiteral("left"));
  if (sideUsed != QLatin1String("left") && sideUsed != QLatin1String("right")) {
    errorOut = fail(QStringLiteral("side 는 \"left\" 또는 \"right\" 입니다."));
    return false;
  }
  thickness = args.contains(QStringLiteral("wallThickness"))
                  ? args.value(QStringLiteral("wallThickness")).toDouble()
                  : TwoLineTool::globalSpacing();
  if (thickness < 10.0) {
    errorOut = fail(QStringLiteral("wallThickness(벽 두께, mm)는 10 이상이어야 "
                                   "합니다."));
    return false;
  }

  const double ux = (toward.x() - hinge.x()) / run.length();
  const double uy = (toward.y() - hinge.y()) / run.length();
  const double sign = (sideUsed == QLatin1String("left")) ? 1.0 : -1.0;
  // 진행 방향을 90도 반시계 회전한 법선이 "왼쪽"
  p1 = hinge;
  p2 = QPointF(hinge.x() - uy * thickness * sign,
               hinge.y() + ux * thickness * sign);
  p3 = QPointF(hinge.x() + ux * 100.0, hinge.y() + uy * 100.0);
  return true;
}

// 평면도어 탭의 도어 종류. kind 이름 → 도구 계열 · 세부 형상
namespace {
enum DoorFamily {
  FamSingle,
  FamDouble,
  FamBiSwing,
  FamDoubleBiSwing,
  FamRevolving,
  FamSimple
};

struct DoorKindInfo {
  const char *name;
  DoorFamily family;
  int style;                 // PlanDoorTool / PlanDoorDoubleTool 의 doorStyle (1 = door-113 형상)
  SimpleDoorKind simpleKind; // FamSimple 일 때만 의미가 있다
};

const DoorKindInfo kDoorKinds[] = {
    {"single", FamSingle, 0, SimpleSingle},
    {"single113", FamSingle, 1, SimpleSingle},
    {"double", FamDouble, 0, SimpleSingle},
    {"double114", FamDouble, 1, SimpleSingle},
    {"biswing", FamBiSwing, 0, SimpleSingle},
    {"doublebiswing", FamDoubleBiSwing, 0, SimpleSingle},
    {"revolving", FamRevolving, 0, SimpleSingle},
    {"simple", FamSimple, 0, SimpleSingle},
    {"simpledouble", FamSimple, 0, SimpleDouble},
    {"simplebiswing", FamSimple, 0, SimpleBiSwing},
    {"simpledoublebiswing", FamSimple, 0, SimpleDoubleBiSwing}};

// 도구가 쓰는 정적 설정값 중 이번 호출에서 바꾼 것을 되돌리기 위한 보관소
struct DoorSettingsBackup {
  DoorFamily family = FamSingle;
  bool oldUse = false;
  double oldH = 0.0;
  double oldH1 = 0.0;
  double oldH2 = 0.0;
  int oldWings = 0;
};

const DoorKindInfo *findDoorKind(const QString &kind) {
  const int n = int(sizeof(kDoorKinds) / sizeof(kDoorKinds[0]));
  for (int i = 0; i < n; ++i) {
    if (kind == QLatin1String(kDoorKinds[i].name))
      return &kDoorKinds[i];
  }
  return nullptr;
}
} // namespace

// 평면 도어(평면도어 탭의 11종). 설정창·벽위치창 없이 도면 기본값 + 넘긴 값으로 실행한다.
// 벽(다중선)이 이미 있으면 도구가 개구부 트림과 마감선까지 사람이 그릴 때처럼 처리한다.
QJsonObject AiBridgeServer::toolCreateDoor(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF p1;
  QPointF p2;
  QPointF p3;
  double thickness = 0.0;
  QString side;
  if (!readWallOpening(args, p1, p2, p3, thickness, side, err))
    return err;

  const int style = args.value(QStringLiteral("style")).toInt(0);
  if (style != 0 && style != 1)
    return fail(QStringLiteral("style 은 0(기본 평면도어) 또는 1(30도 열린 문짝 + "
                               "호) 입니다."));

  // kind 가 없으면 기존 방식(style)으로 외여닫이 계열을 고른다
  QString kind = args.value(QStringLiteral("kind")).toString();
  if (kind.isEmpty())
    kind = QStringLiteral("single");
  if (style == 1 && kind == QLatin1String("single"))
    kind = QStringLiteral("single113");
  else if (style == 1 && kind == QLatin1String("double"))
    kind = QStringLiteral("double114");
  const DoorKindInfo *info = findDoorKind(kind);
  if (!info)
    return fail(QStringLiteral(
        "kind 는 single, single113, double, double114, biswing, doublebiswing, "
        "revolving, simple, simpledouble, simplebiswing, simpledoublebiswing 중 "
        "하나입니다."));
  const DoorFamily family = info->family;
  const bool twoLeaf = (family == FamDouble || family == FamDoubleBiSwing);

  const bool hasWidth = args.contains(QStringLiteral("width"));
  const double width = args.value(QStringLiteral("width")).toDouble();
  if (hasWidth && width < 100.0)
    return fail(QStringLiteral("width(문 폭, mm)는 100 이상이어야 합니다."));
  const bool hasW1 = args.contains(QStringLiteral("width1"));
  const bool hasW2 = args.contains(QStringLiteral("width2"));
  const double width1 = args.value(QStringLiteral("width1")).toDouble();
  const double width2 = args.value(QStringLiteral("width2")).toDouble();
  if ((hasW1 || hasW2) && !twoLeaf)
    return fail(QStringLiteral("width1/width2 는 double, double114, doublebiswing "
                               "에서만 쓸 수 있습니다. 나머지는 width 를 쓰세요."));
  if ((hasW1 && width1 < 100.0) || (hasW2 && width2 < 100.0))
    return fail(QStringLiteral("width1/width2(문1·문2 폭, mm)는 100 이상이어야 "
                               "합니다."));
  if (hasWidth && (hasW1 || hasW2))
    return fail(QStringLiteral("width(전체 폭)와 width1/width2 는 함께 쓰지 "
                               "마세요."));
  const bool hasWings = args.contains(QStringLiteral("wings"));
  const int wings = args.value(QStringLiteral("wings")).toInt(4);
  if (hasWings && family != FamRevolving)
    return fail(QStringLiteral("wings(날개 수)는 kind=revolving 에서만 쓸 수 "
                               "있습니다."));
  if (hasWings && wings != 3 && wings != 4)
    return fail(QStringLiteral("wings(날개 수)는 3 또는 4 입니다."));

  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  QJsonObject used;
  {
    SnapSuspender guard(m_ctx->snapManager());
    AbstractTool *tool = nullptr;
    DoorSettingsBackup bk;
    bk.family = family;

    switch (family) {
    case FamSingle: {
      tool = new PlanDoorTool(nullptr, info->style);
      m_ctx->startTool(tool); // 이때 도면 기본값이 로드된다
      bk.oldH = PlanDoorTool::globalH();
      bk.oldUse = PlanDoorTool::useWallThickness();
      if (hasWidth)
        PlanDoorTool::setGlobalH(width);
      // 벽두께 모드: P2 로 두께를 정하므로 벽위치 선택창이 뜨지 않는다
      PlanDoorTool::setUseWallThickness(true);
      used.insert(QStringLiteral("width"), PlanDoorTool::globalH());
      break;
    }
    case FamDouble: {
      tool = new PlanDoorDoubleTool(nullptr, info->style);
      m_ctx->startTool(tool);
      bk.oldH1 = PlanDoorDoubleTool::globalH1();
      bk.oldH2 = PlanDoorDoubleTool::globalH2();
      bk.oldUse = PlanDoorDoubleTool::useWallThickness();
      if (hasWidth) {
        PlanDoorDoubleTool::setGlobalH1(width * 0.5);
        PlanDoorDoubleTool::setGlobalH2(width * 0.5);
      }
      if (hasW1)
        PlanDoorDoubleTool::setGlobalH1(width1);
      if (hasW2)
        PlanDoorDoubleTool::setGlobalH2(width2);
      PlanDoorDoubleTool::setUseWallThickness(true);
      used.insert(QStringLiteral("width1"), PlanDoorDoubleTool::globalH1());
      used.insert(QStringLiteral("width2"), PlanDoorDoubleTool::globalH2());
      used.insert(QStringLiteral("width"),
                  PlanDoorDoubleTool::globalH1() + PlanDoorDoubleTool::globalH2());
      break;
    }
    case FamBiSwing: {
      tool = new PlanDoorBiSwingTool(nullptr);
      m_ctx->startTool(tool);
      bk.oldH = PlanDoorBiSwingTool::globalH();
      bk.oldUse = PlanDoorBiSwingTool::useWallThickness();
      if (hasWidth)
        PlanDoorBiSwingTool::setGlobalH(width);
      PlanDoorBiSwingTool::setUseWallThickness(true);
      used.insert(QStringLiteral("width"), PlanDoorBiSwingTool::globalH());
      break;
    }
    case FamDoubleBiSwing: {
      tool = new PlanDoorDoubleBiSwingTool(nullptr);
      m_ctx->startTool(tool);
      bk.oldH1 = PlanDoorDoubleBiSwingTool::globalH1();
      bk.oldH2 = PlanDoorDoubleBiSwingTool::globalH2();
      bk.oldUse = PlanDoorDoubleBiSwingTool::useWallThickness();
      if (hasWidth) {
        PlanDoorDoubleBiSwingTool::setGlobalH1(width * 0.5);
        PlanDoorDoubleBiSwingTool::setGlobalH2(width * 0.5);
      }
      if (hasW1)
        PlanDoorDoubleBiSwingTool::setGlobalH1(width1);
      if (hasW2)
        PlanDoorDoubleBiSwingTool::setGlobalH2(width2);
      PlanDoorDoubleBiSwingTool::setUseWallThickness(true);
      used.insert(QStringLiteral("width1"), PlanDoorDoubleBiSwingTool::globalH1());
      used.insert(QStringLiteral("width2"), PlanDoorDoubleBiSwingTool::globalH2());
      used.insert(QStringLiteral("width"), PlanDoorDoubleBiSwingTool::globalH1() +
                                               PlanDoorDoubleBiSwingTool::globalH2());
      break;
    }
    case FamRevolving: {
      tool = new PlanRevolvingDoorTool(nullptr);
      m_ctx->startTool(tool);
      bk.oldH = PlanRevolvingDoorTool::globalH();
      bk.oldWings = PlanRevolvingDoorTool::wingCount();
      bk.oldUse = PlanRevolvingDoorTool::useWallThickness();
      if (hasWidth)
        PlanRevolvingDoorTool::setGlobalH(width);
      if (hasWings)
        PlanRevolvingDoorTool::setWingCount(wings);
      PlanRevolvingDoorTool::setUseWallThickness(true);
      used.insert(QStringLiteral("width"), PlanRevolvingDoorTool::globalH());
      used.insert(QStringLiteral("wings"), PlanRevolvingDoorTool::wingCount());
      break;
    }
    case FamSimple: {
      tool = new PlanDoorSimpleTool(info->simpleKind, nullptr);
      m_ctx->startTool(tool);
      bk.oldH = PlanDoorSimpleTool::globalH();
      if (hasWidth)
        PlanDoorSimpleTool::setGlobalH(width);
      used.insert(QStringLiteral("width"), PlanDoorSimpleTool::globalH());
      break;
    }
    }

    tool->mousePress(p1, Qt::LeftButton);
    tool->mousePress(p2, Qt::LeftButton);
    tool->mousePress(p3, Qt::LeftButton);

    // 이번 호출에서 바꾼 도구 설정을 원래대로 되돌린다
    switch (bk.family) {
    case FamSingle:
      PlanDoorTool::setGlobalH(bk.oldH);
      PlanDoorTool::setUseWallThickness(bk.oldUse);
      break;
    case FamDouble:
      PlanDoorDoubleTool::setGlobalH1(bk.oldH1);
      PlanDoorDoubleTool::setGlobalH2(bk.oldH2);
      PlanDoorDoubleTool::setUseWallThickness(bk.oldUse);
      break;
    case FamBiSwing:
      PlanDoorBiSwingTool::setGlobalH(bk.oldH);
      PlanDoorBiSwingTool::setUseWallThickness(bk.oldUse);
      break;
    case FamDoubleBiSwing:
      PlanDoorDoubleBiSwingTool::setGlobalH1(bk.oldH1);
      PlanDoorDoubleBiSwingTool::setGlobalH2(bk.oldH2);
      PlanDoorDoubleBiSwingTool::setUseWallThickness(bk.oldUse);
      break;
    case FamRevolving:
      PlanRevolvingDoorTool::setGlobalH(bk.oldH);
      PlanRevolvingDoorTool::setWingCount(bk.oldWings);
      PlanRevolvingDoorTool::setUseWallThickness(bk.oldUse);
      break;
    case FamSimple:
      PlanDoorSimpleTool::setGlobalH(bk.oldH);
      break;
    }
    m_ctx->finishCurrentTool(); // 도구는 연속 배치 상태로 남으므로 종료
  }

  QJsonObject result = newItemsResult(
      scene, before,
      QStringLiteral("문이 만들어지지 않았습니다. hinge/toward/wallThickness 를 "
                     "확인하세요."));
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  used.insert(QStringLiteral("kind"), kind);
  used.insert(QStringLiteral("wallThickness"), thickness);
  used.insert(QStringLiteral("side"), side);
  used.insert(QStringLiteral("style"), info->style);
  result.insert(QStringLiteral("paramsUsed"), used);
  return result;
}

// 평면 창문(평면 창문 탭의 7종). 문과 같은 입력.
//   kind="sliding"(기본) 2/3/4미서기 — sashes 로 짝 수, double 로 단창/이중창
//   kind="top"          2/3/4상미서기(상단부만) — sashes 로 짝 수
//   kind="front"        전면창(4짝 상단부 옆 연결형) — 2짝 기준 고정
QJsonObject AiBridgeServer::toolCreateWindow(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF p1;
  QPointF p2;
  QPointF p3;
  double thickness = 0.0;
  QString side;
  if (!readWallOpening(args, p1, p2, p3, thickness, side, err))
    return err;

  const int sashes = args.value(QStringLiteral("sashes")).toInt(2);
  if (sashes < 2 || sashes > 4)
    return fail(QStringLiteral("sashes(창짝 수)는 2, 3, 4 중 하나입니다."));
  const QString kind = args.value(QStringLiteral("kind")).toString(QStringLiteral("sliding"));
  int variant = 1; // 1 = 미서기(단창/이중창은 double 설정이 정한다)
  if (kind == QLatin1String("top"))
    variant = 3;
  else if (kind == QLatin1String("front"))
    variant = 4;
  else if (kind != QLatin1String("sliding"))
    return fail(QStringLiteral("kind 는 sliding(미서기), top(상미서기), front(전면창) 중 "
                               "하나입니다."));
  if (variant == 4 && args.contains(QStringLiteral("sashes")) && sashes != 2)
    return fail(QStringLiteral("kind=front(전면창)는 sashes 를 지정하지 않습니다(2짝 기준)."));
  if (variant != 1 && args.contains(QStringLiteral("double")))
    return fail(QStringLiteral("double(단창/이중창)은 kind=sliding 에서만 쓸 수 있습니다."));
  const bool hasWidth = args.contains(QStringLiteral("width"));
  const double width = args.value(QStringLiteral("width")).toDouble();
  if (hasWidth && width < 100.0)
    return fail(QStringLiteral("width(창 폭, mm)는 100 이상이어야 합니다."));
  const bool hasDouble = args.contains(QStringLiteral("double"));

  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  double usedWidth = 0.0;
  bool usedDouble = false;
  {
    SnapSuspender guard(m_ctx->snapManager());
    PlanWindowTool *tool = new PlanWindowTool(nullptr, variant, sashes);
    m_ctx->startTool(tool); // 이때 도면 기본값이 로드된다

    const bool oldUse = PlanWindowTool::useWallThickness();
    const bool oldDouble = PlanWindowTool::sashDouble();
    const double oldW = sashes == 4   ? PlanWindowTool::globalW4()
                        : sashes == 3 ? PlanWindowTool::globalW3()
                                      : PlanWindowTool::globalW();
    if (hasWidth) {
      if (sashes == 4)
        PlanWindowTool::setGlobalW4(width);
      else if (sashes == 3)
        PlanWindowTool::setGlobalW3(width);
      else
        PlanWindowTool::setGlobalW(width);
    }
    if (hasDouble)
      PlanWindowTool::setSashDouble(args.value(QStringLiteral("double")).toBool());
    PlanWindowTool::setUseWallThickness(true);
    tool->refreshVariant();
    usedWidth = hasWidth ? width : oldW;
    usedDouble = PlanWindowTool::sashDouble();

    tool->mousePress(p1, Qt::LeftButton);
    tool->mousePress(p2, Qt::LeftButton);
    tool->mousePress(p3, Qt::LeftButton);

    PlanWindowTool::setUseWallThickness(oldUse);
    PlanWindowTool::setSashDouble(oldDouble);
    if (hasWidth) {
      if (sashes == 4)
        PlanWindowTool::setGlobalW4(oldW);
      else if (sashes == 3)
        PlanWindowTool::setGlobalW3(oldW);
      else
        PlanWindowTool::setGlobalW(oldW);
    }
    m_ctx->finishCurrentTool();
  }

  QJsonObject result = newItemsResult(
      scene, before,
      QStringLiteral("창문이 만들어지지 않았습니다. hinge/toward/wallThickness 를 "
                     "확인하세요."));
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  QJsonObject used;
  used.insert(QStringLiteral("width"), usedWidth);
  used.insert(QStringLiteral("kind"), kind);
  used.insert(QStringLiteral("sashes"), sashes);
  if (variant == 1)
    used.insert(QStringLiteral("double"), usedDouble);
  used.insert(QStringLiteral("wallThickness"), thickness);
  used.insert(QStringLiteral("side"), side);
  result.insert(QStringLiteral("paramsUsed"), used);
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 화면 확인용: 도면(또는 영역)을 PNG 로 저장하고 경로를 돌려준다. Y 위쪽이 +.
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject AiBridgeServer::toolScreenshot(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QRectF source;
  if (args.contains(QStringLiteral("region"))) {
    const QJsonArray r = args.value(QStringLiteral("region")).toArray();
    if (r.size() != 4)
      return fail(QStringLiteral("region 은 [x, y, 폭, 높이] 숫자 4개입니다."));
    source = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(),
                    r.at(3).toDouble());
  } else {
    // 내용이 있는 객체만 모아 범위를 계산 (레이어 루트 같은 빈 항목 제외)
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
      if (!dynamic_cast<CadItem *>(items.at(i)))
        continue;
      source = source.isNull() ? items.at(i)->sceneBoundingRect()
                               : source.united(items.at(i)->sceneBoundingRect());
    }
    if (source.isNull())
      return fail(QStringLiteral("도면에 객체가 없습니다."));
    const double pad = qMax(source.width(), source.height()) * 0.05 + 50.0;
    source.adjust(-pad, -pad, pad, pad);
  }
  if (source.width() < 1.0 || source.height() < 1.0)
    return fail(QStringLiteral("region 의 폭과 높이는 0 보다 커야 합니다."));

  int widthPx = args.value(QStringLiteral("widthPx")).toInt(1400);
  widthPx = qBound(200, widthPx, 3000);
  int heightPx = qRound(widthPx * source.height() / source.width());
  heightPx = qBound(100, heightPx, 3000);

  scene->clearOverlay(); // 스냅 마커·고스트 선 등 임시 표시는 제외
  QImage image(widthPx, heightPx, QImage::Format_ARGB32);
  image.fill(QColor(30, 30, 30));
  {
    ScreenCaptureGuard screenColors;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(0, heightPx);
    painter.scale(1.0, -1.0); // CAD 는 Y 위쪽이 +
    scene->render(&painter, QRectF(0, 0, widthPx, heightPx), source,
                  Qt::IgnoreAspectRatio);
  }

  // 영상·설명용 자막: caption 을 주면 아래(기본) 또는 위쪽에 반투명 띠와 함께 그린다
  const QString caption = args.value(QStringLiteral("caption")).toString();
  if (!caption.isEmpty()) {
    QPainter cp(&image);
    cp.setRenderHint(QPainter::Antialiasing, true);
    QFont font = cp.font();
    font.setPixelSize(qMax(14, widthPx / 38));
    font.setBold(true);
    cp.setFont(font);
    const int barH = qRound(font.pixelSize() * 2.1);
    const bool top = args.value(QStringLiteral("captionPos")).toString() == QLatin1String("top");
    const QRect bar(0, top ? 0 : heightPx - barH, widthPx, barH);
    cp.fillRect(bar, QColor(0, 0, 0, 170));
    cp.setPen(QColor(255, 230, 120));
    cp.drawText(bar.adjusted(font.pixelSize(), 0, -font.pixelSize(), 0),
                Qt::AlignVCenter | Qt::AlignLeft, caption);
  }

  const QString path = QDir::temp().filePath(
      QStringLiteral("autodraft_shot_%1.png").arg(++m_shotCounter));
  if (!image.save(path))
    return fail(QStringLiteral("이미지를 저장하지 못했습니다: %1").arg(path));

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("path"), path);
  QJsonArray reg;
  reg.append(source.left());
  reg.append(source.top());
  reg.append(source.width());
  reg.append(source.height());
  result.insert(QStringLiteral("region"), reg);
  result.insert(QStringLiteral("sizePx"),
                QStringLiteral("%1x%2").arg(widthPx).arg(heightPx));
  return result;
}

// 지정 위치에 가운데 정렬 문자를 만든다 (문자 레이어, 없으면 현재 레이어)
bool AiBridgeServer::placeText(MyGraphicsScene *scene, const QString &text,
                               const QPointF &center, double height) {
  if (!m_ctx->layerManager() || !m_ctx->entityFactory() || text.isEmpty())
    return false;
  LayerManager *lm = m_ctx->layerManager();
  Layer *layer = lm->getLayer(m_ctx->textLayerName());
  if (!layer || layer->isLayerLock())
    layer = lm->getCurrentLayer();
  if (!layer)
    return false;

  QFont font;
  font.setFamily(m_ctx->currentFontFamily());
  font.setPointSizeF(height);
  const QPen pen = penForLayer(layer);
  QGraphicsItem *item = m_ctx->entityFactory()->createText(
      text, QPointF(center.x(), center.y() - height * 0.36), font, pen.color(),
      0.0, 1);
  return item && placeItem(scene, item, layer).value(QStringLiteral("ok")).toBool();
}

// 점 목록을 다중선(2-Line) 한 묶음으로 그린다. closed 면 마지막에서 처음으로 닫는다.
//   extFirst/extLast: 열린 끝이 이웃 벽의 면에서 멈추도록 벽선을 물렸을 때, 그 끝의 "중심선"만
//   이웃 벽의 중심선(교차점)까지 늘린다 — 중심선 격자가 교차점까지 이어지게 한다.
void AiBridgeServer::drawWallChain(const QList<QPointF> &points, bool closed,
                                   const QPointF *extFirst, const QPointF *extLast) {
  if (points.size() < 2)
    return;
  MyGraphicsScene *scene = m_ctx->activeScene();
  QSet<QGraphicsItem *> before;
  const bool extend = scene && (extFirst || extLast);
  if (extend)
    before = snapshotItems(scene);

  {
    SnapSuspender guard(m_ctx->snapManager());
    TwoLineTool *tool = new TwoLineTool(false, false, false);
    m_ctx->startTool(tool);
    for (int i = 0; i < points.size(); ++i)
      tool->mousePress(points.at(i), Qt::LeftButton);
    if (closed)
      tool->processCommand(QStringLiteral("c"));
    else
      tool->mousePress(points.last(), Qt::RightButton);
  }

  if (!extend)
    return;
  const QList<QGraphicsItem *> after = scene->items();
  for (int i = 0; i < after.size(); ++i) {
    LineItem *li = dynamic_cast<LineItem *>(after.at(i));
    if (!li || before.contains(li) ||
        li->data(ML_DATA_ROLE).toString() != QLatin1String("center"))
      continue;
    const MyLine ln = li->getLine();
    QPointF s(ln.start.x, ln.start.y);
    QPointF e(ln.end.x, ln.end.y);
    bool changed = false;
    if (extFirst && QLineF(s, points.first()).length() < 0.5) { s = *extFirst; changed = true; }
    else if (extFirst && QLineF(e, points.first()).length() < 0.5) { e = *extFirst; changed = true; }
    if (extLast && QLineF(e, points.last()).length() < 0.5) { e = *extLast; changed = true; }
    else if (extLast && QLineF(s, points.last()).length() < 0.5) { s = *extLast; changed = true; }
    if (changed)
      li->setLine(MyLine(MyVector(s), MyVector(e)));
  }
}

// 방(사각형). 벽(다중선) → 문·창(개구부 트림) → 실명 순서로 만들고 전체를 undo 1단계로 묶는다.
//   기준: dimensions "centerline"(벽 중심선 사각형, 기본) | "interior"(내부 유효 치수)
//   walls: 이 방이 직접 그릴 변(S,E,N,W). 이웃 방과 공유하는 벽은 한쪽만 그리도록 뺀다.
//   offset: 변의 서쪽/남쪽 끝(중심선 모서리)에서 개구부 시작점까지의 거리
QJsonObject AiBridgeServer::toolCreateRoom(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF origin;
  if (!readPoint(args.value(QStringLiteral("origin")), origin))
    return fail(QStringLiteral("origin 은 [x, y] (사각형의 남서쪽 모서리) 입니다. 예: "
                               "{\"origin\":[0,0],\"width\":3600,\"depth\":3000}"));
  const double width = args.value(QStringLiteral("width")).toDouble(0.0);
  const double depth = args.value(QStringLiteral("depth")).toDouble(0.0);
  if (width < 500.0 || depth < 500.0)
    return fail(QStringLiteral("width(동서 방향)와 depth(남북 방향)는 500mm 이상이어야 "
                               "합니다."));
  const double t = args.contains(QStringLiteral("wallThickness"))
                       ? args.value(QStringLiteral("wallThickness")).toDouble()
                       : TwoLineTool::globalSpacing();
  if (t < 10.0)
    return fail(QStringLiteral("wallThickness(벽 두께, mm)는 10 이상이어야 합니다."));
  const QString dimMode =
      args.value(QStringLiteral("dimensions")).toString(QStringLiteral("centerline"));
  if (dimMode != QLatin1String("centerline") && dimMode != QLatin1String("interior"))
    return fail(QStringLiteral("dimensions 는 \"centerline\" 또는 \"interior\" 입니다."));

  // 벽 중심선 사각형
  const double grow = (dimMode == QLatin1String("interior")) ? t * 0.5 : 0.0;
  const double x0 = origin.x() - grow;
  const double y0 = origin.y() - grow;
  const double x1 = origin.x() + width + grow;
  const double y1 = origin.y() + depth + grow;

  // 직접 그릴 변 (S, E, N, W)
  bool drawSide[4] = {true, true, true, true};
  if (args.contains(QStringLiteral("walls"))) {
    const QJsonArray w = args.value(QStringLiteral("walls")).toArray();
    for (int i = 0; i < 4; ++i)
      drawSide[i] = false;
    for (int i = 0; i < w.size(); ++i) {
      const QString s = w.at(i).toString().toUpper();
      if (s == QLatin1String("S"))
        drawSide[0] = true;
      else if (s == QLatin1String("E"))
        drawSide[1] = true;
      else if (s == QLatin1String("N"))
        drawSide[2] = true;
      else if (s == QLatin1String("W"))
        drawSide[3] = true;
      else
        return fail(QStringLiteral("walls 의 값은 \"S\",\"E\",\"N\",\"W\" 중에서 "
                                   "고릅니다(남,동,북,서)."));
    }
  }

  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  QJsonArray warnings;
  int wallChains = 0;
  int doorsMade = 0;
  int windowsMade = 0;

  if (m_ctx->undoStack())
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: create_room"));

  // ── 1) 벽: 이어지는 변끼리 한 묶음으로 그려야 모서리가 맞는다 ──
  const QPointF corner[4] = {QPointF(x0, y0), QPointF(x1, y0), QPointF(x1, y1),
                             QPointF(x0, y1)};
  bool all = drawSide[0] && drawSide[1] && drawSide[2] && drawSide[3];
  if (all) {
    QList<QPointF> pts;
    for (int i = 0; i < 4; ++i)
      pts.append(corner[i]);
    drawWallChain(pts, true);
    wallChains = 1;
  } else {
    for (int i = 0; i < 4; ++i) {
      // 변 i 는 corner[i] → corner[(i+1)%4]. 앞 변이 없을 때만 묶음의 시작점
      if (!drawSide[i] || drawSide[(i + 3) % 4])
        continue;
      QList<QPointF> pts;
      pts.append(corner[i]);
      int k = i;
      while (drawSide[k]) {
        pts.append(corner[(k + 1) % 4]);
        k = (k + 1) % 4;
        if (k == i)
          break;
      }
      // 이웃 벽(중심선이 같은 위치)의 면에서 멈춰 벽 두께 안에 선이 남지 않게 한다
      const QPointF origFirst = pts.first();
      const QPointF origLast = pts.last();
      bool pulled = false;
      if (pts.size() >= 2) {
        const QLineF first(pts.at(0), pts.at(1));
        const QLineF last(pts.at(pts.size() - 1), pts.at(pts.size() - 2));
        if (first.length() > t && last.length() > t) {
          pts[0] = first.pointAt(t * 0.5 / first.length());
          pts[pts.size() - 1] = last.pointAt(t * 0.5 / last.length());
          pulled = true;
        }
      }
      // 중심선은 이웃 벽 중심선(원래 모서리)까지 이어 둔다
      drawWallChain(pts, false, pulled ? &origFirst : nullptr, pulled ? &origLast : nullptr);
      ++wallChains;
    }
  }

  // ── 2) 개구부 (문/창): 변마다 벽 중심선 좌표 → 벽면 위 점으로 변환 ──
  struct SideGeo {
    const char *name;
    QPointF start;  // 서/남쪽 끝 중심선 점
    QPointF u;      // 변을 따라 증가하는 방향
    QPointF inward; // 방 안쪽 법선
    double length;
  };
  const SideGeo sides[4] = {
      {"S", QPointF(x0, y0), QPointF(1, 0), QPointF(0, 1), x1 - x0},
      {"E", QPointF(x1, y0), QPointF(0, 1), QPointF(-1, 0), y1 - y0},
      {"N", QPointF(x0, y1), QPointF(1, 0), QPointF(0, -1), x1 - x0},
      {"W", QPointF(x0, y0), QPointF(0, 1), QPointF(1, 0), y1 - y0}};

  for (int pass = 0; pass < 2; ++pass) {
    const QString key = pass == 0 ? QStringLiteral("doors") : QStringLiteral("windows");
    const QJsonArray list = args.value(key).toArray();
    for (int i = 0; i < list.size(); ++i) {
      const QJsonObject o = list.at(i).toObject();
      const QString label = QStringLiteral("%1[%2]").arg(key).arg(i);
      const QString sideName = o.value(QStringLiteral("side")).toString().toUpper();
      int si = -1;
      for (int k = 0; k < 4; ++k)
        if (sideName == QLatin1String(sides[k].name))
          si = k;
      if (si < 0) {
        warnings.append(QStringLiteral("%1: side 는 S,E,N,W 중 하나여야 합니다.").arg(label));
        continue;
      }
      const SideGeo &sg = sides[si];
      const QString doorKind =
          (pass == 0) ? o.value(QStringLiteral("kind")).toString() : QString();
      const bool kindRevolving = (doorKind == QLatin1String("revolving"));
      const bool hasSplit = (pass == 0) && (o.contains(QStringLiteral("width1")) ||
                                            o.contains(QStringLiteral("width2")));
      double w = o.value(QStringLiteral("width")).toDouble(
          pass == 0 ? (kindRevolving ? 2400.0 : 900.0) : 1200.0);
      if (hasSplit && !o.contains(QStringLiteral("width")) &&
          o.contains(QStringLiteral("width1")) && o.contains(QStringLiteral("width2")))
        w = o.value(QStringLiteral("width1")).toDouble() +
            o.value(QStringLiteral("width2")).toDouble();
      const double offset = o.value(QStringLiteral("offset")).toDouble(-1.0);
      if (offset < 0.0 || w < 100.0 || offset + w > sg.length + 0.001) {
        warnings.append(
            QStringLiteral("%1: offset(%2)+width(%3)이 %4 변 길이(%5) 안에 있어야 합니다.")
                .arg(label).arg(offset).arg(w).arg(sideName).arg(sg.length));
        continue;
      }
      const bool swingOut = o.value(QStringLiteral("swing")).toString() == QLatin1String("out");
      const bool hingeEnd = o.value(QStringLiteral("hingeAt")).toString() == QLatin1String("end");

      // 진행 방향과 시작점(벽 중심선 위)
      const double dirSign = (pass == 0 && hingeEnd) ? -1.0 : 1.0;
      const double a = (dirSign > 0) ? offset : offset + w;
      const QPointF centerPt(sg.start.x() + sg.u.x() * a, sg.start.y() + sg.u.y() * a);
      const QPointF run(sg.u.x() * dirSign, sg.u.y() * dirSign);
      // P2 쪽(문이 열리거나 창 두께가 향하는 쪽): 문은 안쪽/바깥쪽, 창은 안쪽
      const double inSign = (pass == 0 && swingOut) ? -1.0 : 1.0;
      const QPointF dir(sg.inward.x() * inSign, sg.inward.y() * inSign);
      // P1 은 P2 의 반대쪽 벽면 위
      const QPointF p1(centerPt.x() - dir.x() * t * 0.5, centerPt.y() - dir.y() * t * 0.5);
      const QPointF left(-run.y(), run.x());
      const bool dirIsLeft = (dir.x() * left.x() + dir.y() * left.y()) > 0.0;

      QJsonObject a2;
      QJsonArray h;
      h.append(p1.x());
      h.append(p1.y());
      QJsonArray tw;
      tw.append(p1.x() + run.x() * 1000.0);
      tw.append(p1.y() + run.y() * 1000.0);
      a2.insert(QStringLiteral("hinge"), h);
      a2.insert(QStringLiteral("toward"), tw);
      a2.insert(QStringLiteral("side"), dirIsLeft ? QStringLiteral("left") : QStringLiteral("right"));
      a2.insert(QStringLiteral("wallThickness"), t);
      // width1/width2 로 문1·문2 폭을 따로 준 경우, 회전문 기본 폭인 경우는 전체 폭을 넘기지 않는다
      if (!hasSplit && !(kindRevolving && !o.contains(QStringLiteral("width"))))
        a2.insert(QStringLiteral("width"), w);
      if (pass == 0) {
        if (o.contains(QStringLiteral("style")))
          a2.insert(QStringLiteral("style"), o.value(QStringLiteral("style")));
        if (o.contains(QStringLiteral("kind")))
          a2.insert(QStringLiteral("kind"), o.value(QStringLiteral("kind")));
        if (o.contains(QStringLiteral("width1")))
          a2.insert(QStringLiteral("width1"), o.value(QStringLiteral("width1")));
        if (o.contains(QStringLiteral("width2")))
          a2.insert(QStringLiteral("width2"), o.value(QStringLiteral("width2")));
        if (o.contains(QStringLiteral("wings")))
          a2.insert(QStringLiteral("wings"), o.value(QStringLiteral("wings")));
      } else {
        if (o.contains(QStringLiteral("sashes")))
          a2.insert(QStringLiteral("sashes"), o.value(QStringLiteral("sashes")));
        if (o.contains(QStringLiteral("double")))
          a2.insert(QStringLiteral("double"), o.value(QStringLiteral("double")));
        if (o.contains(QStringLiteral("kind")))
          a2.insert(QStringLiteral("kind"), o.value(QStringLiteral("kind")));
      }
      const QJsonObject r = pass == 0 ? toolCreateDoor(a2) : toolCreateWindow(a2);
      if (r.value(QStringLiteral("ok")).toBool()) {
        if (pass == 0)
          ++doorsMade;
        else
          ++windowsMade;
      } else {
        warnings.append(label + QStringLiteral(": ") + r.value(QStringLiteral("error")).toString());
      }
    }
  }

  // ── 3) 실명 ──
  const QString name = args.value(QStringLiteral("name")).toString();
  bool labelMade = false;
  if (!name.isEmpty()) {
    const double th = args.value(QStringLiteral("textHeight")).toDouble(200.0);
    QPointF labelPos((x0 + x1) * 0.5, (y0 + y1) * 0.5);
    readPoint(args.value(QStringLiteral("nameAt")), labelPos); // 없으면 방 중앙
    labelMade = placeText(scene, name, labelPos, th);
    if (!labelMade)
      warnings.append(QStringLiteral("실명 문자를 만들지 못했습니다."));
  }

  if (m_ctx->undoStack())
    m_ctx->undoStack()->endMacro();

  QJsonObject result = newItemsResult(scene, before,
                                      QStringLiteral("방이 만들어지지 않았습니다."));
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  result.remove(QStringLiteral("entities")); // 개별 객체는 get_entities 로 조회
  QJsonObject room;
  QJsonArray cl;
  cl.append(x0);
  cl.append(y0);
  cl.append(x1 - x0);
  cl.append(y1 - y0);
  room.insert(QStringLiteral("centerlineRect"), cl);
  QJsonArray in;
  in.append(x0 + t * 0.5);
  in.append(y0 + t * 0.5);
  in.append(x1 - x0 - t);
  in.append(y1 - y0 - t);
  room.insert(QStringLiteral("interiorRect"), in);
  room.insert(QStringLiteral("wallThickness"), t);
  room.insert(QStringLiteral("wallChains"), wallChains);
  room.insert(QStringLiteral("doors"), doorsMade);
  room.insert(QStringLiteral("windows"), windowsMade);
  room.insert(QStringLiteral("label"), labelMade);
  result.insert(QStringLiteral("room"), room);
  if (!warnings.isEmpty())
    result.insert(QStringLiteral("warnings"), warnings);
  return result;
}

// 인승(persons) → 카 치수·균형추 설정. persons 가 없으면 아무것도 하지 않고 true.
bool AiBridgeServer::applyElevatorSpec(const QJsonObject &args,
                                       ElevatorParams &params,
                                       ElevatorSpec *specOut,
                                       QJsonObject &errorOut) const {
  if (!args.contains(QStringLiteral("persons")))
    return true;
  const int persons = args.value(QStringLiteral("persons")).toInt();
  const QVector<ElevatorMaker> makers = ElevatorTool::loadMakers();
  if (makers.isEmpty()) {
    errorOut = fail(QStringLiteral(
        "엘리베이터 규격 데이터(Data/elevator/elevator_specs.json)를 찾을 수 없습니다. "
        "persons 대신 carWidth, carDepth, doorSize 를 직접 지정하세요."));
    return false;
  }

  const QString wantMaker = args.value(QStringLiteral("maker")).toString();
  int makerIndex = -1;
  QStringList makerNames;
  for (int i = 0; i < makers.size(); ++i) {
    makerNames.append(makers.at(i).name);
    if (makerIndex < 0 && !wantMaker.isEmpty() &&
        makers.at(i).name.contains(wantMaker, Qt::CaseInsensitive))
      makerIndex = i;
  }
  if (makerIndex < 0 && wantMaker.isEmpty()) {
    for (int i = 0; i < makers.size(); ++i)
      if (makers.at(i).name == params.maker)
        makerIndex = i;
    if (makerIndex < 0)
      makerIndex = 0;
  }
  if (makerIndex < 0) {
    errorOut = fail(QStringLiteral("maker '%1' 를 찾을 수 없습니다. 사용 가능: %2")
                        .arg(wantMaker, makerNames.join(QStringLiteral(" | "))));
    return false;
  }

  const ElevatorMaker &mk = makers.at(makerIndex);
  int smallest = 0;
  int found = -1;
  QStringList available;
  for (int i = 0; i < mk.specs.size(); ++i) {
    const int pn = mk.specs.at(i).persons;
    available.append(QString::number(pn));
    if (i == 0 || pn < smallest)
      smallest = pn;
    if (pn == persons)
      found = i;
  }
  if (found < 0) {
    errorOut = fail(QStringLiteral("'%1' 에 %2인승 규격이 없습니다. 사용 가능한 인승: %3")
                        .arg(mk.name).arg(persons)
                        .arg(available.join(QStringLiteral(", "))));
    return false;
  }

  const ElevatorSpec &sp = mk.specs.at(found);
  params.carWidth = sp.carWidth;
  params.carDepth = sp.carDepth;
  params.doorSize = sp.doorWidth;
  params.maker = mk.name;
  params.compactCw = persons <= smallest; // 최소 인승 이하는 균형추를 작게
  if (specOut)
    *specOut = sp;
  return true;
}

// 문/창 호출용 인자: centerPt = 벽 중심선 위 시작점, run = 벽을 따라 진행하는 단위벡터,
// dir = 벽 두께와 문이 열리는 쪽의 단위벡터 (P1 은 그 반대쪽 벽면 위)
QJsonObject AiBridgeServer::openingArgs(const QPointF &centerPt, const QPointF &run,
                                        const QPointF &dir, double thickness,
                                        double width) const {
  const QPointF p1(centerPt.x() - dir.x() * thickness * 0.5,
                   centerPt.y() - dir.y() * thickness * 0.5);
  const QPointF left(-run.y(), run.x());
  const bool dirIsLeft = (dir.x() * left.x() + dir.y() * left.y()) > 0.0;
  QJsonArray h;
  h.append(p1.x());
  h.append(p1.y());
  QJsonArray tw;
  tw.append(p1.x() + run.x() * 1000.0);
  tw.append(p1.y() + run.y() * 1000.0);
  QJsonObject o;
  o.insert(QStringLiteral("hinge"), h);
  o.insert(QStringLiteral("toward"), tw);
  o.insert(QStringLiteral("side"),
           dirIsLeft ? QStringLiteral("left") : QStringLiteral("right"));
  o.insert(QStringLiteral("wallThickness"), thickness);
  o.insert(QStringLiteral("width"), width);
  return o;
}

// 도면 축척(분모, 60 = 1:60). 계단 UP/DN 문자 크기 등 축척에 비례하는 값에 쓰인다.
QJsonObject AiBridgeServer::toolSetDrawingScale(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const double scale = args.value(QStringLiteral("scale")).toDouble(0.0);
  if (scale < 1.0 || scale > 10000.0)
    return fail(QStringLiteral("scale(축척 분모)는 1 이상 10000 이하입니다. 예: 1/60 도면이면 "
                               "60."));
  scene->setDrawingScale(scale);
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("drawingScale"), scale);
  return result;
}

// 코어(계단실/홀 + 엘리베이터). 서쪽에 U자 계단, 동쪽 북쪽에 승강로, 나머지는 칸막이 없는 한 공간.
//
//        xs0      xs1 xe0      xe1
//   ys1  +--------+--+--------+     북 (승강로 문은 남쪽, 홀 쪽으로 열림)
//        | 계단   |  | 승강로 |
//        |        |  +--  --+-+   <- 승강로 문 개구부 (왼쪽 L 코너, 오른쪽 T 접합)
//        | (U자)  계단실/홀 (한 공간)
//   ys0  +-------------------+     남
//   origin = 계단실 내부 남서쪽 모서리. 모든 치수는 내부 유효 치수, t 는 벽 두께.
//   계단은 기본 디딤판 291, 계단참 700 (UP/DN 연결선이 북쪽 벽에서 약 400 떨어진다).
QJsonObject AiBridgeServer::toolCreateCore(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF origin;
  if (!readPoint(args.value(QStringLiteral("origin")), origin))
    return fail(QStringLiteral("origin 은 [x, y] (계단실 내부의 남서쪽 모서리) 입니다. 예: "
                               "{\"origin\":[0,0],\"persons\":8}"));
  const double t = args.contains(QStringLiteral("wallThickness"))
                       ? args.value(QStringLiteral("wallThickness")).toDouble()
                       : TwoLineTool::globalSpacing();
  if (t < 10.0)
    return fail(QStringLiteral("wallThickness(벽 두께, mm)는 10 이상이어야 합니다."));
  const double sw = args.value(QStringLiteral("stairWidth")).toDouble(2400.0);
  if (sw < 1500.0)
    return fail(QStringLiteral("stairWidth(계단실 내부 폭)는 1500mm 이상이어야 합니다."));
  const double hallMin = args.value(QStringLiteral("hallDepth")).toDouble(2400.0);
  if (hallMin < 1500.0)
    return fail(QStringLiteral("hallDepth(홀 내부 깊이)는 1500mm 이상이어야 합니다."));
  const double doorW = args.value(QStringLiteral("doorWidth")).toDouble(900.0);
  if (doorW < 600.0)
    return fail(QStringLiteral("doorWidth(문 폭)는 600mm 이상이어야 합니다."));
  const QString hallDoor =
      args.value(QStringLiteral("hallDoor")).toString(QStringLiteral("E")).toUpper();
  if (hallDoor != QLatin1String("E") && hallDoor != QLatin1String("S") &&
      hallDoor != QLatin1String("NONE"))
    return fail(QStringLiteral("hallDoor 는 \"E\"(동쪽 벽), \"S\"(남쪽 벽), \"none\" 입니다."));

  // ── 계단: 도면 기본값 + 덮어쓰기. 계단실 깊이 = 계단참 + 계단 수 x 디딤판 폭 ──
  StairPlanParams sp = StairPlanTool::loadParams(scene);
  sp.type = 1;
  if (args.contains(QStringLiteral("up")))
    sp.up = args.value(QStringLiteral("up")).toInt();
  if (args.contains(QStringLiteral("down")))
    sp.down = args.value(QStringLiteral("down")).toInt();
  // 코어 기본 계단: 디딤판 291, 계단참 700 (도면 기본값을 쓰지 않는다)
  sp.tread = args.contains(QStringLiteral("tread"))
                 ? args.value(QStringLiteral("tread")).toDouble()
                 : 291.0;
  sp.land = args.contains(QStringLiteral("land"))
                ? args.value(QStringLiteral("land")).toDouble()
                : 700.0;
  if (sp.up < 1 || sp.down < 1 || sp.tread <= 0.0 || sp.land < 0.0)
    return fail(QStringLiteral("코어의 계단은 꺾임(U자) 계단입니다. up, down 은 1 이상, "
                               "tread 는 0 보다 큰 값이어야 합니다."));
  const double stairDepth = sp.land + qMax(sp.up, sp.down) * sp.tread;

  // ── 엘리베이터: 인승 규격 → 승강로 내부 크기 (들어가지 않으면 키운다) ──
  ElevatorParams ep = ElevatorTool::loadParams(scene);
  ElevatorSpec spec;
  QJsonObject eargs;
  eargs.insert(QStringLiteral("persons"), args.value(QStringLiteral("persons")).toInt(8));
  if (args.contains(QStringLiteral("maker")))
    eargs.insert(QStringLiteral("maker"), args.value(QStringLiteral("maker")));
  if (!applyElevatorSpec(eargs, ep, &spec, err))
    return err;
  double ew = args.contains(QStringLiteral("shaftWidth"))
                  ? args.value(QStringLiteral("shaftWidth")).toDouble()
                  : (spec.shaftWidth > 0.0 ? spec.shaftWidth : ep.carWidth + 300.0);
  double ed = args.contains(QStringLiteral("shaftDepth"))
                  ? args.value(QStringLiteral("shaftDepth")).toDouble()
                  : (spec.shaftDepth > 0.0 ? spec.shaftDepth : ep.carDepth + 900.0);
  bool fits = false;
  for (int dw = 0; dw <= 2000 && !fits; dw += 50)
    for (int dd = 0; dd <= 3000 && !fits; dd += 50)
      if (ElevatorTool::checkFit(ew + dw, ed + dd, ep).isEmpty()) {
        ew += dw;
        ed += dd;
        fits = true;
      }
  if (!fits)
    return fail(QStringLiteral("승강로 크기를 정하지 못했습니다. shaftWidth/shaftDepth 를 "
                               "직접 지정하세요."));
  const double openW = ep.doorSize + 200.0; // 승강로 문 앞 벽 개구부 폭
  if (ew < openW + 200.0)
    return fail(QStringLiteral("승강로 폭(%1)이 문 개구부(%2)에 비해 너무 좁습니다.")
                    .arg(ew).arg(openW));

  // ── 전체 배치 (내부 치수 기준) ──
  const double D = qMax(stairDepth, hallMin + t + ed); // 동쪽 열의 내부 깊이 = 계단실 깊이
  const double hd = D - t - ed;                         // 홀 내부 깊이
  const double landUsed = sp.land; // 계단참은 지정한 길이 그대로 (남는 깊이는 계단 아래 홀 쪽 공간)
  const double xs0 = origin.x();
  const double xs1 = xs0 + sw;
  const double ys0 = origin.y();
  const double ys1 = ys0 + D;
  const double xe0 = xs1 + t;
  const double xe1 = xe0 + ew;
  const double hy1 = ys0 + hd;   // 홀 북쪽 면
  const double ysh0 = hy1 + t;   // 승강로 남쪽 내부 면
  // 벽 중심선 좌표
  const double xa = xs0 - t * 0.5;
  const double xb = xs1 + t * 0.5;
  const double xc = xe1 + t * 0.5;
  const double ya = ys0 - t * 0.5;
  const double yb = hy1 + t * 0.5;
  const double yc = ys1 + t * 0.5;
  const double cx = (xe0 + xe1) * 0.5;

  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  QJsonArray warnings;
  if (m_ctx->undoStack())
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: create_core"));

  // 1) 벽 — 계단실과 홀은 칸막이 없이 한 공간이다. 다중선 탭에서 사람이 하는 순서와 같게 그린다:
  //    통과하는 벽(바깥 둘레 4변)과 승강로 벽(입구 벽 왼쪽 조각 + 서쪽 벽), 입구 벽 오른쪽 조각을
  //    각각 끊기지 않는 직선 다중선으로 그린 뒤, 네 모서리는 L-코너, 두 T 접합부는 T-코너(합쳐진 T)로 처리한다.
  //    (T-코너는 통과 벽이 줄기와 별개의 직선 하나일 때만 올바르게 동작한다)
  QJsonObject ma;
  ma.insert(QStringLiteral("spacing"), t);
  QJsonArray walls[6];
  addPt(walls[0], xc, yc); // 북쪽 벽
  addPt(walls[0], xa, yc);
  addPt(walls[1], xa, yc); // 서쪽 벽
  addPt(walls[1], xa, ya);
  addPt(walls[2], xa, ya); // 남쪽 벽
  addPt(walls[2], xc, ya);
  addPt(walls[3], xc, ya); // 동쪽 벽
  addPt(walls[3], xc, yc);
  addPt(walls[4], cx - openW * 0.5, yb); // 승강로 입구 벽 왼쪽 조각 + 승강로 서쪽 벽 (L 코너)
  addPt(walls[4], xb, yb);
  addPt(walls[4], xb, yc);
  addPt(walls[5], cx + openW * 0.5, yb); // 승강로 입구 벽 오른쪽 조각 (동쪽 벽에 T)
  addPt(walls[5], xc, yb);
  QJsonObject r;
  for (int i = 0; i < 6; ++i) {
    ma.insert(QStringLiteral("points"), walls[i]);
    r = toolCreateMultiline(ma);
    if (!r.value(QStringLiteral("ok")).toBool())
      warnings.append(QStringLiteral("벽: ") + r.value(QStringLiteral("error")).toString());
  }

  // 바깥 모서리 4곳 L-코너 (클릭 점은 각 벽 위에서 모서리 가까이)
  const double lc = 250.0;
  const double lClicks[4][4] = {{xa + lc, yc, xa, yc - lc},  // 북서
                                {xa, ya + lc, xa + lc, ya},  // 남서
                                {xc - lc, ya, xc, ya + lc},  // 남동
                                {xc - lc, yc, xc, yc - lc}}; // 북동
  for (int i = 0; i < 4; ++i) {
    r = toolMultilineCorner(
        cornerArgs(lClicks[i][0], lClicks[i][1], lClicks[i][2], lClicks[i][3]), 'L');
    if (!r.value(QStringLiteral("ok")).toBool())
      warnings.append(QStringLiteral("L 코너: ") + r.value(QStringLiteral("error")).toString());
  }

  // T-코너 2곳: clickA = 통과 벽, clickB = 줄기 벽의 자를 끝 쪽(교점을 지난 바깥쪽, 통과 벽 선과 겹치지 않을 만큼 멀리)
  r = toolMultilineCorner(cornerArgs(xc, yb + 900.0, xc + 400.0, yb), 'T');  // 입구 벽 오른쪽 → 동쪽 벽
  if (!r.value(QStringLiteral("ok")).toBool())
    warnings.append(QStringLiteral("T 코너(입구 벽): ") + r.value(QStringLiteral("error")).toString());
  r = toolMultilineCorner(cornerArgs(xb + 800.0, yc, xb - t * 0.5, yc + 435.0), 'T'); // 승강로 서쪽 벽 → 북쪽 벽
  if (!r.value(QStringLiteral("ok")).toBool())
    warnings.append(QStringLiteral("T 코너(승강로): ") + r.value(QStringLiteral("error")).toString());

  // 2) 홀 출입문
  QJsonObject a;
  if (hallDoor == QLatin1String("E")) {
    a = openingArgs(QPointF(xc, ys0 + qMax(0.0, qMin(500.0, hd - doorW))), QPointF(0, 1),
                    QPointF(-1, 0), t, doorW);
    r = toolCreateDoor(a);
  } else if (hallDoor == QLatin1String("S")) {
    a = openingArgs(QPointF(xe0 + (ew - doorW) * 0.5, ya), QPointF(1, 0), QPointF(0, 1), t, doorW);
    r = toolCreateDoor(a);
  } else {
    r = QJsonObject();
    r.insert(QStringLiteral("ok"), true);
  }
  if (!r.value(QStringLiteral("ok")).toBool())
    warnings.append(QStringLiteral("홀 문: ") + r.value(QStringLiteral("error")).toString());

  // 3) 계단 (남쪽으로 뻗는다). UP/DN 을 잇는 연결선은 계단참 중간 높이에 그려지므로,
  //    연결선이 계단실 북쪽 벽 안쪽 면에서 약 400 떨어지도록 기준선 높이를 정한다
  const double stairBaseY = ys1 - 402.0 + sp.land * 0.5 + 1.875;
  const double stairLabelY =
      stairBaseY - sp.land - (qMax(sp.up, sp.down) - 1) * sp.tread; // UP/DN 원의 높이
  QJsonObject sa;
  QJsonArray p1;
  p1.append(xs0);
  p1.append(stairBaseY);
  QJsonArray p2;
  p2.append(xs1);
  p2.append(stairBaseY);
  QJsonArray tw;
  tw.append((xs0 + xs1) * 0.5);
  tw.append(stairBaseY - 1000.0);
  sa.insert(QStringLiteral("p1"), p1);
  sa.insert(QStringLiteral("p2"), p2);
  sa.insert(QStringLiteral("toward"), tw);
  sa.insert(QStringLiteral("type"), 1);
  sa.insert(QStringLiteral("up"), sp.up);
  sa.insert(QStringLiteral("down"), sp.down);
  sa.insert(QStringLiteral("tread"), sp.tread);
  sa.insert(QStringLiteral("land"), landUsed);
  r = toolCreateStair(sa);
  if (!r.value(QStringLiteral("ok")).toBool())
    warnings.append(QStringLiteral("계단: ") + r.value(QStringLiteral("error")).toString());

  // 4) 엘리베이터 (문이 남쪽 = 홀 쪽)
  QJsonObject ea = eargs;
  QJsonArray e1;
  e1.append(xe0);
  e1.append(ysh0);
  QJsonArray e2;
  e2.append(xe1);
  e2.append(ysh0 + ed);
  ea.insert(QStringLiteral("p1"), e1);
  ea.insert(QStringLiteral("p2"), e2);
  ea.insert(QStringLiteral("drawWalls"), false);
  if (args.contains(QStringLiteral("drawDims")))
    ea.insert(QStringLiteral("drawDims"), args.value(QStringLiteral("drawDims")));
  r = toolCreateElevator(ea);
  if (!r.value(QStringLiteral("ok")).toBool())
    warnings.append(QStringLiteral("엘리베이터: ") + r.value(QStringLiteral("error")).toString());

  // 5) 실명
  const double th = args.value(QStringLiteral("textHeight")).toDouble(200.0);
  // 계단실과 홀은 한 공간이므로 실명은 하나이며 UP/DN 글자 아래에 둔다
  const QString stairName =
      args.value(QStringLiteral("stairName")).toString(QStringLiteral("계단실/홀"));
  const QString hallName = args.value(QStringLiteral("hallName")).toString(QString());
  if (!stairName.isEmpty())
    placeText(scene, stairName, QPointF((xs0 + xs1) * 0.5, stairLabelY - 1150.0), th);
  if (!hallName.isEmpty())
    placeText(scene, hallName, QPointF(xe0 + ew * 0.5, hy1 - 420.0), th);

  if (m_ctx->undoStack())
    m_ctx->undoStack()->endMacro();

  QJsonObject result = newItemsResult(scene, before,
                                      QStringLiteral("코어가 만들어지지 않았습니다."));
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  result.remove(QStringLiteral("entities"));

  QJsonObject core;
  QJsonArray stairRect;
  stairRect << xs0 << ys0 << sw << D;
  QJsonArray hallRect;
  hallRect << xe0 << ys0 << ew << hd;
  QJsonArray shaftRect;
  shaftRect << xe0 << ysh0 << ew << ed;
  QJsonArray outer;
  outer << (xa - t * 0.5) << (ya - t * 0.5) << (xc - xa + t) << (yc - ya + t);
  core.insert(QStringLiteral("stairRoomInterior"), stairRect);
  core.insert(QStringLiteral("hallInterior"), hallRect);
  core.insert(QStringLiteral("shaftInterior"), shaftRect);
  core.insert(QStringLiteral("outerBounds"), outer);
  core.insert(QStringLiteral("wallThickness"), t);
  core.insert(QStringLiteral("persons"), eargs.value(QStringLiteral("persons")));
  core.insert(QStringLiteral("landUsed"), landUsed);
  result.insert(QStringLiteral("core"), core);
  if (!warnings.isEmpty())
    result.insert(QStringLiteral("warnings"), warnings);
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 블록 라이브러리 (blocks/<분류>/<이름>.cdb)
// ─────────────────────────────────────────────────────────────────────────────

// 실행 파일 위치에서 위로 올라가며 blocks 폴더를 찾는다 (개발/배포 폴더 구조 모두 대응)
QString AiBridgeServer::blocksRoot() const {
  QDir dir(QCoreApplication::applicationDirPath());
  for (int up = 0; up < 7; ++up) {
    if (QDir(dir.filePath(QStringLiteral("blocks"))).exists())
      return QDir::cleanPath(dir.filePath(QStringLiteral("blocks")));
    if (!dir.cdUp())
      break;
  }
  return QString();
}

// 분류 폴더 이름을 대소문자 무시로 찾아 실제 이름을 돌려준다 (없으면 빈 문자열)
QString AiBridgeServer::resolveBlockCategory(const QString &category,
                                             QStringList *allCategories) const {
  const QString root = blocksRoot();
  if (root.isEmpty())
    return QString();
  const QStringList dirs =
      QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  if (allCategories)
    *allCategories = dirs;
  for (int i = 0; i < dirs.size(); ++i)
    if (dirs.at(i).compare(category, Qt::CaseInsensitive) == 0)
      return dirs.at(i);
  return QString();
}

QStringList AiBridgeServer::blockNames(const QString &category) const {
  QStringList names;
  const QStringList files = QDir(blocksRoot() + QLatin1Char('/') + category)
                                .entryList(QStringList() << QStringLiteral("*.cdb"),
                                           QDir::Files);
  for (int i = 0; i < files.size(); ++i)
    names.append(QFileInfo(files.at(i)).completeBaseName());
  std::sort(names.begin(), names.end(), naturalLess);
  return names;
}

// 레이어가 없으면 만든다. 표준 레이어 이름은 정답 도면의 색을 따른다.
Layer *AiBridgeServer::ensureLayer(const QString &name, bool *created) {
  LayerManager *lm = m_ctx->layerManager();
  if (created)
    *created = false;
  if (!lm || name.isEmpty())
    return nullptr;
  Layer *layer = lm->getLayer(name);
  if (layer)
    return layer;
  const QString upper = name.toUpper();
  QColor color(Qt::white);
  if (upper == QLatin1String("FUR") || upper == QLatin1String("DOOR") ||
      upper == QLatin1String("WID") || upper == QLatin1String("COR"))
    color = QColor(0, 255, 255);
  lm->addLayer(Layer(name, color, 0.25, QStringLiteral("Continuous")));
  if (created)
    *created = true;
  return lm->getLayer(name);
}

// 블록 목록: 분류를 주면 이름·크기·미리보기 모음 이미지를, 생략하면 분류 목록을 돌려준다.
QJsonObject AiBridgeServer::toolListBlocks(const QJsonObject &args) {
  const QString root = blocksRoot();
  if (root.isEmpty())
    return fail(QStringLiteral("블록 라이브러리 폴더(blocks)를 찾을 수 없습니다."));

  const QString wanted = args.value(QStringLiteral("category")).toString();
  QStringList cats;
  const QString category = wanted.isEmpty() ? QString() : resolveBlockCategory(wanted, &cats);
  if (wanted.isEmpty())
    resolveBlockCategory(QString(), &cats);
  if (!wanted.isEmpty() && category.isEmpty())
    return fail(QStringLiteral("분류 '%1' 가 없습니다. 사용 가능한 분류: %2")
                    .arg(wanted, cats.join(QStringLiteral(", "))));

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);

  if (wanted.isEmpty()) {
    QJsonArray list;
    for (int i = 0; i < cats.size(); ++i) {
      QJsonObject o;
      o.insert(QStringLiteral("category"), cats.at(i));
      o.insert(QStringLiteral("count"), blockNames(cats.at(i)).size());
      list.append(o);
    }
    result.insert(QStringLiteral("categories"), list);
    result.insert(QStringLiteral("hint"),
                  QStringLiteral("category 를 지정해 다시 호출하면 블록 이름, 크기, "
                                 "미리보기 이미지를 돌려줍니다."));
    return result;
  }

  FileManager *fm = m_ctx->fileManager();
  if (!fm)
    return fail(QStringLiteral("파일 관리자를 사용할 수 없습니다."));

  const QStringList names = blockNames(category);
  QJsonArray blocks;
  struct Cell {
    QString label;
    QImage thumb;
  };
  QList<Cell> cells;

  for (int i = 0; i < names.size(); ++i) {
    const QString path = blocksRoot() + QLatin1Char('/') + category + QLatin1Char('/') +
                         names.at(i) + QStringLiteral(".cdb");
    QString origLayer;
    QList<CadItem *> items = fm->importBlock(path, &origLayer);
    QJsonObject o;
    o.insert(QStringLiteral("name"), names.at(i));
    QString sizeText = QStringLiteral("?");
    if (!items.isEmpty()) {
      bool hasAttr = false;
      MyGraphicsScene temp;
      for (int k = 0; k < items.size(); ++k) {
        temp.addItem(items.at(k));
        if (items.at(k)->getProperties().value(QStringLiteral("isAttribute"), false).toBool())
          hasAttr = true;
      }
      const QRectF r = temp.itemsBoundingRect();
      for (int k = 0; k < items.size(); ++k)
        temp.removeItem(items.at(k));
      QJsonArray size;
      size.append(qRound(r.width()));
      size.append(qRound(r.height()));
      QJsonArray bbox; // 삽입점(블록 원점) 기준 위치 [x, y, 폭, 높이]
      bbox.append(qRound(r.left()));
      bbox.append(qRound(r.top()));
      bbox.append(qRound(r.width()));
      bbox.append(qRound(r.height()));
      o.insert(QStringLiteral("sizeMm"), size);
      o.insert(QStringLiteral("bboxFromInsertPoint"), bbox);
      o.insert(QStringLiteral("entities"), items.size());
      o.insert(QStringLiteral("hasAttributes"), hasAttr);
      o.insert(QStringLiteral("originalLayer"), origLayer);
      sizeText = QStringLiteral("%1 x %2").arg(qRound(r.width())).arg(qRound(r.height()));
      qDeleteAll(items);
    } else {
      o.insert(QStringLiteral("error"), QStringLiteral("블록을 읽지 못했습니다."));
    }
    blocks.append(o);

    Cell cell;
    cell.label = names.at(i) + QStringLiteral("  ") + sizeText;
    cell.thumb = fm->loadThumbnail(path);
    cells.append(cell);
  }
  result.insert(QStringLiteral("category"), category);
  result.insert(QStringLiteral("count"), names.size());
  result.insert(QStringLiteral("blocks"), blocks);

  // 미리보기 모음 이미지
  const int cellW = 240;
  const int cellH = 200;
  const int cols = 5;
  const int rows = qMax(1, (cells.size() + cols - 1) / cols);
  QImage sheet(cols * cellW, rows * cellH, QImage::Format_ARGB32);
  sheet.fill(QColor(30, 30, 30));
  {
    QPainter painter(&sheet);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    for (int i = 0; i < cells.size(); ++i) {
      const int cx = (i % cols) * cellW;
      const int cy = (i / cols) * cellH;
      painter.setPen(QColor(70, 70, 70));
      painter.drawRect(cx + 2, cy + 2, cellW - 4, cellH - 4);
      if (!cells.at(i).thumb.isNull()) {
        const QImage scaled = cells.at(i).thumb.scaled(
            cellW - 20, cellH - 50, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(cx + (cellW - scaled.width()) / 2,
                          cy + 8 + (cellH - 50 - scaled.height()) / 2, scaled);
      }
      painter.setPen(QColor(255, 220, 0));
      painter.drawText(QRect(cx + 8, cy + cellH - 38, cellW - 16, 30),
                       Qt::AlignLeft | Qt::AlignVCenter, cells.at(i).label);
    }
  }
  const QString sheetPath = QDir::temp().filePath(
      QStringLiteral("autodraft_blocks_%1.png").arg(category));
  if (sheet.save(sheetPath))
    result.insert(QStringLiteral("sheetPath"), sheetPath);
  result.insert(QStringLiteral("note"),
                QStringLiteral("sizeMm 는 선 두께가 포함된 근사값입니다. 미리보기 이미지의 "
                               "이름·크기로 어떤 블록인지 확인하고 insert_block 으로 삽입하세요."));
  return result;
}

// 블록 삽입: 도구를 거치지 않고 블록 파일을 바로 불러와 BlockItem 으로 넣는다.
QJsonObject AiBridgeServer::toolInsertBlock(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  if (blocksRoot().isEmpty())
    return fail(QStringLiteral("블록 라이브러리 폴더(blocks)를 찾을 수 없습니다."));

  QStringList cats;
  const QString category =
      resolveBlockCategory(args.value(QStringLiteral("category")).toString(), &cats);
  if (category.isEmpty())
    return fail(QStringLiteral("category 가 없거나 잘못되었습니다. 사용 가능한 분류: %1")
                    .arg(cats.join(QStringLiteral(", "))));
  const QStringList names = blockNames(category);
  const QString wantedName = args.value(QStringLiteral("name")).toString();
  QString name;
  for (int i = 0; i < names.size(); ++i)
    if (names.at(i).compare(wantedName, Qt::CaseInsensitive) == 0)
      name = names.at(i);
  if (name.isEmpty())
    return fail(QStringLiteral("'%1' 분류에 블록 '%2' 가 없습니다. 사용 가능: %3")
                    .arg(category, wantedName, names.join(QStringLiteral(", "))));

  QPointF at;
  if (!readPoint(args.value(QStringLiteral("at")), at))
    return fail(QStringLiteral("at 은 삽입점 [x, y] 입니다. 예: "
                               "{\"category\":\"furniture\",\"name\":\"ft1\",\"at\":[1000,500]}"));
  const double rotation = args.value(QStringLiteral("rotation")).toDouble(0.0);
  const double scale = args.value(QStringLiteral("scale")).toDouble(1.0);
  if (scale <= 0.0 || scale > 100.0)
    return fail(QStringLiteral("scale 은 0 보다 크고 100 이하여야 합니다 (1 = 원래 크기)."));

  FileManager *fm = m_ctx->fileManager();
  if (!fm)
    return fail(QStringLiteral("파일 관리자를 사용할 수 없습니다."));
  const QString path = blocksRoot() + QLatin1Char('/') + category + QLatin1Char('/') +
                       name + QStringLiteral(".cdb");
  QString origLayer;
  QList<CadItem *> items = fm->importBlock(path, &origLayer);
  if (items.isEmpty())
    return fail(QStringLiteral("블록 파일을 읽지 못했습니다: %1").arg(name));

  // 레이어: 지정값 > 블록 원래 레이어 > 현재 레이어. 지정한 레이어가 없으면 만든다.
  QString layerName = args.value(QStringLiteral("layer")).toString();
  if (layerName.isEmpty())
    layerName = origLayer;
  bool created = false;
  Layer *layer = layerName.isEmpty() ? m_ctx->layerManager()->getCurrentLayer()
                                     : ensureLayer(layerName, &created);
  if (!layer) {
    qDeleteAll(items);
    return fail(QStringLiteral("레이어를 정하지 못했습니다."));
  }
  if (layer->isLayerLock()) {
    qDeleteAll(items);
    return fail(QStringLiteral("레이어 '%1' 는 잠겨 있습니다. 다른 레이어를 지정하세요.")
                    .arg(layer->name()));
  }

  // 속성 블록은 입력창을 띄우지 않고 블록에 저장된 기본값을 그대로 쓴다
  BlockItem *block = new BlockItem(items, MyVector(at), name, scale, scale, rotation);
  block->setLayerName(layer->name());
  block->setPen(penForLayer(layer));
  if (m_ctx->undoStack())
    m_ctx->undoStack()->push(new AddEntityCommand(scene, block));
  else
    scene->addItem(block);

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("entity"), describeItem(block));
  result.insert(QStringLiteral("block"),
                QStringLiteral("%1/%2").arg(category, name));
  result.insert(QStringLiteral("layer"), layer->name());
  result.insert(QStringLiteral("layerCreated"), created);
  const QRectF b = block->sceneBoundingRect();
  QJsonArray placed;
  placed.append(qRound(b.left()));
  placed.append(qRound(b.top()));
  placed.append(qRound(b.width()));
  placed.append(qRound(b.height()));
  result.insert(QStringLiteral("placedBounds"), placed);
  return result;
}

// 선형·연속 치수. 점 목록(2점 이상)을 주면 이웃한 점끼리 치수를 만들고(연속 치수), 같은 치수선 위에 놓는다.
//   orientation: horizontal(가로 길이, lineAt = 치수선의 y) | vertical(세로 길이, lineAt = 치수선의 x) |
//                aligned(두 점과 평행, offset = 치수선까지 수직 거리, 부호로 쪽 선택) | auto(점 배열 모양으로 판단)
//   overallAt: 있으면 첫 점~끝 점의 전체 치수를 그 좌표의 치수선에 한 줄 더 만든다
QJsonObject AiBridgeServer::toolCreateDimension(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  const QJsonArray arr = args.value(QStringLiteral("points")).toArray();
  QList<QPointF> pts;
  for (int i = 0; i < arr.size(); ++i) {
    QPointF p;
    if (!readPoint(arr.at(i), p))
      return fail(QStringLiteral("points[%1] 이 [x, y] 형식이 아닙니다. 예: "
                                 "{\"points\":[[0,0],[3600,0],[7200,0]],\"orientation\":\"horizontal\","
                                 "\"lineAt\":-600}")
                      .arg(i));
    pts.append(p);
  }
  if (pts.size() < 2)
    return fail(QStringLiteral("points 에 두 점 이상 필요합니다. 3점 이상이면 연속 치수가 "
                               "만들어집니다."));

  // 방향 결정
  QString orient = args.value(QStringLiteral("orientation")).toString(QStringLiteral("auto"));
  if (orient != QLatin1String("horizontal") && orient != QLatin1String("vertical") &&
      orient != QLatin1String("aligned") && orient != QLatin1String("auto"))
    return fail(QStringLiteral("orientation 은 horizontal, vertical, aligned, auto 중 "
                               "하나입니다."));
  double minX = pts.first().x(), maxX = minX, minY = pts.first().y(), maxY = minY;
  for (int i = 1; i < pts.size(); ++i) {
    minX = qMin(minX, pts.at(i).x());
    maxX = qMax(maxX, pts.at(i).x());
    minY = qMin(minY, pts.at(i).y());
    maxY = qMax(maxY, pts.at(i).y());
  }
  if (orient == QLatin1String("auto"))
    orient = (maxX - minX >= maxY - minY) ? QStringLiteral("horizontal")
                                          : QStringLiteral("vertical");

  const bool horizontal = orient == QLatin1String("horizontal");
  const bool vertical = orient == QLatin1String("vertical");
  const bool aligned = orient == QLatin1String("aligned");

  // 치수선 위치
  double lineAt = 0.0;
  double offset = 0.0;
  if (horizontal || vertical) {
    if (!args.contains(QStringLiteral("lineAt")))
      return fail(QStringLiteral("lineAt 이 필요합니다. horizontal 이면 치수선의 y 좌표, "
                                 "vertical 이면 치수선의 x 좌표(mm)입니다."));
    lineAt = args.value(QStringLiteral("lineAt")).toDouble();
  } else {
    offset = args.value(QStringLiteral("offset")).toDouble(600.0);
  }

  // 축 방향으로 정렬하고 겹치는 점은 제거 (연속 치수는 순서대로 이어져야 한다)
  QList<QPointF> sorted = pts;
  if (horizontal) {
    for (int i = 1; i < sorted.size(); ++i)
      for (int j = i; j > 0 && sorted.at(j).x() < sorted.at(j - 1).x(); --j)
        std::swap(sorted[j], sorted[j - 1]);
  } else if (vertical) {
    for (int i = 1; i < sorted.size(); ++i)
      for (int j = i; j > 0 && sorted.at(j).y() < sorted.at(j - 1).y(); --j)
        std::swap(sorted[j], sorted[j - 1]);
  }
  QList<QPointF> chain;
  for (int i = 0; i < sorted.size(); ++i)
    if (chain.isEmpty() || QLineF(chain.last(), sorted.at(i)).length() >= 1.0)
      chain.append(sorted.at(i));
  if (chain.size() < 2)
    return fail(QStringLiteral("서로 1mm 이상 떨어진 점이 두 개 이상 필요합니다."));

  // 레이어: 지정값 > 치수 스타일의 치수선 레이어 > "DIM"(없으면 만든다)
  QString layerName = args.value(QStringLiteral("layer")).toString();
  if (layerName.isEmpty())
    layerName = scene->dimensionStyle().dimLineLayer;
  if (layerName.isEmpty())
    layerName = QStringLiteral("DIM");
  bool created = false;
  Layer *layer = ensureLayer(layerName, &created);
  if (!layer)
    return fail(QStringLiteral("치수 레이어를 정하지 못했습니다."));
  if (layer->isLayerLock())
    return fail(QStringLiteral("레이어 '%1' 는 잠겨 있습니다. 다른 레이어를 지정하세요.")
                    .arg(layer->name()));
  const QPen pen = penForLayer(layer);

  // (시작, 끝, 치수선 좌표) 목록: 연속 치수 + 선택적 전체 치수
  struct Seg {
    QPointF a;
    QPointF b;
    double at;
    bool overall;
  };
  QList<Seg> segs;
  for (int i = 0; i + 1 < chain.size(); ++i) {
    Seg s;
    s.a = chain.at(i);
    s.b = chain.at(i + 1);
    s.at = lineAt;
    s.overall = false;
    segs.append(s);
  }
  if (args.contains(QStringLiteral("overallAt")) && chain.size() >= 3 && !aligned) {
    Seg s;
    s.a = chain.first();
    s.b = chain.last();
    s.at = args.value(QStringLiteral("overallAt")).toDouble();
    s.overall = true;
    segs.append(s);
  }

  QJsonArray made;
  if (m_ctx->undoStack())
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: create_dimension"));
  for (int i = 0; i < segs.size(); ++i) {
    const Seg &s = segs.at(i);
    MyDimension d{MyVector(s.a), MyVector(s.b), MyVector(), QString()};
    d.dimensionType = DimensionType::Linear;
    double value = 0.0;
    if (horizontal) {
      d.useFixedAngle = true;
      d.fixedAngle = 0.0;
      d.dimLine = MyVector(QPointF((s.a.x() + s.b.x()) * 0.5, s.at));
      value = qAbs(s.b.x() - s.a.x());
    } else if (vertical) {
      d.useFixedAngle = true;
      d.fixedAngle = 90.0;
      d.dimLine = MyVector(QPointF(s.at, (s.a.y() + s.b.y()) * 0.5));
      value = qAbs(s.b.y() - s.a.y());
    } else {
      const QLineF line(s.a, s.b);
      const QPointF mid((s.a.x() + s.b.x()) * 0.5, (s.a.y() + s.b.y()) * 0.5);
      const QPointF normal(-line.dy() / line.length(), line.dx() / line.length());
      d.useFixedAngle = false;
      d.dimLine = MyVector(QPointF(mid.x() + normal.x() * offset, mid.y() + normal.y() * offset));
      value = line.length();
    }
    QGraphicsItem *item = m_ctx->entityFactory()->createDimension(d, pen);
    if (LinearDimensionItem *li = dynamic_cast<LinearDimensionItem *>(item))
      li->setGripsVisible(false);
    const QJsonObject r = placeItem(scene, item, layer);
    QJsonObject o;
    o.insert(QStringLiteral("ok"), r.value(QStringLiteral("ok")));
    o.insert(QStringLiteral("value"), qRound(value * 100.0) / 100.0);
    o.insert(QStringLiteral("overall"), s.overall);
    if (r.contains(QStringLiteral("entity")))
      o.insert(QStringLiteral("id"), r.value(QStringLiteral("entity")).toObject().value(QStringLiteral("id")));
    made.append(o);
  }
  if (m_ctx->undoStack())
    m_ctx->undoStack()->endMacro();

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("createdCount"), made.size());
  result.insert(QStringLiteral("orientation"), orient);
  result.insert(QStringLiteral("layer"), layer->name());
  result.insert(QStringLiteral("layerCreated"), created);
  result.insert(QStringLiteral("dimensions"), made);
  return result;
}

// 원·호의 지름/반지름 치수.
//   대상: circleId(create_circle·get_entities 가 돌려준 id, 원 또는 호) 또는 center + radius(원으로 취급)
//   kind: diameter(지름, 원의 기본) | radius(반지름, 호의 기본)
//   치수선 위치: at([x,y]) 를 주면 그 점, 아니면 angle(도, 기본 45)과 inside(기본 false = 원 바깥쪽 지시선)
QJsonObject AiBridgeServer::toolCreateCircleDimension(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF center;
  double radius = 0.0;
  bool fullCircle = true;
  if (args.contains(QStringLiteral("circleId"))) {
    const int id = args.value(QStringLiteral("circleId")).toInt(-1);
    QGraphicsItem *found = nullptr;
    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i) {
      const QVariant v = all.at(i)->data(kAiHandleDataKey);
      if (v.isValid() && v.toInt() == id) {
        found = all.at(i);
        break;
      }
    }
    if (!found)
      return fail(QStringLiteral("id %1 인 객체를 찾지 못했습니다. create_circle 응답의 entity.id 나 "
                                 "get_entities(type:\"Circle\") 로 확인하세요.")
                      .arg(id));
    if (CircleItem *c = dynamic_cast<CircleItem *>(found)) {
      center = c->getCircle().center.toPointF();
      radius = c->getCircle().radius;
    } else if (ArcItem *a = dynamic_cast<ArcItem *>(found)) {
      center = a->getArc().center.toPointF();
      radius = a->getArc().radius;
      fullCircle = false;
    } else {
      return fail(QStringLiteral("id %1 은 원이나 호가 아닙니다. 선·다각형 길이는 create_dimension "
                                 "을 쓰세요.")
                      .arg(id));
    }
  } else {
    if (!readPoint(args.value(QStringLiteral("center")), center))
      return fail(QStringLiteral("circleId 또는 center([x, y]) + radius 가 필요합니다. 예: "
                                 "{\"circleId\":12} 또는 {\"center\":[0,0],\"radius\":500}"));
    radius = args.value(QStringLiteral("radius")).toDouble(0.0);
  }
  if (radius <= 0.0)
    return fail(QStringLiteral("radius 는 0 보다 큰 숫자(mm)여야 합니다."));

  const QString kind = args.value(QStringLiteral("kind"))
                           .toString(fullCircle ? QStringLiteral("diameter")
                                                : QStringLiteral("radius"));
  if (kind != QLatin1String("diameter") && kind != QLatin1String("radius"))
    return fail(QStringLiteral("kind 는 diameter(지름) 또는 radius(반지름) 입니다."));
  const bool diametric = kind == QLatin1String("diameter");

  // 치수선이 놓일 점
  QPointF place;
  if (args.contains(QStringLiteral("at"))) {
    if (!readPoint(args.value(QStringLiteral("at")), place))
      return fail(QStringLiteral("at 은 [x, y] 형식이어야 합니다."));
    if (QLineF(center, place).length() < 1.0)
      return fail(QStringLiteral("at 이 원 중심과 같습니다. 중심에서 떨어진 점을 주거나 angle 을 "
                                 "쓰세요."));
  } else {
    const double angle = args.value(QStringLiteral("angle")).toDouble(45.0);
    const bool inside = args.value(QStringLiteral("inside")).toBool(false);
    const double rad = angle * M_PI / 180.0;
    const double gap = args.value(QStringLiteral("gap")).toDouble(qMax(radius * 0.5, 500.0));
    const double dist = inside ? radius * 0.5 : radius + gap;
    place = QPointF(center.x() + qCos(rad) * dist, center.y() + qSin(rad) * dist);
  }

  // 레이어: 지정값 > 치수 스타일의 치수선 레이어 > "DIM"(없으면 만든다)
  QString layerName = args.value(QStringLiteral("layer")).toString();
  if (layerName.isEmpty())
    layerName = scene->dimensionStyle().dimLineLayer;
  if (layerName.isEmpty())
    layerName = QStringLiteral("DIM");
  bool created = false;
  Layer *layer = ensureLayer(layerName, &created);
  if (!layer)
    return fail(QStringLiteral("치수 레이어를 정하지 못했습니다."));
  if (layer->isLayerLock())
    return fail(QStringLiteral("레이어 '%1' 는 잠겨 있습니다. 다른 레이어를 지정하세요.")
                    .arg(layer->name()));

  const DimensionStyle style = scene->dimensionStyle();
  MyDimension d{MyVector(center), MyVector(QPointF(center.x() + radius, center.y())),
                MyVector(place), QString()};
  d.dimensionType = diametric ? DimensionType::Diametric : DimensionType::Radial;
  d.isFullCircle = fullCircle;
  d.precision = style.precision;
  d.suppressTrailingZeros = style.suppressTrailingZeros;
  d.suppressTrailingZerosTol = style.suppressTrailingZerosTol;

  QGraphicsItem *item = m_ctx->entityFactory()->createDimension(d, penForLayer(layer));
  if (RadialDimensionItem *ri = dynamic_cast<RadialDimensionItem *>(item))
    ri->setGripsVisible(false);
  QJsonObject result = placeItem(scene, item, layer);
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  result.insert(QStringLiteral("kind"), kind);
  result.insert(QStringLiteral("value"), qRound((diametric ? radius * 2.0 : radius) * 100.0) / 100.0);
  result.insert(QStringLiteral("layer"), layer->name());
  result.insert(QStringLiteral("layerCreated"), created);
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 치수선 탭 도구 구동 (run_dimension_tool / merge_dimensions / split_dimension),
// GD&T·데이텀 직접 생성, 속성 편집
// ─────────────────────────────────────────────────────────────────────────────

QGraphicsItem *AiBridgeServer::itemById(MyGraphicsScene *scene, int id) const {
  const QList<QGraphicsItem *> all = scene->items();
  for (int i = 0; i < all.size(); ++i) {
    const QVariant v = all.at(i)->data(kAiHandleDataKey);
    if (v.isValid() && v.toInt() == id)
      return all.at(i);
  }
  return nullptr;
}

// 객체의 도형 위에서 클릭했을 때 그 객체가 맞는 점을 찾는다 (도구의 객체 선택 단계용)
bool AiBridgeServer::pickPointOnItem(MyGraphicsScene *scene, QGraphicsItem *item,
                                     QPointF &out) const {
  CadItem *cad = dynamic_cast<CadItem *>(item);
  if (!cad)
    return false;
  const QPainterPath path = cad->geometryPath();
  if (path.isEmpty())
    return false;
  static const double fractions[] = {0.5,  0.35, 0.65, 0.2,  0.8, 0.1, 0.9,
                                     0.45, 0.55, 0.3,  0.7,  0.05, 0.95};
  const int count = int(sizeof(fractions) / sizeof(fractions[0]));
  // pass 0~1: 그 점의 맨 위 객체가 item 인 점(도구는 최상위 객체를 고른다), pass 2~4: item 이 겹쳐 있기만 해도 허용
  for (int pass = 0; pass < 5; ++pass) {
    for (int i = 0; i < count; ++i) {
      QPointF p = path.pointAtPercent(fractions[i]);
      if (pass == 1 || pass == 3)
        p = item->mapToScene(p);
      QList<QGraphicsItem *> hit;
      if (pass == 4)
        hit = scene->items(QRectF(p.x() - 1.0, p.y() - 1.0, 2.0, 2.0));
      else
        hit = scene->items(p);
      const bool topmostOnly = pass < 2;
      for (int j = 0; j < hit.size(); ++j) {
        if (!dynamic_cast<CadItem *>(hit.at(j)))
          continue;
        if (hit.at(j) == item) {
          out = p;
          return true;
        }
        if (topmostOnly)
          break;
      }
    }
  }
  return false;
}

// 치수선 탭의 클릭 구동 도구 이름 → 도구 객체
AbstractTool *AiBridgeServer::createDimensionTool(const QString &name) const {
  if (name == QLatin1String("dimlinear_hv"))
    return new DimLinearTool();
  if (name == QLatin1String("linearDim"))
    return new LinearDimensionTool();
  if (name == QLatin1String("radDim"))
    return new RadialDimensionTool();
  if (name == QLatin1String("angDim"))
    return new AngularDimensionTool();
  if (name == QLatin1String("dimangular"))
    return new AngularDimensionToolA();
  if (name == QLatin1String("dimbaseline"))
    return new BaselineDimensionTool();
  if (name == QLatin1String("dimcontinue"))
    return new ContinuedimensionTool();
  if (name == QLatin1String("autocontinuousdim"))
    return new AutoContinuousDimTool();
  if (name == QLatin1String("leader"))
    return new LeaderTool();
  if (name == QLatin1String("holecallout"))
    return new HoleCalloutTool();
  if (name == QLatin1String("halfdim"))
    return new HalfDimensionTool();
  if (name == QLatin1String("areadim"))
    return new AreaDimensionTool();
  if (name == QLatin1String("dimlea"))
    return new DimLeaderTool();
  if (name == QLatin1String("dimMoveText"))
    return new DimMoveTextTool();
  if (name == QLatin1String("dimMerge"))
    return new DimMergeTool();
  if (name == QLatin1String("dimSplit"))
    return new DimSplitTool();
  return nullptr;
}

// steps 를 도구가 바로 실행할 수 있는 형태로 정리한다: pick(객체 id) → click(점)
bool AiBridgeServer::normalizeSteps(MyGraphicsScene *scene, const QJsonArray &in,
                                    QJsonArray &out, QString &error) {
  if (in.isEmpty()) {
    error = QStringLiteral("steps 가 비어 있습니다. 예: [{\"click\":[0,0]},{\"click\":[1000,0]},"
                           "{\"click\":[500,-600]}]");
    return false;
  }
  for (int i = 0; i < in.size(); ++i) {
    const QJsonObject s = in.at(i).toObject();
    const QString button = s.value(QStringLiteral("button")).toString(QStringLiteral("left"));
    if (button != QLatin1String("left") && button != QLatin1String("right")) {
      error = QStringLiteral("steps[%1].button 은 left 또는 right 입니다.").arg(i);
      return false;
    }
    QJsonObject n;
    n.insert(QStringLiteral("button"), button);
    if (s.contains(QStringLiteral("pick"))) {
      const int id = s.value(QStringLiteral("pick")).toInt(-1);
      QGraphicsItem *item = itemById(scene, id);
      if (!item) {
        error = QStringLiteral("steps[%1]: id %2 인 객체를 찾지 못했습니다. get_entities 로 id 를 "
                               "확인하세요.")
                    .arg(i)
                    .arg(id);
        return false;
      }
      QPointF p;
      if (!pickPointOnItem(scene, item, p)) {
        error = QStringLiteral("steps[%1]: id %2 객체 위에서 클릭할 점을 찾지 못했습니다(다른 객체에 "
                               "가려졌을 수 있음). click 으로 직접 점을 주세요.")
                    .arg(i)
                    .arg(id);
        return false;
      }
      n.insert(QStringLiteral("click"), QJsonArray{p.x(), p.y()});
    } else if (s.contains(QStringLiteral("click")) || s.contains(QStringLiteral("move"))) {
      const QString key = s.contains(QStringLiteral("click")) ? QStringLiteral("click")
                                                              : QStringLiteral("move");
      QPointF p;
      if (!readPoint(s.value(key), p)) {
        error = QStringLiteral("steps[%1].%2 는 [x, y] 형식이어야 합니다.").arg(i).arg(key);
        return false;
      }
      n.insert(key, QJsonArray{p.x(), p.y()});
    } else if (s.contains(QStringLiteral("command"))) {
      n.insert(QStringLiteral("command"), s.value(QStringLiteral("command")).toString());
    } else {
      error = QStringLiteral("steps[%1] 은 click([x,y]), pick(객체 id), move([x,y]), "
                             "command(문자) 중 하나를 가져야 합니다.")
                  .arg(i);
      return false;
    }
    out.append(n);
  }
  return true;
}

// 도구를 시작해 steps 를 순서대로 주입한다. 만들어진 객체·지워진 객체·실행 취소 단계 수로 결과를 보고한다.
QJsonObject AiBridgeServer::driveTool(MyGraphicsScene *scene, AbstractTool *rawTool,
                                      const QJsonArray &steps, const QString &label) {
  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  QSet<QGraphicsItem *> beforeCad;
  QHash<QGraphicsItem *, int> knownIds;
  for (QSet<QGraphicsItem *>::const_iterator it = before.constBegin(); it != before.constEnd();
       ++it) {
    if (!dynamic_cast<CadItem *>(*it))
      continue;
    beforeCad.insert(*it);
    const QVariant v = (*it)->data(kAiHandleDataKey);
    if (v.isValid())
      knownIds.insert(*it, v.toInt());
  }
  const int undoBefore = m_ctx->undoStack() ? m_ctx->undoStack()->index() : 0;

  QString stepError;
  int done = 0;
  {
    SnapSuspender guard(m_ctx->snapManager());
    QPointer<AbstractTool> tool(rawTool);
    m_ctx->startTool(rawTool);
    for (int i = 0; i < steps.size(); ++i) {
      if (tool.isNull()) {
        stepError = QStringLiteral("도구가 steps[%1] 전에 스스로 종료되었습니다(보통 앞 단계에서 "
                                   "작업이 끝났거나 취소된 경우).")
                        .arg(i);
        break;
      }
      const QJsonObject s = steps.at(i).toObject();
      if (s.contains(QStringLiteral("command"))) {
        tool->processCommand(s.value(QStringLiteral("command")).toString());
      } else {
        const QString key = s.contains(QStringLiteral("click")) ? QStringLiteral("click")
                                                                : QStringLiteral("move");
        QPointF p;
        readPoint(s.value(key), p);
        const Qt::MouseButton button =
            s.value(QStringLiteral("button")).toString() == QLatin1String("right")
                ? Qt::RightButton
                : Qt::LeftButton;
        tool->mouseMove(p);
        if (key == QLatin1String("click") && !tool.isNull()) {
          tool->mousePress(p, button);
          if (!tool.isNull())
            tool->mouseRelease(p, button);
        }
      }
      ++done;
    }
    m_ctx->finishCurrentTool();
  }

  const QSet<QGraphicsItem *> after = snapshotItems(scene);
  int removedCount = 0;
  QJsonArray removedIds;
  for (QSet<QGraphicsItem *>::const_iterator it = beforeCad.constBegin();
       it != beforeCad.constEnd(); ++it) {
    if (after.contains(*it))
      continue;
    ++removedCount;
    if (knownIds.contains(*it))
      removedIds.append(knownIds.value(*it));
  }
  const int undoSteps = (m_ctx->undoStack() ? m_ctx->undoStack()->index() : 0) - undoBefore;
  QJsonObject created = newItemsResult(scene, before, QString());
  const bool createdAny = created.value(QStringLiteral("ok")).toBool();

  if (!createdAny && removedCount == 0 && undoSteps <= 0) {
    return fail(QStringLiteral("도구 '%1' 가 아무것도 만들거나 바꾸지 않았습니다(실행한 단계 %2/%3). %4"
                               "도구별 클릭 순서(INSTRUCTIONS 의 [치수선 탭 도구])와 점 위치를 확인하고, "
                               "객체를 고르는 단계는 pick(id)으로 주세요.")
                    .arg(label)
                    .arg(done)
                    .arg(steps.size())
                    .arg(stepError.isEmpty() ? QString() : stepError + QStringLiteral(" ")));
  }

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("tool"), label);
  result.insert(QStringLiteral("stepsDone"), done);
  result.insert(QStringLiteral("undoSteps"), undoSteps);
  if (createdAny) {
    result.insert(QStringLiteral("createdCount"), created.value(QStringLiteral("createdCount")));
    result.insert(QStringLiteral("countByLayer"), created.value(QStringLiteral("countByLayer")));
    result.insert(QStringLiteral("entities"), created.value(QStringLiteral("entities")));
  } else {
    result.insert(QStringLiteral("createdCount"), 0);
  }
  result.insert(QStringLiteral("removedCount"), removedCount);
  if (!removedIds.isEmpty())
    result.insert(QStringLiteral("removedIds"), removedIds);
  if (!stepError.isEmpty())
    result.insert(QStringLiteral("warning"), stepError);
  if (!createdAny && removedCount == 0)
    result.insert(QStringLiteral("note"),
                  QStringLiteral("새 객체 없이 기존 객체만 바뀌었습니다. screenshot 으로 확인하세요."));
  return result;
}

// 치수선 탭의 클릭 구동 도구 하나를 실행한다
QJsonObject AiBridgeServer::toolRunDimensionTool(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const QString name = args.value(QStringLiteral("tool")).toString();
  AbstractTool *tool = createDimensionTool(name);
  if (!tool)
    return fail(QStringLiteral("tool '%1' 은 지원하지 않습니다. 사용 가능: dimlinear_hv, linearDim, "
                               "radDim, angDim, dimangular, dimbaseline, dimcontinue, "
                               "autocontinuousdim, leader, holecallout, halfdim, areadim, dimlea, "
                               "dimMoveText, dimMerge, dimSplit")
                    .arg(name));
  QJsonArray steps;
  QString error;
  if (!normalizeSteps(scene, args.value(QStringLiteral("steps")).toArray(), steps, error)) {
    delete tool;
    return fail(error);
  }
  return driveTool(scene, tool, steps, name);
}

// 치수병합: 선형 치수 2개를 하나로(merge), 또는 여러 개를 감싸는 토탈 치수 추가(total)
QJsonObject AiBridgeServer::toolMergeDimensions(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const QString mode = args.value(QStringLiteral("mode")).toString(QStringLiteral("merge"));
  if (mode != QLatin1String("merge") && mode != QLatin1String("total"))
    return fail(QStringLiteral("mode 는 merge(2개를 1개로 합침, 기본) 또는 total(여러 치수를 "
                               "감싸는 전체 치수 추가, 기존 치수 유지) 입니다."));
  const QJsonArray ids = args.value(QStringLiteral("ids")).toArray();
  if (ids.size() < 2)
    return fail(QStringLiteral("ids 에 치수 id 가 2개 이상 필요합니다. 예: {\"ids\":[5,6]}"));
  if (mode == QLatin1String("merge") && ids.size() != 2)
    return fail(QStringLiteral("merge 는 정확히 2개의 치수만 합칠 수 있습니다. 여러 개의 전체 치수는 "
                               "mode:\"total\" 을 쓰세요."));

  QJsonArray steps;
  for (int i = 0; i < ids.size(); ++i) {
    const int id = ids.at(i).toInt(-1);
    QGraphicsItem *item = itemById(scene, id);
    if (!item)
      return fail(QStringLiteral("ids[%1]: id %2 인 객체를 찾지 못했습니다.").arg(i).arg(id));
    if (!dynamic_cast<LinearDimensionItem *>(item))
      return fail(QStringLiteral("ids[%1]: id %2 는 선형 치수(수평/수직/정렬)가 아닙니다. 치수 "
                                 "병합은 선형 치수만 됩니다.")
                      .arg(i)
                      .arg(id));
    QPointF p;
    if (!pickPointOnItem(scene, item, p))
      return fail(QStringLiteral("ids[%1]: id %2 치수 위에서 클릭할 점을 찾지 못했습니다.")
                      .arg(i)
                      .arg(id));
    QJsonObject s;
    s.insert(QStringLiteral("click"), QJsonArray{p.x(), p.y()});
    s.insert(QStringLiteral("button"), QStringLiteral("left"));
    steps.append(s);
  }
  QJsonObject cmd;
  cmd.insert(QStringLiteral("command"),
             mode == QLatin1String("total") ? QStringLiteral("n") : QString());
  steps.append(cmd);
  return driveTool(scene, new DimMergeTool(), steps,
                   mode == QLatin1String("total") ? QStringLiteral("dimMerge(total)")
                                                  : QStringLiteral("dimMerge"));
}

// 치수분리: 선형 치수 하나를 points 위치에서 여러 치수로 쪼갠다 (keepOriginal 이면 원래 치수를 남기고 안쪽에 추가)
QJsonObject AiBridgeServer::toolSplitDimension(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const int id = args.value(QStringLiteral("id")).toInt(-1);
  QGraphicsItem *item = itemById(scene, id);
  if (!item)
    return fail(QStringLiteral("id %1 인 객체를 찾지 못했습니다. get_entities 로 id 를 확인하세요.")
                    .arg(id));
  LinearDimensionItem *li = dynamic_cast<LinearDimensionItem *>(item);
  const MyDimension *d = li ? dynamic_cast<const MyDimension *>(li->getEntityData()) : nullptr;
  if (!d)
    return fail(QStringLiteral("id %1 는 선형 치수(수평/수직/정렬)가 아닙니다. 치수분리는 선형 "
                               "치수만 됩니다.")
                    .arg(id));

  const QJsonArray pts = args.value(QStringLiteral("points")).toArray();
  if (pts.isEmpty())
    return fail(QStringLiteral("points 에 나눌 위치(치수보조선이 놓일 점 [x,y])가 1개 이상 필요합니다."));

  MyVector dir;
  if (!DimMergeTool::dimDirection(d, dir))
    return fail(QStringLiteral("이 치수의 방향을 알 수 없습니다(두 점이 같음)."));
  if (MyMath::dotProduct(d->end - d->start, dir) < 0)
    dir = dir * -1.0;
  double tStart = MyMath::dotProduct(d->start, dir);
  double tEnd = MyMath::dotProduct(d->end, dir);
  if (tEnd < tStart)
    std::swap(tStart, tEnd);

  QPointF pick;
  if (!pickPointOnItem(scene, item, pick))
    return fail(QStringLiteral("id %1 치수 위에서 클릭할 점을 찾지 못했습니다.").arg(id));

  QJsonArray steps;
  QJsonObject first;
  first.insert(QStringLiteral("click"), QJsonArray{pick.x(), pick.y()});
  first.insert(QStringLiteral("button"), QStringLiteral("left"));
  steps.append(first);
  if (args.value(QStringLiteral("keepOriginal")).toBool(false)) {
    QJsonObject n;
    n.insert(QStringLiteral("command"), QStringLiteral("n"));
    steps.append(n);
  }
  QList<double> seen;
  for (int i = 0; i < pts.size(); ++i) {
    QPointF p;
    if (!readPoint(pts.at(i), p))
      return fail(QStringLiteral("points[%1] 이 [x, y] 형식이 아닙니다.").arg(i));
    const double t = MyMath::dotProduct(MyVector(p), dir);
    if (t <= tStart + 1e-3 || t >= tEnd - 1e-3)
      return fail(QStringLiteral("points[%1] 이 치수 범위 밖입니다. 이 치수는 치수 방향으로 %2 ~ %3 "
                                 "구간이며 나눌 점은 그 안쪽(양 끝 제외)이어야 합니다.")
                      .arg(i)
                      .arg(tStart)
                      .arg(tEnd));
    for (int j = 0; j < seen.size(); ++j)
      if (qAbs(seen.at(j) - t) < 1e-3)
        return fail(QStringLiteral("points[%1] 이 앞의 점과 같은 위치입니다.").arg(i));
    seen.append(t);
    QJsonObject s;
    s.insert(QStringLiteral("click"), QJsonArray{p.x(), p.y()});
    s.insert(QStringLiteral("button"), QStringLiteral("left"));
    steps.append(s);
  }
  QJsonObject fin;
  fin.insert(QStringLiteral("click"), QJsonArray{pick.x(), pick.y()});
  fin.insert(QStringLiteral("button"), QStringLiteral("right"));
  steps.append(fin);
  return driveTool(scene, new DimSplitTool(), steps, QStringLiteral("dimSplit"));
}

namespace {
// GD&T 기호 이름(대소문자 무시, 영문 enum 이름) 또는 0~13 번호
bool parseGdtSymbol(const QJsonValue &v, GdtSymbol &out) {
  static const char *names[] = {"straightness",  "flatness",     "circularity",
                                "cylindricity",  "lineprofile",  "surfaceprofile",
                                "parallelism",   "perpendicularity", "angularity",
                                "position",      "concentricity", "symmetry",
                                "circularrunout", "totalrunout"};
  const int count = int(sizeof(names) / sizeof(names[0]));
  if (v.isDouble()) {
    const int n = v.toInt(-1);
    if (n < 0 || n >= count)
      return false;
    out = static_cast<GdtSymbol>(n);
    return true;
  }
  const QString s = v.toString().trimmed().toLower();
  for (int i = 0; i < count; ++i) {
    if (s == QLatin1String(names[i])) {
      out = static_cast<GdtSymbol>(i);
      return true;
    }
  }
  return false;
}
const char *kGdtSymbolHelp =
    "symbol 은 Straightness(직진도), Flatness(평면도), Circularity(진원도), Cylindricity(원통도), "
    "LineProfile(선의 윤곽도), SurfaceProfile(면의 윤곽도), Parallelism(평행도), "
    "Perpendicularity(직각도), Angularity(경사도), Position(위치도), Concentricity(동축도), "
    "Symmetry(대칭도), CircularRunout(원주 흔들림), TotalRunout(전체 흔들림) 중 하나(또는 0~13).";
} // namespace

// 치수 레이어: 지정값 > 치수 스타일의 치수선 레이어 > DIM
Layer *AiBridgeServer::resolveDimensionLayer(MyGraphicsScene *scene, const QJsonObject &args,
                                             bool *created, QJsonObject &err) {
  QString layerName = args.value(QStringLiteral("layer")).toString();
  if (layerName.isEmpty())
    layerName = scene->dimensionStyle().dimLineLayer;
  if (layerName.isEmpty())
    layerName = QStringLiteral("DIM");
  Layer *layer = ensureLayer(layerName, created);
  if (!layer) {
    err = fail(QStringLiteral("치수 레이어를 정하지 못했습니다."));
    return nullptr;
  }
  if (layer->isLayerLock()) {
    err = fail(QStringLiteral("레이어 '%1' 는 잠겨 있습니다. 다른 레이어를 지정하세요.")
                   .arg(layer->name()));
    return nullptr;
  }
  return layer;
}

// 기하공차(GD&T) 프레임을 창 없이 만든다. points(2점 이상) 이면 지시선 포함, at 이면 프레임만.
QJsonObject AiBridgeServer::toolCreateGdt(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  GdtData data;
  if (!parseGdtSymbol(args.value(QStringLiteral("symbol")), data.symbol))
    return fail(QString::fromUtf8(kGdtSymbolHelp));
  data.toleranceValue = args.value(QStringLiteral("tolerance")).toString();
  data.hasDiameter = args.value(QStringLiteral("diameter")).toBool(false);
  const QJsonArray datums = args.value(QStringLiteral("datums")).toArray();
  if (datums.size() > 3)
    return fail(QStringLiteral("datums 는 최대 3개(A, B, C)입니다."));
  for (int i = 0; i < datums.size(); ++i)
    data.datumRefs.append(datums.at(i).toString());
  data.textHeight = args.value(QStringLiteral("textHeight"))
                        .toDouble(scene->dimensionStyle().textSize);
  if (data.textHeight <= 0.0)
    return fail(QStringLiteral("textHeight 는 0 보다 커야 합니다."));

  if (args.contains(QStringLiteral("points"))) {
    const QJsonArray arr = args.value(QStringLiteral("points")).toArray();
    if (arr.size() < 2)
      return fail(QStringLiteral("points(지시선 꼭짓점)는 2점 이상이어야 합니다. 마지막 점에 프레임이 "
                                 "붙습니다. 지시선 없이 프레임만 놓으려면 at 을 쓰세요."));
    for (int i = 0; i < arr.size(); ++i) {
      QPointF p;
      if (!readPoint(arr.at(i), p))
        return fail(QStringLiteral("points[%1] 이 [x, y] 형식이 아닙니다.").arg(i));
      data.leaderPoints.append(MyVector(p));
    }
    data.useLeader = true;
    data.position = data.leaderPoints.last();
    const MyVector dir = data.leaderPoints.last() - data.leaderPoints.at(data.leaderPoints.size() - 2);
    data.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));
  } else {
    QPointF at;
    if (!readPoint(args.value(QStringLiteral("at")), at))
      return fail(QStringLiteral("at([x, y], 프레임 왼쪽 중앙) 또는 points(지시선 꼭짓점 목록)가 필요합니다. "
                                 "예: {\"symbol\":\"Position\",\"tolerance\":\"0.05\",\"diameter\":true,"
                                 "\"datums\":[\"A\",\"B\"],\"at\":[1000,500]}"));
    data.useLeader = false;
    data.position = MyVector(at);
    data.rotation = 0.0;
  }

  bool created = false;
  Layer *layer = resolveDimensionLayer(scene, args, &created, err);
  if (!layer)
    return err;
  QGraphicsItem *item = m_ctx->entityFactory()->createGdt(data, penForLayer(layer));
  QJsonObject result = placeItem(scene, item, layer);
  if (result.value(QStringLiteral("ok")).toBool()) {
    result.insert(QStringLiteral("layer"), layer->name());
    result.insert(QStringLiteral("layerCreated"), created);
  }
  return result;
}

// 데이텀 기호(삼각형 + 문자 박스)를 창 없이 만든다
QJsonObject AiBridgeServer::toolCreateDatum(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  DatumData data;
  data.text = args.value(QStringLiteral("text")).toString().trimmed();
  if (data.text.isEmpty())
    return fail(QStringLiteral("text(데이텀 문자, 예: A)가 필요합니다."));
  QPointF start;
  QPointF end;
  if (!readPoint(args.value(QStringLiteral("start")), start) ||
      !readPoint(args.value(QStringLiteral("end")), end))
    return fail(QStringLiteral("start(삼각형이 붙는 점)와 end(문자 박스 중심) 는 [x, y] 형식이어야 "
                               "합니다. 예: {\"text\":\"A\",\"start\":[0,0],\"end\":[0,-800]}"));
  if (QLineF(start, end).length() < 1.0)
    return fail(QStringLiteral("start 와 end 가 같은 점입니다."));
  data.startPoint = MyVector(start);
  data.endPoint = MyVector(end);
  data.textHeight = args.value(QStringLiteral("textHeight"))
                        .toDouble(scene->dimensionStyle().textSize);
  if (data.textHeight <= 0.0)
    return fail(QStringLiteral("textHeight 는 0 보다 커야 합니다."));

  bool created = false;
  Layer *layer = resolveDimensionLayer(scene, args, &created, err);
  if (!layer)
    return err;
  QGraphicsItem *item = m_ctx->entityFactory()->createDatum(data, penForLayer(layer));
  QJsonObject result = placeItem(scene, item, layer);
  if (result.value(QStringLiteral("ok")).toBool()) {
    result.insert(QStringLiteral("layer"), layer->name());
    result.insert(QStringLiteral("layerCreated"), created);
  }
  return result;
}

// 기하공차/데이텀 편집(기하편집 도구의 창 없는 버전): 바꿀 값만 주면 나머지는 유지하고 객체를 교체한다
QJsonObject AiBridgeServer::toolEditGdt(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const int id = args.value(QStringLiteral("id")).toInt(-1);
  QGraphicsItem *item = itemById(scene, id);
  if (!item)
    return fail(QStringLiteral("id %1 인 객체를 찾지 못했습니다. get_entities(type:\"Gdt\") 로 확인하세요.")
                    .arg(id));

  CadItem *newItem = nullptr;
  if (GdtItem *g = dynamic_cast<GdtItem *>(item)) {
    GdtData data = g->gdtData();
    if (args.contains(QStringLiteral("symbol")) &&
        !parseGdtSymbol(args.value(QStringLiteral("symbol")), data.symbol))
      return fail(QString::fromUtf8(kGdtSymbolHelp));
    if (args.contains(QStringLiteral("tolerance")))
      data.toleranceValue = args.value(QStringLiteral("tolerance")).toString();
    if (args.contains(QStringLiteral("diameter")))
      data.hasDiameter = args.value(QStringLiteral("diameter")).toBool();
    if (args.contains(QStringLiteral("datums"))) {
      const QJsonArray datums = args.value(QStringLiteral("datums")).toArray();
      if (datums.size() > 3)
        return fail(QStringLiteral("datums 는 최대 3개(A, B, C)입니다."));
      data.datumRefs.clear();
      for (int i = 0; i < datums.size(); ++i)
        data.datumRefs.append(datums.at(i).toString());
    }
    if (args.contains(QStringLiteral("textHeight")))
      data.textHeight = args.value(QStringLiteral("textHeight")).toDouble(data.textHeight);
    newItem = new GdtItem(data);
  } else if (DatumItem *dt = dynamic_cast<DatumItem *>(item)) {
    DatumData data = dt->datumData();
    if (args.contains(QStringLiteral("text")))
      data.text = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("textHeight")))
      data.textHeight = args.value(QStringLiteral("textHeight")).toDouble(data.textHeight);
    newItem = new DatumItem(data);
  } else {
    return fail(QStringLiteral("id %1 는 기하공차 프레임이나 데이텀이 아닙니다.").arg(id));
  }
  CadItem *oldCad = dynamic_cast<CadItem *>(item);
  newItem->setLayerName(oldCad->layerName());
  newItem->setPen(oldCad->pen());
  newItem->setParentItem(item->parentItem());

  if (m_ctx->undoStack()) {
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: edit_gdt"));
    m_ctx->undoStack()->push(new RemoveEntityCommand(scene, item));
    m_ctx->undoStack()->push(new AddEntityCommand(scene, newItem));
    m_ctx->undoStack()->endMacro();
  } else {
    scene->removeItem(item);
    delete item;
    scene->addItem(newItem);
  }
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("replacedId"), id);
  result.insert(QStringLiteral("entity"), describeItem(newItem));
  return result;
}

// 객체 속성 편집(치수 문자·공차 등, 치수편집 도구의 창 없는 버전). 키는 get_entities 의 props 이름.
QJsonObject AiBridgeServer::toolEditEntity(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const int id = args.value(QStringLiteral("id")).toInt(-1);
  QGraphicsItem *item = itemById(scene, id);
  CadItem *cad = item ? dynamic_cast<CadItem *>(item) : nullptr;
  if (!cad)
    return fail(QStringLiteral("id %1 인 객체를 찾지 못했습니다. get_entities 로 id 를 확인하세요.")
                    .arg(id));
  const QJsonObject props = args.value(QStringLiteral("properties")).toObject();
  if (props.isEmpty())
    return fail(QStringLiteral("properties 에 바꿀 속성이 필요합니다. 예: {\"id\":3,\"properties\":"
                               "{\"Prefix\":\"약 \",\"TolType\":1,\"TolUpper\":0.1,\"TolLower\":-0.1}}"));

  const QMap<QString, QVariant> current = cad->getProperties();
  QStringList keys = props.keys();
  for (int i = 0; i < keys.size(); ++i) {
    if (!current.contains(keys.at(i))) {
      QStringList valid = current.keys();
      return fail(QStringLiteral("이 객체에 속성 '%1' 이 없습니다. 사용 가능: %2")
                      .arg(keys.at(i), valid.join(QStringLiteral(", "))));
    }
  }

  if (m_ctx->undoStack())
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: edit_entity"));
  for (int i = 0; i < keys.size(); ++i) {
    const QString key = keys.at(i);
    const QVariant oldValue = current.value(key);
    const QJsonValue jv = props.value(key);
    QVariant newValue;
    switch (oldValue.typeId()) {
    case QMetaType::Int:
      newValue = jv.toInt();
      break;
    case QMetaType::Double:
      newValue = jv.toDouble();
      break;
    case QMetaType::Bool:
      newValue = jv.toBool();
      break;
    case QMetaType::QString:
      newValue = jv.isString() ? jv.toString() : jv.toVariant().toString();
      break;
    default:
      newValue = jv.toVariant();
      break;
    }
    if (m_ctx->undoStack())
      m_ctx->undoStack()->push(new ChangePropertyCommand(cad, key, oldValue, newValue));
    else
      cad->setProperty(key, newValue);
  }
  if (m_ctx->undoStack())
    m_ctx->undoStack()->endMacro();

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("changed"), QJsonArray::fromStringList(keys));
  result.insert(QStringLiteral("entity"), describeItem(item));
  return result;
}


// 치수 스타일(치수설정 창의 값) 조회/변경. style 을 생략하면 현재 값만 돌려준다.
//   값은 도면 축척 1:1 기준 mm 이며 문자·화살표 실제 크기는 dimScale/도면 축척에 비례한다.
QJsonObject AiBridgeServer::toolDimensionStyle(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const DimensionStyle oldStyle = scene->dimensionStyle();
  QJsonObject cur = oldStyle.toJson();
  const QJsonObject change = args.value(QStringLiteral("style")).toObject();

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  if (change.isEmpty()) {
    result.insert(QStringLiteral("style"), cur);
    result.insert(QStringLiteral("note"),
                  QStringLiteral("바꾸려면 style 에 바꿀 항목만 주세요. 예: {\"style\":{\"textSize\":3,"
                                 "\"precision\":0}}"));
    return result;
  }

  static const char *arrowNames[] = {"filled", "open", "filledcircle", "opencircle",
                                     "doublecircle", "hollow"};
  QStringList keys = change.keys();
  for (int i = 0; i < keys.size(); ++i) {
    const QString key = keys.at(i);
    if (!cur.contains(key))
      return fail(QStringLiteral("알 수 없는 스타일 항목 '%1'. 사용 가능: %2")
                      .arg(key, cur.keys().join(QStringLiteral(", "))));
    QJsonValue v = change.value(key);
    if (key == QLatin1String("arrowStyle") && v.isString()) {
      const QString s = v.toString().trimmed().toLower();
      int found = -1;
      for (int k = 0; k < 6; ++k)
        if (s == QLatin1String(arrowNames[k]))
          found = k;
      if (found < 0)
        return fail(QStringLiteral("arrowStyle 은 0~5 또는 filled, open, filledCircle, openCircle, "
                                   "doubleCircle, hollow 입니다."));
      v = found;
    }
    if (key == QLatin1String("arrowStyle") && (v.toInt(-1) < 0 || v.toInt(-1) > 5))
      return fail(QStringLiteral("arrowStyle 은 0~5 입니다."));
    if ((key == QLatin1String("dimLineLayer") || key == QLatin1String("dimTextLayer")) &&
        !v.toString().isEmpty() && m_ctx->layerManager() &&
        !m_ctx->layerManager()->getLayer(v.toString()))
      return fail(QStringLiteral("레이어 '%1' 이 없습니다. get_layers 로 확인하세요.")
                      .arg(v.toString()));
    cur.insert(key, v);
  }
  const DimensionStyle newStyle = DimensionStyle::fromJson(cur);
  if (m_ctx->undoStack())
    m_ctx->undoStack()->push(new ChangeDimStyleCommand(scene, oldStyle, newStyle));
  else
    scene->setDimensionStyle(newStyle);
  result.insert(QStringLiteral("changed"), QJsonArray::fromStringList(keys));
  result.insert(QStringLiteral("style"), newStyle.toJson());
  result.insert(QStringLiteral("note"),
                QStringLiteral("기존 치수에도 바로 적용됩니다. 호출 1회 = 실행 취소 1단계."));
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 박스글자 (글자쓰기 탭 BOXTEXT): 글자를 쓰고 그 글자를 감싸는 박스(닫힌 4점 폴리선)를 함께 만든다.
//   · 새 글자: text + at(글자 시작점) / 기존 글자에 박스만: textId (도구의 [Pick] 옵션)
//   · 레이어: 인자 > 도구 [Setup](ToolDefaults "BoxTextTool") > 앱 글자 레이어/글자와 같은 레이어
//   · 박스 모양은 BoxTextUtil::boxCorners(도구와 같은 계산) — 이후 글자를 편집하면 박스가 따라간다
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject AiBridgeServer::toolCreateBoxText(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  LayerManager *lm = m_ctx->layerManager();
  if (!lm)
    return fail(QStringLiteral("레이어 관리자를 찾을 수 없습니다."));

  ToolDefaults setup(BoxTextUtil::settingsNamespace(), scene);
  const QString setupBox = setup.getString(QStringLiteral("BoxLayer"), QString());
  const QString setupText = setup.getString(QStringLiteral("TextLayer"), QString());

  // ── 검사 단계: 여기서는 아무것도 만들지 않는다 ──
  const QString argText = args.value(QStringLiteral("textLayer")).toString();
  const QString argBox = args.value(QStringLiteral("boxLayer")).toString();
  if (!argText.isEmpty() && !lm->getLayer(argText))
    return fail(QStringLiteral("textLayer '%1' 이 도면에 없습니다. get_layers 로 확인하세요.").arg(argText));
  if (!argBox.isEmpty() && !lm->getLayer(argBox))
    return fail(QStringLiteral("boxLayer '%1' 이 도면에 없습니다. get_layers 로 확인하세요.").arg(argBox));

  const bool existing = args.contains(QStringLiteral("textId"));
  TextItem *textItem = nullptr;
  Layer *textLayer = nullptr;
  QString content;
  QPointF at;
  QFont font;
  double rotation = 0.0;

  if (existing) {
    const int id = args.value(QStringLiteral("textId")).toInt(-1);
    textItem = dynamic_cast<TextItem *>(itemById(scene, id));
    if (!textItem)
      return fail(QStringLiteral("textId %1 은 글자 객체가 아니거나 없습니다. get_entities(type:\"Text\") 로 id 를 "
                                 "확인하세요.")
                      .arg(id));
    textLayer = lm->getLayer(!argText.isEmpty() ? argText : textItem->layerName());
  } else {
    content = args.value(QStringLiteral("text")).toString();
    if (content.trimmed().isEmpty() || !readPoint(args.value(QStringLiteral("at")), at))
      return fail(QStringLiteral("text(내용)와 at(글자 시작점 [x, y])이 필요합니다. 예: "
                                 "{\"text\":\"거실\",\"at\":[1000,2000],\"textHeight\":200}. "
                                 "이미 있는 글자에 박스만 만들려면 textId 를 주세요."));
    const QString textLayerName = !argText.isEmpty() ? argText
                                  : (!setupText.isEmpty() && lm->getLayer(setupText)) ? setupText
                                                                                      : m_ctx->textLayerName();
    textLayer = lm->getLayer(textLayerName);
    if (!textLayer)
      textLayer = lm->getCurrentLayer();
    const double height = args.value(QStringLiteral("textHeight"))
                              .toDouble(double(m_ctx->currentFontSize()) * scene->drawingScale());
    if (height <= 0.0)
      return fail(QStringLiteral("textHeight 는 0 보다 커야 합니다."));
    font.setFamily(args.value(QStringLiteral("font")).toString(m_ctx->currentFontFamily()));
    font.setPointSizeF(height);
    font.setBold(args.value(QStringLiteral("bold")).toBool(m_ctx->currentFontBold()));
    font.setItalic(args.value(QStringLiteral("italic")).toBool(m_ctx->currentFontItalic()));
    rotation = args.value(QStringLiteral("rotation")).toDouble(0.0);
  }
  if (!textLayer || textLayer->isLayerLock())
    return fail(QStringLiteral("글자 레이어를 정하지 못했거나 잠겨 있습니다. textLayer 를 지정하세요."));

  const QString boxLayerName = !argBox.isEmpty() ? argBox
                               : (!setupBox.isEmpty() && lm->getLayer(setupBox)) ? setupBox
                                                                                 : textLayer->name();
  Layer *boxLayer = lm->getLayer(boxLayerName);
  if (!boxLayer || boxLayer->isLayerLock())
    return fail(QStringLiteral("박스 레이어 '%1' 를 쓸 수 없습니다(없거나 잠김). boxLayer 를 지정하세요.")
                    .arg(boxLayerName));

  // ── 실행 단계: 글자(새로 만들기 또는 레이어 이동) + 박스를 한 번의 실행 취소로 ──
  QUndoStack *stack = m_ctx->undoStack();
  if (stack)
    stack->beginMacro(QStringLiteral("AI: 박스글자"));

  if (existing) {
    if (textItem->layerName() != textLayer->name()) {
      const QString oldLayer = textItem->layerName();
      const QPen oldPen = textItem->pen();
      const QPen newPen = penForLayer(textLayer);
      if (stack)
        stack->push(new ChangeLayerCommand(scene, textItem, oldLayer, textLayer->name(), oldPen, newPen));
      else {
        textItem->setLayerName(textLayer->name());
        textItem->setPen(newPen);
        scene->reparentItemToLayer(textItem, textLayer->name());
      }
    }
  } else {
    const QColor color = scene->getLayerStyle(textLayer->name()).color;
    QGraphicsItem *newText =
        m_ctx->entityFactory()->createText(CadUtils::processText(content), at, font, color, rotation);
    const QJsonObject placed = placeItem(scene, newText, textLayer);
    if (!placed.value(QStringLiteral("ok")).toBool()) {
      if (stack) {
        stack->endMacro();
      }
      return placed;
    }
    textItem = dynamic_cast<TextItem *>(newText);
  }

  const std::vector<MyVector> corners = BoxTextUtil::boxCorners(textItem);
  if (corners.size() != 4) {
    if (stack) {
      stack->endMacro();
      stack->undo();
    }
    return fail(QStringLiteral("글자 크기를 계산하지 못해 박스를 만들 수 없습니다. 만든 글자는 취소했습니다."));
  }
  QGraphicsItem *box = m_ctx->entityFactory()->createPolyline(corners, true, penForLayer(boxLayer));
  const QJsonObject boxPlaced = placeItem(scene, box, boxLayer);
  if (stack)
    stack->endMacro();
  if (!boxPlaced.value(QStringLiteral("ok")).toBool())
    return boxPlaced;

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("text"), describeItem(textItem));
  result.insert(QStringLiteral("box"), boxPlaced.value(QStringLiteral("entity")));
  result.insert(QStringLiteral("textLayer"), textItem->layerName());
  result.insert(QStringLiteral("boxLayer"), boxLayer->name());
  QJsonArray c;
  for (size_t i = 0; i < corners.size(); ++i) {
    QJsonArray p;
    p.append(corners[i].x);
    p.append(corners[i].y);
    c.append(p);
  }
  result.insert(QStringLiteral("boxCorners"), c);
  result.insert(QStringLiteral("note"),
                existing ? QStringLiteral("기존 글자에 박스를 만들었습니다. 호출 1회 = 실행 취소 1단계.")
                         : QStringLiteral("글자와 박스를 함께 만들었습니다(실행 취소 1단계). 글자를 편집하면 박스가 따라갑니다."));
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 해칭 (그리기 탭 HATCH): 닫힌 영역 안쪽 점(inside) 또는 사각 영역(box)을 패턴으로 채운다.
//   · 도구의 설정창 대신 인자로 패턴·배율·각도·레이어를 받고, 생략하면 도구 설정([Hatch] ToolDefaults)을 쓴다
//   · 경계 찾기는 도구와 같은 AdvancedBoundaryFinder (inside = Select 모드, box = Box 모드)
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject AiBridgeServer::toolListHatchPatterns(const QJsonObject &args) {
  Q_UNUSED(args);
  HatchPatternManager *pm = m_ctx->hatchPatternManager();
  if (!pm)
    return fail(QStringLiteral("해치 패턴 목록을 불러오지 못했습니다."));
  const QStringList names = pm->getPatternNames();
  QJsonArray list;
  for (int i = 0; i < names.size(); ++i) {
    QJsonObject o;
    o.insert(QStringLiteral("name"), names.at(i));
    const QString desc = pm->getPattern(names.at(i)).description;
    if (!desc.isEmpty())
      o.insert(QStringLiteral("description"), desc);
    list.append(o);
  }
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("count"), list.size());
  result.insert(QStringLiteral("patterns"), list);
  result.insert(QStringLiteral("note"),
                QStringLiteral("SOLID 는 색으로 꽉 채움. 기계 단면은 보통 ANSI31(45도 사선), 콘크리트는 AR-CONC 계열."));
  return result;
}

QJsonObject AiBridgeServer::toolCreateHatch(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  HatchPatternManager *pm = m_ctx->hatchPatternManager();
  LayerManager *lm = m_ctx->layerManager();
  if (!pm || !lm)
    return fail(QStringLiteral("해치 패턴 또는 레이어 관리자를 불러오지 못했습니다."));

  ToolDefaults def(QStringLiteral("Hatch"), scene);

  // 패턴 (대소문자 무시)
  QString pattern = args.value(QStringLiteral("pattern")).toString().trimmed();
  if (pattern.isEmpty())
    pattern = def.getString(QStringLiteral("Pattern"), QStringLiteral("ANSI31"));
  const QStringList names = pm->getPatternNames();
  QString matched;
  for (int i = 0; i < names.size(); ++i)
    if (names.at(i).compare(pattern, Qt::CaseInsensitive) == 0)
      matched = names.at(i);
  if (matched.isEmpty())
    return fail(QStringLiteral("패턴 '%1' 을 찾지 못했습니다. list_hatch_patterns 로 이름을 확인하세요(예: ANSI31, SOLID).")
                    .arg(pattern));

  const double scale = args.value(QStringLiteral("scale")).toDouble(def.getDouble(QStringLiteral("Scale"), 1.0));
  const double angle = args.value(QStringLiteral("angle")).toDouble(def.getDouble(QStringLiteral("Angle"), 0.0));
  if (scale <= 0.0)
    return fail(QStringLiteral("scale(패턴 간격 배율)은 0 보다 커야 합니다."));

  // 레이어: 인자 > 도구 설정 > 현재 레이어
  QString layerName = args.value(QStringLiteral("layer")).toString();
  if (layerName.isEmpty())
    layerName = def.getString(QStringLiteral("Layer"), QString());
  Layer *layer = layerName.isEmpty() ? nullptr : lm->getLayer(layerName);
  if (!layer && !args.value(QStringLiteral("layer")).toString().isEmpty())
    return fail(QStringLiteral("레이어 '%1' 이 도면에 없습니다. get_layers 로 확인하세요.").arg(layerName));
  if (!layer)
    layer = lm->getCurrentLayer();
  if (!layer)
    return fail(QStringLiteral("해치 레이어를 정하지 못했습니다."));
  if (layer->isLayerLock())
    return fail(QStringLiteral("레이어 '%1' 는 잠겨 있습니다. 다른 레이어를 지정하세요.").arg(layer->name()));

  // 경계 찾기
  QPainterPath boundary;
  const bool boxMode = args.contains(QStringLiteral("box"));
  if (boxMode == args.contains(QStringLiteral("inside")))
    return fail(QStringLiteral("inside([x,y], 닫힌 영역 안쪽 점) 또는 box([x,y,폭,높이], 사각 영역) 중 하나만 주세요. 예: "
                               "{\"inside\":[5000,3000],\"pattern\":\"ANSI31\",\"scale\":50}"));

  QRectF searchRect;
  QPointF inside;
  if (boxMode) {
    const QJsonArray b = args.value(QStringLiteral("box")).toArray();
    if (b.size() != 4)
      return fail(QStringLiteral("box 는 [x, y, 폭, 높이] 숫자 4개입니다."));
    searchRect = QRectF(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble()).normalized();
  } else {
    if (!readPoint(args.value(QStringLiteral("inside")), inside))
      return fail(QStringLiteral("inside 는 [x, y] 형식이어야 합니다."));
    if (args.contains(QStringLiteral("region"))) {
      const QJsonArray r = args.value(QStringLiteral("region")).toArray();
      if (r.size() != 4)
        return fail(QStringLiteral("region 은 [x, y, 폭, 높이] 숫자 4개입니다."));
      searchRect = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble()).normalized();
    } else {
      const double half = args.value(QStringLiteral("searchRadius")).toDouble(15000.0);
      searchRect = QRectF(inside.x() - half, inside.y() - half, half * 2.0, half * 2.0);
    }
  }
  if (searchRect.width() < 1e-5 || searchRect.height() < 1e-5)
    return fail(QStringLiteral("영역의 폭과 높이는 0 보다 커야 합니다."));

  QList<CadItem *> items;
  const QList<QGraphicsItem *> hit = scene->items(searchRect, Qt::IntersectsItemShape);
  for (int i = 0; i < hit.size(); ++i)
    if (CadItem *ci = dynamic_cast<CadItem *>(hit.at(i)))
      items.append(ci);
  if (items.isEmpty())
    return fail(QStringLiteral("해칭할 영역 안에 객체가 없습니다. 경계가 될 선·도형이 있는 위치를 확인하세요."));

  boundary = boxMode ? AdvancedBoundaryFinder::findBoundaryPathInRect(items, searchRect)
                     : AdvancedBoundaryFinder::findBoundaryPath(items, inside);
  if (boundary.isEmpty())
    return fail(boxMode ? QStringLiteral("사각 영역 안에서 해칭 경계를 만들지 못했습니다.")
                        : QStringLiteral("inside 점을 둘러싸는 닫힌 영역을 찾지 못했습니다. 경계선이 끊겨 있지 않은지, "
                                         "점이 영역 안쪽인지, 경계가 region(기본: 점 주변 ±15000mm) 안에 있는지 "
                                         "확인하세요."));

  HatchItem *hatch = new HatchItem(boundary, matched, scale, angle);
  QPen p;
  p.setColor(layer->color());
  p.setWidthF(layer->weight() > 0 ? layer->weight() : 0.0);
  p.setCosmetic(true);
  hatch->setPen(p);
  QJsonObject result = placeItem(scene, hatch, layer);
  if (!result.value(QStringLiteral("ok")).toBool())
    return result;
  result.insert(QStringLiteral("pattern"), matched);
  result.insert(QStringLiteral("scale"), scale);
  result.insert(QStringLiteral("angle"), angle);
  result.insert(QStringLiteral("layer"), layer->name());
  const QRectF bb = boundary.boundingRect();
  result.insert(QStringLiteral("boundaryBounds"), QJsonArray{bb.x(), bb.y(), bb.width(), bb.height()});
  result.insert(QStringLiteral("note"),
                QStringLiteral("패턴이 너무 촘촘하거나 안 보이면 scale 을 키우세요(도면 축척이 클수록 큰 값). 호출 1회 = 실행 취소 1단계."));
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 레이어 만들기 (레이어 도크의 [만들기] 버튼): 이름·색·선두께·선종류로 새 레이어를 추가한다.
//   · 도크와 같은 규칙: 이름은 비어 있으면 안 되고 이미 있으면 거절, 선두께는 도면 선두께 목록의 값
//   · 레이어 추가는 도크와 마찬가지로 실행 취소(undo) 대상이 아니다
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject AiBridgeServer::toolCreateLayer(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  LayerManager *lm = m_ctx->layerManager();
  if (!lm)
    return fail(QStringLiteral("레이어 관리자를 찾을 수 없습니다."));

  const QString name = args.value(QStringLiteral("name")).toString().trimmed();
  if (name.isEmpty())
    return fail(QStringLiteral("name(레이어 이름)이 필요합니다. 예: {\"name\":\"외벽\",\"color\":\"#ff8000\","
                               "\"weight\":0.35,\"linetype\":\"Continuous\"}"));
  if (Layer *dup = lm->getLayer(name))
    return fail(QStringLiteral("레이어 '%1' 이 이미 있습니다(색 %2, 선두께 %3, 선종류 %4). 다른 이름을 쓰거나 "
                               "그 레이어를 그대로 사용하세요.")
                    .arg(name, dup->color().name())
                    .arg(dup->weight())
                    .arg(dup->linetype()));

  // 색: "#rrggbb" 또는 색 이름(red, yellow ... / 앱 색 목록의 이름)
  QColor color(QStringLiteral("#ffffff"));
  if (args.contains(QStringLiteral("color"))) {
    const QString cs = args.value(QStringLiteral("color")).toString().trimmed();
    QColor c(cs);
    if (!c.isValid() && m_ctx->styleManager()) {
      const QList<SelectColor> colors = m_ctx->styleManager()->getColors();
      for (int i = 0; i < colors.size(); ++i)
        if (colors.at(i).name.compare(cs, Qt::CaseInsensitive) == 0)
          c = colors.at(i).color;
    }
    if (!c.isValid())
      return fail(QStringLiteral("color '%1' 을 알 수 없습니다. \"#rrggbb\" 형식(예: #ff0000)이나 red, yellow, cyan "
                                 "같은 색 이름을 쓰세요.")
                      .arg(cs));
    color = c;
  }

  // 선두께(mm): 도면 선두께 목록의 값
  double weight = 0.25;
  if (args.contains(QStringLiteral("weight"))) {
    weight = args.value(QStringLiteral("weight")).toDouble(-1.0);
    bool listed = false;
    QStringList valid;
    if (m_ctx->styleManager()) {
      const QList<LineWeight> ws = m_ctx->styleManager()->getLineWeights();
      for (int i = 0; i < ws.size(); ++i) {
        valid << QString::number(ws.at(i).weight, 'f', 2);
        if (qAbs(ws.at(i).weight - weight) < 1e-6)
          listed = true;
      }
    }
    if (weight <= 0.0 || (!valid.isEmpty() && !listed))
      return fail(QStringLiteral("weight(선두께 mm)는 목록의 값이어야 합니다: %1").arg(valid.join(QStringLiteral(", "))));
  }

  // 선종류: 도면에 등록된 이름 (대소문자 무시)
  QString linetype = QStringLiteral("Continuous");
  if (args.contains(QStringLiteral("linetype"))) {
    const QString want = args.value(QStringLiteral("linetype")).toString().trimmed();
    QString matched;
    QStringList all;
    if (m_ctx->lineTypeManager()) {
      const QList<LineType> types = m_ctx->lineTypeManager()->getAllLinetypes();
      for (int i = 0; i < types.size(); ++i) {
        all << types.at(i).name;
        if (types.at(i).name.compare(want, Qt::CaseInsensitive) == 0)
          matched = types.at(i).name;
      }
    }
    if (matched.isEmpty() && want.compare(QStringLiteral("Continuous"), Qt::CaseInsensitive) == 0)
      matched = QStringLiteral("Continuous");
    if (matched.isEmpty()) {
      // AutoDraft 고유 선종류를 모두, AutoCAD 호환(S)/ISO 는 개수만 알려준다
      QStringList own;
      int others = 0;
      for (int i = 0; i < all.size(); ++i) {
        if (LineTypeManager::groupOf(all.at(i)) == LineTypeManager::LinetypeGroup::AutoDraft)
          own << all.at(i);
        else
          ++others;
      }
      own.sort();
      return fail(QStringLiteral("linetype '%1' 이 등록돼 있지 않습니다. AutoDraft 선종류: %2 (그 밖에 AutoCAD 호환 이름 %3개: "
                                 "HIDDEN, CENTER, DASHED, ACAD_ISO02W100 등도 쓸 수 있음)")
                      .arg(want)
                      .arg(own.join(QStringLiteral(", ")))
                      .arg(others));
    }
    linetype = matched;
  }

  if (!lm->addLayer(Layer(name, color, weight, linetype, false, true, true, false)))
    return fail(QStringLiteral("레이어 '%1' 을 추가하지 못했습니다.").arg(name));
  bool madeCurrent = false;
  if (args.value(QStringLiteral("makeCurrent")).toBool(false))
    madeCurrent = lm->setCurrentLayer(name);

  QJsonObject layerInfo;
  layerInfo.insert(QStringLiteral("name"), name);
  layerInfo.insert(QStringLiteral("color"), color.name());
  layerInfo.insert(QStringLiteral("weight"), weight);
  layerInfo.insert(QStringLiteral("linetype"), linetype);
  layerInfo.insert(QStringLiteral("current"), madeCurrent);
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("layer"), layerInfo);
  result.insert(QStringLiteral("layerCount"), int(lm->getAllLayers().size()));
  result.insert(QStringLiteral("note"),
                QStringLiteral("레이어를 만들었습니다. 이후 create_* 의 layer 인자로 이름을 쓰면 됩니다. 레이어 추가는 undo "
                               "대상이 아닙니다."));
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 그룹 이름표와 레이아웃 스펙 (build_plan)
// ─────────────────────────────────────────────────────────────────────────────

// snapshot 이후 새로 생긴 최상위 객체(블록 내부 자식 제외)에 그룹 이름표를 붙인다
int AiBridgeServer::tagNewItems(MyGraphicsScene *scene,
                                const QSet<QGraphicsItem *> &before,
                                const QString &group) {
  int count = 0;
  const QList<QGraphicsItem *> all = scene->items();
  for (int i = 0; i < all.size(); ++i) {
    QGraphicsItem *item = all.at(i);
    if (before.contains(item) || !dynamic_cast<CadItem *>(item))
      continue;
    if (dynamic_cast<BlockItem *>(item->parentItem()) != nullptr)
      continue;
    item->setData(kGroupDataKey, group);
    ++count;
  }
  return count;
}

// 이름표가 group 인 최상위 객체 목록
QList<QGraphicsItem *> AiBridgeServer::collectGroupItems(MyGraphicsScene *scene,
                                                         const QString &group) const {
  QList<QGraphicsItem *> targets;
  const QList<QGraphicsItem *> all = scene->items();
  for (int i = 0; i < all.size(); ++i) {
    QGraphicsItem *item = all.at(i);
    if (!dynamic_cast<CadItem *>(item))
      continue;
    if (dynamic_cast<BlockItem *>(item->parentItem()) != nullptr)
      continue;
    if (item->data(kGroupDataKey).toString() == group)
      targets.append(item);
  }
  return targets;
}

// 이름표가 group 인 객체를 모두 지운다(실행 취소 가능). 호출 쪽에서 매크로로 묶는다.
int AiBridgeServer::removeGroupItems(MyGraphicsScene *scene, const QString &group) {
  const QList<QGraphicsItem *> targets = collectGroupItems(scene, group);
  for (int i = 0; i < targets.size(); ++i) {
    if (m_ctx->undoStack())
      m_ctx->undoStack()->push(new RemoveEntityCommand(scene, targets.at(i)));
    else
      scene->removeItem(targets.at(i));
  }
  return targets.size();
}

// 이후 create_* / insert_block 으로 만드는 객체에 자동으로 이름표를 붙인다 (빈 이름이면 해제)
QJsonObject AiBridgeServer::toolSetGroup(const QJsonObject &args) {
  m_activeGroup = args.value(QStringLiteral("name")).toString().trimmed();
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("activeGroup"), m_activeGroup);
  result.insert(QStringLiteral("note"),
                m_activeGroup.isEmpty()
                    ? QStringLiteral("그룹 이름표 자동 부착을 껐습니다.")
                    : QStringLiteral("이후 만드는 객체에 '%1' 이름표가 붙습니다. "
                                     "delete_group 으로 이 이름표의 객체만 지울 수 있습니다.")
                          .arg(m_activeGroup));
  return result;
}

QJsonObject AiBridgeServer::toolDeleteGroup(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  const QString group = args.value(QStringLiteral("name")).toString().trimmed();
  if (group.isEmpty())
    return fail(QStringLiteral("name(그룹 이름)이 필요합니다. get_plan 으로 현재 그룹을 "
                               "확인하세요."));
  if (collectGroupItems(scene, group).isEmpty())
    return fail(QStringLiteral("이름표가 '%1' 인 객체가 없습니다. get_plan 으로 그룹 목록을 "
                               "확인하세요.")
                    .arg(group));
  if (m_ctx->undoStack())
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: delete_group %1").arg(group));
  const int removed = removeGroupItems(scene, group);
  if (m_ctx->undoStack())
    m_ctx->undoStack()->endMacro();
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("group"), group);
  result.insert(QStringLiteral("removed"), removed);
  result.insert(QStringLiteral("note"),
                QStringLiteral("실행 취소(undo) 1회로 복원됩니다. 저장된 스펙(get_plan)은 그대로 "
                               "남아 있습니다."));
  return result;
}

// 그룹 목록 또는 한 그룹의 마지막 스펙을 돌려준다
QJsonObject AiBridgeServer::toolGetPlan(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;
  QHash<QString, int> counts;
  const QList<QGraphicsItem *> all = scene->items();
  for (int i = 0; i < all.size(); ++i) {
    QGraphicsItem *item = all.at(i);
    if (!dynamic_cast<CadItem *>(item) ||
        dynamic_cast<BlockItem *>(item->parentItem()) != nullptr)
      continue;
    const QString g = item->data(kGroupDataKey).toString();
    if (!g.isEmpty())
      counts[g] += 1;
  }
  const QString group = args.value(QStringLiteral("group")).toString().trimmed();
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  if (group.isEmpty()) {
    QJsonArray list;
    QStringList names = counts.keys();
    const QStringList specNames = m_plans.keys();
    for (int i = 0; i < specNames.size(); ++i)
      if (!names.contains(specNames.at(i)))
        names.append(specNames.at(i));
    names.sort();
    for (int i = 0; i < names.size(); ++i) {
      QJsonObject o;
      o.insert(QStringLiteral("name"), names.at(i));
      o.insert(QStringLiteral("objects"), counts.value(names.at(i), 0));
      o.insert(QStringLiteral("hasSpec"), m_plans.contains(names.at(i)));
      list.append(o);
    }
    result.insert(QStringLiteral("groups"), list);
    result.insert(QStringLiteral("activeGroup"), m_activeGroup);
    return result;
  }
  result.insert(QStringLiteral("group"), group);
  result.insert(QStringLiteral("objects"), counts.value(group, 0));
  if (m_plans.contains(group))
    result.insert(QStringLiteral("spec"), m_plans.value(group));
  else
    result.insert(QStringLiteral("note"),
                  QStringLiteral("이 그룹의 저장된 스펙이 없습니다(build_plan 으로 만든 그룹만 "
                                 "스펙이 기억되며, AutoDraft 를 다시 켜면 사라집니다)."));
  return result;
}

// 블록 삽입점 기준 범위(선 두께 포함 근사). 읽지 못하면 빈 사각형.
QRectF AiBridgeServer::blockBounds(const QString &category, const QString &name) const {
  FileManager *fm = m_ctx->fileManager();
  if (!fm)
    return QRectF();
  const QString path = blocksRoot() + QLatin1Char('/') + category + QLatin1Char('/') +
                       name + QStringLiteral(".cdb");
  QString orig;
  QList<CadItem *> items = fm->importBlock(path, &orig);
  if (items.isEmpty())
    return QRectF();
  QRectF r;
  {
    MyGraphicsScene temp;
    for (int i = 0; i < items.size(); ++i)
      temp.addItem(items.at(i));
    r = temp.itemsBoundingRect();
    for (int i = 0; i < items.size(); ++i)
      temp.removeItem(items.at(i));
  }
  qDeleteAll(items);
  return r;
}

// 가구 한 개: left/right/cx 중 하나, bottom/top/cy 중 하나로 위치를 정한다 (스펙 좌표, 회전 0/90/180/270)
QJsonObject AiBridgeServer::placeFurniture(const QJsonObject &f, double dx, double dy,
                                           const QString &defaultLayer) {
  QStringList cats;
  const QString category =
      resolveBlockCategory(f.value(QStringLiteral("category")).toString(), &cats);
  if (category.isEmpty())
    return fail(QStringLiteral("category 가 없거나 잘못되었습니다. 사용 가능: %1")
                    .arg(cats.join(QStringLiteral(", "))));
  const QString wanted = f.value(QStringLiteral("name")).toString();
  QString name;
  const QStringList names = blockNames(category);
  for (int i = 0; i < names.size(); ++i)
    if (names.at(i).compare(wanted, Qt::CaseInsensitive) == 0)
      name = names.at(i);
  if (name.isEmpty())
    return fail(QStringLiteral("'%1' 분류에 블록 '%2' 가 없습니다.").arg(category, wanted));

  const int rot = f.value(QStringLiteral("rotation")).toInt(0);
  if (rot != 0 && rot != 90 && rot != 180 && rot != 270)
    return fail(QStringLiteral("rotation 은 0, 90, 180, 270 중 하나여야 합니다."));
  const double scale = f.value(QStringLiteral("scale")).toDouble(1.0);
  if (scale <= 0.0 || scale > 100.0)
    return fail(QStringLiteral("scale 은 0 보다 크고 100 이하여야 합니다."));

  const QRectF b0 = blockBounds(category, name);
  if (b0.isNull())
    return fail(QStringLiteral("블록 범위를 읽지 못했습니다: %1/%2").arg(category, name));
  const double x = b0.left() * scale;
  const double y = b0.top() * scale;
  const double w = b0.width() * scale;
  const double h = b0.height() * scale;
  double bx = x, by = y, bw = w, bh = h;
  if (rot == 90) {
    bx = -(y + h); by = x; bw = h; bh = w;
  } else if (rot == 180) {
    bx = -(x + w); by = -(y + h);
  } else if (rot == 270) {
    bx = y; by = -(x + w); bw = h; bh = w;
  }

  double atX = 0.0;
  double atY = 0.0;
  if (f.contains(QStringLiteral("left")))
    atX = f.value(QStringLiteral("left")).toDouble() - bx;
  else if (f.contains(QStringLiteral("right")))
    atX = f.value(QStringLiteral("right")).toDouble() - (bx + bw);
  else if (f.contains(QStringLiteral("cx")))
    atX = f.value(QStringLiteral("cx")).toDouble() - (bx + bw * 0.5);
  else
    return fail(QStringLiteral("가로 위치로 left, right, cx 중 하나가 필요합니다."));
  if (f.contains(QStringLiteral("bottom")))
    atY = f.value(QStringLiteral("bottom")).toDouble() - by;
  else if (f.contains(QStringLiteral("top")))
    atY = f.value(QStringLiteral("top")).toDouble() - (by + bh);
  else if (f.contains(QStringLiteral("cy")))
    atY = f.value(QStringLiteral("cy")).toDouble() - (by + bh * 0.5);
  else
    return fail(QStringLiteral("세로 위치로 bottom, top, cy 중 하나가 필요합니다."));

  QJsonObject a;
  a.insert(QStringLiteral("category"), category);
  a.insert(QStringLiteral("name"), name);
  QJsonArray at;
  at.append(atX + dx);
  at.append(atY + dy);
  a.insert(QStringLiteral("at"), at);
  a.insert(QStringLiteral("rotation"), rot);
  a.insert(QStringLiteral("scale"), scale);
  a.insert(QStringLiteral("layer"), f.contains(QStringLiteral("layer"))
                                        ? f.value(QStringLiteral("layer")).toString()
                                        : defaultLayer);
  return toolInsertBlock(a);
}

namespace {
QJsonArray shiftedPoint(const QJsonValue &v, double dx, double dy) {
  const QJsonArray a = v.toArray();
  if (a.size() != 2)
    return a;
  QJsonArray r;
  r.append(a.at(0).toDouble() + dx);
  r.append(a.at(1).toDouble() + dy);
  return r;
}
} // namespace

// 레이아웃 스펙 한 번에 만들기: [이전 그룹 삭제] → 코어 → 방들(순서대로) → 가구 → 치수. 전체가 undo 1단계.
//   스펙 좌표는 모두 spec.origin 기준 상대 좌표다.
QJsonObject AiBridgeServer::toolBuildPlan(const QJsonObject &spec) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  const QString group = spec.value(QStringLiteral("group")).toString(QStringLiteral("plan")).trimmed();
  if (group.isEmpty() || group.size() > 40)
    return fail(QStringLiteral("group 은 1~40자의 이름이어야 합니다."));
  const QJsonArray rooms = spec.value(QStringLiteral("rooms")).toArray();
  const bool hasCore = spec.value(QStringLiteral("core")).isObject();
  if (rooms.isEmpty() && !hasCore)
    return fail(QStringLiteral("spec 에 core 또는 rooms 가 필요합니다. 예: "
                               "{\"group\":\"plan32\",\"origin\":[0,0],\"core\":{...},"
                               "\"rooms\":[{\"name\":\"거실\",\"origin\":[0,0],\"width\":4400,"
                               "\"depth\":4400}]}"));
  QPointF origin(0, 0);
  readPoint(spec.value(QStringLiteral("origin")), origin);
  const double dx = origin.x();
  const double dy = origin.y();
  const bool replace = spec.value(QStringLiteral("replace")).toBool(true);
  const bool hasWallT = spec.contains(QStringLiteral("wallThickness"));
  const double wallT = spec.value(QStringLiteral("wallThickness")).toDouble();
  if (hasWallT && wallT < 10.0)
    return fail(QStringLiteral("wallThickness 는 10 이상이어야 합니다."));
  const QString furnLayer =
      spec.value(QStringLiteral("furnitureLayer")).toString(QStringLiteral("FUR"));

  QJsonArray steps;
  QList<QRectF> centerRects;   // 벽 중심선 사각형 (방 + 코어)
  QList<QRectF> interiorRects; // 내부 유효 사각형
  int removedOld = 0;
  double footprint = 0.0;
  double interiorTotal = 0.0;
  QJsonObject interiorByRoom;

  if (m_ctx->undoStack())
    m_ctx->undoStack()->beginMacro(QStringLiteral("AI: build_plan %1").arg(group));
  if (replace)
    removedOld = removeGroupItems(scene, group);
  const QSet<QGraphicsItem *> before = snapshotItems(scene);

  // 1) 코어
  if (hasCore) {
    QJsonObject c = spec.value(QStringLiteral("core")).toObject();
    if (c.contains(QStringLiteral("origin")))
      c.insert(QStringLiteral("origin"), shiftedPoint(c.value(QStringLiteral("origin")), dx, dy));
    else
      c.insert(QStringLiteral("origin"), shiftedPoint(QJsonArray{0, 0}, dx, dy));
    if (hasWallT && !c.contains(QStringLiteral("wallThickness")))
      c.insert(QStringLiteral("wallThickness"), wallT);
    const QJsonObject r = toolCreateCore(c);
    QJsonObject s;
    s.insert(QStringLiteral("step"), QStringLiteral("core"));
    s.insert(QStringLiteral("ok"), r.value(QStringLiteral("ok")));
    if (r.contains(QStringLiteral("error")))
      s.insert(QStringLiteral("error"), r.value(QStringLiteral("error")));
    if (r.contains(QStringLiteral("warnings")))
      s.insert(QStringLiteral("warnings"), r.value(QStringLiteral("warnings")));
    if (r.contains(QStringLiteral("core")))
      s.insert(QStringLiteral("core"), r.value(QStringLiteral("core")));
    steps.append(s);
    if (r.value(QStringLiteral("ok")).toBool()) {
      const QJsonObject co = r.value(QStringLiteral("core")).toObject();
      const QJsonArray ob = co.value(QStringLiteral("outerBounds")).toArray();
      const double ct = co.value(QStringLiteral("wallThickness")).toDouble(200.0);
      if (ob.size() == 4) {
        footprint += (ob.at(2).toDouble() - ct) * (ob.at(3).toDouble() - ct);
        centerRects.append(QRectF(ob.at(0).toDouble() + ct * 0.5, ob.at(1).toDouble() + ct * 0.5,
                                  ob.at(2).toDouble() - ct, ob.at(3).toDouble() - ct));
      }
      const QString interiors[3] = {QStringLiteral("stairRoomInterior"),
                                    QStringLiteral("hallInterior"),
                                    QStringLiteral("shaftInterior")};
      for (int k = 0; k < 3; ++k) {
        const QJsonArray ia = co.value(interiors[k]).toArray();
        if (ia.size() == 4)
          interiorRects.append(QRectF(ia.at(0).toDouble(), ia.at(1).toDouble(),
                                      ia.at(2).toDouble(), ia.at(3).toDouble()));
      }
    }
  }

  // 2) 방 (앞선 방이 그린 벽을 뒤의 방이 공유하므로 순서가 중요하다)
  for (int i = 0; i < rooms.size(); ++i) {
    QJsonObject a = rooms.at(i).toObject();
    const QString label = a.value(QStringLiteral("name")).toString(QStringLiteral("room%1").arg(i));
    if (a.contains(QStringLiteral("origin")))
      a.insert(QStringLiteral("origin"), shiftedPoint(a.value(QStringLiteral("origin")), dx, dy));
    if (a.contains(QStringLiteral("nameAt")))
      a.insert(QStringLiteral("nameAt"), shiftedPoint(a.value(QStringLiteral("nameAt")), dx, dy));
    if (hasWallT && !a.contains(QStringLiteral("wallThickness")))
      a.insert(QStringLiteral("wallThickness"), wallT);
    if (!a.contains(QStringLiteral("dimensions")))
      a.insert(QStringLiteral("dimensions"), QStringLiteral("interior"));
    const QJsonObject r = toolCreateRoom(a);
    QJsonObject s;
    s.insert(QStringLiteral("step"), QStringLiteral("room"));
    s.insert(QStringLiteral("name"), label);
    s.insert(QStringLiteral("ok"), r.value(QStringLiteral("ok")));
    if (r.contains(QStringLiteral("error")))
      s.insert(QStringLiteral("error"), r.value(QStringLiteral("error")));
    if (r.contains(QStringLiteral("warnings")))
      s.insert(QStringLiteral("warnings"), r.value(QStringLiteral("warnings")));
    steps.append(s);
    if (r.value(QStringLiteral("ok")).toBool()) {
      const QJsonObject room = r.value(QStringLiteral("room")).toObject();
      const QJsonArray cl = room.value(QStringLiteral("centerlineRect")).toArray();
      const QJsonArray in = room.value(QStringLiteral("interiorRect")).toArray();
      if (cl.size() == 4) {
        footprint += cl.at(2).toDouble() * cl.at(3).toDouble();
        centerRects.append(QRectF(cl.at(0).toDouble(), cl.at(1).toDouble(),
                                  cl.at(2).toDouble(), cl.at(3).toDouble()));
      }
      if (in.size() == 4) {
        interiorRects.append(QRectF(in.at(0).toDouble(), in.at(1).toDouble(),
                                    in.at(2).toDouble(), in.at(3).toDouble()));
        const double area = in.at(2).toDouble() * in.at(3).toDouble() / 1.0e6;
        interiorTotal += area;
        interiorByRoom.insert(label, qRound(area * 10.0) / 10.0);
      }
    }
  }

  // 3) 가구
  const QJsonArray furn = spec.value(QStringLiteral("furniture")).toArray();
  int furnOk = 0;
  QJsonArray furnProblems;
  for (int i = 0; i < furn.size(); ++i) {
    QJsonObject f = furn.at(i).toObject();
    // 스펙 좌표(상대) → 절대 좌표로 옮긴다
    const QJsonObject r = placeFurniture(f, dx, dy, furnLayer);
    if (r.value(QStringLiteral("ok")).toBool()) {
      ++furnOk;
    } else {
      furnProblems.append(QStringLiteral("furniture[%1] %2/%3: %4")
                              .arg(i)
                              .arg(f.value(QStringLiteral("category")).toString(),
                                   f.value(QStringLiteral("name")).toString(),
                                   r.value(QStringLiteral("error")).toString()));
    }
  }
  if (!furn.isEmpty()) {
    QJsonObject s;
    s.insert(QStringLiteral("step"), QStringLiteral("furniture"));
    s.insert(QStringLiteral("ok"), furnProblems.isEmpty());
    s.insert(QStringLiteral("placed"), furnOk);
    if (!furnProblems.isEmpty())
      s.insert(QStringLiteral("warnings"), furnProblems);
    steps.append(s);
  }

  // 3-1) 자동 치수: 방·코어 사각형의 가장자리 좌표로 연속 치수 + 전체 치수 (기본: 벽 중심선 ↔ 중심선)
  if (spec.value(QStringLiteral("autoDimensions")).isObject()) {
    const QJsonObject ad = spec.value(QStringLiteral("autoDimensions")).toObject();
    const QString basis = ad.value(QStringLiteral("basis")).toString(QStringLiteral("centerline"));
    const bool useInterior = basis == QLatin1String("interior");
    const QList<QRectF> &rects = useInterior ? interiorRects : centerRects;
    QStringList sides;
    if (ad.value(QStringLiteral("sides")).isArray()) {
      const QJsonArray sa = ad.value(QStringLiteral("sides")).toArray();
      for (int i = 0; i < sa.size(); ++i)
        sides.append(sa.at(i).toString().toUpper());
    } else {
      sides << QStringLiteral("S") << QStringLiteral("E");
    }
    const double gap = ad.value(QStringLiteral("gap")).toDouble(700.0);
    const double overallGap = ad.value(QStringLiteral("overallGap")).toDouble(600.0);
    const double tt = hasWallT ? wallT : TwoLineTool::globalSpacing();
    // 기준 선에서 벽 바깥 면까지의 거리: 중심선 기준이면 t/2, 내부 면 기준이면 t
    const double toOuterFace = useInterior ? tt : tt * 0.5;
    if (rects.isEmpty()) {
      QJsonObject s;
      s.insert(QStringLiteral("step"), QStringLiteral("autoDimensions"));
      s.insert(QStringLiteral("ok"), false);
      s.insert(QStringLiteral("error"), QStringLiteral("치수를 만들 방이 없습니다."));
      steps.append(s);
    } else {
      double minX = rects.first().left(), maxX = rects.first().right();
      double minY = rects.first().top(), maxY = rects.first().bottom();
      for (int i = 1; i < rects.size(); ++i) {
        minX = qMin(minX, rects.at(i).left());
        maxX = qMax(maxX, rects.at(i).right());
        minY = qMin(minY, rects.at(i).top());
        maxY = qMax(maxY, rects.at(i).bottom());
      }
      for (int si = 0; si < sides.size(); ++si) {
        const QString side = sides.at(si);
        if (side != QLatin1String("S") && side != QLatin1String("N") &&
            side != QLatin1String("W") && side != QLatin1String("E"))
          continue;
        const bool horizontal = (side == QLatin1String("S") || side == QLatin1String("N"));
        QList<double> edges; // 변을 따라 놓인 가장자리 좌표
        for (int i = 0; i < rects.size(); ++i) {
          const QRectF &rc = rects.at(i);
          bool touches = false;
          if (side == QLatin1String("S"))
            touches = qAbs(rc.top() - minY) < 1.0;
          else if (side == QLatin1String("N"))
            touches = qAbs(rc.bottom() - maxY) < 1.0;
          else if (side == QLatin1String("W"))
            touches = qAbs(rc.left() - minX) < 1.0;
          else
            touches = qAbs(rc.right() - maxX) < 1.0;
          if (!touches)
            continue;
          if (horizontal) {
            edges.append(rc.left());
            edges.append(rc.right());
          } else {
            edges.append(rc.top());
            edges.append(rc.bottom());
          }
        }
        std::sort(edges.begin(), edges.end());
        QList<double> uniq;
        for (int i = 0; i < edges.size(); ++i)
          if (uniq.isEmpty() || edges.at(i) - uniq.last() >= 1.0)
            uniq.append(edges.at(i));
        if (uniq.size() < 2)
          continue;
        QJsonObject da;
        QJsonArray pts;
        double lineAt = 0.0;
        double overallAt = 0.0;
        for (int i = 0; i < uniq.size(); ++i) {
          QJsonArray p;
          if (horizontal) {
            p.append(uniq.at(i));
            p.append(side == QLatin1String("S") ? minY : maxY);
          } else {
            p.append(side == QLatin1String("W") ? minX : maxX);
            p.append(uniq.at(i));
          }
          pts.append(p);
        }
        if (side == QLatin1String("S")) {
          lineAt = minY - (toOuterFace + gap);
          overallAt = lineAt - overallGap;
        } else if (side == QLatin1String("N")) {
          lineAt = maxY + (toOuterFace + gap);
          overallAt = lineAt + overallGap;
        } else if (side == QLatin1String("W")) {
          lineAt = minX - (toOuterFace + gap);
          overallAt = lineAt - overallGap;
        } else {
          lineAt = maxX + (toOuterFace + gap);
          overallAt = lineAt + overallGap;
        }
        da.insert(QStringLiteral("points"), pts);
        da.insert(QStringLiteral("orientation"),
                  horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical"));
        da.insert(QStringLiteral("lineAt"), lineAt);
        if (uniq.size() >= 3)
          da.insert(QStringLiteral("overallAt"), overallAt);
        if (ad.contains(QStringLiteral("layer")))
          da.insert(QStringLiteral("layer"), ad.value(QStringLiteral("layer")));
        const QJsonObject r = toolCreateDimension(da);
        QJsonObject s;
        s.insert(QStringLiteral("step"), QStringLiteral("autoDimensions"));
        s.insert(QStringLiteral("side"), side);
        s.insert(QStringLiteral("basis"), basis);
        s.insert(QStringLiteral("ok"), r.value(QStringLiteral("ok")));
        if (r.contains(QStringLiteral("error"))) {
          s.insert(QStringLiteral("error"), r.value(QStringLiteral("error")));
        } else {
          s.insert(QStringLiteral("created"), r.value(QStringLiteral("createdCount")));
          QJsonArray values;
          const QJsonArray dl = r.value(QStringLiteral("dimensions")).toArray();
          for (int k = 0; k < dl.size(); ++k)
            values.append(dl.at(k).toObject().value(QStringLiteral("value")));
          s.insert(QStringLiteral("values"), values);
        }
        steps.append(s);
      }
    }
  }

  // 4) 치수
  const QJsonArray dims = spec.value(QStringLiteral("dimensions")).toArray();
  for (int i = 0; i < dims.size(); ++i) {
    QJsonObject a = dims.at(i).toObject();
    const QJsonArray pts = a.value(QStringLiteral("points")).toArray();
    QJsonArray moved;
    for (int k = 0; k < pts.size(); ++k)
      moved.append(shiftedPoint(pts.at(k), dx, dy));
    a.insert(QStringLiteral("points"), moved);
    const QString orient = a.value(QStringLiteral("orientation")).toString();
    const double shift = (orient == QLatin1String("horizontal")) ? dy
                         : (orient == QLatin1String("vertical")) ? dx
                                                                  : 0.0;
    if (a.contains(QStringLiteral("lineAt")))
      a.insert(QStringLiteral("lineAt"), a.value(QStringLiteral("lineAt")).toDouble() + shift);
    if (a.contains(QStringLiteral("overallAt")))
      a.insert(QStringLiteral("overallAt"), a.value(QStringLiteral("overallAt")).toDouble() + shift);
    const QJsonObject r = toolCreateDimension(a);
    QJsonObject s;
    s.insert(QStringLiteral("step"), QStringLiteral("dimension"));
    s.insert(QStringLiteral("ok"), r.value(QStringLiteral("ok")));
    if (r.contains(QStringLiteral("error")))
      s.insert(QStringLiteral("error"), r.value(QStringLiteral("error")));
    else
      s.insert(QStringLiteral("created"), r.value(QStringLiteral("createdCount")));
    steps.append(s);
  }

  const int tagged = tagNewItems(scene, before, group);
  if (m_ctx->undoStack())
    m_ctx->undoStack()->endMacro();
  m_plans.insert(group, spec);

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("group"), group);
  result.insert(QStringLiteral("objects"), tagged);
  result.insert(QStringLiteral("replacedObjects"), removedOld);
  result.insert(QStringLiteral("steps"), steps);
  QJsonObject areas;
  areas.insert(QStringLiteral("interiorByRoomM2"), interiorByRoom);
  areas.insert(QStringLiteral("interiorTotalM2"), qRound(interiorTotal * 10.0) / 10.0);
  areas.insert(QStringLiteral("footprintM2"), qRound(footprint / 1.0e6 * 10.0) / 10.0);
  areas.insert(QStringLiteral("footprintPyeong"),
               qRound(footprint / 1.0e6 / 3.3058 * 10.0) / 10.0);
  areas.insert(QStringLiteral("note"),
               QStringLiteral("footprint 는 방·코어의 벽 중심선 사각형 면적의 합(벽 포함)입니다. "
                              "방 안내는 내부 유효 면적."));
  result.insert(QStringLiteral("areas"), areas);
  result.insert(QStringLiteral("note"),
                QStringLiteral("undo 1회로 이 호출 전체(이전 그룹 삭제 포함)가 되돌아갑니다. "
                               "스펙을 고쳐 build_plan 을 다시 호출하면 같은 그룹이 교체됩니다."));
  return result;
}

// 다중선(2/4/6-Line)을 기존 TwoLineTool 로 만든다.
//   점 목록을 도구에 "클릭"으로 넣고 우클릭으로 확정하므로 코너 처리·중심선·레이어 등
//   사람이 그릴 때와 같은 규칙이 적용된다. 설정창은 띄우지 않고 도면에 저장된 Setup
//   값(간격·레이어 등)을 그대로 쓰며, spacing 을 주면 이번 호출에만 덮어쓴다.
QJsonObject AiBridgeServer::toolCreateMultiline(const QJsonObject &args) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  const QJsonArray pts = args.value(QStringLiteral("points")).toArray();
  QList<QPointF> points;
  for (int i = 0; i < pts.size(); ++i) {
    QPointF p;
    if (!readPoint(pts.at(i), p))
      return fail(QStringLiteral("points[%1] 이 [x, y] 형식이 아닙니다. 예: "
                                 "{\"points\":[[0,0],[100,0]]}")
                      .arg(i));
    if (!points.isEmpty() && QLineF(points.last(), p).length() < 0.001)
      return fail(QStringLiteral("points[%1] 이 앞의 점과 같은 위치입니다. 연속된 "
                                 "점은 서로 달라야 합니다.")
                      .arg(i));
    points.append(p);
  }
  if (points.size() < 2)
    return fail(QStringLiteral("points 에 두 점 이상 필요합니다. 예: "
                               "{\"points\":[[0,0],[100,0]]}"));

  const int lines = args.value(QStringLiteral("lines")).toInt(2);
  if (lines != 2 && lines != 4 && lines != 6)
    return fail(QStringLiteral("lines 는 2, 4, 6 중 하나여야 합니다."));

  const double oldSpacing = TwoLineTool::globalSpacing();
  const bool overrideSpacing = args.contains(QStringLiteral("spacing"));
  if (overrideSpacing) {
    const double s = args.value(QStringLiteral("spacing")).toDouble(0.0);
    if (s <= 0.0)
      return fail(QStringLiteral("spacing(선 사이 간격, mm)은 0 보다 커야 합니다."));
    TwoLineTool::setGlobalSpacing(s);
  }

  // 이미 있던 객체를 기억해 두었다가 새로 생긴 것만 돌려준다
  QSet<QGraphicsItem *> before;
  const QList<QGraphicsItem *> beforeItems = scene->items();
  for (int i = 0; i < beforeItems.size(); ++i)
    before.insert(beforeItems.at(i));

  SnapManager *snap = m_ctx->snapManager();
  if (snap)
    snap->setSuspended(true);

  TwoLineTool *tool = new TwoLineTool(lines == 4, lines == 6, false);
  m_ctx->startTool(tool);
  for (int i = 0; i < points.size(); ++i)
    tool->mousePress(points.at(i), Qt::LeftButton);
  tool->mousePress(points.last(), Qt::RightButton); // 확정 (실행 취소 1단위)

  if (snap)
    snap->setSuspended(false);
  if (overrideSpacing)
    TwoLineTool::setGlobalSpacing(oldSpacing);

  QJsonArray created;
  int createdCount = 0;
  const QList<QGraphicsItem *> afterItems = scene->items();
  for (int i = 0; i < afterItems.size(); ++i) {
    QGraphicsItem *item = afterItems.at(i);
    if (before.contains(item) || !dynamic_cast<CadItem *>(item))
      continue;
    ++createdCount;
    if (created.size() < 40)
      created.append(describeItem(item));
  }
  if (createdCount == 0)
    return fail(QStringLiteral("다중선이 만들어지지 않았습니다. 점 위치를 "
                               "확인하세요."));

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("createdCount"), createdCount);
  result.insert(QStringLiteral("spacingUsed"), overrideSpacing
                                                    ? args.value(QStringLiteral("spacing")).toDouble()
                                                    : oldSpacing);
  result.insert(QStringLiteral("entities"), created);
  if (createdCount > created.size())
    result.insert(QStringLiteral("note"),
                  QStringLiteral("생성 객체가 많아 앞 %1개만 표시했습니다.")
                      .arg(created.size()));
  return result;
}

// 다중선 탭의 코너 도구(L / T / C). 사람이 두 번 클릭하는 것과 같게 실행한다.
//   clickA = 첫 번째 다중선을 코너 근처에서 클릭하는 점
//            (L-코너는 클릭한 쪽 끝이 잘린다, T-코너는 통과하는 교차선)
//   clickB = 두 번째 다중선을 코너 근처에서 클릭하는 점
//            (T-코너는 끝이 잘리는 줄기선, 자를 끝 쪽을 클릭)
//   클릭 점은 다중선 위(또는 그 근처)여야 하고 서로 다른 다중선 그룹이어야 한다.
QJsonObject AiBridgeServer::toolMultilineCorner(const QJsonObject &args,
                                                char kind) {
  QJsonObject err;
  MyGraphicsScene *scene = requireScene(err);
  if (!scene)
    return err;

  QPointF clickA;
  QPointF clickB;
  if (!readPoint(args.value(QStringLiteral("clickA")), clickA) ||
      !readPoint(args.value(QStringLiteral("clickB")), clickB))
    return fail(QStringLiteral("clickA, clickB 는 [x, y] 형식이어야 합니다. 두 다중선을 "
                               "코너 근처에서 클릭하는 점입니다. 예: "
                               "{\"clickA\":[900,0],\"clickB\":[0,900]}"));
  if (QLineF(clickA, clickB).length() < 0.001)
    return fail(QStringLiteral("clickA 와 clickB 가 같은 점입니다. 서로 다른 다중선 "
                               "위의 점을 지정하세요."));

  // 클릭 전 다중선 선분 위치를 기억해 두었다가 바뀐/새로 생긴 선을 센다
  QMap<QGraphicsItem *, QLineF> geomBefore;
  const QSet<QGraphicsItem *> before = snapshotItems(scene);
  const QList<QGraphicsItem *> beforeItems = scene->items();
  for (int i = 0; i < beforeItems.size(); ++i) {
    LineItem *li = dynamic_cast<LineItem *>(beforeItems.at(i));
    if (!li || li->data(ML_DATA_GROUP_ID).toString().isEmpty())
      continue;
    const MyLine d = li->getLine();
    geomBefore.insert(li, QLineF(d.start.toPointF(), d.end.toPointF()));
  }

  {
    SnapSuspender guard(m_ctx->snapManager());
    AbstractTool *tool = nullptr;
    if (kind == 'L')
      tool = new MultilineRaycastLCornerTool();
    else if (kind == 'T')
      tool = new MultilineRaycastTCornerTool();
    else
      tool = new MultilineCCornerTool();
    m_ctx->startTool(tool);
    tool->mousePress(clickA, Qt::LeftButton);
    tool->mousePress(clickB, Qt::LeftButton);
    m_ctx->finishCurrentTool(); // 도구는 반복 선택 상태로 남으므로 종료
  }

  int modified = 0;
  QJsonArray changed;
  const QList<QGraphicsItem *> afterItems = scene->items();
  for (int i = 0; i < afterItems.size(); ++i) {
    QGraphicsItem *item = afterItems.at(i);
    if (!dynamic_cast<CadItem *>(item))
      continue;
    if (!before.contains(item)) {
      // 새로 생긴 선(C-코너 분할 조각 등)
      ++modified;
      if (changed.size() < 40)
        changed.append(describeItem(item));
      continue;
    }
    if (!geomBefore.contains(item))
      continue;
    LineItem *li = dynamic_cast<LineItem *>(item);
    if (!li)
      continue;
    const MyLine d = li->getLine();
    const QLineF now(d.start.toPointF(), d.end.toPointF());
    const QLineF old = geomBefore.value(item);
    if (QLineF(now.p1(), old.p1()).length() > 0.001 ||
        QLineF(now.p2(), old.p2()).length() > 0.001) {
      ++modified;
      if (changed.size() < 40)
        changed.append(describeItem(item));
    }
  }

  if (modified == 0)
    return fail(QStringLiteral("코너가 처리되지 않았습니다. clickA/clickB 가 서로 다른 "
                               "다중선(다중선 도구로 만든 선) 위 또는 근처인지, 두 다중선이 "
                               "만나는 방향인지 확인하세요."));

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("kind"), QString(QChar::fromLatin1(kind)));
  result.insert(QStringLiteral("changedCount"), modified);
  result.insert(QStringLiteral("entities"), changed);
  if (modified > changed.size())
    result.insert(QStringLiteral("note"),
                  QStringLiteral("변경 객체가 많아 앞 %1개만 표시했습니다.")
                      .arg(changed.size()));
  return result;
}

// 기존 도면 파일(.cdb) 또는 템플릿(.cdt)을 연다. 이미 열려 있으면 그 창을 앞으로 가져온다.
// 저장(덮어쓰기)은 일부러 도구로 제공하지 않는다 — 사용자가 직접 저장한다.
QJsonObject AiBridgeServer::toolOpenDrawing(const QJsonObject &args) {
  const QString raw = args.value(QStringLiteral("path")).toString().trimmed();
  if (raw.isEmpty())
    return fail(QStringLiteral("path 가 필요합니다. 예: "
                               "{\"path\":\"C:/Autodraft/data/ai-test.cdb\"}"));
  const QFileInfo fi(raw);
  if (!fi.exists() || !fi.isFile())
    return fail(QStringLiteral("파일이 없습니다: %1").arg(raw));
  const QString suffix = fi.suffix().toLower();
  if (suffix != QLatin1String("cdb") && suffix != QLatin1String("cdt"))
    return fail(QStringLiteral("AutoDraft 도면(.cdb) 또는 템플릿(.cdt) 파일만 열 수 있습니다: %1")
                    .arg(fi.fileName()));
  const QString abs = QDir::cleanPath(fi.absoluteFilePath());

  // 이미 열려 있는 창인지 확인 (같은 파일을 두 번 열지 않는다)
  bool already = false;
  if (m_ctx->mdiArea()) {
    const QList<QMdiSubWindow *> subs = m_ctx->mdiArea()->subWindowList();
    for (int i = 0; i < subs.size(); ++i) {
      DrawSubWindow *dw = dynamic_cast<DrawSubWindow *>(subs.at(i));
      if (!dw)
        continue;
      const QString opened =
          QDir::cleanPath(dw->property("filePath").toString());
      if (!opened.isEmpty() && opened.compare(abs, Qt::CaseInsensitive) == 0) {
        m_ctx->mdiArea()->setActiveSubWindow(dw);
        already = true;
        break;
      }
    }
  }
  if (!already) {
    FileOpenCommand cmd(m_ctx);
    cmd.setFilePath(abs);
    cmd.execute();
  }
  emit drawingOpened();

  DrawSubWindow *active = m_ctx->activeDrawWindow();
  const QString activePath =
      active ? QDir::cleanPath(active->property("filePath").toString()) : QString();
  if (!active || activePath.compare(abs, Qt::CaseInsensitive) != 0)
    return fail(QStringLiteral("파일을 열지 못했습니다(또는 다른 도면이 활성 상태입니다): %1. "
                               "AutoDraft 화면에 오류 창이 떠 있는지 확인하세요.")
                    .arg(abs));

  MyGraphicsScene *scene = active->scene();
  int cadCount = 0;
  const QList<QGraphicsItem *> items = scene->items();
  for (int i = 0; i < items.size(); ++i)
    if (dynamic_cast<CadItem *>(items.at(i)))
      ++cadCount;
  QJsonArray layerNames;
  const QList<Layer> layers = m_ctx->layerManager()->getAllLayers();
  for (int i = 0; i < layers.size(); ++i)
    layerNames.append(layers.at(i).name());

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("path"), abs);
  result.insert(QStringLiteral("alreadyOpen"), already);
  result.insert(QStringLiteral("entityCount"), cadCount);
  result.insert(QStringLiteral("drawingScale"), scene->drawingScale());
  result.insert(QStringLiteral("layers"), layerNames);
  result.insert(QStringLiteral("dimensionLayer"), scene->dimensionStyle().dimLineLayer);
  result.insert(QStringLiteral("note"),
                QStringLiteral("이 도면에서 계속 작업합니다. 저장은 도구가 없으므로 사용자가 "
                               "직접(Ctrl+S) 해야 하며, 저장하면 이 파일이 덮어써집니다."));
  return result;
}

QJsonObject AiBridgeServer::toolNewDrawing() {
  emit newDrawingRequested();
  MyGraphicsScene *scene = m_ctx ? m_ctx->activeScene() : nullptr;
  if (!scene)
    return fail(QStringLiteral("새 도면을 열지 못했습니다. AutoDraft 화면에서 "
                               "직접 새 도면을 만든 뒤 다시 시도하세요."));
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("hasActiveDrawing"), true);
  return result;
}

QJsonObject AiBridgeServer::toolUndo() {
  if (!m_ctx || !m_ctx->undoStack())
    return fail(QStringLiteral("실행 취소 스택을 사용할 수 없습니다."));
  if (!m_ctx->undoStack()->canUndo())
    return fail(QStringLiteral("되돌릴 작업이 없습니다."));
  m_ctx->undoStack()->undo();
  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 헬퍼
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject AiBridgeServer::fail(const QString &message) {
  QJsonObject o;
  o.insert(QStringLiteral("ok"), false);
  o.insert(QStringLiteral("error"), message);
  return o;
}

bool AiBridgeServer::readPoint(const QJsonValue &v, QPointF &out) {
  if (!v.isArray())
    return false;
  const QJsonArray a = v.toArray();
  if (a.size() != 2 || !a.at(0).isDouble() || !a.at(1).isDouble())
    return false;
  out = QPointF(a.at(0).toDouble(), a.at(1).toDouble());
  return true;
}

MyGraphicsScene *AiBridgeServer::requireScene(QJsonObject &errorOut) const {
  MyGraphicsScene *scene = m_ctx ? m_ctx->activeScene() : nullptr;
  if (!scene)
    errorOut = fail(QStringLiteral("열려 있는 도면이 없습니다. AutoDraft 에서 "
                                   "새 도면을 만들거나 파일을 연 뒤 다시 "
                                   "시도하세요."));
  return scene;
}

Layer *AiBridgeServer::resolveLayer(const QJsonObject &args,
                                    QJsonObject &errorOut) const {
  if (!m_ctx || !m_ctx->layerManager()) {
    errorOut = fail(QStringLiteral("레이어 관리자를 사용할 수 없습니다."));
    return nullptr;
  }
  LayerManager *lm = m_ctx->layerManager();
  const QString name = args.value(QStringLiteral("layer")).toString();
  Layer *layer = name.isEmpty() ? lm->getCurrentLayer() : lm->getLayer(name);
  if (!layer) {
    QStringList names;
    const QList<Layer> all = lm->getAllLayers();
    for (int i = 0; i < all.size(); ++i)
      names.append(all.at(i).name());
    errorOut = fail(QStringLiteral("레이어 '%1' 가 없습니다. 존재하는 레이어: %2")
                        .arg(name, names.join(QStringLiteral(", "))));
    return nullptr;
  }
  if (layer->isLayerLock()) {
    errorOut = fail(QStringLiteral("레이어 '%1' 는 잠겨 있어 객체를 추가할 수 "
                                   "없습니다. 다른 레이어를 지정하세요.")
                        .arg(layer->name()));
    return nullptr;
  }
  return layer;
}

// LineTool::finishLine 과 같은 규칙: 레이어 색·선두께·선종류를 펜에 반영
QPen AiBridgeServer::penForLayer(const Layer *layer) const {
  QPen pen(layer->color());
  pen.setWidthF(layer->weight());

  const QString linetype = layer->linetype();
  if (linetype == QLatin1String("Continuous") ||
      linetype == QLatin1String("Continous") || !m_ctx->lineTypeManager()) {
    pen.setStyle(Qt::SolidLine);
    return pen;
  }
  const QVector<qreal> pattern =
      m_ctx->lineTypeManager()->getQtDashPattern(linetype);
  if (pattern.isEmpty()) {
    pen.setStyle(Qt::SolidLine);
  } else {
    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern(pattern);
  }
  return pen;
}

QJsonObject AiBridgeServer::placeItem(MyGraphicsScene *scene,
                                      QGraphicsItem *item, const Layer *layer) {
  if (!item)
    return fail(QStringLiteral("객체를 만들지 못했습니다."));

  if (CadItem *cad = dynamic_cast<CadItem *>(item))
    cad->setLayerName(layer->name());

  item->setParentItem(scene->getLayerRoot(layer->name()));
  // setParentItem 이 씬에 추가했을 수 있으므로 명령으로 관리하기 위해 잠시 제거
  if (item->scene() == scene)
    scene->removeItem(item);

  if (m_ctx->undoStack())
    m_ctx->undoStack()->push(new AddEntityCommand(scene, item));
  else
    scene->addItem(item);

  QJsonObject result;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("entity"), describeItem(item));
  return result;
}

int AiBridgeServer::idForItem(QGraphicsItem *item) {
  const QVariant existing = item->data(kAiHandleDataKey);
  if (existing.isValid())
    return existing.toInt();
  const int id = m_nextId++;
  item->setData(kAiHandleDataKey, id);
  return id;
}

QJsonObject AiBridgeServer::describeItem(QGraphicsItem *item) {
  QJsonObject o;
  o.insert(QStringLiteral("id"), idForItem(item));
  if (item->data(kGroupDataKey).isValid())
    o.insert(QStringLiteral("group"), item->data(kGroupDataKey).toString());

  CadItem *cad = dynamic_cast<CadItem *>(item);
  if (cad) {
    const QMap<QString, QVariant> props = cad->getProperties();
    o.insert(QStringLiteral("type"),
             props.value(QStringLiteral("Type")).toString());
    o.insert(QStringLiteral("layer"), cad->layerName());
    QJsonObject p;
    for (QMap<QString, QVariant>::const_iterator it = props.constBegin();
         it != props.constEnd(); ++it) {
      if (it.key() == QLatin1String("Type"))
        continue;
      p.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    o.insert(QStringLiteral("props"), p);
  }

  const QRectF box = item->sceneBoundingRect();
  QJsonArray bbox;
  bbox.append(box.left());
  bbox.append(box.top());
  bbox.append(box.width());
  bbox.append(box.height());
  o.insert(QStringLiteral("bbox"), bbox);
  return o;
}
