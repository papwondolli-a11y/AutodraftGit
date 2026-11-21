#ifndef BEARINGPICKERDIALOG_H
#define BEARINGPICKERDIALOG_H

#include "bearinglayer.h"
#include "commandbase.h"
#include "iappcontext.h"
#include "deepgroovebearingtool.h"
#include "taperedrollerbearingtool.h"
#include "thrustbearingtool.h"

#include <QAbstractButton>
#include <QDialog>
#include <QPainterPath>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
// 베어링 종류 선택 — "기계부품" 탭의 "베어링" 버튼을 누르면 뜨는 아이콘 선택 다이얼로그.
// boltpickerdialog 와 같은 방식: 각 툴의 buildPath() 결과를 QPainter 로 직접 그려 아이콘으로 쓴다.
// 깊은홈볼 / 테이퍼롤러 / 스러스트 3종. (타이핑 명령 dgbearing / trbearing / thbearing 은 그대로 유지)
// ─────────────────────────────────────────────────────────────────────────
enum class BearingKind { DeepGroove, TaperedRoller, Thrust };

class BearingIconButton : public QAbstractButton {
    Q_OBJECT
public:
    BearingIconButton(const QString &label, const QPainterPath &previewPath, BearingKind kind,
                      QWidget *parent = nullptr);

    BearingKind kind() const { return m_kind; }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    QPainterPath m_previewPath;
    BearingKind  m_kind;
};

class QComboBox;

class BearingPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit BearingPickerDialog(IAppContext *ctx, QWidget *parent = nullptr);

    BearingKind selectedKind() const { return m_selectedKind; }
    QString bearingLayer() const;   // 콤보에서 고른 베어링 레이어

private slots:
    void onIconClicked();

private:
    void setupUI();
    static QPainterPath previewPathFor(BearingKind kind);

    BearingKind  m_selectedKind;
    IAppContext *m_ctx;
    QComboBox   *m_layerCombo = nullptr;
};

// 아이콘 선택 후 고른 베어링 툴을 시작한다.
class BearingPickerCommand : public CommandBase {
public:
    explicit BearingPickerCommand(IAppContext *ctx, QObject *parent = nullptr)
        : CommandBase(parent), m_ctx(ctx) {}

    void execute() override {
        if (!m_ctx) return;
        BearingPickerDialog dlg(m_ctx);
        if (dlg.exec() != QDialog::Accepted) return;
        BearingLayer::setBodyLayer(m_ctx->activeScene(), dlg.bearingLayer());
        switch (dlg.selectedKind()) {
        case BearingKind::DeepGroove:    m_ctx->startTool(new DeepGrooveBearingTool());    break;
        case BearingKind::TaperedRoller: m_ctx->startTool(new TaperedRollerBearingTool()); break;
        case BearingKind::Thrust:        m_ctx->startTool(new ThrustBearingTool());        break;
        }
    }

private:
    IAppContext *m_ctx;
};

#endif // BEARINGPICKERDIALOG_H
