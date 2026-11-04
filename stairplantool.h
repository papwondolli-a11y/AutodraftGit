#ifndef STAIRPLANTOOL_H
#define STAIRPLANTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include <QColor>
#include <QLineF>
#include <QPointF>
#include <QString>
#include <QVector>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────────
// StairPlanParams — 계단평면 설정 (lisp/stair.dcl 의 "Stair Drawing Tools" 다이얼로그 값)
// ─────────────────────────────────────────────────────────────────────────────
struct StairPlanParams {
    int     type      = 1;       // 1 = 기본, 2 = 계단참 부분에도 중간계단 생성 (꺾임 계단 전용)
    int     midSteps  = 3;       // 타입2 중간계단 수
    int     up        = 10;      // UP 계단 수 (0 이면 DN 단독 계단)
    int     down      = 10;      // DN 계단 수 (0 이면 UP 단독 계단)
    double  tread     = 280.0;   // 디딤판 폭 (Tread Width)
    double  treadLen  = 0.0;     // 디딤판 길이(가로, 꺾임 계단 전용). 0 = 자동 = (벽선길이-200)/2
    double  land      = 1500.0;  // 계단참 길이 (Landing length)
    bool    numbering = false;   // 계단 번호 (Stair Number)
    bool    nonslip   = true;    // 미끄럼방지 (Nonslip Drawing)
    QString layer        = QStringLiteral("st");  // 계단 레이어 (미끄럼방지선 제외 전부, 단 원/글자는 글자 레이어)
    QString nonslipLayer = QStringLiteral("st");  // 미끄럼방지선 레이어
};

// 계단 형상을 이루는 기본 도형 모음 (실제 아이템 생성 / 고스트 미리보기 공용)
struct StairPrims {
    struct Circle { QPointF c; double r; };
    struct Text   { QString s; QPointF pos; double h; };
    struct Poly   { QVector<QPointF> pts; bool closed; };

    QVector<QLineF> lines;
    QVector<Poly>   polys;
    QVector<Poly>   nosings;   // 미끄럼방지선 (별도 레이어)
    QVector<Circle> circles;
    QVector<Text>   texts;
};

// ─────────────────────────────────────────────────────────────────────────────
// StairPlanTool — 계단평면 그리기 (lisp/stair.lsp 의 c:stair 이식)
//
//   0) 설정 다이얼로그 (UP/DN 계단 수, 디딤판 폭, 계단참 길이, 번호, 미끄럼방지, 레이어)
//   1) 계단참 벽선의 첫 모서리 (UP) 지정
//   2) 계단참 벽선의 두번째 모서리 (DN) 지정
//   3) 계단 진행 방향 지정 (벽선에 수직인 쪽 중 커서가 놓인 쪽)
//   UP, DN 둘 다 > 0  : 꺾임(U자) 계단   /  한쪽이 0 : 직진 단일 계단
//
//   우클릭 : 한 단계 되돌리기 (첫 단계에서는 종료)
// ─────────────────────────────────────────────────────────────────────────────
class StairPlanTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { WaitUp, WaitDown, WaitDirection };

    explicit StairPlanTool(const StairPlanParams &params, QObject *parent = nullptr);

    // true 면 설정창을 띄우지 않고 생성자에 준 값(저장된 기본값 + 덮어쓴 값)을 그대로 쓴다.
    // AI(MCP) 호출용. 저장된 도구 기본값은 건드리지 않는다.
    void setHeadless(bool on) { m_headless = on; }

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    // 설정 저장/불러오기 (도면 DocumentSettings + 전역 QSettings)
    static StairPlanParams loadParams(MyGraphicsScene *scene);
    static void            saveParams(MyGraphicsScene *scene, const StairPlanParams &p);

    // 형상 계산 (순수 함수)
    //   up, dn : 계단참 벽선 두 모서리   lag : 계단 진행 방향 각도(rad)
    //   dimScale : 문자·원 크기 배율 (lisp 의 DIMSCALE)
    static StairPrims buildPrims(const StairPlanParams &p, const QPointF &up,
                                 const QPointF &dn, double lag, double dimScale);

private:
    void showPrompt();
    void adjustTreadLength();
    bool showSetupDialog();
    void cancelDrawing();
    double directionAngle(const QPointF &cursor) const;
    double dimScale() const;
    void updateGhost(const QPointF &cursor);
    void finish(const QPointF &cursor);
    void ensureLayer(IAppContext *ctx, const QString &name);
    QGraphicsItem *finalizeItem(QGraphicsItem *item, IAppContext *ctx, const QString &layer);

    bool            m_headless = false;
    State           m_state;
    StairPlanParams m_params;
    QPointF         m_up;
    QPointF         m_dn;
    QPointF         m_cur;
};

// ─────────────────────────────────────────────────────────────────────────────
// StairPlanCommand — 설정 다이얼로그를 띄운 뒤 StairPlanTool 시작
// ─────────────────────────────────────────────────────────────────────────────
class StairPlanCommand : public CommandBase {
    Q_OBJECT

public:
    explicit StairPlanCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // STAIRPLANTOOL_H
