#include "modifyentitycommand.h"
#include "basedimensionitem.h"
#include <QList>

ModifyEntityCommand::ModifyEntityCommand(BaseDimensionItem *item,
                                         const QMap<QString, QVariant> &oldProps,
                                         const QMap<QString, QVariant> &newProps,
                                         QUndoCommand *parent)
    : QUndoCommand(parent), m_item(item), m_oldProps(oldProps), m_newProps(newProps) {
    setText("Modify Dimension Properties");
}

void ModifyEntityCommand::undo() {
    if (m_item) {
        // 컨테이너 detach 경고를 방지하기 위해 인덱스 기반 루프 사용
        QList<QString> keys = m_oldProps.keys();
        for (int i = 0; i < keys.size(); ++i) {
            QString key = keys.at(i);
            m_item->setProperty(key, m_oldProps.value(key));
        }
    }
}

void ModifyEntityCommand::redo() {
    if (m_item) {
        // 컨테이너 detach 경고를 방지하기 위해 인덱스 기반 루프 사용
        QList<QString> keys = m_newProps.keys();
        for (int i = 0; i < keys.size(); ++i) {
            QString key = keys.at(i);
            m_item->setProperty(key, m_newProps.value(key));
        }
    }
}