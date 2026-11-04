#ifndef ATTRIBUTEINPUTDIALOG_H
#define ATTRIBUTEINPUTDIALOG_H

#include <QDialog>
#include <QMap>
#include <QString>
#include <QLineEdit>
#include <QFormLayout>
#include <QPushButton>

class AttributeInputDialog : public QDialog {
    Q_OBJECT
public:
    explicit AttributeInputDialog(const QMap<QString, QString> &prompts,
                                  const QMap<QString, QString> &defaults,
                                  QWidget *parent = nullptr);
    ~AttributeInputDialog();

    QMap<QString, QString> getAttributeValues() const;

private:
    QMap<QString, QLineEdit*> m_inputFields;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif // ATTRIBUTEINPUTDIALOG_H
