#include "drawsubwindow.h"
#include "abstracttool.h"
#include "appcontext.h"
#include "caditem.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "selectionmanager.h"
#include "textitem.h"
#include "thememanager.h"
//#include "filecommands.h"
#include "filemanager.h"
#include "iappcontext.h"
#include "subwindowfloatcontroller.h"

#include <QUndoStack>
#include <QDebug>
#include <QIcon>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>

DrawSubWindow::DrawSubWindow(QWidget *parent)
    : QMdiSubWindow(parent), myScene(nullptr), myView(nullptr),
      m_activeTool(nullptr), m_defaultTool(nullptr), m_ctx(nullptr),
      m_layerManager(nullptr), m_undoStack(nullptr),
      m_selectionManager(nullptr) { // [복구] 변수 초기화
  // [ThemeManager 연동]
  setWindowFlags(Qt::SubWindow | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_StyledBackground, true);
  setObjectName("DrawSubWindow");

  QWidget *container = new QWidget;

  container->setObjectName("DrawSubWindowContainer");
  container->setAttribute(Qt::WA_StyledBackground, true);
  // [버그 수정] 서브윈도우를 최소화하면 QMdiSubWindow가 이 위젯(및 내부의
  // QOpenGLWidget 뷰포트)을 폭/높이 0에 가깝게 줄이는데, 그 상태에서
  // QOpenGLWidget이 프레임버퍼(FBO)를 재생성하면 "Framebuffer incomplete
  // attachment" / "Failed to create wrapper texture" 오류가 반복 발생한다.
  // 최소 크기를 강제로 지정해 QMdiSubWindow가 절대 0(또는 그에 가까운) 크기로
  // 줄이지 못하도록 막는다 (setGeometry/resize는 minimumSize를 항상 존중함).
  container->setMinimumSize(64, 64);

  QVBoxLayout *layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);

  myScene = new MyGraphicsScene(this);
  myView = new MyGraphicsView(this);

  myView->setScene(myScene);
  myView->setupBottomLeftOrigin();

  layout->addWidget(myView);
  setWidget(container); // [수정] 위젯을 View가 아닌 Container로 설정
  setAttribute(Qt::WA_DeleteOnClose, true);
  myScene->setSceneRect(0, 0, 10000, 10000);

  connect(myView, &MyGraphicsView::mouseMoved, this,
          &DrawSubWindow::coordinateChanged);

  connect(myScene, &QGraphicsScene::selectionChanged, this,
          &DrawSubWindow::onSelectionChanged);

  // [복구] 윈도우 아이콘 설정
//  setWindowIcon(QIcon(":/icons/Bcompass.png"));
}

void DrawSubWindow::setContext(AppContext *ctx) {
  m_ctx = ctx;

  if (!m_layerManager) {
    m_layerManager = new LayerManager(m_ctx, this);
    connect(m_layerManager, &LayerManager::layerPropertyChanged, this,
            &DrawSubWindow::onLayerPropertyChanged);

    // [수정] 람다를 사용하지 않고 Signal to Signal 방식으로 안전하게 연결
    connect(m_layerManager, &LayerManager::currentLayerChanged, m_ctx,
            &AppContext::currentLayerChanged);

    // [수정] 인덱스 기반 루프 사용 (C++11 detach 경고 방지)
    QList<Layer> layers = m_layerManager->getAllLayers();
    for (int i = 0; i < layers.size(); ++i) {
      onLayerPropertyChanged(layers.at(i).name());
    }
  }

  if (myScene) {
      myScene->setContext(ctx);
  }
  if (!m_undoStack) {
    m_undoStack = new QUndoStack(this);
  }

  if (!m_selectionManager) {
    m_selectionManager = new SelectionManager(this);
  }

    // [핵심] 이중 연결 방지: setContext는 AppContext와 MainWindow에서 두 번 호출됨
    if (!m_themeConnected && m_ctx && m_ctx->themeManager()) {
        m_themeConnected = true;
        ThemeManager *tm = m_ctx->themeManager();
        connect(tm, &ThemeManager::themeChanged, this, &DrawSubWindow::onThemeChanged);

        // 초기 테마 즉시 적용
        onThemeChanged(tm->currentTheme());
    }
}

