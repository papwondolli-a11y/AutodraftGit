#include "bearinginserttool.h"

#include "commanddock.h"
#include "deepgroovebearingdata.h"
#include "deepgroovebearingtool.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "projectionview.h"
#include "scenesymbolregistry.h"
#include "taperedrollerbearingdata.h"
#include "taperedrollerbearingtool.h"
#include "tooldefaults.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

namespace {

QString num(double v)
{
    return QString::number(v, 'g', 6);
}

// 각 베어링 도구 설정창에 저장된 그리기 설정 (도구를 마지막으로 쓴 값, 도면 템플릿 스코프)
void applyDeepSettings(MyGraphicsScene *scene, DeepGrooveBearingParams &p)
{
    ToolDefaults df("DeepGrooveBearingTool", scene);
    p.drawDims = df.getBool("DrawDims", p.drawDims);
    p.drawCenter = df.getBool("DrawCenter", p.drawCenter);
    p.drawHatch = df.getBool("DrawHatch", p.drawHatch);
    p.drawFull = df.getBool("DrawFull", p.drawFull);
    p.hatchScale = df.getDouble("HatchScale", 0.0);
}

void applyTaperSettings(MyGraphicsScene *scene, TaperedRollerBearingParams &p)
{
    ToolDefaults df("TaperedRollerBearingTool", scene);
    p.drawDims = df.getBool("DrawDims", p.drawDims);
    p.drawCenter = df.getBool("DrawCenter", p.drawCenter);
    p.drawHatch = df.getBool("DrawHatch", p.drawHatch);
    p.drawFull = df.getBool("DrawFull", p.drawFull);
    p.hatchScale = df.getDouble("HatchScale", 0.0);
    p.angle = df.getDouble("Angle", p.angle);
}

// 권장 최소 턱 지름 (추정: 링 단면 높이의 40% 를 턱이 받침) — 규격표에 d_a 가 없어 경향식
double shoulderMin(double d, double D)
{
    return d + 0.2 * (D - d);
}

BearingFitCheck check(const BearingInsertPlan &pl, const QString &name, double d, double D, double W)
{
    BearingFitCheck c;
    c.name = name;
    c.d = d;
    c.D = D;
    c.W = W;
    QStringList warn;
    const ShaftSegment &sg = pl.pick.params.segments.at(pl.pick.seg);
    if (W > sg.length + 1e-9)
        warn << QStringLiteral("폭 %1 > 구간 %2").arg(num(W), num(sg.length));
    if (pl.shoulderD <= d + 1e-9)
        warn << QStringLiteral("턱 없음");
    else if (pl.shoulderD < shoulderMin(d, D) - 1e-9)
        warn << QStringLiteral("턱 낮음 %1<%2").arg(num(pl.shoulderD), num(std::ceil(shoulderMin(d, D) * 10.0) / 10.0));
    if (pl.blockD > d + 1e-6)
        warn << QStringLiteral("끼울 쪽에 Ø%1").arg(num(pl.blockD));
    c.ok = warn.isEmpty();
    c.note = c.ok ? QStringLiteral("맞음") : warn.join(QStringLiteral(", "));
    return c;
}

} // namespace

// ── 배치 정보 ──
bool BearingInsertTool::planAt(MyGraphicsScene *scene, const QPointF &pos, double tol, BearingInsertPlan &plan)
{
    plan = BearingInsertPlan();
    if (!scene)
        return false;
    const QList<QGraphicsItem *> near = scene->items(QRectF(pos.x() - tol, pos.y() - tol, tol * 2.0, tol * 2.0));
    for (int i = 0; i < near.size() && !plan.pick.ok; ++i)
        ShaftPick::pick(scene, near.at(i), pos, plan.pick);
    if (!plan.pick.ok)
        return false;
    const ShaftParams &p = plan.pick.params;
    const int s = plan.pick.seg;
    // 클릭 지점의 축 방향 위치 → 가까운 끝 = 붙일 턱
    QPointF a0;
    double ang = 0.0;
    QString nm;
    ProjectionView::partFrame(scene, plan.pick.uuid, a0, ang, nm);
    const double a = qDegreesToRadians(ang);
    const double x = (pos.x() - a0.x()) * std::cos(a) + (pos.y() - a0.y()) * std::sin(a);
    plan.leftSide = (x - plan.pick.xFrom) <= (plan.pick.xTo - x);
    const int nb = plan.leftSide ? s - 1 : s + 1;
    plan.shoulderD = (nb >= 0 && nb < p.segments.size()) ? p.segments.at(nb).diameter : 0.0;
    // 끼워 넣는 쪽 = 턱 반대쪽 끝까지의 구간들
    plan.blockD = 0.0;
    if (plan.leftSide)
        for (int i = s + 1; i < p.segments.size(); ++i)
            plan.blockD = std::max(plan.blockD, p.segments.at(i).diameter);
    else
        for (int i = 0; i < s; ++i)
            plan.blockD = std::max(plan.blockD, p.segments.at(i).diameter);
    return true;
}

