#ifndef WALLPOSITIONDIALOG_H
#define WALLPOSITIONDIALOG_H

#include <QDialog>

class QPushButton;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// WallPositionDialog — 문 위치 선택 (내벽 / 중앙 / 외벽)
//
// V(문틀 두께) < 다중선 전체 간격일 때만 표시.
// ─────────────────────────────────────────────────────────────────────────────
class WallPositionDialog : public QDialog {
    Q_OBJECT

public:
    enum Mode { Inner = 1, Center = 2, Outer = 3 };

    explicit WallPositionDialog(Mode defaultMode = Inner, QWidget *parent = nullptr);
    Mode mode() const;

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    QRadioButton *m_radioInner;
    QRadioButton *m_radioCenter;
    QRadioButton *m_radioOuter;
    QPushButton  *m_btnOk;
    QPushButton  *m_btnCancel;
};

#endif // WALLPOSITIONDIALOG_H
