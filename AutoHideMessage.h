#ifndef AUTOHIDEMESSAGE_H
#define AUTOHIDEMESSAGE_H

#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

class AutoHideMessage : public QDialog
{
    Q_OBJECT
public:
    explicit AutoHideMessage(const QString& msg, int msec = 1500, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_DeleteOnClose);

        QLabel* label = new QLabel(msg, this);
        label->setAlignment(Qt::AlignCenter);

        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->addWidget(label);
        setLayout(layout);
        adjustSize();

        QTimer::singleShot(msec, this, &QDialog::accept);
    }
};

#endif // AUTOHIDEMESSAGE_H