QList<BearingFitCheck> BearingInsertTool::candidates(const BearingInsertPlan &plan, bool tapered)
{
    QList<BearingFitCheck> out;
    if (!plan.pick.ok)
        return out;
    const double d = plan.pick.params.segments.at(plan.pick.seg).diameter;
    if (tapered) {
        const QList<TaperedRollerBearingSpec> &all = TaperedRollerBearingTable::all();
        for (int i = 0; i < all.size(); ++i)
            if (std::fabs(all.at(i).d - d) < 1e-6)
                out << check(plan, all.at(i).name, all.at(i).d, all.at(i).D, all.at(i).T);
    } else {
        const QList<DeepGrooveBearingSpec> &all = DeepGrooveBearingTable::all();
        for (int i = 0; i < all.size(); ++i)
            if (std::fabs(all.at(i).d - d) < 1e-6)
                out << check(plan, all.at(i).name, all.at(i).d, all.at(i).D, all.at(i).B);
    }
    return out;
}

double BearingInsertTool::widthOf(bool tapered, const QString &name)
{
    if (tapered) {
        const TaperedRollerBearingSpec sp = TaperedRollerBearingTable::find(name);
        return sp.name.isEmpty() ? 0.0 : sp.T;
    }
    const DeepGrooveBearingSpec sp = DeepGrooveBearingTable::find(name);
    return sp.name.isEmpty() ? 0.0 : sp.B;
}

double BearingInsertTool::centerFor(const BearingInsertPlan &plan, double width, bool fromShoulder, double distance)
{
    // 기준면에서 구간 안쪽으로 distance 만큼 떨어진 곳에 베어링 옆면
    const bool fromLeft = (fromShoulder == plan.leftSide);
    return fromLeft ? plan.pick.xFrom + distance + width * 0.5 : plan.pick.xTo - distance - width * 0.5;
}

QPainterPath BearingInsertTool::ghost(MyGraphicsScene *scene, const BearingInsertPlan &plan, bool tapered,
                                      const QString &name, double centerX)
{
    QPointF a0;
    double ang = 0.0;
    QString nm;
    if (!ProjectionView::partFrame(scene, plan.pick.uuid, a0, ang, nm))
        return QPainterPath();
    const double a = qDegreesToRadians(ang);
    const QPointF c = a0 + QPointF(std::cos(a), std::sin(a)) * centerX;
    if (tapered) {
        const TaperedRollerBearingSpec sp = TaperedRollerBearingTable::find(name);
        TaperedRollerBearingParams p;
        p.name = sp.name;
        p.d = sp.d;
        p.D = sp.D;
        p.B = sp.B;
        p.T = sp.T;
        p.C = sp.C;
        p.r = sp.r;
        p.R = sp.R;
        applyTaperSettings(scene, p);
        p.drawDims = false;                     // 미리보기는 외형만
        p.drawHatch = false;
        return TaperedRollerBearingTool::buildPath(p, c, ang + (plan.leftSide ? 180.0 : 0.0));
    }
    const DeepGrooveBearingSpec sp = DeepGrooveBearingTable::find(name);
    DeepGrooveBearingParams p;
    p.name = sp.name;
    p.d = sp.d;
    p.D = sp.D;
    p.B = sp.B;
    p.r = sp.r;
    applyDeepSettings(scene, p);
    p.drawDims = false;                         // 미리보기는 외형만
    p.drawHatch = false;
    return DeepGrooveBearingTool::buildPath(p, c, ang);
}

