#include "texttool.h"
#include "cadutils.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include <QUndoStack>

#include <QDebug>
#include <QFont>
#include <QGraphicsTextItem>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QFontMetricsF>
#include <QKeyEvent>

TextTool::TextTool(QObject *parent)
    : AbstractTool(parent), m_activeTextItem(nullptr) {}

TextTool::~TextTool() { finishEditing(); }

void TextTool::activate(MyGraphicsScene *scene) {
  if (m_scene == scene)
    return;

  AbstractTool::activate(scene);
  m_activeTextItem = nullptr;

  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->logMessage("TEXT Command", CommandDock::Command);
    ctx->commandDock()->setPrompt(tr(" 글자 시작 점을 지정 하세요 : "));

    // [신규] 폰트 변경 시 실시간 반영을 위해 시그널 연결
    if (AppContext *appCtx = dynamic_cast<AppContext *>(ctx)) {
      connect(appCtx, &AppContext::currentFontChanged, this,
              &TextTool::onFontChanged);
    }
  }
}

void TextTool::deactivate() {
  finishEditing(); // 혹시 치고 있던 글자가 있다면 안전하게 확정

  // [핵심] 툴이 완전히 종료될 때 커맨드 라인을 기본 상태로 복구
  IAppContext *ctx = getContext();
  if (ctx && ctx->commandDock()) {
    ctx->commandDock()->setPrompt(" Command : ");
    ctx->commandDock()->logMessage("Text tool finished.", CommandDock::Info);

    // [신규] 시그널 연결 해제
    if (AppContext *appCtx = dynamic_cast<AppContext *>(ctx)) {
      disconnect(appCtx, &AppContext::currentFontChanged, this,
                 &TextTool::onFontChanged);
    }
  }

  AbstractTool::deactivate();
}

void TextTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (!m_scene)
    return;

  // 우클릭: 툴 종료
  if (button == Qt::RightButton) {
    if (m_activeTextItem) {
      finishEditing(); // 1단계: 글자 치던 것만 확정
    } else {
      // 2단계: 빈 공간 우클릭 시 툴 자체를 종료 (기본 선택 툴로 복귀)
      if (getContext())
        getContext()->finishCurrentTool();
    }
    return;
  }
  if (button != Qt::LeftButton)
    return;

  // 1. 이미 다른 곳에서 타이핑 중이었다면 -> 입력 확정 후 명령 종료
  if (m_activeTextItem) {
    // [예외 처리] 클릭한 곳이 현재 타이핑 중인 텍스트 영역 내부라면 무시
    // (마우스로 커서 이동 허용)
    if (m_scene->itemAt(pos, QTransform()) == m_activeTextItem.data()) {
      return;
    } else {
      // 다른 영역을 클릭했으므로 글자 고정 및 툴 종료 (SelectTool로 복원)
      finishEditing();
      if (getContext())
        getContext()->finishCurrentTool();
      return;
    }
  }

  IAppContext *ctx = getContext();
  if (!ctx)
    return;

  // 2. [핵심] 새로운 임시 텍스트 아이템 생성 (입력용)
  // 입력 중에는 QGraphicsTextItem의 강력한 편집 기능을 활용합니다.
  m_activeTextItem = new QGraphicsTextItem("");
  m_activeTextItem->setPos(pos);

  QFont textFont;
  textFont.setFamily(ctx->currentFontFamily());
  // 도면 축척(Scale) 적용
  double drawingScale = m_scene ? m_scene->drawingScale() : 1.0;
  textFont.setPointSizeF(ctx->currentFontSize() * drawingScale);
  textFont.setBold(ctx->currentFontBold());
  textFont.setItalic(ctx->currentFontItalic());
  textFont.setUnderline(ctx->currentFontUnderline());
  m_activeTextItem->setFont(textFont);

  // [수정] 전역 현재 레이어 대신 '텍스트 레이어'의 색상을 가져옵니다.
  QString textLayer = ctx->textLayerName();
  if (m_scene) {
    m_activeTextItem->setDefaultTextColor(
        m_scene->getLayerStyle(textLayer).color);
  }

  // 5. 위치 및 실시간 타이핑(TextEditorInteraction) 모드 켜기
  m_startPos = pos;
  m_activeTextItem->setPos(pos);
  m_activeTextItem->setTransform(
      QTransform::fromScale(1, -1)); // Y축 뒤집힘 방지
  m_activeTextItem->setTextInteractionFlags(Qt::TextEditorInteraction);

  m_scene->addItem(m_activeTextItem);

  // 포커스를 줘서 키보드 입력이 즉시 이 텍스트로 향하게 합니다.
  m_activeTextItem->setFocus();

  // 입력 내용 변화에 따라 위치를 보정 — 확정될 TextItem과 같은 위치 유지
  connect(m_activeTextItem->document(), &QTextDocument::contentsChanged, this,
          &TextTool::updatePreviewPos);
  updatePreviewPos();

  if (ctx->commandDock()) {
    ctx->commandDock()->setPrompt(
        tr(" 글자를 입력 하세요 (종료 : 입력란 외부를 클릭 하세요) : "));
  }
}

