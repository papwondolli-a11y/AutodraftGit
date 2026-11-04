#ifndef DOORLAYERDIALOG_H
#define DOORLAYERDIALOG_H

#include <QDialog>
#include "commandbase.h"
#include "iappcontext.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// DoorLayerDialog — 도어 레이어설정 다이얼로그 (색상 아이콘 콤보)
//   문틀 / 나머지 / Trim 제외 레이어
// ─────────────────────────────────────────────────────────────────────────────
class DoorLayerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DoorLayerDialog(LayerManager *layerMgr,
                             const QString &frameLayer,
                             const QString &otherLayer,
                             const QString &trimExcludeLayer,
                             double jambOuter, double stopWidth, double stepHeight,
                             QWidget *parent = nullptr);

    QString frameLayer() const;
    QString otherLayer() const;
    QString trimExcludeLayer() const;   // 선택 없음 = 빈 문자열
    double  jambOuter() const;          // 문틀 상세치수 J
    double  stopWidth() const;          // S
    double  stepHeight() const;         // K/T

private slots:
    void onOkClicked();

private:
    static void populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                                   const QString &selected, bool withNone);

    QComboBox   *m_comboFrame;
    QComboBox   *m_comboOther;
    QComboBox   *m_comboTrimExclude;
    QDoubleSpinBox *m_spinJamb;
    QDoubleSpinBox *m_spinStop;
    QDoubleSpinBox *m_spinStep;
    QPushButton *m_btnOk;
    QPushButton *m_btnCancel;
};

// ─────────────────────────────────────────────────────────────────────────────
class DoorLayerCommand : public CommandBase {
    Q_OBJECT

public:
    explicit DoorLayerCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // DOORLAYERDIALOG_H
