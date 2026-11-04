#include "leadertextedittool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "holecalloutlink.h"
#include "iappcontext.h"
#include "leaderitem.h"
#include "mygraphicsscene.h"

#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextDocument>
#include <QTextOption>
#include <QFontMetricsF>
#include <QTransform>

LeaderTextEditTool::LeaderTextEditTool(LeaderItem *target, QObject *parent)
    : AbstractTool(parent), m_targetItem(target), m_editorProxy(nullptr) {
    if (m_targetItem) {
        m_originalText = m_targetItem->getProperties().value("Content").toString();
    }
}

LeaderTextEditTool::~LeaderTextEditTool() {}

void LeaderTextEditTool::activate(MyGraphicsScene *scene) {
    if (m_editorProxy) return;
    AbstractTool::activate(scene);

    if (!m_targetItem || !m_scene || m_targetItem->leaderData().points.isEmpty()) {
        emit toolFinished();
        return;
    }

    m_scene->clearSelection();

    // [수정] 아이템을 씬에서 빼면 지시선까지 사라지므로, 텍스트만 숨기고 지시선은 유지
    m_targetItem->setTextHidden(true);

    m_editorProxy = new QGraphicsTextItem();
    m_editorProxy->setPlainText(m_originalText);
    m_scene->addItem(m_editorProxy);

    DimensionStyle style = m_scene->dimensionStyle();
    double scale = m_scene->drawingScale();
    QFont font;
    font.setFamily(style.fontFamily);
    font.setPointSizeF(style.textSize * scale);
    m_editorProxy->setFont(font);
    // LeaderItem::paint()와 같은 색 규칙 (dimTextLayer 우선)
    QColor textColor = m_targetItem->pen().color();
    if (!style.dimTextLayer.isEmpty())
        textColor = m_scene->getLayerStyle(style.dimTextLayer).color;
    m_editorProxy->setDefaultTextColor(textColor);

    m_editorProxy->document()->setDocumentMargin(0);
    // LeaderItem::paint()와 동일하게 로컬 (1,-1) 반전 적용 — 뷰의 전역 Y반전을 상쇄해 텍스트가 바로 보이게 함
    m_editorProxy->setTransform(QTransform::fromScale(1, -1));

    placeEditor();

    m_editorProxy->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_editorProxy->setFocus();

    QTextCursor cursor = m_editorProxy->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_editorProxy->setTextCursor(cursor);

    connect(m_editorProxy->document(), &QTextDocument::contentsChanged, this,
            &LeaderTextEditTool::updatePos);

    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(
            tr(" 편집 중 (종료: 외부클릭, Esc: 취소, Enter: 줄바꿈) : "));
    }

    m_scene->invalidate(m_scene->sceneRect());
    m_scene->update();
}

void LeaderTextEditTool::deactivate() {
    if (m_editorProxy) {
        if (m_scene) m_scene->removeItem(m_editorProxy);
        delete m_editorProxy;
        m_editorProxy = nullptr;
    }

    if (m_targetItem) {
        m_targetItem->setTextHidden(false);
    }

    if (m_scene) {
        m_scene->invalidate(m_scene->sceneRect());
        m_scene->update();
    }

    AbstractTool::deactivate();
}

void LeaderTextEditTool::mousePress(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(button)
    if (m_editorProxy && !m_editorProxy->sceneBoundingRect().contains(pos)) {
        finishEditing(true);
    }
}

// Enter는 줄바꿈으로 그대로 두고(콜아웃은 여러 줄일 수 있음), Esc만 취소로 가로챈다.
// 종료는 외부 클릭(mousePress)으로 처리.
void LeaderTextEditTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        finishEditing(false);
        return;
    }
    AbstractTool::keyPress(event);
}

void LeaderTextEditTool::finishEditing(bool accepted) {
    if (m_editorProxy) {
        m_editorProxy->clearFocus();
    }

    if (m_scene) {
        m_scene->clearFocus();
        QList<QGraphicsView *> views = m_scene->views();
        for (int i = 0; i < views.size(); ++i) {
            if (views[i]) views[i]->clearFocus();
        }
    }

    if (qApp && qApp->inputMethod()) {
        qApp->inputMethod()->commit();
        qApp->inputMethod()->reset();
    }

    if (accepted && m_targetItem && m_editorProxy) {
        QString newText = m_editorProxy->toPlainText().trimmed();
        IAppContext *ctx = getContext();
        QWidget *dialogParent = (m_scene && !m_scene->views().isEmpty()) ? m_scene->views().constFirst() : nullptr;
        // 구멍 콜아웃이면 텍스트를 구멍 사양으로 해석해 구멍 데이터에 반영 (일반 지시선이면 false)
        if (newText != m_originalText &&
            !HoleCalloutLink::applyTextEdit(ctx, m_targetItem, newText, dialogParent)) {
            if (ctx && ctx->undoStack()) {
                ctx->undoStack()->push(new ChangePropertyCommand(
                    m_targetItem, "Content", m_originalText, newText));
            } else {
                m_targetItem->setProperty("Content", newText);
            }
        }
    }

    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
        QLineEdit *cmdLine = getContext()->commandDock()->findChild<QLineEdit *>("CommandInputLine");
        if (cmdLine) cmdLine->setFocus();
    }

    emit toolFinished();
}

// [수정] 편집기를 LeaderItem::paint()의 텍스트 위치에 정확히 겹치게 배치.
// paint(): 꺾임점 기준 (1,-1) 반전 후 첫 기준선이 착지선 위 textGap, 왼쪽 방향이면 오른쪽 정렬.
// 편집기 문서의 마지막 줄 기준선을 그 위치에 맞추고, 줄이 늘면 위로 자라게 한다.
void LeaderTextEditTool::placeEditor() {
    if (!m_editorProxy || !m_targetItem)
        return;
    const QList<MyVector> &pts = m_targetItem->leaderData().points;
    if (pts.size() < 2)
        return;

    MyVector elbow = pts.last();
    bool toRight = pts.last().x >= pts[pts.size() - 2].x;

    QTextDocument *doc = m_editorProxy->document();
    doc->setTextWidth(-1);
    double width = doc->idealWidth();
    QTextOption opt = doc->defaultTextOption();
    opt.setAlignment(toRight ? Qt::AlignLeft : Qt::AlignRight);
    doc->setDefaultTextOption(opt);
    if (!toRight)
        doc->setTextWidth(width); // 오른쪽 정렬 기준 폭

    const double textGap = 1.0; // LeaderItem::paint()와 동일
    QFontMetricsF fm(m_editorProxy->font());
    double lastBaseline = doc->size().height() - fm.descent();

    double x = toRight ? elbow.x : elbow.x - width;
    // 로컬 y → 씬 -y 이므로 문서 기준선 lastBaseline이 씬 y = elbow.y + textGap이 되도록
    double y = elbow.y + textGap + lastBaseline;
    m_editorProxy->setPos(QPointF(x, y));
}

void LeaderTextEditTool::updatePos() {
    if (m_editorProxy && m_scene) {
        placeEditor();
        m_editorProxy->update();
        m_scene->invalidate(m_scene->sceneRect());
        m_scene->update();

        QList<QGraphicsView *> views = m_scene->views();
        for (int i = 0; i < views.size(); ++i) {
            if (views[i] && views[i]->viewport()) {
                views[i]->viewport()->repaint();
            }
        }
    }
}
