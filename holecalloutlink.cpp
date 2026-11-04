#include "holecalloutlink.h"
#include "cadcommands.h"
#include "holecalloutformatter.h"
#include "holedata.h"
#include "holeitem.h"
#include "holesync.h"
#include "iappcontext.h"
#include "leaderitem.h"
#include "mygraphicsscene.h"
#include <QMessageBox>
#include <QPushButton>
#include <QUndoStack>
#include <QUuid>

namespace {

// 구멍 스펙 필드 ↔ HoleItem 속성 키. 위치·레이어·표시 옵션은 콜아웃 텍스트에 없으므로 제외.
const char *const kSpecKeys[] = {
    "IsThreaded", "ThreadSpec", "ThreadClass", "Diameter", "ToleranceClass",
    "ThroughHole", "Depth", "ThreadDepth",
    "HasCounterbore", "CbDiameter", "CbDepth",
    "HasCountersink", "CsDiameter", "CsAngle",
};

QVariant specValue(const MyHole &h, const QString &key) {
    if (key == "IsThreaded") return h.isThreaded;
    if (key == "ThreadSpec") return h.threadSpec;
    if (key == "ThreadClass") return h.threadClass;
    if (key == "Diameter") return h.diameter;
    if (key == "ToleranceClass") return h.toleranceClass;
    if (key == "ThroughHole") return h.throughHole;
    if (key == "Depth") return h.depth;
    if (key == "ThreadDepth") return h.threadDepth;
    if (key == "HasCounterbore") return h.hasCounterbore;
    if (key == "CbDiameter") return h.cbDiameter;
    if (key == "CbDepth") return h.cbDepth;
    if (key == "HasCountersink") return h.hasCountersink;
    if (key == "CsDiameter") return h.csDiameter;
    if (key == "CsAngle") return h.csAngle;
    return QVariant();
}

QUndoStack *undoStackOf(IAppContext *ctx) {
    return ctx ? ctx->undoStack() : nullptr;
}

void pushProperty(IAppContext *ctx, CadItem *item, const QString &key,
                  const QVariant &oldValue, const QVariant &newValue) {
    if (QUndoStack *stack = undoStackOf(ctx)) {
        stack->push(new ChangePropertyCommand(item, key, oldValue, newValue));
    } else {
        item->setProperty(key, newValue);
    }
}

} // namespace

QString HoleCalloutLink::ensureHoleId(QGraphicsItem *hole) {
    if (!hole) return QString();
    QString id = hole->data(HOLE_DATA_GROUP_ID).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        hole->setData(HOLE_DATA_GROUP_ID, id);
    }
    return id;
}

QList<HoleItem *> HoleCalloutLink::linkedHoles(const LeaderItem *leader) {
    QList<HoleItem *> result;
    if (!leader || leader->leaderData().holeIds.isEmpty() || !leader->scene()) return result;

    const QStringList &ids = leader->leaderData().holeIds;
    const QList<QGraphicsItem *> items = leader->scene()->items();
    for (int i = 0; i < items.size(); ++i) {
        HoleItem *hole = dynamic_cast<HoleItem *>(items.at(i));
        if (!hole) continue;
        if (dynamic_cast<CadItem *>(hole->parentItem())) continue; // 블록 내부 구멍 제외
        if (ids.contains(hole->data(HOLE_DATA_GROUP_ID).toString())) result.append(hole);
    }
    return result;
}

void HoleCalloutLink::refreshCalloutsForHole(HoleItem *hole) {
    if (!hole || !hole->scene()) return;
    const QString id = hole->data(HOLE_DATA_GROUP_ID).toString();
    if (id.isEmpty()) return;

    const QList<QGraphicsItem *> items = hole->scene()->items();
    for (int i = 0; i < items.size(); ++i) {
        LeaderItem *leader = dynamic_cast<LeaderItem *>(items.at(i));
        if (leader && leader->leaderData().holeIds.contains(id)) refreshCallout(leader);
    }
}

void HoleCalloutLink::refreshCallout(LeaderItem *leader) {
    const QList<HoleItem *> holes = linkedHoles(leader);
    if (holes.isEmpty()) return;

    QList<MyHole> specs;
    for (int i = 0; i < holes.size(); ++i) specs.append(holes.at(i)->holeData());

    bool ok = false;
    const QString text = HoleCalloutFormatter::formatGrouped(specs, ok);
    if (ok && text != leader->leaderData().text) leader->setProperty("Content", text);
}

// 텍스트를 그대로 쓰고 구멍과의 연결을 끊는다(undo 시 연결·텍스트 모두 복원).
void HoleCalloutLink::applyTextOnly(IAppContext *ctx, LeaderItem *leader, const QString &newText) {
    QUndoStack *stack = undoStackOf(ctx);
    if (stack) stack->beginMacro(QObject::tr("구멍 콜아웃 텍스트 편집(연결 해제)"));
    pushProperty(ctx, leader, "HoleLinks", leader->leaderData().holeIds, QStringList());
    pushProperty(ctx, leader, "Content", leader->leaderData().text, newText);
    if (stack) stack->endMacro();
}

