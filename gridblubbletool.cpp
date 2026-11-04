#include "gridblubbletool.h"

#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "gridbubblesetupdialog.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "mytext.h"
#include "snapmanager.h"
#include "textitem.h"
#include "tooldefaults.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>
#include <QUndoStack>
#include <QtMath>

// 3-2: 도면 스코프 우선(ToolDefaults). 네임스페이스 "GridBubbleTool" = 기존 그룹명.
static void saveGridBubbleSettings(MyGraphicsScene *scene,
                                   int labelType, const QString &layer,
                                   const QString &labelLayer, bool tilt,
                                   double labelSize, double diameter)
{
    ToolDefaults d("GridBubbleTool", scene);
    d.put("LabelType",   labelType);
    d.put("Layer",       layer);
    d.put("LabelLayer",  labelLayer);
    d.put("TiltEnabled", tilt);
    d.put("LabelSize",   labelSize);
    d.put("Diameter",    diameter);
}

static void loadGridBubbleSettings(MyGraphicsScene *scene,
                                   int &labelType, QString &layer,
                                   QString &labelLayer, bool &tilt,
                                   double &labelSize, double &diameter)
{
    ToolDefaults d("GridBubbleTool", scene);
    labelType  = d.getInt("LabelType",   2);
    layer      = d.getString("Layer",       QString());
    labelLayer = d.getString("LabelLayer",  QString());
    tilt       = d.getBool("TiltEnabled", false);
    labelSize  = d.getDouble("LabelSize",   4.0);
    diameter   = d.getDouble("Diameter",    10.0);
}

GridBubbleTool::GridBubbleTool(QObject *parent)
    : AbstractTool(parent)
    , m_state(WaitStart)
    , m_hasDirection(false)
    , m_labelType(LabelType::TypeA)
    , m_tiltEnabled(false)
    , m_labelSize(4.0)
    , m_diameter(10.0)
    , m_counter(0)
{
    // 생성 시점엔 씬이 없으므로 전역/코드 기본값만. 도면값은 activate()에서 반영.
    int lt = static_cast<int>(m_labelType);
    loadGridBubbleSettings(nullptr, lt, m_fixedLayer, m_labelLayer, m_tiltEnabled,
                           m_labelSize, m_diameter);
    m_labelType = static_cast<LabelType>(lt);
}

GridBubbleTool::~GridBubbleTool()
{
    cancelDrawing();
}

// ── 절대 좌표 → 방향 단위벡터 변환 ─────────────────────────────────────────
static MyVector toUnitVec(const MyVector &from, const MyVector &to)
{
    MyVector d(to.x - from.x, to.y - from.y);
    double len = qSqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-6) return MyVector(0.0, 1.0);
    return MyVector(d.x / len, d.y / len);
}

// ── 현재 라벨 문자열 ─────────────────────────────────────────────────────────
QString GridBubbleTool::currentLabel() const
{
    switch (m_labelType) {
    case LabelType::TypeX:
        return QStringLiteral("X") + QString::number(m_counter);
    case LabelType::TypeY:
        return QStringLiteral("Y") + QString::number(m_counter);
    case LabelType::TypeA: {
        // A-Z, 그 다음 AA, AB ...
        int n = m_counter;
        QString s;
        do {
            s.prepend(QChar('A' + (n % 26)));
            n = n / 26 - 1;
        } while (n >= 0);
        return s;
    }
    case LabelType::TypeNum:
        return QString::number(m_counter);
    }
    return QString::number(m_counter);
}

// ── 라벨 카운터 증가 ─────────────────────────────────────────────────────────
void GridBubbleTool::incrementLabel()
{
    ++m_counter;
}

// ── 시작점 프롬프트 ─────────────────────────────────────────────────────────
void GridBubbleTool::showStartPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;

    QStringList options;
    options << "Setup(S)";

    QString label = currentLabel();
    ctx->commandDock()->showInteractivePrompt(
        tr(" 시작점을 지정 하세요  [%1]  <Setup> : ").arg(label), options);
}

