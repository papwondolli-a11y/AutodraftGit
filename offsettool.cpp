#include "offsettool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "myarc.h"
#include "mycircle.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "mymath.h"
#include "mypolyline.h"
#include "multilinetagutil.h"
#include <QDebug>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QUndoStack>
#include <QtMath>

double OffsetTool::s_lastDistance =
    0.0; // AutoCAD default is initially Through mode

OffsetTool::OffsetTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_WaitingForDistance),
      m_distance(0.0), m_hasDistance(false), m_isThroughMode(true) {}

OffsetTool::~OffsetTool() { clearOverlay(); }

void OffsetTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_currentState = State_WaitingForDistance;
  m_selectedItem = nullptr;
  // s_lastDistance <= 0.0 이면 Through 모드를 의미함
  m_isThroughMode = (s_lastDistance <= 0.0);
  m_distance = (s_lastDistance > 0.0) ? s_lastDistance : 0.0;
  m_hasDistance = (s_lastDistance > 0.0);

  if (m_scene) {
    hideGripsOfSelectedItems();
    m_scene->clearSelection();
  }

  updatePrompt();
}

void OffsetTool::deactivate() {
  clearOverlay();

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setPrompt(" Command : ");
  }
  AbstractTool::deactivate();
}

void OffsetTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  if (button == Qt::RightButton) {
    emit toolFinished();
    return;
  }

  if (button != Qt::LeftButton)
    return;

  if (m_currentState == State_WaitingForDistance) {
    // 거리를 입력하지 않고 클릭 시 기본값(Through 또는 기존 거리) 승인으로 처리
    if (s_lastDistance <= 0.0)
      m_isThroughMode = true;
    m_currentState = State_SelectingObject;
    updatePrompt();
  } else if (m_currentState == State_SelectingObject) {
    QGraphicsItem *item = itemAtPos(pos);
    if (item) {
      CadItem *cadItem = dynamic_cast<CadItem *>(item);
      if (!cadItem && item->parentItem()) {
        cadItem = dynamic_cast<CadItem *>(item->parentItem());
      }
      if (cadItem) {
        m_scene->clearSelection();
        cadItem->setSelected(true);
        hideGripsOfSelectedItems();
        m_selectedItem = cadItem;
        m_currentState = State_WaitingForSide;
        updatePrompt();
      }
    }
  } else if (m_currentState == State_WaitingForSide) {
    if (m_isThroughMode) {
      executeOffsetDirect(pos);
    } else {
      executeOffset(pos);
    }
    // 오프셋 작업 후 다시 객체 선택 단계로 루프
    m_scene->clearSelection();
    m_selectedItem = nullptr;
    m_currentState = State_SelectingObject;
    updatePrompt();
  }
}

void OffsetTool::mouseMove(QPointF pos) {
  if (!m_scene || !m_selectedItem)
    return;

  QPainterPath path;
  if (m_currentState == State_WaitingForSide) {
    if (m_isThroughMode) {
      path = calculateOffsetPathDirect(m_selectedItem, pos);
    } else if (m_hasDistance) {
      path = calculateOffsetPath(m_selectedItem, m_distance, pos);
    }
  }

  if (!path.isEmpty()) {
    updateGhostPreview(path, m_selectedItem->layerName());
  } else {
    updateGhostPreview(QPainterPath(), "");
  }
}

bool OffsetTool::processCommand(const QString &command) {
  if (m_currentState == State_WaitingForDistance) {
    QString cmd = command.trimmed().toUpper();
    if (cmd.isEmpty()) {
      if (s_lastDistance <= 0.0) {
        m_isThroughMode = true;
      } else {
        m_distance = s_lastDistance;
        m_hasDistance = true;
        m_isThroughMode = false;
      }
      m_currentState = State_SelectingObject;
      updatePrompt();
      return true;
    }

    if (cmd == "T" || cmd == "THROUGH") {
      m_isThroughMode = true;
      s_lastDistance = 0.0;
      m_currentState = State_SelectingObject;
      updatePrompt();
      return true;
    }

    bool ok;
    double dist = cmd.toDouble(&ok);
    if (ok && dist > 0) {
      m_distance = dist;
      s_lastDistance = dist;
      m_isThroughMode = false;
      m_hasDistance = true;
      m_currentState = State_SelectingObject;
      updatePrompt();
      return true;
    }
  } else if (m_currentState == State_WaitingForSide) {
    if (command.trimmed().isEmpty()) {
      // 투과점/방향 대기 중 엔터 시 -> 다시 객체 선택 단계로 (또는 종료)
      m_scene->clearSelection();
      m_selectedItem = nullptr;
      m_currentState = State_SelectingObject;
      updatePrompt();
      return true;
    }
  } else if (m_currentState == State_SelectingObject) {
    if (command.trimmed().isEmpty()) {
      emit toolFinished(); // Exit on Empty in selection state
      return true;
    }
  }
  return AbstractTool::processCommand(command);
}