bool HoleCalloutLink::applyTextEdit(IAppContext *ctx, LeaderItem *leader, const QString &newText,
                                    QWidget *parent) {
    if (!leader) return false;
    const QList<HoleItem *> holes = linkedHoles(leader);
    if (holes.isEmpty()) return false; // 일반 지시선(또는 연결된 구멍이 모두 지워짐)

    if (newText.trimmed() == leader->leaderData().text.trimmed()) return true;

    MyHole spec;
    int count = 1;
    QString error;
    if (!HoleCalloutFormatter::parse(newText, holes.first()->holeData(), spec, count, error)) {
        QMessageBox box(parent);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QObject::tr("구멍 콜아웃 편집"));
        box.setText(QObject::tr("입력한 텍스트를 구멍 사양으로 해석할 수 없습니다.\n%1").arg(error));
        box.setInformativeText(QObject::tr(
            "• 텍스트만 사용: 입력한 텍스트를 그대로 쓰고 구멍과의 연결을 해제합니다.\n"
            "  (이후 구멍을 바꿔도 콜아웃이 갱신되지 않습니다)\n"
            "• 편집 취소: 원래 텍스트로 되돌립니다."));
        QPushButton *textOnlyBtn = box.addButton(QObject::tr("텍스트만 사용"), QMessageBox::AcceptRole);
        QPushButton *cancelBtn = box.addButton(QObject::tr("편집 취소"), QMessageBox::RejectRole);
        box.setDefaultButton(cancelBtn);
        box.setEscapeButton(cancelBtn);
        box.exec();
        if (box.clickedButton() == textOnlyBtn) applyTextOnly(ctx, leader, newText);
        return true;
    }

    if (count != holes.size()) {
        QMessageBox box(parent);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QObject::tr("구멍 콜아웃 개수"));
        box.setText(QObject::tr("콜아웃의 개수(%1x)가 연결된 구멍 수(%2개)와 다릅니다.\n"
                                "구멍 개수는 콜아웃 텍스트로 바꿀 수 없습니다.")
                        .arg(count).arg(holes.size()));
        box.setInformativeText(QObject::tr(
            "• 개수 복원 후 반영: 지름·깊이 등 사양만 구멍에 반영하고 개수는 %1개로 되돌립니다.\n"
            "• 텍스트만 사용: 입력한 텍스트를 그대로 쓰고 구멍과의 연결을 해제합니다.\n"
            "  (이후 구멍을 바꿔도 콜아웃이 갱신되지 않습니다)\n"
            "• 편집 취소: 원래 텍스트로 되돌립니다.").arg(holes.size()));
        QPushButton *restoreBtn = box.addButton(QObject::tr("개수 복원 후 반영"), QMessageBox::AcceptRole);
        QPushButton *textOnlyBtn = box.addButton(QObject::tr("텍스트만 사용"), QMessageBox::ActionRole);
        QPushButton *cancelBtn = box.addButton(QObject::tr("편집 취소"), QMessageBox::RejectRole);
        box.setDefaultButton(restoreBtn);
        box.setEscapeButton(cancelBtn);
        box.exec();
        if (box.clickedButton() == textOnlyBtn) {
            applyTextOnly(ctx, leader, newText);
            return true;
        }
        if (box.clickedButton() != restoreBtn) return true; // 편집 취소
    }

    // 사양 반영: 연결된 모든 구멍에 같은 스펙을 속성 명령으로 적용한다.
    // 각 setProperty가 refreshCalloutsForHole을 부르므로 콜아웃 텍스트는 자동으로 따라온다.
    QUndoStack *stack = undoStackOf(ctx);
    if (stack) stack->beginMacro(QObject::tr("구멍 콜아웃 편집"));

    for (int i = 0; i < holes.size(); ++i) {
        HoleItem *hole = holes.at(i);
        const MyHole current = hole->holeData();
        bool changed = false;
        for (const char *k : kSpecKeys) {
            const QString key = QString::fromLatin1(k);
            const QVariant oldValue = specValue(current, key);
            const QVariant newValue = specValue(spec, key);
            if (oldValue == newValue) continue;
            pushProperty(ctx, hole, key, oldValue, newValue);
            changed = true;
        }
        if (changed) {
            // 속성창에서 구멍을 고쳤을 때와 같은 후속 처리
            HoleSync::syncCenterLines(ctx, hole);
            HoleSync::confirmAndSyncSections(ctx, hole, parent);
        }
    }

    if (stack) stack->endMacro();

    // 스펙은 같고 표기만 다른 경우(예: "Ø10.0") 정규 표기로 되돌린다
    refreshCallout(leader);
    return true;
}
