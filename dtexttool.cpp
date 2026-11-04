#include "dtexttool.h"
#include "cadutils.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "dtextproxyitem.h"
#include <QUndoStack>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLineEdit> // [추가] QLineEdit 탐색용

DTextTool::DTextTool(QObject *parent)
    : AbstractTool(parent), m_activeTextItem(nullptr) {}

DTextTool::~DTextTool() { finishEditing(); }

void DTextTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;

    AbstractTool::activate(scene);
    m_activeTextItem = nullptr;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(commandLogName(), CommandDock::Command);
        showStartPrompt();

        if (AppContext *appCtx = dynamic_cast<AppContext *>(ctx)) {
            connect(appCtx, &AppContext::currentFontChanged, this,
                    &DTextTool::onFontChanged);
        }
    }
}

void DTextTool::deactivate() {
    finishEditing();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
        ctx->commandDock()->logMessage("DText tool finished.", CommandDock::Info);

        if (AppContext *appCtx = dynamic_cast<AppContext *>(ctx)) {
            disconnect(appCtx, &AppContext::currentFontChanged, this,
                       &DTextTool::onFontChanged);
        }
    }
    AbstractTool::deactivate();
}

void DTextTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        if (m_activeTextItem) {
            finishEditing();
        }
        if (getContext()) getContext()->finishCurrentTool();
        return;
    }

    if (button != Qt::LeftButton) return;

    if (m_activeTextItem) {
        finishEditing();
    }

    IAppContext *ctx = getContext();
    if (!ctx) return;

    m_activeTextItem = new DTextProxyItem();
    m_startPos = pos;

    QFont textFont;
    textFont.setFamily(ctx->currentFontFamily());
    // 도면 축척(Scale) 적용
    double drawingScale = m_scene ? m_scene->drawingScale() : 1.0;
    textFont.setPointSizeF(ctx->currentFontSize() * drawingScale);
    textFont.setBold(ctx->currentFontBold());
    textFont.setItalic(ctx->currentFontItalic());
    textFont.setUnderline(ctx->currentFontUnderline());
    m_activeTextItem->setFont(textFont);

    QString textLayer = targetTextLayer();
    m_activeTextItem->setTextColor(m_scene->getLayerStyle(textLayer).color);

    m_activeTextItem->setPos(pos);
    m_activeTextItem->setTransform(QTransform::fromScale(1, -1));

    m_scene->addItem(m_activeTextItem);

    // 다시 캔버스를 클릭하면 캔버스로 포커스를 회수합니다.
    QList<QGraphicsView*> views = m_scene->views();
    for (int i = 0; i < views.size(); ++i) {
        if (views[i]) {
            views[i]->setFocus();
            break;
        }
    }
    m_activeTextItem->setFocus();

    if (ctx->commandDock()) {
        ctx->commandDock()->setPrompt(tr(" 글자를 입력 하세요 (종료 :엔터)  : "));
    }
}

void DTextTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_activeTextItem) {
            finishEditing();
        }
        if (getContext()) getContext()->finishCurrentTool();
        return;
    }

    if (m_activeTextItem) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finishEditing();
            if (getContext()) getContext()->finishCurrentTool();
            return;
        }
        else if (event->key() == Qt::Key_Backspace) {
            m_activeTextItem->backspace();
            return;
        }
        else if (event->key() == Qt::Key_Delete) {
            m_activeTextItem->deleteChar();
            return;
        }
        else if (event->key() == Qt::Key_Left) {
            m_activeTextItem->moveCursorLeft();
            return;
        }
        else if (event->key() == Qt::Key_Right) {
            m_activeTextItem->moveCursorRight();
            return;
        }
        else if (event->key() == Qt::Key_Home) {
            m_activeTextItem->moveCursorHome();
            return;
        }
        else if (event->key() == Qt::Key_End) {
            m_activeTextItem->moveCursorEnd();
            return;
        }
        else if (!event->text().isEmpty()) {
            m_activeTextItem->insertText(event->text());
            return;
        }
    }

    AbstractTool::keyPress(event);
}

