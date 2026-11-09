#include "dimbasereftool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "dimmergetool.h"
#include "iappcontext.h"
#include "lineardimensionitem.h"
#include "modifyentitycommand.h"
#include "mydimension.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include <QMap>
#include <QUndoStack>
#include <QVariant>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

const double kEps = 1e-3;

// 치수 하나의 계산용 정보 (치수 방향 투영 기준)
struct DimInfo {
    LinearDimensionItem *item = nullptr;
    MyVector pLo;       // 치수 방향으로 가장 작은 쪽 점
    MyVector pHi;       // 가장 큰 쪽 점
    double tLo = 0.0;
    double tHi = 0.0;
    double row = 0.0;   // 치수선의 법선축 절대 위치
    double startNormal = 0.0;
};

// 선택 치수를 큰 쪽 끝(tHi) 순으로 정렬 (람다 대신 함수 객체)
struct HighLess {
    bool operator()(const DimInfo &a, const DimInfo &b) const { return a.tHi < b.tHi; }
};

bool readInfo(LinearDimensionItem *li, const MyVector &dir, const MyVector &normal, DimInfo &out) {
    const MyDimension *d = dynamic_cast<const MyDimension *>(li->getEntityData());
    if (!d) return false;
    MyVector di;
    if (!DimMergeTool::dimDirection(d, di)) return false;
    if (std::abs(MyMath::dotProduct(dir, MyVector(-di.y, di.x))) > kEps) return false; // 평행해야 함

    const double t1 = MyMath::dotProduct(d->start, dir);
    const double t2 = MyMath::dotProduct(d->end, dir);
    out.item = li;
    if (t1 <= t2) { out.pLo = d->start; out.pHi = d->end;   out.tLo = t1; out.tHi = t2; }
    else          { out.pLo = d->end;   out.pHi = d->start; out.tLo = t2; out.tHi = t1; }
    out.row = MyMath::dotProduct(d->dimLine, normal);
    out.startNormal = MyMath::dotProduct(d->start, normal);
    return true;
}

bool touches(const DimInfo &a, const DimInfo &b) {
    return a.tLo <= b.tHi + kEps && a.tHi >= b.tLo - kEps;
}

bool inList(const QList<DimInfo> &list, const LinearDimensionItem *item) {
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).item == item) return true;
    return false;
}

} // namespace

DimBaseRefTool::DimBaseRefTool(QObject *parent) : AbstractTool(parent) {}

DimBaseRefTool::~DimBaseRefTool() { resetTool(); }

void DimBaseRefTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->showInteractivePrompt(
            tr(" 기준점 치수로 바꿀 치수선을 선택하세요 (엔터/우클릭:실행) : "), {});
    }
}

void DimBaseRefTool::deactivate() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    resetTool();
    AbstractTool::deactivate();
}

void DimBaseRefTool::resetTool() {
    m_selected.clear();
    if (m_scene) m_scene->clearOverlay();
}

void DimBaseRefTool::finishTool() {
    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void DimBaseRefTool::onCancel() { finishTool(); }

void DimBaseRefTool::updateSelectionGhost() {
    if (!m_scene) return;
    QPainterPath sel;
    for (int i = 0; i < m_selected.size(); ++i) sel.addPath(m_selected.at(i)->geometryPath());
    m_scene->setGhostPath(sel, true, Qt::cyan, 1.0);
}

void DimBaseRefTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    if (button == Qt::RightButton) {
        execute();
        return;
    }
    if (button != Qt::LeftButton) return;

    LinearDimensionItem *dim = dynamic_cast<LinearDimensionItem *>(itemAtPos(pos));
    if (!dim) return;

    if (m_selected.contains(dim)) m_selected.removeAll(dim); // 다시 클릭하면 선택 해제
    else                          m_selected.append(dim);
    updateSelectionGhost();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 치수선 %1개 선택됨 - 추가 선택 또는 엔터/우클릭:실행 : ").arg(m_selected.size()), {});
    }
}

