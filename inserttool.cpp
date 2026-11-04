#include "inserttool.h"
#include "attributeinputdialog.h"
#include "textitem.h"
#include "blockitem.h"
#include "mygraphicsview.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "filemanager.h"
#include "inputparser.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QTimer>

InsertTool::InsertTool(QObject *parent)
    : AbstractTool(parent), m_state(PickingFile), m_scale(1.0),
      m_rotation(0.0) {}

InsertTool::~InsertTool() { clearItems(); }

void InsertTool::setBlockItems(const QList<CadItem *> &items,
                               const QString &name,
                               const QString &originalLayer) {
  clearItems();
  m_items = items;
  m_filePath = name; // blockName 추출을 위해 사용
  m_originalLayerName = originalLayer;

  if (!m_items.isEmpty()) {
    // [보정] 정확한 바운딩 박스 계산을 위해 임시 씬 활용
    MyGraphicsScene tempScene;
    for (CadItem *item : std::as_const(m_items)) {
      if (item)
        tempScene.addItem(item);
    }
    QRectF rect = tempScene.itemsBoundingRect();
    m_thumbnailOffset = rect.topLeft();
    m_thumbnailSize = rect.size();

    // 씬에서 다시 제거 (소유권 유지)
    for (CadItem *item : std::as_const(m_items)) {
      if (item)
        tempScene.removeItem(item);
    }
  }
}

void InsertTool::activate(MyGraphicsScene *scene) {
  // 이미 현재 씬에서 활성화된 상태라면 중복 실행(다이얼로그 재호출)을
  // 방지합니다
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);

  // 이벤트 처리가 완료된 후 실행 (Enter 키 입력 누수를 방지)
  QTimer::singleShot(0, this, [this]() {
    // 1. 이미 아이템들이 준비되어 있다면 (Current Drawing 모드 등) 로드 생략
    if (!m_items.isEmpty()) {
      m_state = PickingPoint;
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->showInteractivePrompt(
            tr(" 블록 삽입점 지정 : "), {});
      }
      return;
    }

    if (m_filePath.isEmpty()) {
      emit toolFinished();
      return;
    }

    // 2. 블록 데이터 로드
    FileManager *fm = m_appContext->fileManager();
    if (fm) {
      m_items = fm->importBlock(m_filePath, &m_originalLayerName);
      m_thumbnail = QPixmap::fromImage(fm->loadThumbnail(m_filePath));

      if (!m_items.isEmpty()) {
        MyGraphicsScene tempScene;
        for (CadItem *item : std::as_const(m_items)) {
          if (item)
            tempScene.addItem(item);
        }
        QRectF rect = tempScene.itemsBoundingRect();
        m_thumbnailOffset = rect.topLeft();
        m_thumbnailSize = rect.size();

        for (CadItem *item : std::as_const(m_items)) {
          if (item)
            tempScene.removeItem(item);
        }
      }
    }

    if (m_items.isEmpty()) {
//      qDebug() << "InsertTool: No items imported from" << m_filePath;
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->logMessage(
            tr("블록을 불러오지 못했습니다. 파일이 비어 있거나 손상되었을 수 "
               "있습니다."),
            CommandDock::Error);
      }
      m_filePath.clear(); // 오류 시 경로 초기화
      emit toolFinished();
      return;
    }

    m_state = PickingPoint;
  //  qDebug() << "InsertTool: Items loaded. State: PickingPoint";
    if (m_appContext && m_appContext->commandDock()) {
      m_appContext->commandDock()->showInteractivePrompt(
          tr(" 블록 삽입점 지정 : "), {});
    }
  });
}

void InsertTool::deactivate() {
  if (m_scene)
    m_scene->clearOverlay();
  clearItems(); // Finished나 Cancel 시 메모리 해제
  AbstractTool::deactivate();
}

void InsertTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (button != Qt::LeftButton)
    return;

  if (m_state == PickingPoint) {
    m_insertionPoint = pos;
    m_state = PickingScale;
//    qDebug() << "InsertTool: Point picked. State: PickingScale";
    if (m_appContext && m_appContext->commandDock()) {
      m_appContext->commandDock()->showInteractivePrompt(
          tr(" 축척 지정 [Enter: 1.0]: "), {});
    }
  } else if (m_state == PickingScale) {
    // 축척 확정 (기본 1.0, 마우스 거리 비례)
    double dist = MyVector(m_insertionPoint).distanceTo(MyVector(pos));
    if (dist > 1.0)
      m_scale = dist / 10.0; // 10px = scale 1.0 기준
    else
      m_scale = 1.0;

    m_state = PickingRotation;
    // qDebug() << "InsertTool: Scale picked" << m_scale
    //          << ". State: PickingRotation";
    if (m_appContext && m_appContext->commandDock()) {
      m_appContext->commandDock()->showInteractivePrompt(
          tr(" 회전 각도 지정 [Enter: 0.0]: "), {});
    }
  } else if (m_state == PickingRotation) {
    // 회전 확정
    double angleRad = std::atan2(pos.y() - m_insertionPoint.y(),
                                 pos.x() - m_insertionPoint.x());
    m_rotation = MyMath::rad2deg(angleRad);

    createAndInsertBlock();

    m_state = Finished;
    if (m_appContext && m_appContext->commandDock()) {
      m_appContext->commandDock()->setPrompt(tr(" Command : "));
    }
    emit toolFinished();
  }
}

