#include "multilinetagutil.h"
#include "multilinedata.h"
#include <QGraphicsItem>
#include <QUuid>

void MultilineTagUtil::copyTags(const QGraphicsItem *src, QGraphicsItem *dst) {
    if (!src || !dst)
        return;
    const QString gid = src->data(ML_DATA_GROUP_ID).toString();
    if (gid.isEmpty())
        return;
    dst->setData(ML_DATA_GROUP_ID, gid);
    dst->setData(ML_DATA_ROLE, src->data(ML_DATA_ROLE));
    const double sp = src->data(ML_DATA_SPACING).toDouble();
    if (sp > 0.0)
        dst->setData(ML_DATA_SPACING, sp);
}

void MultilineTagUtil::remapGroupId(QGraphicsItem *item, QMap<QString, QString> &remap) {
    if (!item)
        return;
    const QString gid = item->data(ML_DATA_GROUP_ID).toString();
    if (gid.isEmpty())
        return;
    if (!remap.contains(gid))
        remap.insert(gid, QUuid::createUuid().toString(QUuid::WithoutBraces));
    item->setData(ML_DATA_GROUP_ID, remap.value(gid));
}

void MultilineTagUtil::copyTagsWithNewGroup(const QGraphicsItem *src, QGraphicsItem *dst,
                                            QMap<QString, QString> &remap) {
    copyTags(src, dst);
    remapGroupId(dst, remap);
}
