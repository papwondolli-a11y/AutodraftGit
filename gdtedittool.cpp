#include "gdtedittool.h"
#include "gdtitem.h"
#include "datumitem.h"
#include "gdtdialog.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"

#include <QUndoStack>
#include <QKeyEvent>

GdtEditTool::GdtEditTool(QObject *parent)
    : AbstractTool(parent)
{
}

void GdtEditTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    if (m_scene) {
        m_scene->clearSelection();
    }

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("GDTEDIT Command", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 편집할 기하공차 프레임 또는 데이텀을 선택하세요: "), {});
    }
}

void GdtEditTool::cancelTool() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    emit toolFinished();
}

void GdtEditTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void GdtEditTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    // 우클릭 시 도구 종료
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }

    if (button != Qt::LeftButton) return;

    // 클릭한 위치에 있는 아이템 탐색 (인덱스 기반 루프 적용)
    QList<QGraphicsItem*> itemsAtPos = m_scene->items(pos);
    for (int i = 0; i < itemsAtPos.size(); ++i) {
        if (GdtItem *gdt = dynamic_cast<GdtItem*>(itemsAtPos[i])) {
            editGdtItem(gdt);
            return;
        }
        else if (DatumItem *datum = dynamic_cast<DatumItem*>(itemsAtPos[i])) {
            editDatumItem(datum);
            return;
        }
    }

    // 빈 공간을 클릭한 경우
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("기하공차 또는 데이텀 객체를 찾을 수 없습니다."), CommandDock::Error);
    }
}

void GdtEditTool::editGdtItem(GdtItem *item) {
    if (!item) return;

    GdtDialog dlg;
    dlg.setData(item->gdtData());

    if (dlg.exec() == QDialog::Accepted) {
        // 다이얼로그에서 수정한 데이터 가져오기
        GdtData newData = dlg.resultData();
        const GdtData &oldData = item->gdtData();

        // 뷰포트 종속 데이터(위치, 지시선, 회전 등)는 기존 객체 정보 유지
        newData.position = oldData.position;
        newData.leaderPoints = oldData.leaderPoints;
        newData.rotation = oldData.rotation;
        newData.useLeader = oldData.useLeader;

        // 새 데이터로 객체 생성 및 속성 복사
        GdtItem *newItem = new GdtItem(newData);
        newItem->setLayerName(item->layerName());
        newItem->setPen(item->pen());
        newItem->setParentItem(item->parentItem());

        // 매크로를 이용해 이전 객체 제거 및 새 객체 삽입 (Undo/Redo 지원)
        IAppContext *ctx = getContext();
        if (ctx && ctx->undoStack()) {
            ctx->undoStack()->beginMacro("Edit GD&T");
            ctx->undoStack()->push(new RemoveEntityCommand(m_scene, item));
            ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));
            ctx->undoStack()->endMacro();
        } else {
            m_scene->removeItem(item);
            delete item;
            m_scene->addItem(newItem);
        }
    }

    // 편집 후 도구 상태 유지 (연속 편집 가능)
}

void GdtEditTool::editDatumItem(DatumItem *item) {
    if (!item) return;

    GdtDialog dlg;

    // 데이텀 편집을 위한 더미 데이터 생성 (나머지는 모두 공백/기본값 처리)
    GdtData dummyData;
    dummyData.symbol = GdtSymbol::Straightness; // 아무 기호나 기본값 지정 (UI에 빈 공차로 보이게 함)
    dummyData.toleranceValue = "";
    dummyData.hasDiameter = false;
    dummyData.textHeight = item->datumData().textHeight;
    dummyData.datumRefs.append(item->datumData().text); // 첫 번째 데이텀 입력칸에 현재 텍스트 표시

    dlg.setData(dummyData);

    if (dlg.exec() == QDialog::Accepted) {
        GdtData resData = dlg.resultData();
        DatumData newData = item->datumData();

        // 수정된 데이텀 문자 및 높이 갱신
        newData.textHeight = resData.textHeight;
        if (!resData.datumRefs.isEmpty()) {
            newData.text = resData.datumRefs[0]; // 다이얼로그의 첫 번째 데이텀 값을 가져옴
        } else {
            newData.text = ""; // 모두 지웠을 경우
        }

        // 새 데이텀 객체 생성 및 속성 복사
        DatumItem *newItem = new DatumItem(newData);
        newItem->setLayerName(item->layerName());
        newItem->setPen(item->pen());
        newItem->setParentItem(item->parentItem());

        // 교체 로직
        IAppContext *ctx = getContext();
        if (ctx && ctx->undoStack()) {
            ctx->undoStack()->beginMacro("Edit Datum");
            ctx->undoStack()->push(new RemoveEntityCommand(m_scene, item));
            ctx->undoStack()->push(new AddEntityCommand(m_scene, newItem));
            ctx->undoStack()->endMacro();
        } else {
            m_scene->removeItem(item);
            delete item;
            m_scene->addItem(newItem);
        }
    }
}