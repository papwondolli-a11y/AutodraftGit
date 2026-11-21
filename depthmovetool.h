#ifndef DEPTHMOVETOOL_H
#define DEPTHMOVETOOL_H

#include "abstracttool.h"

#include <QPointF>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// 깊이(이동) — 메뉴 "3D 기계용 > 깊이(이동)" (명령 depth3d)
//   ① 움직일 부품 클릭: 정면도의 단면 회전체(해칭·영역 공간) 또는 돌출 부품 선, 측면도의 돌출 공간
//   ② 기준면: 정면도·평면도에서 점·선 클릭(스냅) → 축 방향 위치
//   ③ 깊이 값 → 고른 부품의 "기준면 쪽 끝면"이 기준면에서 그만큼 떨어지게 축 방향 이동
//   돌출: 시작 거리(Offset)만 바뀜. 단면 회전체: 해칭 + 단면 경계선(자동으로 찾음)을 옮기고
//   투상도·돌출·구멍을 다시 그림(측면도는 제자리).
// ─────────────────────────────────────────────────────────────────────────────
class DepthMove {
public:
    // 클릭한 부품 UUID (돌출이면 isExtrude = true)
    static QString partAt(MyGraphicsScene *scene, const QPointF &pos, bool &isExtrude);
    // 이동 실행 (undo 매크로 안에서). delta = 축 방향 이동량
    static bool apply(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid, bool isExtrude,
                      const QPointF &refPoint, double depth, double *delta, QString *error);
};

class DepthMoveTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DepthMoveTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    enum State { PickPart, PickRef, EnterDepth };
    void showPrompt();
    void cancelTool();
    void runWith(double depth);

    State m_state = PickPart;
    QString m_part;
    bool m_isExtrude = false;
    QPointF m_ref;
};

#endif // DEPTHMOVETOOL_H