bool DimBaseRefTool::processCommand(const QString &command) {
    if (command.trimmed().isEmpty()) {
        execute();
        return true;
    }
    return false;
}

void DimBaseRefTool::execute() {
    IAppContext *ctx = getContext();
    CommandDock *dock = ctx ? ctx->commandDock() : nullptr;

    if (m_selected.size() < 2) {
        if (dock) dock->logMessage(tr("치수선을 2개 이상 선택해야 합니다."), CommandDock::Info);
        if (m_selected.isEmpty()) finishTool();
        return;
    }

    // 기준 방향은 첫 번째 선택 치수를 따른다.
    const MyDimension *d0 = dynamic_cast<const MyDimension *>(m_selected.first()->getEntityData());
    MyVector dir;
    if (!d0 || !DimMergeTool::dimDirection(d0, dir)) { finishTool(); return; }
    const MyVector normal(-dir.y, dir.x);

    // 바깥쪽 방향(치수선이 대상물에서 멀어지는 쪽): 첫 치수의 시작점 → 치수선 방향
    const double hFirst = MyMath::dotProduct(d0->dimLine - d0->start, normal);
    const double sign = (hFirst >= 0.0) ? 1.0 : -1.0;

    // 선택 치수 정보 수집 (방향이 다른 치수가 있으면 중단)
    QList<DimInfo> sel;
    for (int i = 0; i < m_selected.size(); ++i) {
        DimInfo info;
        if (!readInfo(m_selected.at(i), dir, normal, info)) {
            if (dock) dock->logMessage(tr("방향이 다른 치수선이 있어 기준 치수로 바꿀 수 없습니다."), CommandDock::Info);
            finishTool();
            return;
        }
        sel.append(info);
    }
    std::sort(sel.begin(), sel.end(), HighLess());

    // 공통 기준점 = 선택 치수의 가장 작은 쪽 점, 선택 치수 중 가장 안쪽 치수선 위치가 시작 단
    MyVector pO = sel.first().pLo;
    double tMin = sel.first().tLo;
    double chainAbs = sel.first().row;
    for (int i = 1; i < sel.size(); ++i) {
        if (sel.at(i).tLo < tMin) { tMin = sel.at(i).tLo; pO = sel.at(i).pLo; }
        if ((sel.at(i).row - chainAbs) * sign < 0.0) chainAbs = sel.at(i).row;
    }
    for (int i = 0; i < sel.size(); ++i) {
        if (sel.at(i).tHi - tMin < kEps) {
            if (dock) dock->logMessage(tr("길이가 0인 치수선이 있습니다."), CommandDock::Info);
            finishTool();
            return;
        }
    }

    const int n = sel.size();
    const double spacing = m_scene->dimensionStyle().baselineSpacing * m_scene->drawingScale();
    const double shift = (n - 1) * spacing * sign;   // 바깥 치수가 밀려날 거리

    // 씬의 나머지 선형 치수 중 같은 방향인 것을 모은다
    QList<DimInfo> others;
    const QList<QGraphicsItem *> all = m_scene->items();
    for (int i = 0; i < all.size(); ++i) {
        LinearDimensionItem *li = dynamic_cast<LinearDimensionItem *>(all.at(i));
        if (!li || m_selected.contains(li)) continue;
        DimInfo info;
        if (readInfo(li, dir, normal, info)) others.append(info);
    }

    // 같은 줄에서 선택 치수와 이어지는 치수(25, 12)를 찾는다 (끝점이 맞닿는 연쇄)
    QList<DimInfo> group = sel;
    QList<DimInfo> mates;
    bool grew = true;
    while (grew) {
        grew = false;
        for (int i = 0; i < others.size(); ++i) {
            const DimInfo &c = others.at(i);
            if (inList(group, c.item)) continue;
            if (std::abs(c.row - chainAbs) > kEps) continue;
            bool hit = false;
            for (int g = 0; g < group.size() && !hit; ++g) hit = touches(c, group.at(g));
            if (hit) { group.append(c); mates.append(c); grew = true; }
        }
    }
    double uLo = group.first().tLo, uHi = group.first().tHi;
    for (int i = 1; i < group.size(); ++i) {
        uLo = std::min(uLo, group.at(i).tLo);
        uHi = std::max(uHi, group.at(i).tHi);
    }

    // 치수선이 더 바깥에 있고 같은 범위에 걸친 치수(전체 82)도 함께 밀어낸다
    QList<DimInfo> outer;
    for (int i = 0; i < others.size(); ++i) {
        const DimInfo &c = others.at(i);
        if (inList(group, c.item)) continue;
        if ((c.row - chainAbs) * sign <= kEps) continue;
        if (c.tLo < uHi - kEps && c.tHi > uLo + kEps) outer.append(c);
    }

    if (ctx && ctx->undoStack()) ctx->undoStack()->beginMacro(tr("치수기준"));

    // 1) 선택 치수 → 기준점 치수 (작은 치수가 안쪽 단)
    for (int r = 0; r < n; ++r) {
        LinearDimensionItem *li = sel.at(r).item;
        const double rowAbs = chainAbs + sign * spacing * r;
        const MyVector dimLine = pO + normal * (rowAbs - MyMath::dotProduct(pO, normal));

        QMap<QString, QVariant> oldProps = li->getProperties();
        QMap<QString, QVariant> np = oldProps;
        np["StartX"] = pO.x;                np["StartY"] = pO.y;
        np["EndX"] = sel.at(r).pHi.x;       np["EndY"] = sel.at(r).pHi.y;
        np["DimLineX"] = dimLine.x;         np["DimLineY"] = dimLine.y;
        np["UseFixedAngle"] = true;
        np["FixedAngle"] = qRadiansToDegrees(qAtan2(dir.y, dir.x));
        np["Text"] = QString();             // 측정값이 바뀌므로 자동 문자로
        np["ExtChainH1"] = 0.0;             np["ExtChainH2"] = 0.0;
        np["UseCustomTextPos"] = false;
        np["FlipArrow1"] = false;           np["FlipArrow2"] = false;
        // 공통 기준점의 치수보조선은 가장 바깥 치수만 그린다 (나머지는 겹치므로 숨김)
        np["HideExt1"] = (r != n - 1);
        np["HideExt2"] = false;

        if (ctx && ctx->undoStack()) ctx->undoStack()->push(new ModifyEntityCommand(li, oldProps, np));
        else {
            const QList<QString> keys = np.keys();
            for (int k = 0; k < keys.size(); ++k) li->setProperty(keys.at(k), np.value(keys.at(k)));
        }
    }

    // 2) 이어지는 치수와 바깥 치수를 단 수만큼 바깥으로 이동
    QList<DimInfo> movers;
    movers.append(mates);
    movers.append(outer);
    for (int i = 0; i < movers.size(); ++i) {
        LinearDimensionItem *li = movers.at(i).item;
        const MyDimension *dm = dynamic_cast<const MyDimension *>(li->getEntityData());
        if (!dm) continue;
        const MyVector newLine = dm->dimLine + normal * shift;

        QMap<QString, QVariant> oldProps = li->getProperties();
        QMap<QString, QVariant> np = oldProps;
        np["DimLineX"] = newLine.x;
        np["DimLineY"] = newLine.y;
        if (dm->useCustomTextPos) {
            const MyVector tp = dm->customTextPos + normal * shift;
            np["CustomTextPosX"] = tp.x;
            np["CustomTextPosY"] = tp.y;
        }
        if (ctx && ctx->undoStack()) ctx->undoStack()->push(new ModifyEntityCommand(li, oldProps, np));
        else {
            const QList<QString> keys = np.keys();
            for (int k = 0; k < keys.size(); ++k) li->setProperty(keys.at(k), np.value(keys.at(k)));
        }
    }

    if (ctx && ctx->undoStack()) ctx->undoStack()->endMacro();

    if (dock) {
        dock->logMessage(tr("기준점 치수 %1개로 변경하고 치수 %2개를 이동했습니다.")
                             .arg(n).arg(movers.size()), CommandDock::Info);
    }
    finishTool();
}
