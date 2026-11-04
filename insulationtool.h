#ifndef INSULATIONTOOL_H
#define INSULATIONTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include <QDialog>
#include <QPointF>
#include <QString>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// InsulationParams — 단열재 설정
// ─────────────────────────────────────────────────────────────────────────────
struct InsulationParams {
    int     type   = 1;       // 1 = ins1.cdb 심볼, 2 = ins2.cdb 심볼
    double  height = 100.0;   // 심볼 높이. 원본(높이 1)을 이 값만큼 균등 확대 (가로도 같은 비율)
    QString layer;            // 빈 문자열 = 현재 레이어
};

// 도면에 생성할 호 한 개
struct InsulationArc {
    QPointF center;
    double  radius;
    double  startDeg;
    double  spanDeg;
};

// ─────────────────────────────────────────────────────────────────────────────
// InsulationTool — 단열재: P1-P2 가상선 위에 단열재 심볼(곡선)을 간격 없이 이어서 배열
//
//   1) P1   2) P2   3) 설정창(타입·높이·레이어)   → 심볼 배열 생성
//   심볼 기준점(좌하단)이 가상선 위에 놓이고 가로 방향이 P1→P2 방향. 심볼은 가상선 왼쪽(위쪽)으로 솟는다.
//   피치 = 심볼 가로 길이(타입1: 0.5, 타입2: 1.0) x 높이. 균등 확대라 높이 = 배율.
//   우클릭 : 한 단계 되돌리기 (첫 단계에서는 종료)
// ─────────────────────────────────────────────────────────────────────────────
class InsulationTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { WaitP1, WaitP2 };

    explicit InsulationTool(const InsulationParams &params, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static InsulationParams loadParams(MyGraphicsScene *scene);
    static void             saveParams(MyGraphicsScene *scene, const InsulationParams &p);

    // 1단위 가로 길이 (원본 크기 기준)
    static double unitWidth(int type);
    // P1-P2 길이에 완전히 들어가는 심볼 개수
    static int    countFor(double length, const InsulationParams &p);
    // 심볼 호 전부 (P1→P2 방향 배열)
    static QVector<InsulationArc> buildArcs(const QPointF &p1, const QPointF &p2,
                                            const InsulationParams &p);

private:
    void showPrompt();
    void cancelDrawing();
    bool showSetupDialog();
    void finish();
    void ensureLayer(IAppContext *ctx, const QString &name);

    State            m_state;
    InsulationParams m_params;
    QPointF          m_p1;
    QPointF          m_p2;
};

// ─────────────────────────────────────────────────────────────────────────────
// InsulationDialog — 타입·높이·레이어 설정
// ─────────────────────────────────────────────────────────────────────────────
class InsulationDialog : public QDialog {
    Q_OBJECT

public:
    InsulationDialog(const InsulationParams &params, double length, LayerManager *layerMgr,
                     QWidget *parent = nullptr);

    InsulationParams params() const;

private slots:
    void updateInfo();

private:
    double          m_length;
    QRadioButton   *m_radioType1;
    QRadioButton   *m_radioType2;
    QDoubleSpinBox *m_spinHeight;
    QComboBox      *m_comboLayer;
    QLabel         *m_labelInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// InsulationCommand — 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class InsulationCommand : public CommandBase {
    Q_OBJECT

public:
    explicit InsulationCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // INSULATIONTOOL_H
