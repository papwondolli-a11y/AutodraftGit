#include "spacefinishgroupbox.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QRadioButton>
#include <QVBoxLayout>

SpaceFinishGroupBox::SpaceFinishGroupBox(QWidget *parent)
    : QGroupBox(tr("공간벽 마감형태"), parent)
{
    const int ICW = 76, ICH = 62;   // 원본 153x125 의 절반
    QButtonGroup *btnGroup = new QButtonGroup(this);
    QHBoxLayout  *row      = new QHBoxLayout;
    row->setSpacing(4);

    for (int i = 0; i < 4; ++i) {
        QVBoxLayout *col = new QVBoxLayout;
        col->setSpacing(2);
        col->setAlignment(Qt::AlignHCenter);

        QLabel *icon = new QLabel(this);
        icon->setPixmap(QPixmap(QString(":/icons/swtype%1.png").arg(i + 1))
                            .scaled(ICW, ICH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        icon->setAlignment(Qt::AlignCenter);

        m_radio[i] = new QRadioButton(tr("Type %1").arg(i + 1), this);
        btnGroup->addButton(m_radio[i], i);

        col->addWidget(icon);
        col->addWidget(m_radio[i]);
        row->addLayout(col);
    }
    m_radio[0]->setChecked(true);   // 초기 선택: swtype1
    setLayout(row);
}

void SpaceFinishGroupBox::setType(int type)
{
    m_radio[qBound(0, type, 3)]->setChecked(true);
}

int SpaceFinishGroupBox::type() const
{
    for (int i = 0; i < 4; ++i)
        if (m_radio[i]->isChecked())
            return i;
    return 0;
}
