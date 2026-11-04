#include "textedittool.h"
#include "boxtextutil.h"
#include "polylineitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "textitem.h"

#include <QDebug>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QUndoStack>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextOption>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLineEdit> // [추가] 커맨드 라인 포커스 제어

TextEditTool::TextEditTool(TextItem *target, QObject *parent)
    : AbstractTool(parent), m_targetItem(target), m_editorProxy(nullptr),
    m_originalParent(nullptr), m_boxTolerance(0.0) {
    if (m_targetItem) {
        m_originalPos = m_targetItem->pos();
        m_originalText = m_targetItem->getProperties().value("Content").toString();
        m_originalBoxCorners = BoxTextUtil::boxCorners(m_targetItem);
        m_boxTolerance = BoxTextUtil::matchTolerance(m_targetItem);
    }
}

TextEditTool::~TextEditTool() {}

void TextEditTool::activate(MyGraphicsScene *scene) {
    if (m_editorProxy)
        return;
    AbstractTool::activate(scene);

    if (!m_targetItem || !m_scene) {
        emit toolFinished();
        return;
    }

    m_scene->clearSelection();

    m_originalParent = m_targetItem->parentItem();
    if (m_targetItem->scene()) {
        m_scene->removeItem(m_targetItem);
    }
    m_targetItem->setParentItem(nullptr);

    m_editorProxy = new QGraphicsTextItem();
    m_editorProxy->setPlainText(m_originalText);
    m_scene->addItem(m_editorProxy);

    auto props = m_targetItem->getProperties();
    QFont font(props.value("FontFamily").toString());
    font.setPointSizeF(props.value("FontSize").toDouble());
    m_editorProxy->setFont(font);
    // TextItem::paint()와 같은 색(아이템 펜 색) 사용
    m_editorProxy->setDefaultTextColor(m_targetItem->pen().color());

    m_editorProxy->document()->setDocumentMargin(0);
    m_editorProxy->setTransform(m_targetItem->transform());

    placeEditor();

    m_editorProxy->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_editorProxy->setFocus();

    QTextCursor cursor = m_editorProxy->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_editorProxy->setTextCursor(cursor);

    // 포인터 함수를 사용한 안전한 연결
    connect(m_editorProxy->document(), &QTextDocument::contentsChanged, this,
            &TextEditTool::updatePos);

    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(
            tr(" 편집 중 (종료: 외부클릭, Esc: 취소) : "));
    }

    m_scene->invalidate(m_scene->sceneRect());
    m_scene->update();
}

void TextEditTool::deactivate() {
    if (m_editorProxy) {
        if (m_scene)
            m_scene->removeItem(m_editorProxy);
        delete m_editorProxy;
        m_editorProxy = nullptr;
    }

    if (m_targetItem && m_scene) {
        m_targetItem->setParentItem(m_originalParent);
        if (!m_originalParent) {
            m_scene->addItem(m_targetItem);
        }
        m_targetItem->setVisible(true);
        m_targetItem->update();
    }

    if (m_scene) {
        m_scene->invalidate(m_scene->sceneRect());
        m_scene->update();
    }

    AbstractTool::deactivate();
}

void TextEditTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    Q_UNUSED(button)
    if (m_editorProxy && !m_editorProxy->sceneBoundingRect().contains(pos)) {
        finishEditing(true);
    }
}

void TextEditTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishEditing(true);
        return;
    } else if (event->key() == Qt::Key_Escape) {
        finishEditing(false);
        return;
    }
    AbstractTool::keyPress(event);
}

