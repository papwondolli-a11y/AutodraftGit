#ifndef MODIFYENTITYCOMMAND_H
#define MODIFYENTITYCOMMAND_H

#include <QUndoCommand>
#include <QMap>
#include <QVariant>
#include <QString>

class BaseDimensionItem;

class ModifyEntityCommand : public QUndoCommand {
public:
    ModifyEntityCommand(BaseDimensionItem *item,
                        const QMap<QString, QVariant> &oldProps,
                        const QMap<QString, QVariant> &newProps,
                        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    BaseDimensionItem *m_item;
    QMap<QString, QVariant> m_oldProps;
    QMap<QString, QVariant> m_newProps;
};

#endif // MODIFYENTITYCOMMAND_H