void DTextTool::finishEditing() {
    if (m_activeTextItem) {
        QString finalStr = m_activeTextItem->text().trimmed();

        // 텍스트 아이템과 뷰의 포커스를 해제합니다.
        m_activeTextItem->clearFocus();
        if (m_scene) {
            m_scene->clearFocus();
            QList<QGraphicsView*> views = m_scene->views();
            for (int i = 0; i < views.size(); ++i) {
                if (views[i]) views[i]->clearFocus();
            }
        }

        if (QGuiApplication::inputMethod()) {
            QGuiApplication::inputMethod()->commit();
            QGuiApplication::inputMethod()->reset();
        }

        if (finalStr.isEmpty()) {
            if (m_scene) m_scene->removeItem(m_activeTextItem);
            delete m_activeTextItem;
        } else {
            IAppContext *ctx = getContext();
            if (ctx && ctx->undoStack() && ctx->entityFactory()) {
                if (m_scene) m_scene->removeItem(m_activeTextItem);

                QColor textColor = m_scene->getLayerStyle(targetTextLayer()).color;

                QFont finalFont;
                finalFont.setFamily(ctx->currentFontFamily());
                // 도면 축척(Scale) 적용
                double drawingScale = m_scene ? m_scene->drawingScale() : 1.0;
                finalFont.setPointSizeF(ctx->currentFontSize() * drawingScale);
                finalFont.setBold(ctx->currentFontBold());
                finalFont.setItalic(ctx->currentFontItalic());
                finalFont.setUnderline(ctx->currentFontUnderline());

                QGraphicsItem *newItem = ctx->entityFactory()->createText(
                    CadUtils::processText(finalStr), m_startPos, finalFont, textColor);

                if (CadItem *ci = dynamic_cast<CadItem *>(newItem)) {
                    QString textLayer = targetTextLayer();
                    if (!textLayer.isEmpty()) {
                        ci->setLayerName(textLayer);

                        QGraphicsItem *layerFolder = m_scene->getLayerRoot(textLayer);
                        newItem->setParentItem(layerFolder);

                        if (newItem->scene() == m_scene) {
                            m_scene->removeItem(newItem);
                        }
                    }
                }

                // 글자 생성과 후처리(박스 등)를 한 번의 Undo로 되돌리도록 매크로로 묶음
                ctx->undoStack()->beginMacro(tr("글자쓰기"));
                ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));
                onTextCommitted(newItem);
                ctx->undoStack()->endMacro();
                delete m_activeTextItem;
            } else {
                if (m_scene) m_scene->removeItem(m_activeTextItem);
                delete m_activeTextItem;
            }
        }

        m_activeTextItem = nullptr;

        if (getContext() && getContext()->commandDock()) {
            showStartPrompt();

            // [핵심 버그 수정 포인트]
            // 텍스트 아이템이 사라진 직후, 한/영 전환 및 키보드 입력이
            // CommandDock의 QLineEdit에 온전히 전달되도록 포커스를 강제 이동시킵니다.
            // inputLine 객체는 private이므로 findChild를 통해 객체 이름으로 찾아옵니다.
            QLineEdit *cmdLine = getContext()->commandDock()->findChild<QLineEdit *>("CommandInputLine");
            if (cmdLine) {
                cmdLine->setFocus();
            }
        }
    }
}

void DTextTool::showStartPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setPrompt(tr(" 글자 시작점을 지정하세요 : "));
    }
}

QString DTextTool::targetTextLayer() const {
    IAppContext *ctx = getContext();
    return ctx ? ctx->textLayerName() : QString();
}

void DTextTool::onFontChanged(const QString &family, qreal size) {
    if (m_activeTextItem) {
        QFont font;
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
    }
}