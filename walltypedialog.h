#ifndef WALLTYPEDIALOG_H
#define WALLTYPEDIALOG_H

#include <QDialog>
#include "commandbase.h"
#include "iappcontext.h"

class QButtonGroup;

// ─────────────────────────────────────────────────────────────────────────────
// WallTypeDialog — 벽체 다이얼로그 (wall-1~4 아이콘을 2x2 박스로 표시)
// ─────────────────────────────────────────────────────────────────────────────
class WallTypeDialog : public QDialog {
    Q_OBJECT

public:
    // wall-<firstIcon>.png ~ wall-<firstIcon+count-1>.png 를 columns열 박스로 표시
    explicit WallTypeDialog(int firstIcon, int count, const QString &title,
                            int columns = 2, QWidget *parent = nullptr);

    int selectedType() const;   // 클릭한 아이콘 번호, 선택 없음 = 0 (클릭 시 즉시 확정)

private slots:
    void onTypeClicked(int id);

private:
    QButtonGroup *m_group;
    int           m_selected;
};

// ─────────────────────────────────────────────────────────────────────────────
// WallSettingsDialog — 벽체설정 (중심선 체크 / 벽체선 레이어 / 몰타르선 레이어)
// ─────────────────────────────────────────────────────────────────────────────
class QCheckBox;
class QComboBox;
class LayerManager;

class WallSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit WallSettingsDialog(bool centerLineEnabled,
                                const QString &wallLayerName,
                                const QString &mortarLayerName,
                                LayerManager *layerMgr,
                                QWidget *parent = nullptr);

    bool    centerLineEnabled() const;
    QString wallLayerName() const;     // 빈 문자열 = 현재 레이어
    QString mortarLayerName() const;   // 빈 문자열 = 현재 레이어

private:
    void populateCombo(QComboBox *combo, LayerManager *mgr, const QString &selectedName);

    QCheckBox *m_centerLineCheck  = nullptr;
    QComboBox *m_wallLayerCombo   = nullptr;
    QComboBox *m_mortarLayerCombo = nullptr;
};

class WallSettingsCommand : public CommandBase {
    Q_OBJECT

public:
    explicit WallSettingsCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

// ─────────────────────────────────────────────────────────────────────────────
class WallTypeCommand : public CommandBase {
    Q_OBJECT

public:
    enum Kind { Plain, Mortar, SpaceMortar };
    explicit WallTypeCommand(IAppContext *ctx, Kind kind = Plain,
                             QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);        // 벽체 (2-Line)
    static CommandBase *createMortarCommand(IAppContext *c);  // 벽+몰탈 (편심4-Line)
    static CommandBase *createSpaceMortarCommand(IAppContext *c);  // 벽+공간+몰탈 (편심6-Line)

private:
    void executePlain(QWidget *parentWin);
    void executeMortar(QWidget *parentWin);
    void executeSpaceMortar(QWidget *parentWin);

    IAppContext *m_ctx;
    Kind         m_kind;
};

#endif // WALLTYPEDIALOG_H
