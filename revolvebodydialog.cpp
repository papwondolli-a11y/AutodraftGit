#include "revolvebodydialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

RevolveBodyDialog::RevolveBodyDialog(const QList<RevolveCylinder> &cylinders, const QString &defaultName,
                                     const QMap<QString, QString> &initialFits, bool updating, QWidget *parent)
    : QDialog(parent), m_cyls(cylinders)
{
    setWindowTitle(updating ? tr("단면 회전체 고치기") : tr("단면 회전체"));
    QVBoxLayout *lay = new QVBoxLayout(this);

    QFormLayout *form = new QFormLayout();
    m_name = new QLineEdit(defaultName, this);
    form->addRow(tr("이름"), m_name);
    lay->addLayout(form);

    lay->addWidget(new QLabel(tr("원통면 (자동으로 찾음) — 끼워맞춤 기호를 넣으면 간섭 검사에서 공차로 판정합니다"), this));
    m_table = new QTableWidget(m_cyls.size(), 4, this);
    m_table->setHorizontalHeaderLabels({tr("구분"), tr("지름"), tr("길이"), tr("끼워맞춤")});
    m_table->verticalHeader()->setVisible(false);
    for (int i = 0; i < m_cyls.size(); ++i) {
        const RevolveCylinder &c = m_cyls.at(i);
        QTableWidgetItem *kind = new QTableWidgetItem(c.hole ? tr("구멍") : tr("바깥"));
        QTableWidgetItem *dia = new QTableWidgetItem(QStringLiteral("Ø%1").arg(c.diameter));
        QTableWidgetItem *len = new QTableWidgetItem(QString::number(c.x1 - c.x0, 'g', 6));
        kind->setFlags(kind->flags() & ~Qt::ItemIsEditable);
        dia->setFlags(dia->flags() & ~Qt::ItemIsEditable);
        len->setFlags(len->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, kind);
        m_table->setItem(i, 1, dia);
        m_table->setItem(i, 2, len);
        m_table->setItem(i, 3, new QTableWidgetItem(initialFits.value(c.key())));
    }
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    lay->addWidget(m_table);
    if (m_cyls.isEmpty())
        lay->addWidget(new QLabel(tr("축과 나란한 원통면이 없습니다."), this));

    m_mirror = new QCheckBox(tr("중심선 반대쪽에 대칭(미러) 단면 생성 — 반단면만 그렸을 때"), this);
    m_mirror->setChecked(true);
    lay->addWidget(m_mirror);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    resize(420, 320);
}

QString RevolveBodyDialog::partName() const
{
    const QString n = m_name->text().trimmed();
    return n.isEmpty() ? tr("회전체") : n;
}

QMap<QString, QString> RevolveBodyDialog::fits() const
{
    QMap<QString, QString> out;
    for (int i = 0; i < m_cyls.size(); ++i) {
        const QTableWidgetItem *it = m_table->item(i, 3);
        const QString code = it ? it->text().trimmed() : QString();
        if (!code.isEmpty())
            out.insert(m_cyls.at(i).key(), code);
    }
    return out;
}

bool RevolveBodyDialog::mirror() const
{
    return m_mirror && m_mirror->isChecked();
}