DrawSubWindow::~DrawSubWindow() {
  // 공유 SelectTool(m_defaultTool)이 이 창의 씬을 가리킨 채 남으면 다른 창
  // 활성화 시 파괴된 씬을 접근해 크래시하므로 씬 소멸 전에 해제한다.
  if (myScene) {
    if (m_defaultTool)
      m_defaultTool->detachFromScene(myScene);
    if (m_activeTool && m_activeTool != m_defaultTool)
      m_activeTool->detachFromScene(myScene);
  }

  if (myScene)
    disconnect(myScene, nullptr, this, nullptr);
  if (myView)
    disconnect(myView, nullptr, this, nullptr);
  if (m_layerManager)
    disconnect(m_layerManager, nullptr, this, nullptr);
  if (m_ctx && m_ctx->themeManager())
    disconnect(m_ctx->themeManager(), nullptr, this, nullptr);

  if (m_activeTool && m_activeTool != m_defaultTool) {
    disconnect(m_activeTool, nullptr, this, nullptr);
    delete m_activeTool;
  }
}

AbstractTool *DrawSubWindow::tool() const { return m_activeTool; }
LayerManager *DrawSubWindow::layerManager() const { return m_layerManager; }
QUndoStack *DrawSubWindow::undoStack() const { return m_undoStack; }
SelectionManager *DrawSubWindow::selectionManager() const {
  return m_selectionManager;
}
MyGraphicsScene *DrawSubWindow::scene() const { return myScene; }
MyGraphicsView *DrawSubWindow::view() const { return myView; }

void DrawSubWindow::focusInEvent(QFocusEvent *event) {
  if (m_ctx) {
    if (m_ctx->floatController()) {
      if (parentWidget() == nullptr || m_ctx->floatController()->isDetached(this)) {
        m_ctx->floatController()->setActiveFloatingWindow(this);
      } else {
        m_ctx->floatController()->clearActiveFloatingWindow();
      }
    }
    m_ctx->setLastActiveWindow(this);
  }

  if (m_activeTool && myScene) {
    m_activeTool->activate(myScene);
    myScene->setTool(m_activeTool);
  }

  QMdiSubWindow::focusInEvent(event);
}

void DrawSubWindow::mousePressEvent(QMouseEvent *event) {
  if (m_ctx) {
    if (m_ctx->floatController()) {
      if (parentWidget() == nullptr || m_ctx->floatController()->isDetached(this)) {
        m_ctx->floatController()->setActiveFloatingWindow(this);
      } else {
        m_ctx->floatController()->clearActiveFloatingWindow();
      }
    }
    m_ctx->setLastActiveWindow(this);
  }
  QMdiSubWindow::mousePressEvent(event);
}

void DrawSubWindow::changeEvent(QEvent *event) {
  if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
    if (m_ctx) {
      if (m_ctx->floatController()) {
        if (parentWidget() == nullptr || m_ctx->floatController()->isDetached(this)) {
          m_ctx->floatController()->setActiveFloatingWindow(this);
        }
      }
      m_ctx->setLastActiveWindow(this);
    }
  }
  QMdiSubWindow::changeEvent(event);
}

void DrawSubWindow::setDefaultTool(AbstractTool *tool) {
  m_defaultTool = tool;
  setTool(m_defaultTool);
}

void DrawSubWindow::resetToDefaultTool() {
  //    qDebug() << "resetToDefaultTool :---" << m_defaultTool;
  if (m_defaultTool) {
    setTool(m_defaultTool);
  }
}