void InsertTool::mouseMove(QPointF pos) { updateGhost(pos); }

void InsertTool::keyPress(QKeyEvent *event) {
  if (m_inDialog) {
    event->ignore();
    return;
  }
  if (event->key() == Qt::Key_Escape) {
    emit toolFinished();
    event->accept();
  } else if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
    if (m_state == PickingScale) {
      m_scale = 1.0;
      m_state = PickingRotation;
      updateGhost(m_insertionPoint);
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->showInteractivePrompt(
            tr(" 회전 각도 지정 [Enter: 0.0]: "), {});
      }
      event->accept();
    } else if (m_state == PickingRotation) {
      m_rotation = 0.0;
      createAndInsertBlock();

      // [핵심] 명령 반복 방지: 커맨드 독의 입력창을 비워 반복 실행을 차단
      if (m_appContext && m_appContext->commandDock() &&
          m_appContext->commandDock()->inputWidget()) {
        m_appContext->commandDock()->inputWidget()->clear();
      }

      // [수정] Enter 키로 툴을 종료할 때 이벤트 누수로 인해 기본
      // 툴(SelectTool)이 Enter 이벤트를 받아 이전 명령어(Insert)를 다시
      // 실행하는 무한 루프를 방지합니다. 이벤트 처리가 완료된 후 툴을
      // 종료하도록 지연시킵니다.
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->setPrompt(tr(" Command : "));
      }
      QTimer::singleShot(0, this, [this]() { emit toolFinished(); });
      event->accept();
    }
  }
}

void InsertTool::clearItems() {
  qDeleteAll(m_items);
  m_items.clear();
}

void InsertTool::updateGhost(const QPointF &pos) {
  if (m_items.isEmpty() || !m_scene)
    return;

  double tempScale = m_scale;
  double tempRotation = m_rotation;
  QPointF tempPoint = m_insertionPoint;

  if (m_state == PickingPoint) {
    tempPoint = pos;
  } else if (m_state == PickingScale) {
    double dist = MyVector(m_insertionPoint).distanceTo(MyVector(pos));
    tempScale = (dist > 1.0) ? (dist / 10.0) : 1.0;
  } else if (m_state == PickingRotation) {
    double angleRad = std::atan2(pos.y() - m_insertionPoint.y(),
                                 pos.x() - m_insertionPoint.x());
    tempRotation = MyMath::rad2deg(angleRad);
  }

  if (!m_thumbnail.isNull()) {
    // 썸네일 이미지를 사용하는 경우 (고화질/고성능)
    // m_thumbnailOffset(topLeft)과 m_thumbnailSize를 합쳐 0,0 기준 BBox 생성
    QRectF blockRect(m_thumbnailOffset, m_thumbnailSize);
    m_scene->setGhostPixmap(m_thumbnail, tempPoint, blockRect, tempScale,
                            tempRotation, true);
  } else {
    // 썸네일이 없는 경우 기존 패스 방식 사용
    QPainterPath totalPath;
    MyVector base(0, 0);

    for (CadItem *item : std::as_const(m_items)) {
      QPainterPath p = item->getScaledPath(base, tempScale);
      QTransform trans;
      trans.translate(tempPoint.x(), tempPoint.y());
      trans.rotate(tempRotation);
      totalPath.addPath(trans.map(p));
    }
    m_scene->setGhostPath(totalPath, true, m_scene->defaultGhostColor(), 1.0, {5, 5});
  }
}

void InsertTool::applyTransformations() {
  for (CadItem *item : std::as_const(m_items)) {
    // 1. Scale/Rotate at (0,0)
    if (m_scale != 1.0)
      item->scale(MyVector(0, 0), m_scale);
    if (m_rotation != 0.0)
      item->rotate(MyVector(0, 0), m_rotation);

    // 2. Move to insertion point
    item->move(MyVector(m_insertionPoint));
  }
}

