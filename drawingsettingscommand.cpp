#include "drawingsettingscommand.h"

#include "blockitem.h"
#include "cadcommands.h"
#include "drawingsettingsdialog.h"
#include "filemanager.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "pickdistancetool.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QSettings>
#include <QUndoStack>

// ── 전역 중심선 레이어 ────────────────────────────────────────────────────────
QString DrawingSettingsCommand::s_centerLineLayer = QString();

static void saveCenterLineLayerSetting() {
    QSettings s; // 정규 .ini (main() 지정)
    s.beginGroup("DrawingSettings");
    s.setValue("CenterLineLayer", DrawingSettingsCommand::s_centerLineLayer);
    s.endGroup();
}

static void loadCenterLineLayerSetting() {
    QSettings s; // 정규 .ini (main() 지정)
    s.beginGroup("DrawingSettings");
    DrawingSettingsCommand::s_centerLineLayer =
        s.value("CenterLineLayer", QString()).toString();
    s.endGroup();
}

// ─────────────────────────────────────────────────────────────────────────────
// PickDistanceHandler
// ─────────────────────────────────────────────────────────────────────────────
PickDistanceHandler::PickDistanceHandler(DrawingSettingsDialog *dlg,
                                         MyGraphicsScene       *scene,
                                         IAppContext           *ctx,
                                         QObject               *parent)
    : QObject(parent)
    , m_dlg(dlg)
    , m_scene(scene)
    , m_ctx(ctx)
    , m_pickTool(nullptr)
    , m_loop(nullptr)
{
}

void PickDistanceHandler::onPickScaleRequested()
{
    m_dlg->hide();

    m_pickTool = new PickDistanceTool(m_scene);
    m_scene->setTool(m_pickTool);

    QObject::connect(m_pickTool, &PickDistanceTool::distancePicked,
                     this, &PickDistanceHandler::onDistancePicked);
    QObject::connect(m_pickTool, &PickDistanceTool::cancelled,
                     this, &PickDistanceHandler::onCancelled);

    m_loop = new QEventLoop(this);
    m_loop->exec();

    m_scene->setTool(nullptr);
    m_dlg->show();
    m_pickTool->deleteLater();
    m_pickTool = nullptr;

    delete m_loop;
    m_loop = nullptr;
}

void PickDistanceHandler::onDistancePicked(double dist)
{
    m_dlg->setDrawingScale(dist);
    if (m_loop && m_loop->isRunning())
        m_loop->quit();
}

void PickDistanceHandler::onCancelled()
{
    if (m_loop && m_loop->isRunning())
        m_loop->quit();
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawingSettingsCommand
// ─────────────────────────────────────────────────────────────────────────────
DrawingSettingsCommand::DrawingSettingsCommand(IAppContext *ctx)
    : m_ctx(ctx)
{
    loadCenterLineLayerSetting();
}

void DrawingSettingsCommand::execute()
{
    MyGraphicsScene *scene = m_ctx->activeScene();
    if (!scene)
        return;

    LayerManager *lm = m_ctx->layerManager();
    DrawingSettingsDialog dlg(lm, nullptr);

    dlg.setInitialValues(DrawingSettingsDialog::Mode_Basic, "None",
                         scene->drawingScale());
    dlg.setCenterLineLayer(s_centerLineLayer);
    dlg.setLinearUnit(scene->linearUnit()); // [Phase 7] 도면 스코프 단위 초기값

    PickDistanceHandler handler(&dlg, scene, m_ctx);
    QObject::connect(&dlg, &DrawingSettingsDialog::pickScaleRequested,
                     &handler, &PickDistanceHandler::onPickScaleRequested);

    if (dlg.exec() == QDialog::Accepted) {
        // 도면 스케일
        double scale = dlg.drawingScale();
        scene->setDrawingScale(scale);

        // [Phase 7] 도면 길이 단위 (mm/inch) — DocumentSettings 에 산다(도면 스코프).
        //   QSettings 저장 불필요: 값은 .cdb/.cdt 의 doc_settings 로 직렬화된다.
        scene->setLinearUnit(dlg.linearUnit());

        if (dlg.sizeMode() == DrawingSettingsDialog::Mode_Basic) {
            QString size = dlg.selectedSize();
            scene->setDrawingSizeName(size);
            QRectF limits = getPaperSize(size);
            limits = QRectF(limits.x()      * scale,
                            limits.y()      * scale,
                            limits.width()  * scale,
                            limits.height() * scale);
            scene->setDrawingLimits(limits);
            if (MyGraphicsView *view = m_ctx->activeView())
                view->zoomToRect(limits);
        } else {
            QString size = dlg.selectedSize();
            scene->setDrawingSizeName(size);
            BlockItem *frame = insertFrameBlock(scene, size, scale);
            scene->setDrawingLimits(QRectF());
            if (frame && m_ctx->activeView())
                m_ctx->activeView()->zoomToRect(frame->sceneBoundingRect());
        }

        // 중심선 레이어 저장
        s_centerLineLayer = dlg.centerLineLayer();
        saveCenterLineLayerSetting();
    }
}

QRectF DrawingSettingsCommand::getPaperSize(const QString &size) const
{
    if (size == "A4") return QRectF(0, 0, 297,  210);
    if (size == "A3") return QRectF(0, 0, 420,  297);
    if (size == "A2") return QRectF(0, 0, 594,  420);
    if (size == "A1") return QRectF(0, 0, 841,  594);
    if (size == "A0") return QRectF(0, 0, 1189, 841);
    return QRectF();
}

BlockItem *DrawingSettingsCommand::insertFrameBlock(MyGraphicsScene *scene,
                                                     const QString   &size,
                                                     double           scale)
{
    QString framePath;
    QString fileName = size.toLower() + ".cdb";
    QString appDirPath = QCoreApplication::applicationDirPath();

    framePath = QDir(appDirPath).absoluteFilePath("frames/" + fileName);
    if (!QFile::exists(framePath))
        framePath = QDir::cleanPath(appDirPath + "/../frames/" + fileName);
#ifdef APP_SOURCE_PATH
    if (!QFile::exists(framePath))
        framePath = QDir(APP_SOURCE_PATH).absoluteFilePath("frames/" + fileName);
#endif

    if (!QFile::exists(framePath)) {
        m_ctx->showStatusText("Frame file not found: " + framePath);
        return nullptr;
    }

    FileManager *fm = m_ctx->fileManager();
    if (!fm)
        return nullptr;

    QString originalLayer;
    QList<CadItem *> items = fm->importBlock(framePath, &originalLayer);

    if (!items.isEmpty()) {
        BlockItem *frame = new BlockItem(items, MyVector(0, 0), size, scale, scale, 0.0);
        frame->setLayerName("0");
        m_ctx->undoStack()->push(new AddEntityCommand(scene, frame));
        return frame;
    }
    return nullptr;
}