void DrawSubWindow::setTool(AbstractTool *tool) {
  if (m_activeTool == tool)
    return;

  if (m_activeTool) {
    disconnect(m_activeTool, &AbstractTool::toolFinished, this,
               &DrawSubWindow::onToolFinished);

    if (m_activeTool != m_defaultTool) {
      m_activeTool->deleteLater();
    }
    m_activeTool = nullptr;
  }

  m_activeTool = tool;

  if (m_activeTool) {
    myScene->setTool(m_activeTool);

    connect(m_activeTool, &AbstractTool::toolFinished, this,
            &DrawSubWindow::onToolFinished);

    //        qDebug() << "Tool Activated";
    myView->setCustomCrossCursor();
  } else {
    myView->setCustomCrossCursor();
  }

  onSelectionChanged();
}

void DrawSubWindow::onToolFinished() {
  //    qDebug() << "Tool finished.";
  resetToDefaultTool();
}

void DrawSubWindow::onSelectionChanged() {
  if (!myScene || !m_selectionManager)
    return;

  // [성능] Qt는 setSelected() 호출마다 selectionChanged를 emit하므로, 박스
  // 선택 확정·move/copy처럼 수천 항목을 선택하면 이 슬롯이 수천 번 불려
  // SelectionManager→PropertyDock 재구성이 반복돼 수 초가 걸린다.
  // 대량 구간이면 아예 건너뛰고(구간 종료 시 scene이 1회 재발행),
  // 그 밖에도 한 이벤트 루프 턴에 몰린 신호는 0-타이머로 모아 1회만 처리한다.
  if (myScene->isBulkSelectionChange())
    return;
  if (m_selectionUpdatePending)
    return;
  m_selectionUpdatePending = true;
  QTimer::singleShot(0, this, &DrawSubWindow::flushSelectionChanged);
}

void DrawSubWindow::flushSelectionChanged() {
  m_selectionUpdatePending = false;
  if (!myScene || !m_selectionManager)
    return;

  QList<QGraphicsItem *> selected = myScene->selectedItems();

  // 선택된 글자가 있으면 툴바의 굵게/기울임/밑줄 버튼 표시를 그 글자들에 맞춘다.
  {
    bool any = false, allBold = true, allItalic = true, allUnderline = true;
    bool sameFamily = true, sameSize = true;
    QString family;
    qreal size = 0.0;
    const double drawScale = myScene->drawingScale() > 0.0 ? myScene->drawingScale() : 1.0;
    for (int i = 0; i < selected.size(); ++i) {
      if (TextItem *ti = dynamic_cast<TextItem *>(selected.at(i))) {
        const QMap<QString, QVariant> p = ti->getProperties();
        const QString f = p.value("FontFamily").toString();
        const qreal s = p.value("FontSize").toDouble() / drawScale;
        if (!any) { family = f; size = s; }
        else {
          if (f != family) sameFamily = false;
          if (!qFuzzyCompare(s + 1.0, size + 1.0)) sameSize = false;
        }
        any = true;
        allBold = allBold && p.value("FontBold").toBool();
        allItalic = allItalic && p.value("FontItalic").toBool();
        allUnderline = allUnderline && p.value("FontUnderline").toBool();
      }
    }
    if (any) {
      if (m_ctx)
        emit m_ctx->selectedTextStyleChanged(allBold, allItalic, allUnderline);
      if (m_ctx)
        emit m_ctx->selectedTextFontChanged(sameFamily ? family : QString(),
                                            sameSize ? size : 0.0);
    }
  }

  if (m_activeTool) {
    QList<CadItem *> topItems = m_activeTool->getTopLevelCadItems(selected);
    QList<QGraphicsItem *> filtered;

    // [수정] 인덱스 기반 루프 사용 (C++11 detach 경고 방지)
    for (int i = 0; i < topItems.size(); ++i) {
      filtered.append(topItems.at(i));
    }
    m_selectionManager->setSelectedItems(filtered);
  } else {
    m_selectionManager->setSelectedItems(selected);
  }
}

