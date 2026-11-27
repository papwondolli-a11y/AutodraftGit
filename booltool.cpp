#include "booltool.h"

#include "abstracttool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "layer.h"
#include "layermanager.h"
#include "mechpartdata.h"
#include "myarc.h"
#include "mycircle.h"
#include "extrudefeature.h"
#include "extrudetarget.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "plateview.h"
#include "projectionview.h"
#include "scenesymbolregistry.h"

#include <QApplication>
#include <QBrush>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QPen>
#include <QRadioButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// 미리보기 도형 (저장·실행 취소와 무관한 임시 객체)
void addPath(MyGraphicsScene *scene, QList<QGraphicsItem *> &out, const QPainterPath &path, const QColor &pen,
             double width, Qt::PenStyle style, const QBrush &brush, double z)
{
    if (path.isEmpty())
        return;
    QGraphicsPathItem *it = new QGraphicsPathItem(path);
    QPen p(pen, width, style);
    p.setCosmetic(true);
    it->setPen(p);
    it->setBrush(brush);
    it->setZValue(z);
    it->setAcceptedMouseButtons(Qt::NoButton);
    scene->addItem(it);
    out << it;
}

// 판의 지금 평면 모양
QPainterPath plateShape(MyGraphicsScene *scene, const QString &uuid)
{
    ExtrudeInfo ei;
    if (!ExtrudeFeature::resolve(scene, uuid, ei))
        return QPainterPath();
    return ExtrudeFeature::ringsPath(ei.rings);
}

QString plateText(MyGraphicsScene *scene, const QString &uuid)
{
    ExtrudeInfo ei;
    if (!ExtrudeFeature::resolve(scene, uuid, ei))
        return QString();
    return QStringLiteral("%1 · Z %2 ~ %3").arg(ei.name).arg(ei.x0).arg(ei.x1);
}

// 점을 품은 판 — 부품 합치기에 쓰인 도구 판까지 (풀기용)
QString anyPlateAt(MyGraphicsScene *scene, const QPointF &pos)
{
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude") || !p.value(QStringLiteral("Standalone")).toBool())
            continue;
        if (p.value(QStringLiteral("Bool")).toString().isEmpty() && p.value(QStringLiteral("BoolInto")).toString().isEmpty())
            continue;
        // A 는 합친 모양, B 는 자기 모양 — 둘 다 원래 스케치 공간으로 봄
        ExtrudeInfo ei;
        if (!ExtrudeFeature::resolve(scene, it.key(), ei))
            continue;
        QPainterPath own = ExtrudeFeature::ringsPath(ei.rings);
        if (own.contains(pos))
            return it.key();
    }
    return QString();
}

// 판 고르기 (부품 합치기 도구로 쓰인 판은 PlateView::plateAt 이 빼 줌)
QString pickPlate(MyGraphicsScene *scene, const QPointF &pos) { return PlateView::plateAt(scene, pos, true); }
} // namespace

// ── 설정창 ──
BoolDialog::BoolDialog(const QString &aText, const QString &bText, const QString &aName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("부품 합치기"));
    QVBoxLayout *lay = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();
    form->addRow(tr("A (남김)"), new QLabel(aText, this));
    form->addRow(tr("B (도구)"), new QLabel(bText, this));
    QWidget *ops = new QWidget(this);
    QVBoxLayout *ol = new QVBoxLayout(ops);
    ol->setContentsMargins(0, 0, 0, 0);
    m_union = new QRadioButton(tr("합치기  A ∪ B"), ops);
    m_subtract = new QRadioButton(tr("빼기  A − B"), ops);
    m_intersect = new QRadioButton(tr("겹침 남기기  A ∩ B"), ops);
    m_union->setChecked(true);
    ol->addWidget(m_union);
    ol->addWidget(m_subtract);
    ol->addWidget(m_intersect);
    form->addRow(tr("방법"), ops);
    m_name = new QLineEdit(aName, this);
    form->addRow(tr("이름"), m_name);
    lay->addLayout(form);
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    lay->addWidget(m_note);
    lay->addWidget(new QLabel(tr("스케치 선은 그대로 둡니다 — 선을 고치면 결과도 따라가고, [부품 합치기 풀기]로 되돌릴 수 있습니다."), this));
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(m_union, &QRadioButton::toggled, this, &BoolDialog::onToggled);
    connect(m_subtract, &QRadioButton::toggled, this, &BoolDialog::onToggled);
    connect(m_intersect, &QRadioButton::toggled, this, &BoolDialog::onToggled);
}

