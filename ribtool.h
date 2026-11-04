#ifndef RIBTOOL_H
#define RIBTOOL_H

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
// RibParams — Rib 설정
// ─────────────────────────────────────────────────────────────────────────────
struct RibParams {
    double  height = 20.0;   // 심볼 높이 (원본 20). 가로 피치(150)는 고정, 세로만 height/20 비율로 변환
    QString layer;           // 빈 문자열 = 현재 레이어
};

// ─────────────────────────────────────────────────────────────────────────────
// RibTool — Rib: P1-P2 가상선 위에 Rib 심볼(rib.cdb)을 간격 없이 이어서 배열
//
//   1) P1 지정   2) P2 지정   3) 설정창(높이·레이어)   → 심볼 배열 생성
//   심볼 기준점(좌하단)이 가상선 위에 놓이고 가로 방향이 P1→P2 방향이다.
//   심볼은 가상선 위쪽(진행 방향 기준 왼쪽)으로 솟는다. 첫 심볼의 기준점은 P1, 간격 없음(피치 = 가로 길이).
//   우클릭 : 한 단계 되돌리기 (첫 단계에서는 종료)
// ─────────────────────────────────────────────────────────────────────────────
class RibTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { WaitP1, WaitP2 };

    explicit RibTool(const RibParams &params, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static RibParams loadParams(MyGraphicsScene *scene);
    static void         saveParams(MyGraphicsScene *scene, const RibParams &p);

    // 배치될 심볼 개수 (P1-P2 길이에 다 들어가는 개수)
    static int  countFor(double length, const RibParams &p);
    // 심볼 선분 전부 (P1→P2 방향 배열)
    static QVector<QLineF> buildLines(const QPointF &p1, const QPointF &p2, const RibParams &p);

private:
    void showPrompt();
    void cancelDrawing();
    bool showSetupDialog();
    void finish();
    void ensureLayer(IAppContext *ctx, const QString &name);

    State        m_state;
    RibParams m_params;
    QPointF      m_p1;
    QPointF      m_p2;
};

// ─────────────────────────────────────────────────────────────────────────────
// RibDialog — 간격·배율·레이어 설정
// ─────────────────────────────────────────────────────────────────────────────
class RibDialog : public QDialog {
    Q_OBJECT

public:
    RibDialog(const RibParams &params, double length, LayerManager *layerMgr,
                 QWidget *parent = nullptr);

    RibParams params() const;

private slots:
    void updateInfo();

private:
    double          m_length;
    QDoubleSpinBox *m_spinHeight;
    QComboBox      *m_comboLayer;
    class QLabel   *m_labelInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// RibCommand — 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class RibCommand : public CommandBase {
    Q_OBJECT

public:
    explicit RibCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // RIBTOOL_H