void DrawSubWindow::onLayerPropertyChanged(const QString &name) {
  if (!m_layerManager || !myScene || !m_ctx)
    return;

  Layer *layer = m_layerManager->getLayer(name);
  if (!layer) {
    myScene->removeLayerStyle(name);
    return;
  }

  QVector<qreal> pattern;
  if (m_ctx->lineTypeManager()) {
    pattern = m_ctx->lineTypeManager()->getQtDashPattern(layer->linetype());
  }

  myScene->updateLayerItems(name, layer->color(), layer->weight(),
                            layer->linetype(), pattern);
  myScene->setLayerVisible(name, layer->isLayerOn());
}

void DrawSubWindow::scheduleInitialViewFit() {
  // [핵심] addSubWindow() 직후에는 MDI 서브윈도우가 아직 최대화되기 전(임시
  // 크기)이라, 지금 바로 화면을 맞추면 잘못된 크기 기준으로 계산되어 최대화가
  // 끝난 뒤 원점(0,0)이 좌측 하단이 아닌 엉뚱한 위치로 밀리는 문제가 있었다.
  // 파일 열기(FileOpenCommand)의 zoomAll() 지연 호출과 동일하게, 뷰포트
  // 크기가 완전히 확정되는 시점(약 100ms 뒤)에 다시 맞춰준다.
  QTimer::singleShot(100, this, &DrawSubWindow::onInitialViewFitTimeout);
}

void DrawSubWindow::onInitialViewFitTimeout() {
  if (!myView)
    return;

  // A3 사이즈(420x297)를 기준으로 여백(-20) 포함하여 보여줌.
  // 이를 통해 원점(0,0)이 좌측 하단에 적절히 배치된다.
  myView->zoomToRect(QRectF(-20, -20, 440, 317));
}

// [신규 슬롯] 테마 매니저로부터 테마 변경 시그널을 받을 때 처리
void DrawSubWindow::onThemeChanged(ThemeManager::ThemeType type) {
  Q_UNUSED(type)
  if (m_ctx && m_ctx->themeManager() && myScene && myView) {
    ThemeManager *tm = m_ctx->themeManager();
    myScene->setBackgroundBrush(tm->canvasBackground());
    myView->updateTheme(tm);
    myScene->updateOverlayTheme(tm);
  }
}


void DrawSubWindow::closeEvent(QCloseEvent *event) {
    QUndoStack *stack = undoStack();

    if (stack && !stack->isClean()) {
        QString docName = windowTitle();
        docName.remove("[*]");

        QString msg = tr("도면 '%1'에 변경된 내용이 있습니다.\n창을 닫기 전에 저장하시겠습니까?").arg(docName);

        QMessageBox::StandardButton reply;
        reply = QMessageBox::warning(this, tr("도면 저장"), msg,
                                     QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (reply == QMessageBox::Yes) {
            // m_context 대신 m_ctx 사용
            if (m_ctx && m_ctx->fileManager()) {
                QString filePath = property("filePath").toString();
                bool saved = false;

                if (filePath.isEmpty()) {
                    filePath = QFileDialog::getSaveFileName(this, tr("Save Drawing"), docName, tr("CAD Database (*.cdb);;All Files (*)"));
                    if (!filePath.isEmpty()) {
                        saved = m_ctx->fileManager()->saveDrawing(filePath, scene());
                        if (saved) {
                            setProperty("filePath", filePath);
                            setWindowTitle(QFileInfo(filePath).fileName());
                            m_ctx->showStatusText(tr("Saved As: %1").arg(filePath));
                            m_ctx->addToRecentFiles(filePath);
                        }
                    }
                } else {
                    saved = m_ctx->fileManager()->saveDrawing(filePath, scene());
                    if (saved) {
                        m_ctx->showStatusText(tr("Saved: %1").arg(filePath));
                    }
                }

                if (!saved) {
                    event->ignore();
                    return;
                }

                stack->setClean();
            }
        } else if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
    }

    event->accept();
}