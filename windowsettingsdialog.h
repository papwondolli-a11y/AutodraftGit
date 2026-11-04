#ifndef WINDOWSETTINGSDIALOG_H
#define WINDOWSETTINGSDIALOG_H

#include <QDialog>
#include "commandbase.h"
#include "iappcontext.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// WindowSettingsDialog — 창문설정 다이얼로그 (평면창문 탭 공용)
//   Layer (색상 아이콘 콤보): 창문 프레임 / 창문 나머지 / Trim 제외
//   창 상세 치수: 미서기창 Win1~7 / 상단부 전용 Win1~7
// ─────────────────────────────────────────────────────────────────────────────
class WindowSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit WindowSettingsDialog(LayerManager *layerMgr,
                                  const QString &frameLayer,
                                  const QString &otherLayer,
                                  const QString &trimExcludeLayer,
                                  const double *win, const double *winTop,
                                  QWidget *parent = nullptr);

    QString frameLayer() const;
    QString otherLayer() const;
    QString trimExcludeLayer() const;   // 선택 없음 = 빈 문자열
    double  win(int idx) const;         // 미서기창 Win1~7 (idx 0..6)
    double  winTop(int idx) const;      // 상단부 전용 Win1~7

private:
    static void populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                                   const QString &selected, bool withNone);
    QWidget *buildWinPage(QDoubleSpinBox **spins, const double *vals, bool topOnly);

    QComboBox      *m_comboFrame;
    QComboBox      *m_comboOther;
    QComboBox      *m_comboTrimExclude;
    QDoubleSpinBox *m_spinWin[7];
    QDoubleSpinBox *m_spinWinTop[7];
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

// ─────────────────────────────────────────────────────────────────────────────
class WindowSettingsCommand : public CommandBase {
    Q_OBJECT

public:
    explicit WindowSettingsCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // WINDOWSETTINGSDIALOG_H
