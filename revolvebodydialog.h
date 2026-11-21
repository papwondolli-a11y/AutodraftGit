#ifndef REVOLVEBODYDIALOG_H
#define REVOLVEBODYDIALOG_H

#include "revolveprofile.h"

#include <QDialog>
#include <QMap>

class QCheckBox;
class QLineEdit;
class QTableWidget;

// ─────────────────────────────────────────────────────────────────────────────
// RevolveBodyDialog — 단면 회전체 설정창: 이름 + 자동으로 찾은 원통면별 끼워맞춤 기호
//   원통면 표: 구분(구멍/바깥) · 지름 · 길이(같은 지름 합) · 끼워맞춤(입력, 예 H7 / k5)
// ─────────────────────────────────────────────────────────────────────────────
class RevolveBodyDialog : public QDialog {
    Q_OBJECT
public:
    RevolveBodyDialog(const QList<RevolveCylinder> &cylinders, const QString &defaultName,
                      const QMap<QString, QString> &initialFits, bool updating, QWidget *parent = nullptr);

    QString partName() const;
    QMap<QString, QString> fits() const;   // 키 RevolveCylinder::key() → 기호 (빈 칸 제외)
    bool mirror() const;                   // 중심선 반대쪽에 대칭(미러) 단면 생성

private:
    QList<RevolveCylinder> m_cyls;
    QLineEdit *m_name = nullptr;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_mirror = nullptr;
};

#endif // REVOLVEBODYDIALOG_H