// ── activate ────────────────────────────────────────────────────────────────
void GridBubbleTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);

    // [3-2] 현재 도면의 그리드버블 기본값 반영(도면값 없으면 전역/코드 기본값).
    {
        int lt = static_cast<int>(m_labelType);
        loadGridBubbleSettings(scene, lt, m_fixedLayer, m_labelLayer,
                               m_tiltEnabled, m_labelSize, m_diameter);
        m_labelType = static_cast<LabelType>(lt);
    }

    m_state        = WaitStart;
    m_hasDirection = false;
    m_counter      = (m_labelType == LabelType::TypeA) ? 0 : 1;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("GRIDBUBBLE", CommandDock::Command);
    }
    showStartPrompt();
}

void GridBubbleTool::deactivate()
{
    cancelDrawing();
    AbstractTool::deactivate();
}

// ── mousePress ──────────────────────────────────────────────────────────────
void GridBubbleTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene || button != Qt::LeftButton)
        return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitDirection)
                       ? QPointF(m_pt1.x, m_pt1.y) : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitStart) {
        m_pt1 = MyVector(snapped.x(), snapped.y());

        if (m_hasDirection) {
            // 두 번째 이후: 방향 재사용 → 즉시 생성
            finalizeBubble();
        } else {
            // 첫 배치: 방향 지정 필요
            m_state = WaitDirection;
            updateGhostPreview(snapped);
            if (ctx && ctx->commandDock())
                ctx->commandDock()->showInteractivePrompt(tr(" 방향을 지정 하세요 : "), {});
        }

    } else if (m_state == WaitDirection) {
        // 방향을 단위벡터로 저장 → 이후 반복 배치 시 m_pt1이 달라져도 방향 유지
        m_dirVec       = toUnitVec(m_pt1, MyVector(snapped.x(), snapped.y()));
        m_hasDirection = true;
        m_state        = WaitStart;
        finalizeBubble();
    }
}

// ── mouseMove ───────────────────────────────────────────────────────────────
void GridBubbleTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitDirection)
                       ? QPointF(m_pt1.x, m_pt1.y) : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }
    m_currentMousePos = snapped;

    if (m_state == WaitDirection)
        updateGhostPreview(snapped);

    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitDirection)
                       ? QPointF(m_pt1.x, m_pt1.y) : QPointF();
        snapType = ctx->snapManager()->snapPoint(pos, m_scene, base).type;
    }
    m_scene->setSnapMarker(snapped, snapType);
}

