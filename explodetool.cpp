#include "explodetool.h"
#include "blockitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "basedimensionitem.h"
#include "leaderitem.h"
#include "gdtitem.h"
#include "datumitem.h"         // [추가] 데이텀 폭파용 헤더 포함
#include "gripitem.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "mygraphicsscene.h"
#include "polylineitem.h"
#include <QDebug>
#include <QKeyEvent>
#include <QUndoStack>

ExplodeTool::ExplodeTool(QObject *parent)
    : AbstractTool(parent), m_isBoxSelecting(false) {}

ExplodeTool::~ExplodeTool() { clearOverlay(); }

void ExplodeTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_isBoxSelecting = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("EXPLODE", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 분해할 객체를 선택 하세요 : "), {});
    }
    hideGripsOfSelectedItems();
}

void ExplodeTool::deactivate() {
    clearOverlay();
    m_isBoxSelecting = false;
    AbstractTool::deactivate();
}

void ExplodeTool::mousePress(QPointF pos, Qt::MouseButton button) {
    handleSelectionPress(pos, button);
}

void ExplodeTool::mouseMove(QPointF pos) { handleSelectionMove(pos); }

void ExplodeTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void ExplodeTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        executeExplode();
    } else if (event->key() == Qt::Key_Escape) {
        if (m_scene) m_scene->clearSelection();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

void ExplodeTool::clearOverlay() {
    if (m_scene) m_scene->clearOverlay();
}

void ExplodeTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        executeExplode();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (!m_isBoxSelecting) {
        QGraphicsItem *item = itemAtPos(pos);
        if (item) {
            if (item->type() == GripItem::Type) {
                if (item->parentItem()) item = item->parentItem();
            }
            if (item && item->type() != GripItem::Type) {
                item->setSelected(!item->isSelected());
                hideGripsOfSelectedItems();
                return;
            }
        }
        m_isBoxSelecting = true;
        m_firstCorner = pos;
    } else {
        processBoxSelection(pos);
        m_isBoxSelecting = false;
        clearOverlay();
    }
}

void ExplodeTool::handleSelectionMove(QPointF pos) {
    if (m_isBoxSelecting && m_scene) {
        MyGraphicsScene::SelectionMode mode = (pos.x() > m_firstCorner.x())
        ? MyGraphicsScene::Mode_Window
        : MyGraphicsScene::Mode_Crossing;
        m_scene->setSelectionRect(m_firstCorner, pos, mode);
    }
}

void ExplodeTool::processBoxSelection(QPointF p2) {
    bool isWindow = (p2.x() > m_firstCorner.x());
    QList<CadItem *> topItems = isWindow
        ? collectItemsInWindowRect(m_firstCorner, p2)
        : collectItemsInCrossingRect(m_firstCorner, p2);

    for (int i = 0; i < topItems.size(); ++i) {
        topItems[i]->setSelected(true);
    }
    hideGripsOfSelectedItems();
}

void ExplodeTool::executeExplode() {
    if (!m_scene) {
        emit toolFinished();
        return;
    }

    QList<QGraphicsItem *> selected = m_scene->selectedItems();
    QList<CadItem *> topSelected = getTopLevelCadItems(selected);
    if (topSelected.isEmpty()) {
        emit toolFinished();
        return;
    }

    IAppContext *ctx = getContext();
    if (!ctx) return;

    QUndoStack *stack = ctx->undoStack();
    if (stack) {
        stack->beginMacro(tr("Explode %1 items").arg(topSelected.size()));
    }

    int explodedCount = 0;

    // [수정] clazy-range-loop-detach 방지를 위해 인덱스 기반 루프 사용
    for (int i = 0; i < topSelected.size(); ++i) {
        CadItem *item = topSelected[i];

        if (auto *block = dynamic_cast<BlockItem *>(item)) {
            QList<CadItem *> children = block->getItems();
            for (int j = 0; j < children.size(); ++j) {
                CadItem *child = children[j];
                if (!child) continue;
                CadItem *cloned = child->clone();
                if (stack) stack->push(new AddEntityCommand(m_scene, cloned));
                else m_scene->addItem(cloned);
            }
            if (stack) stack->push(new RemoveEntityCommand(m_scene, block));
            else { m_scene->removeItem(block); delete block; }
            explodedCount++;

        } else if (auto *pline = dynamic_cast<PolylineItem *>(item)) {
            MyPolyline data = pline->getPolyline();
            for (size_t k = 0; k < data.vertices.size(); ++k) {
                if (!data.isClosed && k == data.vertices.size() - 1) break;
                size_t nextIdx = (k + 1) % data.vertices.size();
                LineItem *newLine = new LineItem(data.vertices[k], data.vertices[nextIdx]);
                newLine->setLayerName(pline->layerName());
                newLine->setPen(pline->pen());

                if (stack) stack->push(new AddEntityCommand(m_scene, newLine));
                else m_scene->addItem(newLine);
            }
            if (stack) stack->push(new RemoveEntityCommand(m_scene, pline));
            else { m_scene->removeItem(pline); delete pline; }
            explodedCount++;

        } else if (auto *baseDim = dynamic_cast<BaseDimensionItem *>(item)) {
            QList<CadItem *> parts = baseDim->explode();
            for (int j = 0; j < parts.size(); ++j) {
                CadItem *part = parts[j];
                if (!part) continue;
                if (stack) stack->push(new AddEntityCommand(m_scene, part));
                else m_scene->addItem(part);
            }
            if (stack) stack->push(new RemoveEntityCommand(m_scene, baseDim));
            else { m_scene->removeItem(baseDim); delete baseDim; }
            explodedCount++;

        } else if (auto *leader = dynamic_cast<LeaderItem *>(item)) {
            QList<CadItem *> parts = leader->explode();
            for (int j = 0; j < parts.size(); ++j) {
                CadItem *part = parts[j];
                if (!part) continue;
                if (stack) stack->push(new AddEntityCommand(m_scene, part));
                else m_scene->addItem(part);
            }
            if (stack) stack->push(new RemoveEntityCommand(m_scene, leader));
            else { m_scene->removeItem(leader); delete leader; }
            explodedCount++;

        } else if (auto *gdt = dynamic_cast<GdtItem *>(item)) {
            QList<CadItem *> parts = gdt->explode();
            for (int j = 0; j < parts.size(); ++j) {
                CadItem *part = parts[j];
                if (!part) continue;
                if (stack) stack->push(new AddEntityCommand(m_scene, part));
                else m_scene->addItem(part);
            }
            if (stack) stack->push(new RemoveEntityCommand(m_scene, gdt));
            else { m_scene->removeItem(gdt); delete gdt; }
            explodedCount++;

            // -------------------------------------------------------------
            // [신규 추가] DatumItem 처리 분기
            // -------------------------------------------------------------
        } else if (auto *datum = dynamic_cast<DatumItem *>(item)) {
            QList<CadItem *> parts = datum->explode();
            for (int j = 0; j < parts.size(); ++j) {
                CadItem *part = parts[j];
                if (!part) continue;
                if (stack) stack->push(new AddEntityCommand(m_scene, part));
                else m_scene->addItem(part);
            }
            if (stack) stack->push(new RemoveEntityCommand(m_scene, datum));
            else { m_scene->removeItem(datum); delete datum; }
            explodedCount++;
        }
        // -------------------------------------------------------------
    }

    if (stack) stack->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("%1 items exploded.").arg(explodedCount), CommandDock::Info);
        ctx->commandDock()->setPrompt(" Command : ");
    }

    m_scene->clearSelection();
    emit toolFinished();
}