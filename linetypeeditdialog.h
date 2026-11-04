#ifndef LINETYPEEDITDIALOG_H
#define LINETYPEEDITDIALOG_H

#include <QDialog>
#include <QString>

class LineTypeEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LineTypeEditDialog(QWidget *parent = nullptr);
    ~LineTypeEditDialog();

    // 다이얼로그에 초기 값을 설정 (수정용)
    void setName(const QString &name);
    void setPattern(const QString &pattern);

    // 입력된 값을 가져옴
    QString getName() const;
    QString getPattern() const;

};

#endif // LINETYPEEDITDIALOG_H