QString BearingInsertTool::insert(IAppContext *ctx, MyGraphicsScene *scene, const BearingInsertPlan &plan, bool tapered,
                                  const QString &name, bool setK5, double centerX)
{
    if (!ctx || !scene || !plan.pick.ok)
        return QString();
    QPointF a0;
    double ang = 0.0;
    QString nm;
    if (!ProjectionView::partFrame(scene, plan.pick.uuid, a0, ang, nm))
        return QString();
    const double a = qDegreesToRadians(ang);
    const QPointF u(std::cos(a), std::sin(a));
    QUndoStack *undo = ctx->undoStack();
    if (undo)
        undo->beginMacro(QObject::tr("베어링 넣기"));
    QString placed;
    if (tapered) {
        const TaperedRollerBearingSpec sp = TaperedRollerBearingTable::find(name);
        if (!sp.name.isEmpty()) {
            TaperedRollerBearingParams p;
            p.name = sp.name;
            p.d = sp.d;
            p.D = sp.D;
            p.B = sp.B;
            p.T = sp.T;
            p.C = sp.C;
            p.r = sp.r;
            p.R = sp.R;
            applyTaperSettings(scene, p);       // 치수·중심선·해칭·전체그리기·경사각 = 테이퍼롤러 설정창 값
            const double xc = qIsNaN(centerX) ? centerFor(plan, sp.T, true, 0.0) : centerX;
            // 콘 후면(로컬 +x)이 턱 쪽: 왼쪽 턱이면 180° 돌림
            TaperedRollerBearingTool::placeDirect(ctx, scene, p, a0 + u * xc, ang + (plan.leftSide ? 180.0 : 0.0));
            placed = sp.name;
        }
    } else {
        const DeepGrooveBearingSpec sp = DeepGrooveBearingTable::find(name);
        if (!sp.name.isEmpty()) {
            DeepGrooveBearingParams p;
            p.name = sp.name;
            p.d = sp.d;
            p.D = sp.D;
            p.B = sp.B;
            p.r = sp.r;
            applyDeepSettings(scene, p);        // 치수기입·중심선·단면해칭·전체그리기 = 깊은홈 설정창 값
            const double xc = qIsNaN(centerX) ? centerFor(plan, sp.B, true, 0.0) : centerX;
            DeepGrooveBearingTool::placeDirect(ctx, scene, p, a0 + u * xc, ang);
            placed = sp.name;
        }
    }
    // 축 공차 k5 (축 데이터만 — 3D·간섭 검사용)
    if (!placed.isEmpty() && setK5) {
        SceneSymbolRegistry *reg = scene->symbolRegistry();
        const SymbolInfo oldInfo = reg->symbolInfo(plan.pick.uuid);
        ShaftParams sp = ShaftParams::fromJsonString(oldInfo.props.value(QStringLiteral("Params")).toString(),
                                                     ShaftParams::sample());
        if (plan.pick.seg < sp.segments.size() && sp.segments.at(plan.pick.seg).fit != QLatin1String("k5")) {
            sp.segments[plan.pick.seg].fit = QStringLiteral("k5");
            SymbolInfo info = oldInfo;
            info.props[QStringLiteral("Params")] = sp.toJsonString();
            if (undo)
                undo->push(new UpdateRegistryCommand(reg, plan.pick.uuid, oldInfo, info));
            else
                reg->registerSymbol(plan.pick.uuid, info);
        }
    }
    if (undo)
        undo->endMacro();
    return placed;
}

// ── 설정창 ──
BearingInsertDialog::BearingInsertDialog(const BearingInsertPlan &plan, QWidget *parent) : QDialog(parent), m_plan(plan)
{
    setWindowTitle(tr("베어링 넣기"));
    QVBoxLayout *lay = new QVBoxLayout(this);
    const ShaftSegment &sg = plan.pick.params.segments.at(plan.pick.seg);
    const QString side = plan.leftSide ? tr("왼쪽") : tr("오른쪽");
    const QString sh = plan.shoulderD > 0.0 ? tr("턱 Ø%1").arg(num(plan.shoulderD)) : tr("축 끝 (턱 없음)");
    lay->addWidget(new QLabel(tr("%1 · 구간 %2 · %3 · 길이 %4\n%5 %6 에 붙임")
                                  .arg(plan.pick.name).arg(plan.pick.seg + 1)
                                  .arg(ShaftPick::diameterText(sg, false), num(sg.length), side, sh),
                              this));
    QHBoxLayout *kinds = new QHBoxLayout();
    m_deep = new QRadioButton(tr("깊은 홈 볼"), this);
    m_taper = new QRadioButton(tr("테이퍼 롤러"), this);
    m_deep->setChecked(true);
    kinds->addWidget(m_deep);
    kinds->addWidget(m_taper);
    kinds->addStretch(1);
    lay->addLayout(kinds);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("호칭"), tr("d×D×폭"), tr("검사")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMinimumSize(380, 180);
    lay->addWidget(m_table);
    lay->addWidget(new QLabel(tr("턱 최소 지름은 추정값(d + 0.2(D−d))입니다. 경고가 있어도 놓을 수 있습니다."), this));
    m_k5 = new QCheckBox(tr("축 공차 %1 → k5 로 바꾸기 (권장, 3D·간섭 검사용 축 데이터만 — 도면 치수 글은 그대로)")
                             .arg(sg.fit.trimmed().isEmpty() ? tr("없음") : sg.fit.trimmed()),
                         this);
    m_k5->setChecked(sg.fit.trimmed().isEmpty());
    m_k5->setVisible(sg.fit.trimmed() != QLatin1String("k5"));
    lay->addWidget(m_k5);
    lay->addWidget(new QLabel(tr("치수·중심선·해칭·전체그리기는 각 베어링 도구 설정창의 마지막 설정을 따릅니다."), this));
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("놓기"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_deep, &QRadioButton::toggled, this, &BearingInsertDialog::refill);
    lay->addWidget(bb);
    refill();
}

