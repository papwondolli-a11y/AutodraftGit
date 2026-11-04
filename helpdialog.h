#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;

class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);

private slots:
    void onTocItemClicked(QTreeWidgetItem *item, int column);

private:
    void buildToc();

    QTreeWidget *m_tocTree = nullptr;
    QTextBrowser *m_contentBrowser = nullptr;
};

#endif // HELPDIALOG_H
