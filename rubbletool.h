#ifndef RUBBLETOOL_H
#define RUBBLETOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include <QDialog>
#include <QLineF>
#include <QPointF>
#include <QString>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// RubbleParams — 잡석깔기 설정
// ─────────────────────────────────────────────────────────────────────────────
struct RubbleParams {
    double  gap   = 100.0;   // 심볼과 심볼 사이 간격
    double  height = 800.0;  // 심볼 높이 (원본 800). 가로 크기(300)는 고정, 세로만 height/800 비율로 변환
    QString layer;           // 빈 문자열 = 현재 레이어
};

// ─────────────────────────────────────────────────────────────────────────────
// RubbleTool — 잡석깔기: P1-P2 가상선을 따라 잡석 심볼(dol.cdb)을 일정 간격으로 배열
//
//   1) P1 지정   2) P2 지정   3) 설정창(간격·배율·레이어)   → 심볼 배열 생성
//   심볼 기준점(블록 기준점 = 심볼 좌상단)이 가상선 위에 놓이고 가로 방향이 P1→P2 방향이다.
//   심볼은 가상선 아래(진행 방향 기준 오른쪽)로 뻗는다. 첫 심볼의 기준점은 P1.
//   우클릭 : 한 단계 되돌리기 (첫 단계에서는 종료)
// ─────────────────────────────────────────────────────────────────────────────
class RubbleTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { WaitP1, WaitP2 };

    explicit RubbleTool(const RubbleParams &params, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static RubbleParams loadParams(MyGraphicsScene *scene);
    static void         saveParams(MyGraphicsScene *scene, const RubbleParams &p);

    // 배치될 심볼 개수 (P1-P2 길이에 다 들어가는 개수)
    static int  countFor(double length, const RubbleParams &p);
    // 심볼 선분 전부 (P1→P2 방향 배열)
    static QVector<QLineF> buildLines(const QPointF &p1, const QPointF &p2, const RubbleParams &p);

private:
    void showPrompt();
    void cancelDrawing();
    bool showSetupDialog();
    void finish();
    void ensureLayer(IAppContext *ctx, const QString &name);

    State        m_state;
    RubbleParams m_params;
    QPointF      m_p1;
    QPointF      m_p2;
};

// ─────────────────────────────────────────────────────────────────────────────
// RubbleDialog — 간격·배율·레이어 설정
// ─────────────────────────────────────────────────────────────────────────────
class RubbleDialog : public QDialog {
    Q_OBJECT

public:
    RubbleDialog(const RubbleParams &params, double length, LayerManager *layerMgr,
                 QWidget *parent = nullptr);

    RubbleParams params() const;

private slots:
    void updateInfo();

private:
    double          m_length;
    QDoubleSpinBox *m_spinGap;
    QDoubleSpinBox *m_spinHeight;
    QComboBox      *m_comboLayer;
    class QLabel   *m_labelInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// RubbleCommand — 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class RubbleCommand : public CommandBase {
    Q_OBJECT

public:
    explicit RubbleCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // RUBBLETOOL_H
