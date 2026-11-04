#ifndef CUSTOMTABWIDGET_H
#define CUSTOMTABWIDGET_H

#include "CommandInfo.h"

#include <QMap>
#include <QString>
#include <QTabWidget>
#include <QWidget>

class QToolBar;

class CustomTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    explicit CustomTabWidget(QWidget *parent = nullptr);
    QAction* addCommandAction(const CommandInfo& info);


private:
    QWidget* makeTab(const QString& tabName);
    QToolBar* makeToolBar(QWidget* tabPage);
    QMap<QString, QWidget*> m_tabMap;
    QMap<QWidget*, QToolBar*> m_toolBarMap;
};

#endif // CUSTOMTABWIDGET_H