void OffsetTool::keyPress(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    emit toolFinished();
  } else {
    AbstractTool::keyPress(event);
  }
}

void OffsetTool::updatePrompt() {
  IAppContext *ctx = getContext();
  if (!ctx || !ctx->commandDock())
    return;

  ctx->commandDock()->setToolActive(true);
  switch (m_currentState) {
  case State_WaitingForDistance: {
    QString defaultStr = (s_lastDistance <= 0.0)
                             ? "Through"
                             : QString::number(s_lastDistance, 'f', 4);
    ctx->commandDock()->showInteractivePrompt(
        tr(" 옵셋 거리를 지정 하세요 or [Through] <%1> : ")
            .arg(defaultStr),
        {});
    break;
  }
  case State_SelectingObject: {
    QString distInfo = (s_lastDistance <= 0.0)
                           ? "Through"
                           : QString::number(s_lastDistance, 'f', 4);
    ctx->commandDock()->showInteractivePrompt(
        tr(" 옵셋 객체를 선택 하세요 (Distance=%1) or [Exit] <Exit> : ")
            .arg(distInfo),
        {});
    break;
  }
  case State_WaitingForSide:
    if (m_isThroughMode) {
      ctx->commandDock()->showInteractivePrompt(tr(" 옵셋 점을 지정하세요 : "),
                                                {});
    } else {
      ctx->commandDock()->showInteractivePrompt(
          tr(" 옵셋 방향을 지정 하세요 : "), {});
    }
    break;
  }
}

void OffsetTool::executeOffset(const QPointF &sidePoint) {
  if (!m_selectedItem || !m_scene || !m_hasDistance)
    return;

  QPainterPath path =
      calculateOffsetPath(m_selectedItem, m_distance, sidePoint);
  if (path.isEmpty())
    return;

  IAppContext *ctx = getContext();
  if (!ctx || !ctx->entityFactory())
    return;

  // 생성된 패스를 기반으로 새로운 객체 생성 (실제 구현은 아이템 타입별로
  // 분기하는게 좋음) 여기서는 단순화를 위해 Line, Circle, Arc에 맞춰 직접
  // 생성함
  QGraphicsItem *newItem = nullptr;
  QPen layerPen = m_selectedItem->pen();

  const MyEntity *entity = m_selectedItem->getEntityData();
  if (!entity)
    return;

  if (entity->type() == EntityType::Line) {
    if (path.elementCount() >= 2) {
      QLineF line(path.elementAt(0), path.elementAt(1));
      newItem = ctx->entityFactory()->createLine(line, layerPen);
    }
  } else if (entity->type() == EntityType::Circle) {
    QRectF br = path.boundingRect();
    newItem = ctx->entityFactory()->createCircle(br.center(), br.width() / 2.0,
                                                 layerPen);
  } else if (entity->type() == EntityType::Arc) {
    const MyArc *ma = static_cast<const MyArc *>(entity);
    double dToCenter = MyVector(sidePoint).distanceTo(ma->center);
    double newRadius = (dToCenter > ma->radius) ? (ma->radius + m_distance)
                                                : (ma->radius - m_distance);

    if (newRadius > 0.001) {
      newItem = ctx->entityFactory()->createArc(ma->center.toPointF(),
                                                newRadius, ma->startAngle,
                                                ma->spanAngle, layerPen);
    }
  } else if (entity->type() == EntityType::Polyline) {
    const MyPolyline *mpl = static_cast<const MyPolyline *>(entity);
    std::vector<MyVector> newVertices;
    for (int i = 0; i < path.elementCount(); ++i) {
      QPainterPath::Element el = path.elementAt(i);
      if (el.isMoveTo() || el.isLineTo()) {
        MyVector v(el.x, el.y);
        if (newVertices.empty() || v.distanceTo(newVertices.back()) > 0.001) {
          newVertices.push_back(v);
        }
      }
    }
    // 폐합된 경우 마지막 점이 첫 점과 겹칠 수 있으므로 제거
    if (newVertices.size() > 2 &&
        newVertices.front().distanceTo(newVertices.back()) < 0.001) {
      newVertices.pop_back();
    }

    if (newVertices.size() >= 2) {
      newItem = ctx->entityFactory()->createPolyline(newVertices, mpl->isClosed,
                                                     layerPen);
    }
  }

  if (newItem) {
    if (CadItem *ci = dynamic_cast<CadItem *>(newItem)) {
      ci->setLayerName(m_selectedItem->layerName());
      // 다중선 선을 오프셋하면 ROLE/SPACING 은 유지하되 독립 그룹(새 UUID)으로 둔다
      QMap<QString, QString> gidRemap;
      MultilineTagUtil::copyTagsWithNewGroup(m_selectedItem, ci, gidRemap);
      QGraphicsItem *layerFolder =
          m_scene->getLayerRoot(m_selectedItem->layerName());
      ci->setParentItem(layerFolder);

      if (ci->scene() == m_scene) {
        m_scene->removeItem(ci);
      }

      if (ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, ci));
      } else {
        m_scene->addItem(ci);
      }
    }

    if (ctx->commandDock()) {
      ctx->commandDock()->logMessage("Offset created.", CommandDock::Info);
    }
  }
}