bool InsertTool::processCommand(const QString &command) {
  if (m_inDialog) return false;
  QString cmd = command.toLower().trimmed();

  if (cmd == "c" || cmd == "cancel" || cmd == "exit") {
    if (m_appContext && m_appContext->commandDock()) {
      m_appContext->commandDock()->setPrompt(tr(" Command : "));
    }
    emit toolFinished();
    return true;
  }

  if (cmd.isEmpty() || cmd == "insert") {
    if (m_state == PickingScale) {
      m_scale = 1.0;
      m_state = PickingRotation;
      updateGhost(m_insertionPoint);
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->showInteractivePrompt(
            tr(" 회전 각도 지정 [Enter: 0.0]: "), {});
      }
      blockEnterKeyTemporarily(200);
      return true;
    } else if (m_state == PickingRotation) {
      m_rotation = 0.0;
      createAndInsertBlock();
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->setPrompt(tr(" Command : "));
      }
      QTimer::singleShot(0, this, [this]() { emit toolFinished(); });
      blockEnterKeyTemporarily(200);
      return true;
    }
    return true;
  }

  bool ok = false;
  double val = cmd.toDouble(&ok);

  if (m_state == PickingPoint) {
    if (m_appContext && m_appContext->parser()) {
      QPointF base(0, 0);
      QPointF resultPos;
      bool isOrtho = false;
      if (m_appContext->snapManager()) {
        isOrtho = m_appContext->snapManager()->isOrtho();
      }
      if (m_appContext->parser()->parseCoordinate(cmd, base, m_insertionPoint,
                                                  isOrtho, resultPos)) {
        mousePress(resultPos, Qt::LeftButton);
        blockEnterKeyTemporarily(200);
        return true;
      }
    }
  } else if (m_state == PickingScale) {
    if (ok) {
      m_scale = val;
      m_state = PickingRotation;
      updateGhost(m_insertionPoint);
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->showInteractivePrompt(
            tr(" 회전 각도 지정 [Enter: 0.0]: "), {});
      }
      blockEnterKeyTemporarily(200);
      return true;
    }
  } else if (m_state == PickingRotation) {
    if (ok) {
      m_rotation = val;
      createAndInsertBlock();
      if (m_appContext && m_appContext->commandDock()) {
        m_appContext->commandDock()->setPrompt(tr(" Command : "));
      }
      QTimer::singleShot(0, this, [this]() { emit toolFinished(); });
      blockEnterKeyTemporarily(200);
      return true;
    }
  }

  return false;
}

void InsertTool::createAndInsertBlock() {
  if (!m_scene || m_items.isEmpty())
    return;

  // 1. m_items 내에서 속성 텍스트 수집 (인덱스 기반 루프 사용)
  QList<TextItem *> attrTexts;
  QMap<QString, QString> prompts;
  QMap<QString, QString> defaults;

  for (int i = 0; i < m_items.size(); ++i) {
    TextItem *textItem = dynamic_cast<TextItem *>(m_items.at(i));
    if (textItem && textItem->getProperties().value("isAttribute", false).toBool()) {
      QString tag = textItem->getProperties().value("tag", "").toString();
      QString prompt = textItem->getProperties().value("prompt", "").toString();
      QString content = textItem->getProperties().value("Content", "").toString();
      if (!tag.isEmpty()) {
        attrTexts.append(textItem);
        prompts.insert(tag, prompt);
        defaults.insert(tag, content);
      }
    }
  }

  // 속성값이 있는 경우 사용자에게 속성값을 다이얼로그로 묻는다
  if (!attrTexts.isEmpty()) {
    QWidget *parentWidget = nullptr;
    if (m_appContext && m_appContext->activeView()) {
      parentWidget = m_appContext->activeView();
    }
    m_inDialog = true;
    AttributeInputDialog dialog(prompts, defaults, parentWidget);
    int res = dialog.exec();
    m_inDialog = false;

    if (res == QDialog::Accepted) {
      QMap<QString, QString> userValues = dialog.getAttributeValues();
      for (int i = 0; i < attrTexts.size(); ++i) {
        QString tag = attrTexts.at(i)->getProperties().value("tag", "").toString();
        if (userValues.contains(tag)) {
          attrTexts.at(i)->setProperty("Content", userValues.value(tag));
        }
      }
    } else {
      // 취소 클릭 시 삽입 취소
      clearItems();
      return;
    }
  }

  QString blockName = QFileInfo(m_filePath).baseName();
  if (blockName.isEmpty())
    blockName = "Block";
  BlockItem *block = new BlockItem(m_items, m_insertionPoint, blockName,
                                   m_scale, m_scale, m_rotation);

  // 레이어 우선순위:
  //   1) BlockPaletteDock에서 명시적으로 지정한 레이어 (m_targetLayer)
  //   2) 원본 블록 파일의 레이어 (m_originalLayerName)
  //   3) 현재 활성 레이어
  QString targetLayer = !m_targetLayer.isEmpty()      ? m_targetLayer
                        : !m_originalLayerName.isEmpty() ? m_originalLayerName
                                                         : currentLayerName();
  block->setLayerName(targetLayer);
  block->setPen(getLayerPen(targetLayer));
  m_appContext->undoStack()->push(new AddEntityCommand(m_scene, block));
  m_items.clear(); // 소유권 이전
}
