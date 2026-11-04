#ifndef PARALLELCENTERSETUPDIALOG_H
#define PARALLELCENTERSETUPDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class QPushButton;

class ParallelCenterSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit ParallelCenterSetupDialog(double  offset,
                                       QWidget *parent = nullptr);

    double offset() const;

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    QDoubleSpinBox *m_spinOffset;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // PARALLELCENTERSETUPDIALOG_H