int BoolDialog::op() const
{
    if (m_subtract->isChecked())
        return ExtrudeFeature::BoolSubtract;
    if (m_intersect->isChecked())
        return ExtrudeFeature::BoolIntersect;
    return ExtrudeFeature::BoolUnion;
}

QString BoolDialog::partName() const { return m_name->text().trimmed(); }

void BoolDialog::setNote(const QString &text) { m_note->setText(text); }

void BoolDialog::onToggled(bool on)
{
    if (on)
        emit previewChanged();
}

// ── 부품 합치기 ──
BoolTool::BoolTool(QObject *parent) : AbstractTool(parent) {}

void BoolTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_a.clear();
    m_b.clear();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("BOOL3D (부품 합치기)"), CommandDock::Command);
    }
    showPrompt();
}

void BoolTool::deactivate()
{
    clearPreview();
    AbstractTool::deactivate();
}

void BoolTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    if (m_a.isEmpty())
        ctx->commandDock()->showInteractivePrompt(tr(" 남길 판 A 를 클릭하세요 (겹치지 않은 곳) : "), {});
    else
        ctx->commandDock()->showInteractivePrompt(tr(" 도구 판 B 를 클릭하세요 (겹치지 않은 곳) : "), {});
}

void BoolTool::cancelTool()
{
    clearPreview();
    emit toolFinished();
}

void BoolTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void BoolTool::clearPreview()
{
    for (int i = 0; i < m_preview.size(); ++i) {
        if (m_scene)
            m_scene->removeItem(m_preview.at(i));
        delete m_preview.at(i);
    }
    m_preview.clear();
}

void BoolTool::drawPicked()
{
    if (!m_a.isEmpty())
        addPath(m_scene, m_preview, plateShape(m_scene, m_a), QColor(240, 130, 20), 3.0, Qt::SolidLine, Qt::NoBrush, 1e9);
    if (!m_b.isEmpty())
        addPath(m_scene, m_preview, plateShape(m_scene, m_b), QColor(80, 150, 255), 2.0, Qt::DashLine, Qt::NoBrush,
                1e9 + 1);
}

void BoolTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    IAppContext *ctx = getContext();
    const QString id = pickPlate(m_scene, pos);
    if (id.isEmpty() || id == m_a) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(id.isEmpty() ? tr("이 점에 판(단독 돌출)이 없습니다.") : tr("A 와 다른 판을 고르세요."),
                                           CommandDock::Warning);
        return;
    }
    if (m_a.isEmpty()) {
        m_a = id;
        clearPreview();
        drawPicked();
        showPrompt();
        return;
    }
    m_b = id;
    clearPreview();
    drawPicked();
    finish();
}

void BoolTool::updatePreview()
{
    clearPreview();
    if (!m_scene || !m_dialog)
        return;
    QPainterPath result;
    double z0 = 0.0, z1 = 0.0;
    bool exact = true;
    if (ExtrudeFeature::previewBool(m_scene, m_a, m_b, m_dialog->op(), result, z0, z1, exact)) {
        addPath(m_scene, m_preview, result, QColor(150, 155, 165), 1.0, Qt::SolidLine, QBrush(QColor(139, 144, 153, 150)),
                1e9 - 1);
        QString note;
        if (result.isEmpty())
            note = tr("⚠ 결과가 비어 있습니다 (겹친 곳이 없음).");
        else if (exact)
            note = tr("결과: 판 하나 · Z %1 ~ %2 (3D·투상도·Blender 모두 이 모양)").arg(z0).arg(z1);
        else
            note = tr("두 판의 Z 범위가 달라 3D 보기는 두 판을 겹쳐 그리고, Blender 에서 불리언으로 정확히 처리합니다.");
        m_dialog->setNote(note);
    }
    drawPicked();
}

