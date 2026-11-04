#ifndef COMMANDINFO_H
#define COMMANDINFO_H

#include <QString>

class CommandInfo
{
public:
    CommandInfo(const QString& id
                ,const QString& idMainName
                ,const QString& idSubName
                ,const QString& iconName = QString()
                ,const QString& shortcut = QString())
        : m_id(id)
        , m_idMainName(idMainName)
        , m_idSubName(idSubName)
        , m_iconName(iconName)
        , m_shortcut(shortcut)
    {}

    const QString& id() const { return m_id; }
    const QString& mainName() const { return m_idMainName; }
    const QString& subName() const { return m_idSubName; }
    const QString& iconName() const { return m_iconName; }
    const QString& shortcut() const { return m_shortcut; }

private:
    QString m_id;
    QString m_idMainName;
    QString m_idSubName;
    QString m_iconName;
    QString m_shortcut;
};

#endif // COMMANDINFO_H