void BearingInsertDialog::refill()
{
    m_rows = BearingInsertTool::candidates(m_plan, tapered());
    m_table->setRowCount(m_rows.size());
    int firstOk = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const BearingFitCheck &c = m_rows.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(c.name));
        m_table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1×%2×%3").arg(num(c.d), num(c.D), num(c.W))));
        QTableWidgetItem *n = new QTableWidgetItem(c.note);
        if (!c.ok)
            n->setForeground(QColor(186, 117, 23));
        m_table->setItem(i, 2, n);
        if (c.ok && firstOk < 0)
            firstOk = i;
    }
    m_table->resizeColumnsToContents();
    if (!m_rows.isEmpty())
        m_table->selectRow(firstOk >= 0 ? firstOk : 0);
}

bool BearingInsertDialog::tapered() const { return m_taper->isChecked(); }
QString BearingInsertDialog::name() const
{
    const int r = m_table->currentRow();
    return (r >= 0 && r < m_rows.size()) ? m_rows.at(r).name : QString();
}
bool BearingInsertDialog::setK5() const { return m_k5->isVisible() && m_k5->isChecked(); }

// ── 도구 ──
BearingInsertTool::BearingInsertTool(QObject *parent) : AbstractTool(parent) {}

void BearingInsertTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_state = PickSegment;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("BEARINGIN (베어링 넣기)"), CommandDock::Command);
    }
    showPrompt();
}

void BearingInsertTool::deactivate()
{
    ShaftPick::clearOverlay(m_scene);
    if (m_scene)
        m_scene->setGhostPath(QPainterPath(), false);
    AbstractTool::deactivate();
}

void BearingInsertTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    if (m_state == PickSegment)
        ctx->commandDock()->showInteractivePrompt(
            tr(" 베어링을 넣을 축 구간을 클릭하세요 (붙일 턱 쪽 끝 가까이, 끝: Esc) : "), {});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" %1 위치: 클릭, 또는 거리 입력 (%2 에서 베어링 옆면까지, 엔터 = 붙임) [기준 턱(S)/끝(E)] : ")
                .arg(m_name, m_fromShoulder ? tr("턱") : tr("구간 끝")),
            {tr("턱(S)"), tr("끝(E)")});
}

double BearingInsertTool::mouseCenter(const QPointF &pos) const
{
    QPointF a0;
    double ang = 0.0;
    QString nm;
    if (!m_scene || !ProjectionView::partFrame(m_scene, m_plan.pick.uuid, a0, ang, nm))
        return centerFor(m_plan, m_width, true, 0.0);
    const double a = qDegreesToRadians(ang);
    const double x = (pos.x() - a0.x()) * std::cos(a) + (pos.y() - a0.y()) * std::sin(a);
    const double lo = m_plan.pick.xFrom + m_width * 0.5, hi = m_plan.pick.xTo - m_width * 0.5;
    if (hi < lo)
        return centerFor(m_plan, m_width, true, 0.0);   // 구간이 베어링보다 짧음 → 턱에 붙임
    return std::max(lo, std::min(hi, x));
}