void BoolTool::finish()
{
    IAppContext *ctx = getContext();
    QWidget *parent = QApplication::activeWindow();
    const QString aName = m_scene->symbolRegistry()->symbolInfo(m_a).name;
    BoolDialog dlg(plateText(m_scene, m_a), plateText(m_scene, m_b), aName, parent);
    m_dialog = &dlg;
    connect(&dlg, &BoolDialog::previewChanged, this, &BoolTool::updatePreview);
    updatePreview();
    const bool ok = dlg.exec() == QDialog::Accepted;
    m_dialog = nullptr;
    clearPreview();
    if (!ok) {
        cancelTool();
        return;
    }
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(tr("부품 합치기"));
    QString err;
    const bool done = ExtrudeFeature::setBool(m_scene, undo, m_a, m_b, dlg.op(), dlg.partName(), &err);
    if (done) {
        ProjectionView::regeneratePart(ctx, m_scene, m_a);   // A 의 투상도는 새 모양으로
        ProjectionView::regeneratePart(ctx, m_scene, m_b);
    }
    if (undo)
        undo->endMacro();
    if (!done)
        QMessageBox::warning(parent, tr("부품 합치기"), err);
    else if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("부품 합치기 완료 — 3D 는 부품 3D 보기 [새로고침]. 되돌리려면 [부품 합치기 풀기]."),
                                       CommandDock::Info);
    cancelTool();
}

// ── 합친 판 오른쪽 클릭 메뉴 ──
int BoolMenu::unbool(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid)
{
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(QObject::tr("부품 합치기 풀기"));
    ProjectionView::pushApplyConcealed(undo, scene);
    QString a = uuid;                                   // 결과 외형선은 A 기준
    const QString into = scene->symbolRegistry()->symbolInfo(uuid).props.value(QStringLiteral("BoolInto")).toString();
    if (!into.isEmpty())
        a = into;
    removeOutline(ctx, scene, a);
    const QStringList touched = ExtrudeFeature::clearBool(scene, undo, uuid);
    ProjectionView::pushApplyConcealed(undo, scene);    // 스케치 선을 껐던 것이면 다시 보이게
    for (int i = 0; i < touched.size(); ++i)
        ProjectionView::regeneratePart(ctx, scene, touched.at(i));
    if (undo)
        undo->endMacro();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(QObject::tr("판 %1개를 원래대로 풀었습니다.").arg(touched.size()), CommandDock::Info);
    return int(touched.size());
}

bool BoolMenu::exec(IAppContext *ctx, MyGraphicsScene *scene, const QPointF &pos)
{
    if (!scene)
        return false;
    const QString id = anyPlateAt(scene, pos);
    if (id.isEmpty())
        return false;
    // 메뉴는 A(합친 판) 기준
    QString a = id;
    const QString into = scene->symbolRegistry()->symbolInfo(id).props.value(QStringLiteral("BoolInto")).toString();
    if (!into.isEmpty() && scene->symbolRegistry()->hasSymbol(into))
        a = into;
    const bool hidden = ExtrudeFeature::sketchHidden(scene, a);
    QMenu menu;
    menu.addSection(scene->symbolRegistry()->symbolInfo(a).name);
    QAction *show = menu.addAction(QObject::tr("스케치선 켜기"));
    QAction *hide = menu.addAction(QObject::tr("스케치선 끄기"));
    menu.addSeparator();
    QAction *split = menu.addAction(QObject::tr("합치기 풀기"));
    show->setEnabled(hidden);
    hide->setEnabled(!hidden);
    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return true;
    if (chosen == split) {
        unbool(ctx, scene, a);
        return true;
    }
    setSketch(ctx, scene, a, chosen == hide);
    return true;
}

