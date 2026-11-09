#ifndef SHAFTENDDIALOG_H
#define SHAFTENDDIALOG_H

#include "shaftdata.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

// ─────────────────────────────────────────────────────────────────────────
// ShaftEndDialog — 축 끝단 형상(키홈 / 나사부)의 크기 입력 다이얼로그
//   키홈 : 길이, 상단, 중앙, 하단
//   나사부: 나사 길이, 피치(0=표준 자동) — 호칭경은 끝 구간 지름
// ─────────────────────────────────────────────────────────────────────────
class ShaftEndDialog : public QDialog {
    Q_OBJECT
public:
    // type 은 Keyway 또는 Thread. segDiameter/segLength 는 끝 구간의 지름·길이(안내 표시용).
    ShaftEndDialog(ShaftEndType type, double segDiameter, double segLength,
                   QWidget *parent = nullptr);

    void setKeyway(const ShaftKeyway &k);
    ShaftKeyway keyway() const;
    void setThread(const ShaftThread &t);
    ShaftThread thread() const;

private slots:
    void updateInfo();

private:
    void setupKeywayUI();
    void setupThreadUI();

    ShaftEndType m_type;
    double       m_segDiameter;
    double       m_segLength;

    QDoubleSpinBox *m_kwLength = nullptr;
    QDoubleSpinBox *m_kwUpper  = nullptr;
    QDoubleSpinBox *m_kwCenter = nullptr;
    QDoubleSpinBox *m_kwLower  = nullptr;
    QCheckBox      *m_kwSection = nullptr;
    QDoubleSpinBox *m_kwDepth  = nullptr;
    QComboBox      *m_kwPos    = nullptr;
    QDoubleSpinBox *m_thLength = nullptr;
    QDoubleSpinBox *m_thPitch  = nullptr;
    QLabel         *m_info     = nullptr;
};

#endif // SHAFTENDDIALOG_H