void OffsetTool::executeOffsetDirect(const QPointF &targetPoint) {
  if (!m_selectedItem)
    return;

  // 타겟 포인트까지의 거리를 계산하여 오프셋 실행
  double dist = 0;
  const MyEntity *entity = m_selectedItem->getEntityData();
  if (!entity)
    return;

  if (entity->type() == EntityType::Line) {
    const MyLine *ml = static_cast<const MyLine *>(entity);
    QLineF line(ml->start.toPointF(), ml->end.toPointF());
    // 선에서 점까지의 거리
 //   QLineF normal = line.normalVector();
    QPointF p1 = line.p1();
    QPointF p2 = line.p2();
    double a = p2.y() - p1.y();
    double b = p1.x() - p2.x();
    double c = p2.x() * p1.y() - p1.x() * p2.y();
    dist = qAbs(a * targetPoint.x() + b * targetPoint.y() + c) /
           qSqrt(a * a + b * b);
  } else if (entity->type() == EntityType::Circle ||
             entity->type() == EntityType::Arc) {
    MyVector center;
    double radius;
    if (entity->type() == EntityType::Circle) {
      center = static_cast<const MyCircle *>(entity)->center;
      radius = static_cast<const MyCircle *>(entity)->radius;
    } else {
      center = static_cast<const MyArc *>(entity)->center;
      radius = static_cast<const MyArc *>(entity)->radius;
    }
    dist = qAbs(MyVector(targetPoint).distanceTo(center) - radius);
  } else if (entity->type() == EntityType::Polyline) {
    // 폴리라인에서 가장 가까운 점까지의 거리
    MyVector nearest = entity->getNearestPoint(MyVector(targetPoint));
    dist = nearest.distanceTo(MyVector(targetPoint));
  }

  if (dist > 0.001) {
    m_distance = dist;
    m_hasDistance = true;
    executeOffset(targetPoint);
  }
}

