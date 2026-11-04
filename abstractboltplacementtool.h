#ifndef ABSTRACTBOLTPLACEMENTTOOL_H
#define ABSTRACTBOLTPLACEMENTTOOL_H

#include "abstracttool.h"

#include <QPointF>
#include <QString>
#include <QPainterPath>
#include <QList>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────
// AbstractBoltPlacementTool — BoltTool/WrenchBoltTool/HHBoltTool/HBBoltTool
// 4개 볼트 배치 툴에 바이트 단위로 중복돼 있던 입력/상태머신/레이어배치 로직을
// 한곳에 모은 베이스 클래스.
//
// 각 볼트 종류의 실제 형상 계산(Lisp 이식 알고리즘)은 여기서 다루지 않는다 —
// 서브클래스가 아래 4개의 순수 가상 훅만 구현하면, 클릭으로 기준점 지정 →
// 각도 지정(클릭 또는 숫자 입력) → 배치 → 다음 배치 대기로 돌아가는 흐름과
// 외형선/골지름선/중심선 레이어 라우팅 + undo 매크로는 전부 이 클래스가 처리한다.
//
// 입력 흐름(공통):
//   1) activate() 시 서브클래스가 자신의 SetupDialog로 규격/길이/레이어를 선택
//      → finishActivation(logMessage)를 호출해 마무리
//   2) 클릭 : 삽입 기준점 지정
//   3) 클릭 또는 각도 숫자 입력(Enter=0°) : 방향 지정 → 배치
//   배치 후에는 같은 규격으로 계속 배치할 수 있도록 다시 기준점 입력 대기 상태로 돌아간다.
//   우클릭/ESC : 각도 지정 단계면 기준점 입력으로 되돌리고, 기준점 입력 단계면 툴 종료
// ─────────────────────────────────────────────────────────────────────────
class AbstractBoltPlacementTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitPosition, WaitAngle };

    explicit AbstractBoltPlacementTool(QObject *parent = nullptr);

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

protected:
    // ── 서브클래스가 구현할 훅 ──────────────────────────────────
    // length/origin/angleDeg로 실제 world 좌표 형상(LineItem/ArcItem/CircleItem 목록)을
    // 만든다. 서브클래스는 보통 자신의 static buildGeometry(m_size, length, origin, angleDeg, ...)를
    // 그대로 호출하기만 하면 된다.
    virtual QList<QGraphicsItem *> buildPlacementGeometry(double length, const QPointF &origin,
                                                           double angleDeg) const = 0;
    // 위와 동일한 형상을 고스트 미리보기용 QPainterPath로 반환한다.
    virtual QPainterPath buildPlacementPreviewPath(double length, const QPointF &origin,
                                                    double angleDeg) const = 0;
    // 기준점 입력 대기 상태에서 커맨드독에 표시할 안내문구.
    virtual QString positionPromptText() const = 0;
    // undo 매크로에 붙일 짧은 라벨 (예: "볼트 배치").
    virtual QString toolMacroLabel() const = 0;

    // activate() 맨 끝에서 호출 — 상태를 WaitPosition으로 되돌리고, logMessage가 비어있지
    // 않으면 커맨드독에 로그를 남긴 뒤 기준점 입력 프롬프트를 띄운다. logMessage는 보통
    // 서브클래스가 규격/길이를 포함해 직접 구성한 문자열이다(예: "볼트 배치 (M10 x 40)").
    void finishActivation(const QString &logMessage = QString());

    // angleDeg 방향으로 실제 형상을 만들어 배치하고, 다음 배치를 위해 기준점 입력
    // 상태로 되돌린다. lengthOverride>0 이면 그 값을 길이(L)로 쓰고(2점 지정 모드에서
    // 클릭 거리), 그렇지 않으면 m_length를 쓴다.
    void placeItemAndReset(double angleDeg, double lengthOverride = -1.0);

    void cancelTool();
    void abortCurrentPlacement();
    void showPositionPrompt();

    // ── 4개 볼트 툴이 공유하는 상태 ──────────────────────────────
    double  m_length;          // 선택된 호칭길이(L) — 2점 지정 모드에서는 미리보기 기본값으로만 쓰임
    QString m_outlineLayerName; // 외형선 레이어 (빈 문자열 = 현재 레이어)
    QString m_threadLayerName;  // 골지름선 레이어 (빈 문자열 = 현재 레이어)
    bool    m_twoPointMode;     // true면 기준점→방향점 클릭 거리로 길이(L)를 정한다
    bool    m_centerlineEnabled; // true(기본)면 중심선을 생성한다
    bool    m_sideViewEnabled;   // true(기본)면 평면도를 생성한다

    State   m_state;
    QPointF m_origin; // 삽입 기준점
};

#endif // ABSTRACTBOLTPLACEMENTTOOL_H
