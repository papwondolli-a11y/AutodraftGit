#include "dimstylecommand.h"

#include "cadcommands.h"
#include "dimstyledialog.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "pickdistancetool.h"

#include <QEventLoop>
#include <QUndoStack>

// ─────────────────────────────────────────────────────────────────────────────
// DimPickHandler
// ─────────────────────────────────────────────────────────────────────────────
DimPickHandler::DimPickHandler(DimStyleDialog  *dlg,
                               MyGraphicsScene *scene,
                               QObject         *parent)
    : QObject(parent)
    , m_dlg(dlg)
    , m_scene(scene)
    , m_pickTool(nullptr)
    , m_loop(nullptr)
{
}

void DimPickHandler::onPickRequested()
{
    m_dlg->hide();

    m_pickTool = new PickDistanceTool(m_scene);
    m_scene->setTool(m_pickTool);

    QObject::connect(m_pickTool, &PickDistanceTool::distancePicked,
                     this, &DimPickHandler::onDistancePicked);
    QObject::connect(m_pickTool, &PickDistanceTool::cancelled,
                     this, &DimPickHandler::onCancelled);

    m_loop = new QEventLoop(this);
    m_loop->exec();

    // 이벤트 루프 종료 후 복원
    m_scene->setTool(nullptr);
    m_dlg->show();
    m_pickTool->deleteLater();
    m_pickTool = nullptr;

    delete m_loop;
    m_loop = nullptr;
}

void DimPickHandler::onDistancePicked(double dist)
{
    m_dlg->setDimScale(dist);
    if (m_loop && m_loop->isRunning())
        m_loop->quit();
}

void DimPickHandler::onCancelled()
{
    if (m_loop && m_loop->isRunning())
        m_loop->quit();
}

// ─────────────────────────────────────────────────────────────────────────────
// DimStyleCommand
// ─────────────────────────────────────────────────────────────────────────────
DimStyleCommand::DimStyleCommand(IAppContext *ctx)
    : m_ctx(ctx)
{
}

void DimStyleCommand::execute()
{
    MyGraphicsScene *scene =
        qobject_cast<MyGraphicsScene *>(m_ctx->activeScene());
    if (!scene)
        return;

    DimStyleDialog  dlg(m_ctx, nullptr);
    DimensionStyle  savedStyle = scene->dimensionStyle();
    dlg.setStyle(savedStyle);

    // 치수선 스케일 초기값 결정:
    //   - 저장된 dimScale 이 기본값(1.0) 이면 → 현재 도면 스케일로 초기화
    //   - 이미 설정된 값이 있으면(≠ 1.0)    → setStyle() 이 적용한 값 유지
    if (qFuzzyCompare(savedStyle.dimScale, 1.0)) {
        dlg.setDimScale(scene->drawingScale());
    }

    // 치수선 스케일 픽 요청 연결 — 포인터 함수 방식
    DimPickHandler handler(&dlg, scene);
    QObject::connect(&dlg, &DimStyleDialog::pickDimScaleRequested,
                     &handler, &DimPickHandler::onPickRequested);

    if (dlg.exec() == QDialog::Accepted) {
        DimensionStyle newStyle = dlg.getStyle();

        // 변경 전후를 QUndoStack 에 기록하여 Undo/Redo 지원
        m_ctx->undoStack()->push(
            new ChangeDimStyleCommand(scene, savedStyle, newStyle));
    }
}
