#ifndef VALUESELECTDIALOG_H
#define VALUESELECTDIALOG_H

#include <QDialog>
#include <QList>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <functional>
#include <QHBoxLayout>

template <typename T>
class ValueSelectDialog : public QDialog
{
public :
    using IconFunction = std::function<QIcon(const T&)>;
    using NameFunction = std::function<QString(const T&)>;

    ValueSelectDialog(const QList<T>& items,const QString& selectedName,
                      IconFunction iconFnc, NameFunction nameFnc, QWidget*parent = nullptr)
        : QDialog(parent),m_items(items),m_iconFunc(iconFnc),m_nameFunc(nameFnc)
    {
        setupDialog();
        populateList(selectedName);
    }
    T selectedItem() const{
        QModelIndex index = m_listView -> currentIndex();
        if(!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return T();
        return m_items.at(index.row());
    }
    // 유효한 항목이 실제로 선택돼 있는지 — 사전 매칭 실패로 아무것도 안 잡힌 상태에서
    // 사용자가 그냥 OK를 눌렀을 때 호출부가 잘못된 값(첫 항목/기본값)을 반영하지 않도록.
    bool hasValidSelection() const{
        QModelIndex index = m_listView -> currentIndex();
        return index.isValid() && index.row() >= 0 && index.row() < m_items.size();
    }
private:
    void setupDialog() {
        m_listView = new QListView(this);
        m_model = new QStandardItemModel(this);
        m_listView->setModel(m_model);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        // 라인타입/선두께 미리보기가 보이도록 아이콘을 넉넉히
        m_listView->setIconSize(QSize(48, 16));

        m_okButton = new QPushButton("OK");
        m_cancelButton = new QPushButton("Cancel");

        QHBoxLayout* btnLayout = new QHBoxLayout;
        btnLayout->addStretch();
        btnLayout->addWidget(m_okButton);
        btnLayout->addWidget(m_cancelButton);

        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->addWidget(m_listView);
        layout->addLayout(btnLayout);

        connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    }
    void populateList(const QString& selectedName) {

        int exactRow = -1;
        int ciRow = -1; // 대소문자 무시 매칭 (예전 도면·임포트 이름 대응)

        for (int i = 0; i < m_items.size(); ++i) {
            const T& item = m_items.at(i);
            const QString itemName = m_nameFunc(item);
            QStandardItem* stdItem = new QStandardItem(m_iconFunc(item), itemName);
            m_model->appendRow(stdItem);

            if (itemName == selectedName)
                exactRow = i;
            else if (ciRow < 0 && itemName.compare(selectedName, Qt::CaseInsensitive) == 0)
                ciRow = i;
        }

        const int sel = (exactRow >= 0) ? exactRow : ciRow;
        if (sel >= 0)
            m_listView->setCurrentIndex(m_model->index(sel, 0));
    }

    QList<T> m_items;
    IconFunction m_iconFunc;
    NameFunction m_nameFunc;

    QListView* m_listView;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    QStandardItemModel* m_model;

};

#endif // VALUESELECTDIALOG_H
