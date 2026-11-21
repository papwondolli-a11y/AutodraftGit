#ifndef SHAFTTOOL_H
#define SHAFTTOOL_H

#include "abstracttool.h"
#include "shaftdata.h"

#include <QPointF>

// ─────────────────────────────────────────────────────────────────────────
// ShaftTool — 계단축(Shaft) 외형·중심선·치수를 그리는 툴 ("기계 Tools" 탭)
//
// 입력 흐름:
//   1) 실행하면 ShaftDialog 로 구간(지름·길이·끼워맞춤·모따기) 표와 치수 옵션을 입력한다.
//      (마지막 입력값은 ToolDefaults 로 도면/전역에 기억)
//   2) 클릭 또는 좌표 입력 : 축 왼쪽 끝면 중심 위치 지정 → 외형·중심선·치수 생성
//   3) 같은 값으로 계속 배치할 수 있다. ESC/Enter/Space/우클릭 : 툴 종료
// 레이어: SHAFT(외형) / SHAFT-CEN(중심선, CENTER 선종류) / SHAFT-DIM(치수) — 없으면 만든다.
// 생성된 도형은 undo 한 번으로 한꺼번에 취소된다.
// 도형 계산은 ShaftBuilder 가 맡고, 이 클래스는 입력·미리보기·씬 등록만 한다.
// ─────────────────────────────────────────────────────────────────────────
class ShaftTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ShaftTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // MCP 등 자동 배치: 설정되어 있으면 activate 에서 설정창 없이 이 값을 쓴다
    static void setHeadlessParams(const ShaftParams &params);
    static void clearHeadless();

private:
    static bool        s_headless;
    static ShaftParams s_headlessParams;

    void showPositionPrompt();
    void placeAt(const QPointF &origin);
    void finishTool();
    void ensureLayer(IAppContext *ctx, const QString &name, const QColor &color,
                     const QString &linetype, double weight = 0.25);
    QGraphicsItem *finalizeItem(QGraphicsItem *item, const QString &layer);

    ShaftParams m_params;
    bool m_cancelled = false;   // 설정창에서 취소됨 → 아무것도 그리지 않고 종료 대기
};

#endif // SHAFTTOOL_H
