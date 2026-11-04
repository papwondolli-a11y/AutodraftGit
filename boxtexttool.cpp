#include "boxtexttool.h"
#include "boxtextsetupdialog.h"
#include "boxtextutil.h"
#include "layermanager.h"
#include "tooldefaults.h"
#include "textitem.h"
#include "entityfactory.h"
#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include <QUndoStack>
#include <QKeyEvent>
#include <QPen>
#include <QColor>
#include <QList>
#include <QPair>
#include <QWidget>

BoxTextTool::BoxTextTool(QObject *parent)
    : DTextTool(parent)
{}

void BoxTextTool::activate(MyGraphicsScene *scene) {
    m_pickMode = false;
    loadSettings(scene);
    DTextTool::activate(scene);
}

void BoxTextTool::showStartPrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;

    if (m_pickMode) {
        ctx->commandDock()->showInteractivePrompt(tr(" 박스를 만들 문자 선택: "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(tr(" 글자 시작점을 지정하세요 "), {"Pick", "Setup"});
    }
}

bool BoxTextTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    // 글자 입력 중이거나 이미 Pick 모드면 옵션을 받지 않음
    if (m_pickMode || isEditing()) return false;

    if (cmd == "p" || cmd == "pick") {
        enterPickMode();
        return true;
    }
    if (cmd == "s" || cmd == "setup") {
        openSetupDialog();
        return true;
    }
    return false;
}

void BoxTextTool::loadSettings(MyGraphicsScene *scene) {
    ToolDefaults d(BoxTextUtil::settingsNamespace(), scene);
    m_boxLayer    = d.getString("BoxLayer", QString());
    m_textLayer   = d.getString("TextLayer", QString());
    m_applyTextLayerToPicked = d.getBool("ApplyTextLayerToPicked", true);
    m_linkBoxSize = d.getBool("LinkBoxSize", true);
}

void BoxTextTool::saveSettings(MyGraphicsScene *scene) {
    ToolDefaults d(BoxTextUtil::settingsNamespace(), scene);
    d.put("BoxLayer", m_boxLayer);
    d.put("TextLayer", m_textLayer);
    d.put("ApplyTextLayerToPicked", m_applyTextLayerToPicked);
    d.put("LinkBoxSize", m_linkBoxSize);
}

QString BoxTextTool::resolveLayer(const QString &name, const QString &fallback) const {
    IAppContext *ctx = getContext();
    if (!name.isEmpty() && ctx && ctx->layerManager() && ctx->layerManager()->getLayer(name))
        return name;
    return fallback;
}

QString BoxTextTool::targetTextLayer() const {
    return resolveLayer(m_textLayer, DTextTool::targetTextLayer());
}

void BoxTextTool::openSetupDialog() {
    IAppContext *ctx = getContext();
    if (!ctx) return;

    QList<QPair<QString, QColor>> layers;
    if (ctx->layerManager()) {
        const QList<Layer> all = ctx->layerManager()->getAllLayers();
        for (int i = 0; i < all.size(); ++i)
            layers.append(qMakePair(all.at(i).name(), all.at(i).color()));
    }

    // 미지정이면 글자 레이어 = 앱 글자 레이어, 박스 레이어 = 글자 레이어를 초기 선택
    QString textLayer = targetTextLayer();
    QString boxLayer  = resolveLayer(m_boxLayer, textLayer);

    QWidget *parentWin =
        ctx->commandDock() ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

    BoxTextSetupDialog dlg(layers, boxLayer, textLayer, m_applyTextLayerToPicked,
                           m_linkBoxSize, parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        m_boxLayer    = dlg.boxLayer();
        m_textLayer   = dlg.textLayer();
        m_applyTextLayerToPicked = dlg.applyTextLayerToPicked();
        m_linkBoxSize = dlg.linkBoxSize();
        saveSettings(m_scene);
    }
    showStartPrompt();
}

void BoxTextTool::enterPickMode() {
    m_pickMode = true;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr(" 문자 선택 모드"), CommandDock::Command);
    showStartPrompt();
}

void BoxTextTool::finishTool() {
    if (m_scene) m_scene->clearOverlay();
    if (getContext()) getContext()->finishCurrentTool();
}

void BoxTextTool::keyPress(QKeyEvent *event) {
    if (!m_pickMode) {
        DTextTool::keyPress(event);
        return;
    }

    if (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        finishTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void BoxTextTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_pickMode) {
        DTextTool::mousePress(pos, button);
        return;
    }

    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        finishTool();
        return;
    }

    if (button != Qt::LeftButton) return;

    TextItem *selectedText = dynamic_cast<TextItem *>(itemAtPos(pos));
    if (!selectedText) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("문자 객체를 선택해야 합니다."), CommandDock::Error);
        return;
    }

    // 기존 글자 레이어 변경 + 박스 생성을 한 번의 Undo로 묶음
    // (레이어를 먼저 바꿔야 박스 레이어 미지정 시 박스도 새 글자 레이어를 따름)
    QUndoStack *stack = ctx ? ctx->undoStack() : nullptr;
    if (stack) stack->beginMacro(tr("박스글자"));
    if (m_applyTextLayerToPicked)
        applyTextLayer(selectedText);
    createBoxForText(selectedText);
    if (stack) stack->endMacro();

    // 연속으로 다른 문자를 선택해 박스를 계속 만들 수 있도록 상태 유지
    showStartPrompt();
}

void BoxTextTool::applyTextLayer(TextItem *textItem) {
    IAppContext *ctx = getContext();
    if (!textItem || !m_scene) return;

    QString newLayer = targetTextLayer();
    QString oldLayer = textItem->layerName();
    if (newLayer.isEmpty() || newLayer == oldLayer) return;

    QPen oldPen = textItem->pen();
    QPen newPen = getLayerPen(newLayer);
    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new ChangeLayerCommand(m_scene, textItem, oldLayer,
                                                      newLayer, oldPen, newPen));
    } else {
        textItem->setLayerName(newLayer);
        textItem->setPen(newPen);
        m_scene->reparentItemToLayer(textItem, newLayer);
    }
}

void BoxTextTool::onTextCommitted(QGraphicsItem *textItem) {
    // DText로 방금 생성된 글자 객체에 맞춰 박스 생성 (글자와 같은 Undo 매크로)
    if (TextItem *ti = dynamic_cast<TextItem *>(textItem))
        createBoxForText(ti);
}

void BoxTextTool::createBoxForText(TextItem *textItem) {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory() || !m_scene) return;

    std::vector<MyVector> corners = BoxTextUtil::boxCorners(textItem);
    if (corners.size() != 4) return;

    // Setup의 박스 레이어 우선, 미지정이면 글자와 같은 레이어
    QString srcLayer = resolveLayer(m_boxLayer, textItem->layerName());
    QPen boxPen = getLayerPen(srcLayer);

    QGraphicsItem *newItem = ctx->entityFactory()->createPolyline(corners, true, boxPen);
    if (!newItem) return;

    if (CadItem *ci = dynamic_cast<CadItem *>(newItem)) {
        ci->setLayerName(srcLayer);
        ci->setPen(boxPen);
        QGraphicsItem *layerFolder = m_scene->getLayerRoot(srcLayer);
        ci->setParentItem(layerFolder);

        if (ci->scene() == m_scene) {
            m_scene->removeItem(ci);
        }
    }

    if (ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));
    } else {
        m_scene->addItem(newItem);
    }
}
