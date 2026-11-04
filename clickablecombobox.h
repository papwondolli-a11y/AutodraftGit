#ifndef CLICKABLECOMBOBOX_H
#define CLICKABLECOMBOBOX_H

#include <QComboBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QEvent>

class ClickableComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit ClickableComboBox(QWidget *parent = nullptr);
    virtual ~ClickableComboBox();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void installLineEditFilter();
};

#endif // CLICKABLECOMBOBOX_H