namespace {
QString outlineTag(const QString &a) { return QStringLiteral("boolout:") + a; }

// 숨긴 스케치의 원·호 (씬 좌표) — 결과 외형에서 그 위에 놓인 부분은 원·호로 다시 그린다
struct SketchCircle {
    QPointF c;
    double r = 0.0;
};

// 꼭짓점이 원 위인가
bool onCircle(const QPointF &p, const SketchCircle &sc)
{
    return std::fabs(QLineF(p, sc.c).length() - sc.r) <= std::max(0.01, 0.002 * sc.r);
}

// 변이 원의 짧은 현인가 (양 끝이 원 위 + 중심각 10° 이하)
bool chordOf(const QPointF &p, const QPointF &q, const SketchCircle &sc)
{
    return onCircle(p, sc) && onCircle(q, sc) && QLineF(p, q).length() <= 2.0 * sc.r * std::sin(qDegreesToRadians(7.5));
}

QPointF projectOn(const QPointF &p, const SketchCircle &sc)
{
    const QLineF l(sc.c, p);
    return l.length() < 1e-12 ? p : sc.c + (p - sc.c) * (sc.r / l.length());
}

// 외형 한 조각 (선 또는 호) 추가
void addOutlineItem(IAppContext *ctx, MyGraphicsScene *scene, QGraphicsItem *it, const QString &layer, const QString &tag)
{
    if (!it)
        return;
    if (CadItem *cad = dynamic_cast<CadItem *>(it))
        cad->setLayerName(layer);
    it->setData(MECH_VIEW_ID, tag);                     // 사용자 스케치가 아님 (숨김·공간 찾기 대상에서 빠짐)
    it->setParentItem(scene->getLayerRoot(layer));
    if (it->scene() == scene)
        scene->removeItem(it);
    if (ctx->undoStack())
        ctx->undoStack()->push(new AddEntityCommand(scene, it));
    else
        scene->addItem(it);
}

// 고리 하나를 외형선으로 (숨긴 스케치 원·호의 현 묶음은 원·호로 되살림)
void drawRing(IAppContext *ctx, MyGraphicsScene *scene, QPolygonF g, const QList<SketchCircle> &circles, const QPen &pen,
              const QString &layer, const QString &tag)
{
    const int n = g.size();
    if (n < 3)
        return;
    // 변마다 어느 원의 현인가 (-1 = 직선)
    QList<int> on;
    for (int k = 0; k < n; ++k) {
        int c = -1;
        for (int t = 0; t < circles.size() && c < 0; ++t)
            if (chordOf(g.at(k), g.at((k + 1) % n), circles.at(t)))
                c = t;
        on << c;
    }
    // 고리 전체가 한 원 → 원 하나
    bool whole = on.at(0) >= 0;
    for (int k = 1; k < n && whole; ++k)
        whole = on.at(k) == on.at(0);
    if (whole) {
        const SketchCircle &sc = circles.at(on.at(0));
        addOutlineItem(ctx, scene, ctx->entityFactory()->createCircle(sc.c, sc.r, pen), layer, tag);
        return;
    }
    // 원 위 꼭짓점은 원 위로 정확히 (교점이 현 위에 놓여 생기는 작은 어긋남 제거)
    for (int k = 0; k < n; ++k) {
        const int c = on.at(k) >= 0 ? on.at(k) : on.at((k + n - 1) % n);
        if (c >= 0)
            g[k] = projectOn(g.at(k), circles.at(c));
    }
    // 직선이 시작되는 변에서 출발해 같은 원의 현 묶음은 호 하나로
    int start = 0;
    while (start < n && on.at(start) >= 0 && on.at((start + n - 1) % n) == on.at(start))
        ++start;
    for (int m = 0; m < n;) {
        const int k = (start + m) % n;
        if (on.at(k) < 0) {
            const QLineF seg(g.at(k), g.at((k + 1) % n));
            if (seg.length() > 1e-9)
                addOutlineItem(ctx, scene, ctx->entityFactory()->createLine(seg, pen), layer, tag);
            ++m;
            continue;
        }
        int len = 1;
        while (m + len < n && on.at((start + m + len) % n) == on.at(k))
            ++len;
        const SketchCircle &sc = circles.at(on.at(k));
        const QPointF p0 = g.at(k), p1 = g.at((k + len) % n), pm = g.at((k + (len + 1) / 2) % n);
        // 고리 진행 방향이 원의 반시계면 p0 → p1, 시계면 거꾸로 (호는 반시계로 그림)
        const QPointF u = p0 - sc.c, w = (len > 1 ? pm : (p0 + p1) * 0.5) - sc.c;
        const bool ccw = u.x() * w.y() - u.y() * w.x() > 0.0;
        // 시작각·반시계 회전각 (도)
        const QPointF s0 = (ccw ? p0 : p1) - sc.c, s1 = (ccw ? p1 : p0) - sc.c;
        const double a0 = qRadiansToDegrees(std::atan2(s0.y(), s0.x()));
        double span = qRadiansToDegrees(std::atan2(s1.y(), s1.x())) - a0;
        while (span <= 0.0)
            span += 360.0;
        addOutlineItem(ctx, scene, ctx->entityFactory()->createArc(sc.c, sc.r, a0 < 0.0 ? a0 + 360.0 : a0, span, pen),
                       layer, tag);
        m += len;
    }
}

// 경로 → 고리들 (닫는 중복점·겹친 점·일직선 중간점 제거 — 합집합 계산이 변 위에 남기는 점)
QList<QPolygonF> pathRings(const QPainterPath &path, double tol)
{
    QList<QPolygonF> out;
    const QList<QPolygonF> polys = path.simplified().toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i) {
        QPolygonF g;
        for (int k = 0; k < polys.at(i).size(); ++k)
            if (g.isEmpty() || QLineF(g.last(), polys.at(i).at(k)).length() > tol)
                g << polys.at(i).at(k);
        while (g.size() > 1 && QLineF(g.first(), g.last()).length() <= tol)
            g.removeLast();
        bool changed = true;
        while (changed && g.size() > 3) {
            changed = false;
            for (int k = 0; k < g.size(); ++k) {
                const QPointF a = g.at((k + g.size() - 1) % g.size()), b = g.at(k), c = g.at((k + 1) % g.size());
                const QPointF d1 = b - a, d2 = c - b;
                const double l1 = std::hypot(d1.x(), d1.y()), l2 = std::hypot(d2.x(), d2.y());
                if (l1 < 1e-12 || l2 < 1e-12)
                    continue;
                const double cr = (d1.x() * d2.y() - d1.y() * d2.x()) / (l1 * l2);
                const double dt = (d1.x() * d2.x() + d1.y() * d2.y()) / (l1 * l2);
                if (std::fabs(cr) < 1e-6 && dt > 0.0) {         // 일직선으로 이어지는 중간점
                    g.remove(k);
                    changed = true;
                    break;
                }
            }
        }
        if (g.size() >= 3)
            out << g;
    }
    return out;
}

