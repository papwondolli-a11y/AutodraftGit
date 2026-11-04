#ifndef MULTILINETAGUTIL_H
#define MULTILINETAGUTIL_H

#include <QMap>
#include <QString>

class QGraphicsItem;

// 다중선 태그(ML_DATA_GROUP_ID / ROLE / SPACING) 복사·재매핑 공용 유틸
class MultilineTagUtil {
public:
    // src 의 ML 태그를 dst 에 그대로 복사 (GROUP_ID 가 없으면 아무것도 하지 않음)
    static void copyTags(const QGraphicsItem *src, QGraphicsItem *dst);

    // item 의 GROUP_ID 를 새 UUID 로 교체. 같은 remap 을 공유하면 같은 원본 그룹은 같은 새 ID 로 매핑됨
    static void remapGroupId(QGraphicsItem *item, QMap<QString, QString> &remap);

    // src 의 ROLE/SPACING 을 복사하고 GROUP_ID 는 remap 으로 새 UUID 부여 (오프셋처럼 독립 그룹이 되는 경우)
    static void copyTagsWithNewGroup(const QGraphicsItem *src, QGraphicsItem *dst,
                                     QMap<QString, QString> &remap);
};

#endif // MULTILINETAGUTIL_H