void BearingInsertTool::placeAtCenter(double centerX)
{
    IAppContext *ctx = getContext();
    ShaftPick::clearOverlay(m_scene);
    if (m_scene)
        m_scene->setGhostPath(QPainterPath(), false);
    const QString placed = insert(ctx, m_scene, m_plan, m_tapered, m_name, m_k5, centerX);
    if (ctx && ctx->commandDock()) {
        // 기준면에서 거리 (보고용)
        const bool fromLeft = (m_fromShoulder == m_plan.leftSide);
        const double dist = fromLeft ? centerX - m_width * 0.5 - m_plan.pick.xFrom
                                     : m_plan.pick.xTo - centerX - m_width * 0.5;
        ctx->commandDock()->logMessage(placed.isEmpty() ? tr("베어링을 넣지 못했습니다.")
                                                        : tr("%1 · 구간 %2 에 베어링 %3 (%4 에서 %5)")
                                                              .arg(m_plan.pick.name).arg(m_plan.pick.seg + 1)
                                                              .arg(placed, m_fromShoulder ? tr("턱") : tr("구간 끝"))
                                                              .arg(QString::number(dist, 'g', 6)),
                                       placed.isEmpty() ? CommandDock::Warning : CommandDock::Info);
    }
    m_state = PickSegment;
    showPrompt();
}

bool BearingInsertTool::processCommand(const QString &command)
{
    if (m_state != PlaceBearing)
        return false;
    const QString c = command.trimmed().toUpper();
    if (c == QLatin1String("S") || c.contains(QLatin1String("(S)"))) {
        m_fromShoulder = true;
        showPrompt();
        return true;
    }
    if (c == QLatin1String("E") || c.contains(QLatin1String("(E)"))) {
        m_fromShoulder = false;
        showPrompt();
        return true;
    }
    bool ok = false;
    const double dist = c.isEmpty() ? 0.0 : c.toDouble(&ok);
    if (!c.isEmpty() && (!ok || dist < 0.0))
        return false;
    placeAtCenter(centerFor(m_plan, m_width, m_fromShoulder, dist));
    return true;
}

double BearingInsertTool::pickTol() const
{
    double scale = 1.0;
    if (m_scene && !m_scene->views().isEmpty()) {
        const double m = std::fabs(m_scene->views().constFirst()->transform().m11());
        if (m > 1e-9)
            scale = m;
    }
    return 6.0 / scale;
}

void BearingInsertTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;
    if (m_state == PlaceBearing) {
        // 베어링 미리보기가 축선을 따라 움직임 (구간 안)
        m_scene->setGhostPath(ghost(m_scene, m_plan, m_tapered, m_name, mouseCenter(pos)), true,
                              QColor(55, 138, 221), 1.2);
        return;
    }
    BearingInsertPlan pl;
    if (planAt(m_scene, pos, pickTol(), pl))
        ShaftPick::showOverlay(m_scene, pl.pick);
    else
        ShaftPick::clearOverlay(m_scene);
}

void BearingInsertTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        ShaftPick::clearOverlay(m_scene);
        if (m_state == PlaceBearing) {            // 위치 단계 취소 → 구간 다시 고르기
            m_state = PickSegment;
            showPrompt();
            return;
        }
        emit toolFinished();
        return;
    }
    AbstractTool::keyPress(event);
}

void BearingInsertTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button == Qt::RightButton) {
        ShaftPick::clearOverlay(m_scene);
        emit toolFinished();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    if (m_state == PlaceBearing) {
        placeAtCenter(mouseCenter(pos));
        return;
    }
    IAppContext *ctx = getContext();
    BearingInsertPlan pl;
    if (!planAt(m_scene, pos, pickTol(), pl)) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("축 외형을 클릭하세요 (기계 Tools 축으로 그린 축)."), CommandDock::Warning);
        return;
    }
    QWidget *parent = QApplication::activeWindow();
    if (candidates(pl, false).isEmpty() && candidates(pl, true).isEmpty()) {
        QMessageBox::information(parent, tr("베어링 넣기"),
                                 tr("안지름 Ø%1 인 베어링이 규격표에 없습니다.")
                                     .arg(num(pl.pick.params.segments.at(pl.pick.seg).diameter)));
        return;
    }
    BearingInsertDialog dlg(pl, parent);
    if (dlg.exec() != QDialog::Accepted || dlg.name().isEmpty()) {
        showPrompt();
        return;
    }
    ShaftPick::clearOverlay(m_scene);
    // 위치 지정 단계로
    m_plan = pl;
    m_tapered = dlg.tapered();
    m_name = dlg.name();
    m_k5 = dlg.setK5();
    m_width = widthOf(m_tapered, m_name);
    m_fromShoulder = true;
    m_state = PlaceBearing;
    m_scene->setGhostPath(ghost(m_scene, m_plan, m_tapered, m_name, mouseCenter(pos)), true, QColor(55, 138, 221), 1.2);
    showPrompt();
}