// 점이 고리들의 변 위인가
bool onRings(const QList<QPolygonF> &rings, const QPointF &p, double tol)
{
    for (int r = 0; r < rings.size(); ++r) {
        const QPolygonF &g = rings.at(r);
        for (int k = 0; k < g.size(); ++k) {
            const QPointF a = g.at(k), d = g.at((k + 1) % g.size()) - a;
            const double l2 = d.x() * d.x() + d.y() * d.y();
            const double t = l2 < 1e-24 ? 0.0 : qBound(0.0, ((p - a).x() * d.x() + (p - a).y() * d.y()) / l2, 1.0);
            if (QLineF(p, a + d * t).length() <= tol)
                return true;
        }
    }
    return false;
}

// 단차·포켓 선: rings 의 변 중 region 안쪽(onBoundary 면 region 경계 위 포함)이고 결과 외형(outer) 위가 아닌 부분만
//   고리 전체가 남으면 drawRing 으로 (원·호 되살림), 아니면 남는 조각만 선으로
void drawInnerEdges(IAppContext *ctx, MyGraphicsScene *scene, const QList<QPolygonF> &rings, const QPainterPath &region,
                    const QList<QPolygonF> &regionRings, bool onBoundary, const QList<QPolygonF> &outer,
                    const QList<SketchCircle> &circles, const QPen &pen, const QString &layer, const QString &tag,
                    double tol)
{
    for (int r = 0; r < rings.size(); ++r) {
        const QPolygonF &g = rings.at(r);
        const int n = g.size();
        QList<QLineF> keep;
        bool all = true;
        for (int k = 0; k < n; ++k) {
            const QPointF p = g.at(k), q = g.at((k + 1) % n);
            const QPointF d = q - p;
            // region 경계와 만나는 곳에서 변을 나눔
            QList<double> ts{0.0, 1.0};
            for (int rr = 0; rr < regionRings.size(); ++rr) {
                const QPolygonF &h = regionRings.at(rr);
                for (int j = 0; j < h.size(); ++j) {
                    const QPointF a = h.at(j), e = h.at((j + 1) % h.size()) - a;
                    const double den = d.x() * e.y() - d.y() * e.x();
                    if (std::fabs(den) < 1e-12)
                        continue;                       // 평행 (겹치는 변은 조각 가운데 점 판정으로 처리)
                    const QPointF w = a - p;
                    const double t = (w.x() * e.y() - w.y() * e.x()) / den;
                    const double s = (w.x() * d.y() - w.y() * d.x()) / den;
                    if (t > 1e-9 && t < 1.0 - 1e-9 && s >= -1e-9 && s <= 1.0 + 1e-9)
                        ts << t;
                }
            }
            std::sort(ts.begin(), ts.end());
            QPointF runStart;
            bool inRun = false;
            for (int i = 0; i + 1 < ts.size(); ++i) {
                if (ts.at(i + 1) - ts.at(i) < 1e-9)
                    continue;
                const QPointF a = p + d * ts.at(i), b = p + d * ts.at(i + 1), m = (a + b) * 0.5;
                const bool onB = onRings(regionRings, m, tol);
                const bool ok = (onB ? onBoundary : region.contains(m)) && !onRings(outer, m, tol);
                if (!ok)
                    all = false;
                if (ok && !inRun) {
                    runStart = a;
                    inRun = true;
                } else if (!ok && inRun) {
                    keep << QLineF(runStart, a);
                    inRun = false;
                }
                if (ok && i + 2 == ts.size())
                    keep << QLineF(runStart, b);
            }
        }
        if (all) {
            drawRing(ctx, scene, g, circles, pen, layer, tag);
            continue;
        }
        for (int i = 0; i < keep.size(); ++i)
            if (keep.at(i).length() > tol)
                addOutlineItem(ctx, scene, ctx->entityFactory()->createLine(keep.at(i), pen), layer, tag);
    }
}
} // namespace

