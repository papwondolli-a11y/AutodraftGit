#ifndef BEARINGINSERTTOOL_H
#define BEARINGINSERTTOOL_H

#include "abstracttool.h"
#include "shaftpick.h"

#include <QDialog>
#include <QPainterPath>
#include <QtMath>
#include <QList>
#include <QPointF>
#include <QString>

class QCheckBox;
class QLabel;
class QRadioButton;
class QTableWidget;

// ─────────────────────────────────────────────────────────────────────────────
// BearingInsertTool — 기계 Tools 탭 "베어링 넣기" (명령 bearingin)
//   ① 축 구간 클릭 (ShaftPick) — 클릭이 구간의 어느 끝에 가까운지 = 베어링을 붙일 턱 쪽
//   ② 설정창: 종류(깊은 홈 볼 / 테이퍼 롤러) → 안지름 = 구간 지름인 규격만, 규격마다 검사
//      · 폭(B, 테이퍼는 T) ≤ 구간 길이   · 턱 지름 ≥ 권장 최소 턱 지름 d_a(추정 d + 0.2(D−d))
//      · 반대쪽(끼워 넣는 쪽)에 안지름보다 굵은 구간이 없음
//   ③ 위치 지정: 베어링이 축선을 따라 미리보기로 움직임 → 클릭, 또는 거리 입력(기준면 ~ 베어링 옆면)
//      기준 [턱(S) = 고른 쪽 구간 끝 / 끝(E) = 구간 반대쪽 끝], 엔터 = 기준면에 붙임
//      그린 뒤 다시 ①. 테이퍼는 콘 후면이 고른 턱 쪽 (기존 베어링 도구의 placeDirect)
//      [축 공차를 k5로] — 축 데이터(3D·간섭 검사용)만 바꿈, 도면 지름 치수 글은 그대로
// ─────────────────────────────────────────────────────────────────────────────
struct BearingFitCheck {
    QString name;
    double d = 0, D = 0, W = 0;   // W = 폭 (깊은 홈 B, 테이퍼 T)
    bool ok = true;
    QString note;                 // "맞음" / 경고
};

// 축 구간에 대한 배치 정보 (도구·MCP 공용)
struct BearingInsertPlan {
    ShaftPickInfo pick;
    bool leftSide = true;         // 붙일 턱이 구간 왼쪽
    double shoulderD = 0.0;       // 그쪽 이웃 구간 지름 (0 = 축 끝)
    double blockD = 0.0;          // 반대쪽(끼우는 쪽)에서 가장 굵은 구간 지름
};

class BearingInsertDialog : public QDialog {
    Q_OBJECT
public:
    BearingInsertDialog(const BearingInsertPlan &plan, QWidget *parent = nullptr);
    bool tapered() const;
    QString name() const;
    bool setK5() const;

private slots:
    void refill();

private:
    BearingInsertPlan m_plan;
    QRadioButton *m_deep = nullptr;
    QRadioButton *m_taper = nullptr;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_k5 = nullptr;
    QList<BearingFitCheck> m_rows;
};

class BearingInsertTool : public AbstractTool {
    Q_OBJECT
public:
    explicit BearingInsertTool(QObject *parent = nullptr);
    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

    // 점 근처의 축 구간 → 배치 정보
    static bool planAt(MyGraphicsScene *scene, const QPointF &pos, double tol, BearingInsertPlan &plan);
    // 그 구간에 맞는 규격과 검사
    static QList<BearingFitCheck> candidates(const BearingInsertPlan &plan, bool tapered);
    // 규격 폭 (깊은 홈 B, 테이퍼 T). 없으면 0
    static double widthOf(bool tapered, const QString &name);
    // 거리 → 베어링 중심의 축 방향 위치 (축 왼쪽 끝 기준). fromShoulder = 기준이 고른 턱 쪽 구간 끝
    static double centerFor(const BearingInsertPlan &plan, double width, bool fromShoulder, double distance);
    // 미리보기 외형
    static QPainterPath ghost(MyGraphicsScene *scene, const BearingInsertPlan &plan, bool tapered, const QString &name,
                              double centerX);
    // 그리기 (+ 축 공차 k5). centerX = 베어링 중심의 축 방향 위치 (NaN = 턱에 붙임). 반환 = 그린 규격 이름
    static QString insert(IAppContext *ctx, MyGraphicsScene *scene, const BearingInsertPlan &plan, bool tapered,
                          const QString &name, bool setK5, double centerX = qQNaN());

private:
    enum State { PickSegment, PlaceBearing };
    void showPrompt();
    double pickTol() const;
    double mouseCenter(const QPointF &pos) const;   // 마우스 → 구간 안으로 제한한 중심 위치
    void placeAtCenter(double centerX);

    State m_state = PickSegment;
    BearingInsertPlan m_plan;
    bool m_tapered = false;
    QString m_name;
    bool m_k5 = false;
    double m_width = 0.0;
    bool m_fromShoulder = true;   // 거리 기준: 턱(true) / 끝(false)
};

#endif // BEARINGINSERTTOOL_H
