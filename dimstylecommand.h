#ifndef DIMSTYLECOMMAND_H
#define DIMSTYLECOMMAND_H

#include "commandbase.h"
#include "iappcontext.h"
#include <QObject>

class DimStyleDialog;
class MyGraphicsScene;
class PickDistanceTool;
class QEventLoop;

// ──────────────────────────────────────────────────────────────────────────────
// DimPickHandler
//   DimStyleDialog::pickDimScaleRequested 에 연결되는 슬롯 보유 클래스
//   DrawingSettingsCommand 의 PickDistanceHandler 와 동일한 패턴
// ──────────────────────────────────────────────────────────────────────────────
class DimPickHandler : public QObject {
    Q_OBJECT
public:
    explicit DimPickHandler(DimStyleDialog  *dlg,
                            MyGraphicsScene *scene,
                            QObject         *parent = nullptr);

public slots:
    void onPickRequested();
    void onDistancePicked(double dist);
    void onCancelled();

private:
    DimStyleDialog   *m_dlg;
    MyGraphicsScene  *m_scene;
    PickDistanceTool *m_pickTool;
    QEventLoop       *m_loop;
};

// ──────────────────────────────────────────────────────────────────────────────
// DimStyleCommand
// ──────────────────────────────────────────────────────────────────────────────
class DimStyleCommand : public CommandBase {
public:
    explicit DimStyleCommand(IAppContext *ctx);

    void execute() override;

private:
    IAppContext *m_ctx;
};

#endif // DIMSTYLECOMMAND_H