void GridBubbleTool::mouseRelease(QPointF pos, Qt::MouseButton button)
{
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

// ── processCommand ─────────────────────────────────────────────────────────
bool GridBubbleTool::processCommand(const QString &command)
{
    QString cmd = command.trimmed().toLower();
    if (cmd.isEmpty())
        return false;

    if (cmd == "setup" || cmd == "s" || cmd == "setup(s)") {
        openSetupDialog();
        return true;
    }

    IAppContext *ctx = getContext();

    if (m_state == WaitStart) {
        if (ctx && ctx->parser()) {
            QPointF resultPos;
            if (ctx->parser()->parseCoordinate(command.trimmed(), QPointF(), QPointF(), false, resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                return true;
            }
        }
        return false;
    }

    if (m_state == WaitDirection) {
        if (ctx && ctx->parser()) {
            QPointF resultPos;
            QPointF base(m_pt1.x, m_pt1.y);
            if (ctx->parser()->parseCoordinate(command.trimmed(), base, base, false, resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                return true;
            }
        }
        return false;
    }

    return false;
}

void GridBubbleTool::onFinish()
{
    cancelDrawing();
}

void GridBubbleTool::onCancel()
{
    cancelDrawing();
    emit toolFinished();
}

// ── openSetupDialog ─────────────────────────────────────────────────────────
void GridBubbleTool::openSetupDialog()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager())
        return;

    QString curLayer = m_fixedLayer.isEmpty()
                       ? ctx->layerManager()->getCurrentLayer()->name()
                       : m_fixedLayer;

    QString curLabelLayer = m_labelLayer.isEmpty() ? curLayer : m_labelLayer;

    // 다이얼로그 표시
    QWidget *parentWidget = nullptr;
    if (ctx->commandDock())
        parentWidget = qobject_cast<QWidget *>(ctx->commandDock()->parent());

    GridBubbleSetupDialog dlg(m_labelType, ctx->layerManager(),
                              curLayer, curLabelLayer, m_tiltEnabled,
                              m_labelSize, m_diameter,
                              parentWidget);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_labelType   = dlg.labelType();
    m_fixedLayer  = dlg.layerName();
    m_labelLayer  = dlg.labelLayerName();
    m_tiltEnabled = dlg.tiltEnabled();
    m_labelSize   = dlg.labelSize();
    m_diameter    = dlg.diameter();

    saveGridBubbleSettings(m_scene, static_cast<int>(m_labelType),
                           m_fixedLayer, m_labelLayer, m_tiltEnabled,
                           m_labelSize, m_diameter);

    // 타입 변경 시 카운터 리셋
    m_counter = (m_labelType == LabelType::TypeA) ? 0 : 1;

    showStartPrompt();
}

// ── cancelDrawing ───────────────────────────────────────────────────────────
void GridBubbleTool::cancelDrawing()
{
    m_state = WaitStart;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

// ── updateGhostPreview ──────────────────────────────────────────────────────
void GridBubbleTool::updateGhostPreview(const QPointF &mousePos)
{
    if (!m_scene)
        return;

    double previewScale = m_scene ? m_scene->drawingScale() : 1.0;
    if (previewScale <= 0.0) previewScale = 1.0;
    const double previewRadius = (m_diameter / 2.0) * previewScale;
    MyVector previewDir = toUnitVec(m_pt1, MyVector(mousePos.x(), mousePos.y()));
    MyVector bc = MyVector(m_pt1.x + previewDir.x * 2.0 * previewRadius,
                           m_pt1.y + previewDir.y * 2.0 * previewRadius);

    MyVector gDir(bc.x - m_pt1.x, bc.y - m_pt1.y);
    double gLen = qSqrt(gDir.x * gDir.x + gDir.y * gDir.y);
    QPointF tailEndPt = (gLen > 1e-6)
        ? QPointF(m_pt1.x + gDir.x / gLen * previewRadius,
                  m_pt1.y + gDir.y / gLen * previewRadius)
        : QPointF(m_pt1.x, m_pt1.y + previewRadius);

    QPainterPath path;
    path.moveTo(m_pt1.x, m_pt1.y);
    path.lineTo(tailEndPt);
    path.addEllipse(QPointF(bc.x, bc.y), previewRadius, previewRadius);

    m_scene->setGhostPath(path, true, Qt::yellow, 1.0);
}

// ── finalizeBubble ──────────────────────────────────────────────────────────
void GridBubbleTool::finalizeBubble()
{
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory() || !ctx->layerManager() || !ctx->lineTypeManager())
        return;

    // 레이어 결정
    Layer *curLayer = m_fixedLayer.isEmpty()
                      ? ctx->layerManager()->getCurrentLayer()
                      : ctx->layerManager()->getLayer(m_fixedLayer);
    if (!curLayer)
        curLayer = ctx->layerManager()->getCurrentLayer();

    QPen pen(curLayer->color());
    pen.setWidthF(curLayer->weight());
    QString ltName = curLayer->linetype();
    if (ltName == "Continuous" || ltName == "Continous") {
        pen.setStyle(Qt::SolidLine);
    } else {
        pen.setStyle(Qt::CustomDashLine);
        QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(ltName);
        pen.setDashPattern(!pattern.isEmpty() ? pattern : QVector<qreal>());
        if (pattern.isEmpty()) pen.setStyle(Qt::SolidLine);
    }

    // 기본값에 도면 스케일을 곱해 CAD 단위로 변환
    double drawingScale = m_scene->drawingScale();
    if (drawingScale <= 0.0) drawingScale = 1.0;
    const double radius    = (m_diameter / 2.0) * drawingScale;
    const double fontSize  = m_labelSize * drawingScale;

    // m_dirVec 은 단위벡터이므로 방향이 항상 정확히 유지됨
    MyVector bc = MyVector(m_pt1.x + m_dirVec.x * 2.0 * radius,
                           m_pt1.y + m_dirVec.y * 2.0 * radius);

    // 꼬리선 끝점 (원 경계) = p1 + dirVec * radius
    MyVector tailEnd(m_pt1.x + m_dirVec.x * radius,
                     m_pt1.y + m_dirVec.y * radius);

    // 라벨(텍스트) 전용 레이어
    Layer *labelLayerPtr = m_labelLayer.isEmpty()
                           ? curLayer
                           : ctx->layerManager()->getLayer(m_labelLayer);
    if (!labelLayerPtr) labelLayerPtr = curLayer;

    QPen labelPen(labelLayerPtr->color());
    labelPen.setWidthF(labelLayerPtr->weight());
    {
        QString ltNameL = labelLayerPtr->linetype();
        if (ltNameL == "Continuous" || ltNameL == "Continous") {
            labelPen.setStyle(Qt::SolidLine);
        } else {
            labelPen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pat = ctx->lineTypeManager()->getQtDashPattern(ltNameL);
            labelPen.setDashPattern(!pat.isEmpty() ? pat : QVector<qreal>());
            if (pat.isEmpty()) labelPen.setStyle(Qt::SolidLine);
        }
    }

    QGraphicsItem *layerFolder = m_scene->getLayerRoot(curLayer->name());
    QString layerName = curLayer->name();

    auto addItem = [&](QGraphicsItem *item) {
        if (!item) return;
        if (CadItem *ci = dynamic_cast<CadItem *>(item))
            ci->setLayerName(layerName);
        item->setParentItem(layerFolder);
        if (item->scene() == m_scene)
            m_scene->removeItem(item);
        if (ctx->undoStack())
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        else
            m_scene->addItem(item);
    };

    // 1. 꼬리선 (p1 → 원 경계)
    addItem(ctx->entityFactory()->createLine(
        QLineF(m_pt1.x, m_pt1.y, tailEnd.x, tailEnd.y), pen));

    // 2. 원 (버블)
    addItem(ctx->entityFactory()->createCircle(QPointF(bc.x, bc.y), radius, pen));

    // 3. 텍스트 — 원 정중앙, 기울기 적용 시 방향 정렬
    QFont tmpFont("Arial");
    tmpFont.setPointSizeF(fontSize);
    QFontMetricsF fm(tmpFont);
    double vertOffset = fm.ascent() / 2.0;

    double tiltAngle = 0.0;
    if (m_tiltEnabled) {
        tiltAngle = qRadiansToDegrees(qAtan2(m_dirVec.y, m_dirVec.x)) - 90.0;
    }

    // vertOffset은 텍스트 로컬 +Y 방향(= 전역에서 회전된 수직 방향)으로 적용
    double tiltRad = qDegreesToRadians(tiltAngle);
    MyVector textPos(bc.x + vertOffset * qSin(tiltRad),
                     bc.y - vertOffset * qCos(tiltRad));

    MyText textData;
    textData.pos        = textPos;
    textData.text       = currentLabel();
    textData.fontFamily = "Arial";
    textData.fontSize   = fontSize;
    textData.rotation   = tiltAngle;
    textData.alignH     = 1;  // 수평 중앙

    QGraphicsItem *labelFolder = m_scene->getLayerRoot(labelLayerPtr->name());
    TextItem *ti = new TextItem(textData);
    ti->setLayerName(labelLayerPtr->name());
    ti->setPen(labelPen);
    ti->setParentItem(labelFolder);
    if (ti->scene() == m_scene)
        m_scene->removeItem(ti);
    if (ctx->undoStack())
        ctx->undoStack()->push(new AddEntityCommand(m_scene, ti));
    else
        m_scene->addItem(ti);

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("주열번호 '%1' 생성됨").arg(textData.text), CommandDock::Info);

    incrementLabel();

    // 고스트 제거 후 다음 배치 대기
    if (m_scene)
        m_scene->setGhostPath(QPainterPath(), false);

    m_state = WaitStart;
    showStartPrompt();
}