void BoolMenu::removeOutline(IAppContext *ctx, MyGraphicsScene *scene, const QString &a)
{
    const QString tag = outlineTag(a);
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i)->data(MECH_VIEW_ID).toString() != tag)
            continue;
        if (ctx && ctx->undoStack())
            ctx->undoStack()->push(new RemoveEntityCommand(scene, items.at(i)));
        else
            scene->removeItem(items.at(i));
    }
}

void BoolMenu::drawOutline(IAppContext *ctx, MyGraphicsScene *scene, const QString &a)
{
    if (!ctx || !ctx->entityFactory())
        return;
    ExtrudeInfo ei;
    if (!ExtrudeFeature::resolve(scene, a, ei))
        return;
    // A 에 묶인 B 들. Z 가 같아 A 모양에 이미 계산해 넣은 것(Consumed)은 빼고,
    //   Z 가 다른 합침(Merged)·빼기(Cutter)는 A 모양에 없으므로 여기서 결과 외형을 만든다
    //   (예전에는 A 모양만 그려 Z 가 다른 B 의 외형이 통째로 빠졌음)
    QList<ExtrudeInfo> others;
    {
        const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
        for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
            if (it.value().type != QLatin1String("MechExtrude") ||
                it.value().props.value(QStringLiteral("BoolInto")).toString() != a)
                continue;
            ExtrudeInfo bi;
            if (ExtrudeFeature::resolve(scene, it.key(), bi) && !bi.rings.isEmpty() &&
                (bi.boolRole == ExtrudeInfo::BoolMerged || bi.boolRole == ExtrudeInfo::BoolCutter))
                others << bi;
        }
    }
    const QPainterPath aPath = ExtrudeFeature::ringsPath(ei.rings);
    QRectF all = aPath.boundingRect();
    for (int i = 0; i < others.size(); ++i)
        all = all.united(ExtrudeFeature::ringsPath(others.at(i).rings).boundingRect());
    const double tol = std::max(1e-3, 1e-5 * std::max(all.width(), all.height()));
    const double zTol = 1e-3;
    // 보는 쪽(+Z)에서 본 바깥 외형: 합침은 더하고, A 의 두께를 통째로 뚫는 빼기는 뺀다
    QPainterPath outerPath = aPath;
    for (int i = 0; i < others.size(); ++i) {
        const ExtrudeInfo &b = others.at(i);
        const QPainterPath bp = ExtrudeFeature::ringsPath(b.rings);
        if (b.boolRole == ExtrudeInfo::BoolMerged)
            outerPath = outerPath.united(bp);
        else if (b.x0 <= ei.x0 + zTol && b.x1 >= ei.x1 - zTol)
            outerPath = outerPath.subtracted(bp);
    }
    const QList<QPolygonF> outerRings = others.isEmpty() ? ei.rings : pathRings(outerPath, tol);
    // 레이어 = 숨긴 스케치 선의 레이어 (없으면 현재 레이어)
    QString layer;
    const QRectF box = all.adjusted(-1.0, -1.0, 1.0, 1.0);
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size() && layer.isEmpty(); ++i) {
        const CadItem *ci = dynamic_cast<const CadItem *>(items.at(i));
        if (ci && items.at(i)->data(MECH_CONCEALED_HOLE).toBool() && box.contains(ci->sceneBoundingRect()))
            layer = ci->layerName();
    }
    if (layer.isEmpty() && ctx->layerManager())
        if (Layer *cl = ctx->layerManager()->getCurrentLayer())
            layer = cl->name();
    const QPen pen = AbstractTool::layerPenFor(ctx, layer);
    const QString tag = outlineTag(a);
    // 숨긴 스케치의 원·호
    QList<SketchCircle> circles;
    for (int i = 0; i < items.size(); ++i) {
        const CadItem *ci = dynamic_cast<const CadItem *>(items.at(i));
        if (!ci || !items.at(i)->data(MECH_CONCEALED_HOLE).toBool() || !ci->getEntityData())
            continue;
        const MyEntity *en = ci->getEntityData();
        SketchCircle sc;
        if (en->type() == EntityType::Circle) {
            const MyCircle *mc = static_cast<const MyCircle *>(en);
            sc.c = ci->sceneTransform().map(QPointF(mc->center.x, mc->center.y));
            sc.r = mc->radius;
        } else if (en->type() == EntityType::Arc) {
            const MyArc *ma = static_cast<const MyArc *>(en);
            sc.c = ci->sceneTransform().map(QPointF(ma->center.x, ma->center.y));
            sc.r = ma->radius;
        } else {
            continue;
        }
        if (sc.r > 1e-9 && box.intersects(QRectF(sc.c.x() - sc.r, sc.c.y() - sc.r, 2.0 * sc.r, 2.0 * sc.r)))
            circles << sc;
    }
    // ① 바깥 외형
    for (int r = 0; r < outerRings.size(); ++r)
        drawRing(ctx, scene, outerRings.at(r), circles, pen, layer, tag);
    // ② 외형 안쪽에 보이는 모서리
    for (int i = 0; i < others.size(); ++i) {
        const ExtrudeInfo &b = others.at(i);
        const QPainterPath bp = ExtrudeFeature::ringsPath(b.rings);
        if (b.boolRole == ExtrudeInfo::BoolMerged) {
            // 윗면 높이가 다르면 단차 모서리 = 더 높은 판의 윤곽 중 낮은 판 위(경계 포함)에 놓인 부분
            //   윗면이 같으면 두 판 사이 선은 한 평면 안이라 보이지 않음
            if (std::fabs(b.x1 - ei.x1) <= zTol)
                continue;
            const bool bHigher = b.x1 > ei.x1;
            drawInnerEdges(ctx, scene, bHigher ? b.rings : ei.rings, bHigher ? aPath : bp,
                           bHigher ? ei.rings : b.rings, true, outerRings, circles, pen, layer, tag, tol);
        } else {
            // 빼기: A 윗면까지 열린 포켓이면 그 윤곽 중 A 안쪽 부분 (통째로 뚫는 빼기는 이미 바깥 외형에 들어감)
            const bool through = b.x0 <= ei.x0 + zTol && b.x1 >= ei.x1 - zTol;
            if (through || b.x1 < ei.x1 - zTol)
                continue;                               // 관통 · 속에 묻힌 빈 공간 (위에서 안 보임)
            drawInnerEdges(ctx, scene, b.rings, aPath, ei.rings, false, outerRings, circles, pen, layer, tag, tol);
        }
    }
}

