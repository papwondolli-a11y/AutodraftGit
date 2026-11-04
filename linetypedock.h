#ifndef LINETYPEDOCK_H
#define LINETYPEDOCK_H

#include <QDockWidget>

class QTableView;
class QStandardItemModel;
class AppContext; // 전방 선언

class LineTypeDock : public QDockWidget
{
    Q_OBJECT

public:
    // 생성자에서 AppContext 포인터를 필수로 받도록 변경
    explicit LineTypeDock(AppContext* context, QWidget *parent = nullptr);
    ~LineTypeDock();

private slots:
    void onMakeItem();
    void onEditItem();
    void onEraseItem();
    void onSaveItem();

private:
    void populateTable();

    AppContext* m_appContext; // 포인터 보관용 멤버 변수
    QTableView *tableView;
    QStandardItemModel *model;
    QString linetypeFile;
};

#endif // LINETYPEDOCK_H
