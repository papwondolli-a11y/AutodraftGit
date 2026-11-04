#include "attributeinputdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

AttributeInputDialog::AttributeInputDialog(const QMap<QString, QString> &prompts,
                                           const QMap<QString, QString> &defaults,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("블록 속성값 입력"));
    setMinimumWidth(300);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *formLayout = new QFormLayout;

    QList<QString> tags = prompts.keys();
    for (int i = 0; i < tags.size(); ++i) {
        QString tag = tags.at(i);
        QString prompt = prompts.value(tag);
        QString defaultValue = defaults.value(tag);

        QString labelText = prompt.isEmpty() ? tag : prompt;
        QLineEdit *lineEdit = new QLineEdit(defaultValue, this);
        formLayout->addRow(labelText + ":", lineEdit);

        m_inputFields.insert(tag, lineEdit);
        connect(lineEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
    }

    mainLayout->addLayout(formLayout);

    m_okButton = new QPushButton(tr("확인"), this);
    m_okButton->setDefault(true);
    m_okButton->setAutoDefault(true);
    m_cancelButton = new QPushButton(tr("취소"), this);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_okButton);
    btnLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(btnLayout);

    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

AttributeInputDialog::~AttributeInputDialog()
{
}

QMap<QString, QString> AttributeInputDialog::getAttributeValues() const
{
    QMap<QString, QString> values;
    QList<QString> tags = m_inputFields.keys();
    for (int i = 0; i < tags.size(); ++i) {
        QString tag = tags.at(i);
        QLineEdit *edit = m_inputFields.value(tag);
        if (edit) {
            values.insert(tag, edit->text());
        }
    }
    return values;
}