void BoolMenu::deletePart(IAppContext *ctx, MyGraphicsScene *scene, const QString &a)
{
    if (!ctx || !scene || !scene->symbolRegistry()->hasSymbol(a))
        return;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    QUndoStack *undo = ctx->undoStack();
    // A 와 그 B 들
    QStringList group{a};
    const QMap<QString, SymbolInfo> all = reg->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it)
        if (it.value().type == QLatin1String("MechExtrude") &&
            it.value().props.value(QStringLiteral("BoolInto")).toString() == a)
            group << it.key();
    // 숨긴 스케치 선 (기록을 지우기 전에 모양으로 찾음). 숨겨진 선만 — 다른 판과 같이 쓰는 보이는 선은 남김
    QSet<QGraphicsItem *> sketch;
    for (int i = 0; i < group.size(); ++i)
        sketch.unite(ExtrudeFeature::sketchItemsOf(scene, group.at(i)));
    ProjectionView::pushApplyConcealed(undo, scene);
    for (QSet<QGraphicsItem *>::const_iterator s = sketch.constBegin(); s != sketch.constEnd(); ++s) {
        if (!(*s)->data(MECH_CONCEALED_HOLE).toBool())
            continue;
        if (undo)
            undo->push(new RemoveEntityCommand(scene, *s));
        else
            scene->removeItem(*s);
    }
    // 남은 외형선(일부만 지운 경우는 여기 오지 않지만 안전하게)
    removeOutline(ctx, scene, a);
    // 기록: B 먼저, A 나중 (A 의 투상도·붙은 돌기·파내기도 함께 지워짐)
    for (int i = group.size() - 1; i >= 0; --i)
        if (reg->hasSymbol(group.at(i)))
            ExtrudeTargets::removeRecord(ctx, scene, group.at(i));
    ProjectionView::pushApplyConcealed(undo, scene);
    if (ctx->commandDock())
        ctx->commandDock()->logMessage(QObject::tr("합친 부품 '%1' 을(를) 지웠습니다 (판 %2개).")
                                           .arg(all.value(a).name)
                                           .arg(group.size()),
                                       CommandDock::Info);
}