void TextTool::keyPress(QKeyEvent *event) {
  // Esc 키를 누르면 2단 종료 로직 실행
  if (event->key() == Qt::Key_Escape) {
    if (m_activeTextItem) {
      finishEditing(); // 1단계: 글자 치던 것만 확정
      if (getContext())
        getContext()->finishCurrentTool(); // 즉시 툴 종료
    } else {
      // 2단계: 빈 공간에서 Esc 누를 시 툴 자체를 종료
      if (getContext())
        getContext()->finishCurrentTool();
    }
    return;
  }

  // [핵심] 텍스트 입력 중일 때는 엔터키 포함 다른 키들이 텍스트 아이템으로
  // 전달되도록 합니다.
  if (m_activeTextItem) {
    // 엔터키(Return/Enter)인 경우 부모(AbstractTool)의 종료 로직을 타지 않게
    // 하고 MainWindow에서 가로채지 않았다면 QGraphicsTextItem이 줄바꿈으로
    // 처리하도록 통과시킵니다.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      return;
    }
    return;
  }

  AbstractTool::keyPress(event);
}

void TextTool::finishEditing() {
  if (m_activeTextItem) {
    // 1. 실시간 편집 모드를 끄고 일반 도면 요소로 만듭니다.
    m_activeTextItem->setTextInteractionFlags(Qt::NoTextInteraction);
    m_activeTextItem->clearFocus();

    if (m_activeTextItem->toPlainText().trimmed().isEmpty()) {
      if (m_scene) {
        m_scene->removeItem(m_activeTextItem);
      }
      delete m_activeTextItem;
    } else {
      // 2. 입력된 텍스트가 있으면 실제 도면 객체(TextItem)로 전환
      IAppContext *ctx = getContext();
      if (ctx && ctx->undoStack() && ctx->entityFactory()) {
        // 임시 아이템 제거
        m_scene->removeItem(m_activeTextItem);

        // 실제 TextItem 생성
        QGraphicsItem *newItem = ctx->entityFactory()->createText(
            CadUtils::processText(m_activeTextItem->toPlainText()), m_startPos,
            m_activeTextItem->font(), m_activeTextItem->defaultTextColor());

        // [신규] 텍스트 전용 레이어 적용
        if (CadItem *ci = dynamic_cast<CadItem *>(newItem)) {
          QString textLayer = ctx->textLayerName();
          if (!textLayer.isEmpty()) {
            ci->setLayerName(textLayer);
          }
        }

        // Undo 스택에 추가
        ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));

        delete m_activeTextItem;
      }
    }

    m_activeTextItem = nullptr;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
      ctx->commandDock()->setPrompt(tr(" 글자 시작점을 지정 하세요 : "));

        QLineEdit *cmdLine = getContext()->commandDock()->findChild<QLineEdit *>("CommandInputLine");
        if (cmdLine) {
            cmdLine->setFocus();
        }
    }
  }
}
void TextTool::onFontChanged(const QString &family, qreal size) {
  if (m_activeTextItem) {
    QFont font = m_activeTextItem->font();
    font.setFamily(family);
    // 도면 축척(Scale) 적용
    double drawingScale = m_scene ? m_scene->drawingScale() : 1.0;
    font.setPointSizeF(size * drawingScale);
    if (IAppContext *ctx = getContext()) {
      font.setBold(ctx->currentFontBold());
      font.setItalic(ctx->currentFontItalic());
      font.setUnderline(ctx->currentFontUnderline());
    }
    m_activeTextItem->setFont(font);

    // 크기 변경에 따른 위치 보정
    updatePreviewPos();
  }
}

// [수정] 미리보기를 확정 후 TextItem과 같은 자리에 배치.
// TextItem은 클릭점(m_startPos)을 첫 줄 기준선(baseline)으로 쓰고 다음 줄은 아래로 쌓는다.
// 예전에는 문서 전체의 왼쪽 하단을 클릭점에 맞춰, 확정 순간 descent만큼(여러 줄이면 더 크게) 튀었다.
void TextTool::updatePreviewPos() {
  if (!m_activeTextItem)
    return;

  QTextDocument *doc = m_activeTextItem->document();
  doc->size(); // 레이아웃 강제 수행

  // 문서 첫 줄의 실제 기준선 위치 (레이아웃에서 직접 읽음)
  double baseline = QFontMetricsF(m_activeTextItem->font()).ascent();
  QTextBlock block = doc->firstBlock();
  if (block.isValid() && block.layout() && block.layout()->lineCount() > 0) {
    QTextLine line = block.layout()->lineAt(0);
    baseline = block.layout()->position().y() + line.y() + line.ascent();
  }

  // Transform(1,-1): 로컬 y → 씬 -y. 로컬 baseline이 씬 m_startPos.y에 오도록.
  m_activeTextItem->setPos(m_startPos.x(), m_startPos.y() + baseline);
}