QPainterPath OffsetTool::calculateOffsetPath(CadItem *item, double distance,
                                             const QPointF &sidePoint) const {
  if (!item)
    return QPainterPath();
  const MyEntity *entity = item->getEntityData();
  if (!entity)
    return QPainterPath();

  QPainterPath path;
  if (entity->type() == EntityType::Line) {
    const MyLine *ml = static_cast<const MyLine *>(entity);
    QLineF line(ml->start.toPointF(), ml->end.toPointF());
    QLineF unitNormal = line.normalVector().unitVector();
    QPointF normalVec(unitNormal.dx(), unitNormal.dy());

    QPointF v1 = line.p2() - line.p1();
    QPointF v2 = sidePoint - line.p1();
    double cross = v1.x() * v2.y() - v1.y() * v2.x();

    if (cross > 0) {
      path.moveTo(line.p1() - normalVec * distance);
      path.lineTo(line.p2() - normalVec * distance);
    } else {
      path.moveTo(line.p1() + normalVec * distance);
      path.lineTo(line.p2() + normalVec * distance);
    }
  } else if (entity->type() == EntityType::Circle ||
             entity->type() == EntityType::Arc) {
    MyVector center;
    double radius;
    if (entity->type() == EntityType::Circle) {
      center = static_cast<const MyCircle *>(entity)->center;
      radius = static_cast<const MyCircle *>(entity)->radius;
    } else {
      center = static_cast<const MyArc *>(entity)->center;
      radius = static_cast<const MyArc *>(entity)->radius;
    }

    double dToCenter = MyVector(sidePoint).distanceTo(center);
    double newRadius =
        (dToCenter > radius) ? (radius + distance) : (radius - distance);

    if (newRadius > 0.001) {
      if (entity->type() == EntityType::Circle) {
        path.addEllipse(center.toPointF(), newRadius, newRadius);
      } else {
        const MyArc *ma = static_cast<const MyArc *>(entity);
        QRectF rect(center.x - newRadius, center.y - newRadius, newRadius * 2,
                    newRadius * 2);

        // [핵심 해결] Qt의 좌표계(Y-Down) 보정: 각도와 스팬을 반전(-) 처리
        // (ArcItem::createQtPath와 동일)
        double drawStart = -ma->startAngle;
        double drawSpan = -ma->spanAngle;

        path.arcMoveTo(rect, drawStart);
        path.arcTo(rect, drawStart, drawSpan);
      }
    }
  } else if (entity->type() == EntityType::Polyline) {
    const MyPolyline *mpl = static_cast<const MyPolyline *>(entity);
    if (mpl->vertices.size() < 2)
      return QPainterPath();

    MyVector sideVec(sidePoint);

    // 1. 전역 오프셋 방향(Global Sign) 결정
    int nearestIdx = -1;
    double minSegDist = -1.0;
    size_t vCount = mpl->vertices.size();
    size_t segCount = mpl->isClosed ? vCount : vCount - 1;

    for (size_t i = 0; i < segCount; ++i) {
      MyVector p1 = mpl->vertices[i];
      MyVector p2 = mpl->vertices[(i + 1) % vCount];
      MyVector pt = MyMath::getNearestPointOnSegment(sideVec, MyLine(p1, p2));
      double d = pt.distanceTo(sideVec);
      if (nearestIdx == -1 || d < minSegDist) {
        minSegDist = d;
        nearestIdx = i;
      }
    }

    if (nearestIdx == -1)
      return QPainterPath();

    MyVector s1 = mpl->vertices[nearestIdx];
    MyVector s2 = mpl->vertices[(nearestIdx + 1) % vCount];
    QLineF baseLine(s1.toPointF(), s2.toPointF());
    QLineF baseNormalLine = baseLine.normalVector().unitVector();
    MyVector baseN(baseNormalLine.dx(), baseNormalLine.dy());
    MyVector nearestPtOnBase =
        MyMath::getNearestPointOnSegment(sideVec, MyLine(s1, s2));
    MyVector toSide = sideVec - nearestPtOnBase;
    double globalSign = (MyMath::dotProduct(toSide, baseN) > 0) ? 1.0 : -1.0;

    for (size_t i = 0; i < vCount; ++i) {
      MyVector v = mpl->vertices[i];

      // 이전/다음 꼭짓점 찾기 (폐합 여부 고려)
      MyVector pPrev, pNext;
      bool hasPrev = false, hasNext = false;

      if (i > 0) {
        pPrev = mpl->vertices[i - 1];
        hasPrev = true;
      } else if (mpl->isClosed) {
        pPrev = mpl->vertices[vCount - 1];
        hasPrev = true;
      }

      if (i < vCount - 1) {
        pNext = mpl->vertices[i + 1];
        hasNext = true;
      } else if (mpl->isClosed) {
        pNext = mpl->vertices[0];
        hasNext = true;
      }

      MyVector avgN;
      double scale = 1.0;

      if (!hasPrev && hasNext) {
        // 시작점 (Open Polyline)
        QLineF line(v.toPointF(), pNext.toPointF());
        QLineF norm = line.normalVector().unitVector();
        avgN = MyVector(norm.dx(), norm.dy());
      } else if (hasPrev && !hasNext) {
        // 끝점 (Open Polyline)
        QLineF line(pPrev.toPointF(), v.toPointF());
        QLineF norm = line.normalVector().unitVector();
        avgN = MyVector(norm.dx(), norm.dy());
      } else if (hasPrev && hasNext) {
        // 중간점 또는 Closed 꼭짓점
        QLineF l1(pPrev.toPointF(), v.toPointF());
        QLineF l2(v.toPointF(), pNext.toPointF());

        QLineF n1Unit = l1.normalVector().unitVector();
        QLineF n2Unit = l2.normalVector().unitVector();
        MyVector n1(n1Unit.dx(), n1Unit.dy());
        MyVector n2(n2Unit.dx(), n2Unit.dy());

        MyVector bisector = n1 + n2;
        double bMag =
            std::sqrt(bisector.x * bisector.x + bisector.y * bisector.y);

        if (bMag < 0.001) {
          // 일직선이거나 180도 꺾인 경우
          avgN = n1;
        } else {
          avgN = bisector * (1.0 / bMag);
          // 스케일링: d' = d / cos(phi)
          double cosPhi = MyMath::dotProduct(avgN, n1);
          if (std::abs(cosPhi) > 0.01) {
            scale = 1.0 / std::abs(cosPhi);
          }
          if (scale > 10.0)
            scale = 10.0; // 심한 돌출 방지
        }
      } else {
        // 단일점 (비정상 데이터)
        continue;
      }

      QPointF targetPos =
          v.toPointF() + avgN.toPointF() * (distance * scale * globalSign);

      if (i == 0)
        path.moveTo(targetPos);
      else
        path.lineTo(targetPos);
    }
    if (mpl->isClosed)
      path.closeSubpath();
  }
  return path;
}

