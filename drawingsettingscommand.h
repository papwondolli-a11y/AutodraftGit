#ifndef DRAWINGSETTINGSCOMMAND_H
#define DRAWINGSETTINGSCOMMAND_H

#include "commandbase.h"
#include "iappcontext.h"
#include <QObject>
#include <QRectF>
#include <QString>

class BlockItem;
class CadItem;
class DrawingSettingsDialog;
class MyGraphicsScene;
class PickDistanceTool;
class QEventLoop;

// ──────────────────────────────────────────────────────────────────────────────
// PickDistanceHandler
//   pickScaleRequested 시그널에서 람다 대신 사용할 QObject 슬롯 보유 클래스
// ──────────────────────────────────────────────────────────────────────────────
class PickDistanceHandler : public QObject {
    Q_OBJECT
public:
    explicit PickDistanceHandler(DrawingSettingsDialog *dlg,
                                 MyGraphicsScene      *scene,
                                 IAppContext          *ctx,
                                 QObject              *parent = nullptr);

public slots:
    void onPickScaleRequested();
    void onDistancePicked(double dist);
    void onCancelled();

private:
    DrawingSettingsDialog *m_dlg;
    MyGraphicsScene       *m_scene;
    IAppContext           *m_ctx;
    PickDistanceTool      *m_pickTool;
    QEventLoop            *m_loop;
};

// ──────────────────────────────────────────────────────────────────────────────
// DrawingSettingsCommand
// ──────────────────────────────────────────────────────────────────────────────
class DrawingSettingsCommand : public CommandBase {
public:
    explicit DrawingSettingsCommand(IAppContext *ctx);

    void execute() override;

    // 전역 중심선 레이어 (빈 문자열 = 현재 레이어 사용)
    static QString s_centerLineLayer;

private:
    IAppContext *m_ctx;

    QRectF      getPaperSize(const QString &size) const;
    BlockItem  *insertFrameBlock(MyGraphicsScene *scene,
                                 const QString   &size,
                                 double           scale);
};

#endif // DRAWINGSETTINGSCOMMAND_H
