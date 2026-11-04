#include "linetypeeditdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>


LineTypeEditDialog::LineTypeEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit LineType"));
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 이름 입력 영역
    QHBoxLayout *nameLayout = new QHBoxLayout();
    QLabel *nameLabel = new QLabel(tr("Name    : "), this);
    QLineEdit *nameEdit = new QLineEdit(this);
    nameEdit->setObjectName("nameEdit");
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit);
    mainLayout->addLayout(nameLayout);

    // 패턴 입력 영역
    QHBoxLayout *patternLayout = new QHBoxLayout();
    QLabel *patternLabel = new QLabel(tr("Pattern : "), this);
    QLineEdit *patternEdit = new QLineEdit(this);

    // 정규표현식 설명:
    // ^                          : 문자열 시작
    // [+-]?                     : + 또는 - 기호가 있을 수도 있고 없을 수도 있음
    // \\d+(\\.\\d+)?            : 하나 이상의 숫자와 선택적으로 소숫점 및 그 뒤의 숫자
    // (,[+-]?\\d+(\\.\\d+)?)*    : 콤마로 구분된 위와 동일한 패턴의 항목이 0회 이상 반복
    // $                          : 문자열 끝
    QRegularExpression regExp("^[+-]?\\d+(\\.\\d+)?(,[+-]?\\d+(\\.\\d+)?)*$");
    QRegularExpressionValidator *validator = new QRegularExpressionValidator(regExp, this);
    // QLineEdit에 validator 설정
    patternEdit -> setValidator(validator);

    patternEdit->setObjectName("patternEdit");
    patternLayout->addWidget(patternLabel);
    patternLayout->addWidget(patternEdit);
    mainLayout->addLayout(patternLayout);
    QLabel *patternText = new QLabel("Pattern Ex) 2.0,-1.0,3.0,-1.0",this);
    mainLayout -> addWidget(patternText);
    // 버튼 영역
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("OK"), this);
    QPushButton *cancelButton = new QPushButton(tr("Cancel"), this);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    // 신호 연결
    connect(okButton, &QPushButton::clicked, this, &LineTypeEditDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &LineTypeEditDialog::reject);
}

LineTypeEditDialog::~LineTypeEditDialog()
{
}

QString LineTypeEditDialog::getName() const
{
    QLineEdit *nameEdit = findChild<QLineEdit*>("nameEdit");
    return nameEdit ? nameEdit->text() : "";
}

QString LineTypeEditDialog::getPattern() const
{
    QLineEdit *patternEdit = findChild<QLineEdit*>("patternEdit");
    return patternEdit ? patternEdit->text() : "";
}

void LineTypeEditDialog::setName(const QString &name)
{
    QLineEdit *nameEdit = findChild<QLineEdit*>("nameEdit");
    if(nameEdit)
        nameEdit->setText(name);
}

void LineTypeEditDialog::setPattern(const QString &pattern)
{
    QLineEdit *patternEdit = findChild<QLineEdit*>("patternEdit");
    if(patternEdit)
        patternEdit->setText(pattern);
}