QPainterPath
OffsetTool::calculateOffsetPathDirect(CadItem *item,
                                      const QPointF &targetPoint) const {
  if (!item)
    return QPainterPath();
  const MyEntity *entity = item->getEntityData();
  if (!entity)
    return QPainterPath();

  double dist = 0;
  if (entity->type() == EntityType::Line) {
    const MyLine *ml = static_cast<const MyLine *>(entity);
    QLineF line(ml->start.toPointF(), ml->end.toPointF());
    QPointF p1 = line.p1();
    QPointF p2 = line.p2();
    double a = p2.y() - p1.y();
    double b = p1.x() - p2.x();
    double c = p2.x() * p1.y() - p1.x() * p2.y();
    dist = qAbs(a * targetPoint.x() + b * targetPoint.y() + c) /
           qSqrt(a * a + b * b);
  } else if (entity->type() == EntityType::Circle ||
             entity->type() == EntityType::Arc) {
    MyVector center;
    double radius;
    if (entity->type() == EntityType::Circle) {
      center = static_cast<const MyCircle *>(entity)->center;
      radius = static_cast<const MyCircle *>(entity)->radius;
    } else {
      center = static_cast<const MyArc *>(entity)->center;
      radius = static_cast<const MyArc *>(entity)->radius;
    }
    dist = qAbs(MyVector(targetPoint).distanceTo(center) - radius);
  } else if (entity->type() == EntityType::Polyline) {
    MyVector nearest = entity->getNearestPoint(MyVector(targetPoint));
    dist = nearest.distanceTo(MyVector(targetPoint));
  }

  return calculateOffsetPath(item, dist, targetPoint);
}

void OffsetTool::clearOverlay() {
  if (m_scene) {
    m_scene->clearOverlay();
  }
}

void OffsetTool::hideGripsOfSelectedItems() {
  if (!m_scene)
    return;
  for (auto *item : m_scene->selectedItems()) {
    for (auto *child : item->childItems()) {
      if (dynamic_cast<GripItem *>(child)) {
        child->setVisible(false);
      }
    }
  }
}