void TextEditTool::finishEditing(bool accepted) {
    // 1. 에디터 프록시의 포커스 해제 및 뷰의 포커스 초기화
    if (m_editorProxy) {
        m_editorProxy->clearFocus();
    }

    if (m_scene) {
        m_scene->clearFocus();
        QList<QGraphicsView*> views = m_scene->views();
        for (int i = 0; i < views.size(); ++i) {
            if (views[i]) views[i]->clearFocus();
        }
    }

    // 2. IME 강제 확정 및 초기화
    if (qApp && qApp->inputMethod()) {
        qApp->inputMethod()->commit();
        qApp->inputMethod()->reset();
    }

    // 3. 값 변경 사항 저장
    if (accepted && m_targetItem && m_editorProxy) {
        QString newText = m_editorProxy->toPlainText().trimmed();
        if (newText != m_originalText) {
            IAppContext *ctx = getContext();
            QUndoStack *stack = ctx ? ctx->undoStack() : nullptr;

            // BOXTEXT로 만든 박스가 있으면 (편집 전 글자 기준 박스와 일치하는 폴리선)
            // 편집된 글자에 맞춰 크기를 같이 조정한다.
            // Setup에서 "박스크기 연동"을 끈 경우 박스는 그대로 둔다.
            PolylineItem *box = nullptr;
            if (BoxTextUtil::isLinkBoxSizeEnabled(m_scene))
                box = BoxTextUtil::findBox(m_scene, m_originalBoxCorners, m_boxTolerance);

            if (stack) {
                stack->beginMacro(tr("글자 편집"));
                stack->push(new ChangePropertyCommand(
                    m_targetItem, "Content", m_originalText, newText));
            } else {
                m_targetItem->setProperty("Content", newText);
            }

            if (box) {
                BoxTextUtil::resizeBox(stack, box, m_originalBoxCorners,
                                       BoxTextUtil::boxCorners(m_targetItem),
                                       m_boxTolerance);
            }

            if (stack) stack->endMacro();
        }
    }

    // [핵심] 4. 커맨드 창으로 포커스 복귀 (한글 꼬임 원천 차단)
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
        QLineEdit *cmdLine = getContext()->commandDock()->findChild<QLineEdit *>("CommandInputLine");
        if (cmdLine) {
            cmdLine->setFocus();
        }
    }

    emit toolFinished();
}

// [수정] 편집기를 TextItem::paint()의 글자 위치에 정확히 겹치게 배치.
// TextItem은 pos()가 첫 줄 기준선(baseline) 원점이고 alignH에 따라 줄마다 x를 당긴다.
// 예전에는 pos.y + 문서높이로 놓아 descent만큼 어긋났고, 정렬·회전도 반영되지 않았다.
void TextEditTool::placeEditor() {
    if (!m_editorProxy || !m_targetItem)
        return;

    const MyText *data = static_cast<const MyText *>(m_targetItem->getEntityData());
    int alignH = data ? data->alignH : 0;

    QTextDocument *doc = m_editorProxy->document();
    QTextOption opt = doc->defaultTextOption();
    opt.setAlignment(alignH == 1 ? Qt::AlignHCenter
                                 : (alignH == 2 ? Qt::AlignRight : Qt::AlignLeft));
    doc->setDefaultTextOption(opt);
    doc->setTextWidth(-1);
    double width = doc->idealWidth();
    if (alignH != 0)
        doc->setTextWidth(width); // 가운데/오른쪽 정렬 기준 폭
    doc->size(); // 레이아웃 강제 수행

    // 문서 첫 줄의 실제 기준선 위치 (레이아웃에서 직접 읽음)
    double baseline = QFontMetricsF(m_editorProxy->font()).ascent();
    QTextBlock block = doc->firstBlock();
    if (block.isValid() && block.layout() && block.layout()->lineCount() > 0) {
        QTextLine line = block.layout()->lineAt(0);
        baseline = block.layout()->position().y() + line.y() + line.ascent();
    }

    double xOff = 0.0;
    if (alignH == 1) xOff = -width / 2.0;
    else if (alignH == 2) xOff = -width;

    // 편집기 로컬 (0,0)을 TextItem 로컬 (xOff, -baseline)에 맞춘다.
    // 두 아이템이 같은 transform(반전+회전)을 쓰므로 오프셋만 그 transform으로 변환.
    QPointF offset = m_targetItem->transform().map(QPointF(xOff, -baseline));
    m_editorProxy->setPos(m_originalPos + offset);
}

void TextEditTool::updatePos() {
    if (m_editorProxy && m_scene) {
        placeEditor();
        m_editorProxy->update();
        m_scene->invalidate(m_scene->sceneRect());
        m_scene->update();

        // C++11 range-loop 분리 경고 방지를 위해 인덱스 기반 루프 사용
        QList<QGraphicsView*> views = m_scene->views();
        for (int i = 0; i < views.size(); ++i) {
            if (views[i] && views[i]->viewport()) {
                views[i]->viewport()->repaint();
            }
        }
    }
}