void BoolMenu::setSketch(IAppContext *ctx, MyGraphicsScene *scene, const QString &a, bool hide)
{
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(hide ? QObject::tr("스케치선 끄기") : QObject::tr("스케치선 켜기"));
    ProjectionView::pushApplyConcealed(undo, scene);
    removeOutline(ctx, scene, a);
    ExtrudeFeature::setSketchHidden(scene, undo, a, hide);
    ProjectionView::pushApplyConcealed(undo, scene);
    if (hide)
        drawOutline(ctx, scene, a);                     // 결과 외형선만 보이게
    if (undo)
        undo->endMacro();
}

// ── 부품 합치기 풀기 ──
UnboolTool::UnboolTool(QObject *parent) : AbstractTool(parent) {}

void UnboolTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("BOOL3DCLEAR (부품 합치기 풀기)"), CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 합친 판을 클릭하세요 : "), {});
    }
}

void UnboolTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit toolFinished();
        return;
    }
    AbstractTool::keyPress(event);
}

void UnboolTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button != Qt::LeftButton) {
        emit toolFinished();
        return;
    }
    IAppContext *ctx = getContext();
    const QString id = anyPlateAt(m_scene, pos);
    if (id.isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("이 점에 합친 판이 없습니다."), CommandDock::Warning);
        return;
    }
    BoolMenu::unbool(ctx, m_scene, id);
    emit toolFinished();
}
