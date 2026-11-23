#include "mechparttag.h"

#include "hatchitem.h"
#include "lineitem.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "polylineitem.h"
#include "scenesymbolregistry.h"

#include <QUndoStack>
#include <QUuid>
#include <algorithm>

bool MechPartTag::refPoint(const QGraphicsItem *item, QPointF &out)
{
    if (const PolylineItem *pl = dynamic_cast<const PolylineItem *>(item)) {
        const MyPolyline poly = pl->getPolyline();
        if (poly.vertices.empty())
            return false;
        double x = poly.vertices.front().x, y = poly.vertices.front().y;
        for (size_t i = 1; i < poly.vertices.size(); ++i) {
            x = std::min(x, poly.vertices[i].x);
            y = std::min(y, poly.vertices[i].y);
        }
        out = QPointF(x, y);
        return true;
    }
    if (const HatchItem *hi = dynamic_cast<const HatchItem *>(item)) {
        const QRectF br = hi->sceneTransform().map(hi->geometryPath()).boundingRect();
        if (br.isNull())
            return false;
        out = QPointF(br.left(), br.top());   // 도면 좌표의 최소 X·Y
        return true;
    }
    if (const LineItem *li = dynamic_cast<const LineItem *>(item)) {
        const MyLine ln = li->getLine();
        out = QPointF(ln.start.x, ln.start.y);
        return true;
    }
    return false;
}

QString MechPartTag::registerPart(MyGraphicsScene *scene, QUndoStack *undo, QGraphicsItem *anchor,
                                  const QString &type, const QString &namePrefix,
                                  QMap<QString, QVariant> props, const QPointF &origin, double angleDeg,
                                  const QPointF *refOverride)
{
    if (!scene || (!anchor && !refOverride))
        return QString();
    QPointF ref;
    if (refOverride)
        ref = *refOverride;
    else if (!refPoint(anchor, ref))
        return QString();

    // 이름 번호: 같은 이름 앞부분으로 등록된 부품 번호 중 가장 큰 것 + 1 ("축-3", "볼트 M10x40-1" …)
    //   (개수 + 1 이면 중간 기록이 지워졌을 때 기존 이름과 겹침)
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    int count = 0;
    const QString stem = namePrefix + QLatin1Char('-');
    const QMap<QString, SymbolInfo> &all = reg->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != type || !it.value().name.startsWith(stem))
            continue;
        bool ok = false;
        const int no = it.value().name.mid(stem.size()).toInt(&ok);
        count = ok ? std::max(count, no) : count + 1;
    }

    SymbolInfo info;
    info.type = type;
    info.name = QStringLiteral("%1-%2").arg(namePrefix).arg(count + 1);
    props[QStringLiteral("ox")] = origin.x();
    props[QStringLiteral("oy")] = origin.y();
    props[QStringLiteral("angleDeg")] = angleDeg;
    props[QStringLiteral("refX")] = ref.x();
    props[QStringLiteral("refY")] = ref.y();
    info.props = props;

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (undo)
        undo->push(new RegisterSymbolCommand(reg, uuid, info));
    else
        reg->registerSymbol(uuid, info);
    if (anchor)
        anchor->setData(MECH_DATA_PART_ID, uuid);
    return uuid;
}
