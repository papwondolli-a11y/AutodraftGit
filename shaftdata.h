#ifndef SHAFTDATA_H
#define SHAFTDATA_H

#include <QList>
#include <QString>

class QJsonObject;

// ─────────────────────────────────────────────────────────────────────────
// ShaftSegment / ShaftParams — "축(Shaft)" 툴의 입력값
//
// 축은 왼쪽 끝에서 오른쪽 끝으로 이어지는 "구간(원통) 리스트"로 정의한다.
//   - 구간마다 지름·길이·끼워맞춤 기호(g6, k5 …)·좌/우 모따기·키홈
//   - 칼라(플랜지)·홈 폭 같은 것도 지름이 다른 짧은 구간으로 표현한다.
//   - 나사부는 축 양 끝(첫 구간 왼쪽 / 마지막 구간 오른쪽)에만 낼 수 있다.
// 기준점(클릭 위치)은 축 왼쪽 끝면의 중심(축선 위의 점)이다. 축선은 수평(X축 방향).
// ─────────────────────────────────────────────────────────────────────────

// 키홈 형태
enum class ShaftKeywayType {
    Open = 0,   // 끝 열림: 축 끝면에서 시작하는 홈 (첫/마지막 구간만, 안쪽 끝만 반원)
    Closed      // 막힘: 양 끝이 반원인 홈 (어느 구간이든)
};

// 키홈 위치 기준 (구간 안에서)
enum class ShaftKeywayRef {
    Center = 0, // 구간 중앙
    Left,       // 구간 왼쪽 면에서 offset
    Right       // 구간 오른쪽 면에서 offset
};

// 키홈(측면도): 구간 위에 놓인 홈.
//   길이 = 홈 전체 길이(반원 포함). 끝 열림은 끝면에서 안쪽 반원 끝까지.
//   상단/하단 = 홈의 위쪽·아래쪽 벽이 "홈 중앙선"에서 떨어진 거리,
//   중앙      = 홈 중앙선이 축 중심선에서 위(+)/아래(-)로 벗어난 양.
//   끝 열림은 ref 가 열린 쪽(Left=축 왼쪽 끝, Right=축 오른쪽 끝)이고 offset 은 쓰지 않는다.
struct ShaftKeyway {
    bool            enabled = false;
    ShaftKeywayType type    = ShaftKeywayType::Closed;
    ShaftKeywayRef  ref     = ShaftKeywayRef::Center;
    double offset = 0.0;  // 기준면(구간 왼쪽/오른쪽 면)에서 홈의 가까운 끝까지
    double length = 14.0;
    double upper  = 2.5;  // 상단
    double center = 0.0;  // 중앙
    double lower  = 2.5;  // 하단
    bool   showSection  = false; // 위쪽 절반을 길이 방향 단면(해칭)으로 그림
    double sectionDepth = 3.0;   // 홈 깊이 (축 윗면에서 홈 바닥까지) — 단면 표시·3D 공용

    double width() const { return upper + lower; }

    QJsonObject toJson() const;
    static ShaftKeyway fromJson(const QJsonObject &o, const ShaftKeyway &def);

    // KS B 1311 평행키 표준값 (축 지름 → 키 폭 b, 축 홈 깊이 t1). 범위 밖이면 false.
    static bool standardSize(double shaftDiameter, double &width, double &depth);
};

struct ShaftSegment {
    double  diameter     = 20.0;
    double  length       = 20.0;
    QString fit;                  // 끼워맞춤 등급기호 (비우면 공차 없음)
    double  chamferLeft  = 0.0;   // 왼쪽 끝 모따기(C값, 0이면 없음)
    double  chamferRight = 0.0;   // 오른쪽 끝 모따기
    bool    dimDiameter  = true;  // 지름 치수 기입
    bool    showFit      = true;  // 끼워맞춤 공차(g6 등)를 지름 치수에 표시
    bool    dimLength    = true;  // 구간 길이 치수 기입
    bool    lenTolUse    = false; // 길이 치수에 상·하한 공차 표시
    double  lenTolUpper  = 0.0;   // 길이 상한 편차 (예: +0.01)
    double  lenTolLower  = 0.0;   // 길이 하한 편차 (예: 0 또는 -0.02)
    ShaftKeyway keyway;           // 이 구간의 키홈 (enabled=false 면 없음)
};

// 나사부: 끝 구간의 지름이 나사 호칭경. 골지름 = d - 1.082532 * pitch
struct ShaftThread {
    double length = 12.0; // 끝면에서 나사 시작선까지
    double pitch  = 0.0;  // 0 이면 호칭경에 맞는 표준 거친나사 피치 사용
};

// 나사부를 낼 끝
enum class ShaftThreadSide {
    None = 0,
    Left,
    Right,
    Both
};

struct ShaftParams {
    QList<ShaftSegment> segments;
    ShaftThreadSide threadSide = ShaftThreadSide::None;
    ShaftThread leftThread;     // 첫 구간 왼쪽 끝 나사
    ShaftThread rightThread;    // 마지막 구간 오른쪽 끝 나사
    bool   drawCenterLine = true;
    bool   diameterDims   = true;   // 지름 치수 전체 on/off
    bool   lengthDims     = true;   // 길이 치수(구간별 + 전체) 전체 on/off
    double dimGap         = 8.0;    // 도형에서 첫 치수선까지 거리
    double dimStep        = 7.0;    // 치수선 단 간격
    double centerExtend   = 4.0;    // 중심선이 양 끝에서 튀어나오는 길이
    QString bodyLayer;              // 축 외형을 그릴 레이어 (비우면 기본 "SHAFT")

    // side: 0=왼쪽 끝, 1=오른쪽 끝
    bool hasThread(int side) const;
    const ShaftThread &thread(int side) const { return side == 0 ? leftThread : rightThread; }

    // 예시 기본값(138mm 계단축: Ø12·Ø16·Ø20·칼라 Ø25·Ø20·Ø14)
    static ShaftParams sample();

    // 저장/복원 (ToolDefaults 와 프리셋용 JSON 문자열). 옛 형식(leftEnd/rightEnd)도 읽는다.
    QString toJsonString() const;
    static ShaftParams fromJsonString(const QString &json, const ShaftParams &fallback);

    // 옛 끝단 형식 {type:0/1/2, kwLen…, thLen…} 를 side(0=왼쪽,1=오른쪽) 끝에 적용
    void applyLegacyEnd(int side, const QJsonObject &o);

    // 표준 거친나사 피치 (호칭경 → 피치)
    static double standardPitch(double nominalDiameter);
    // 나사부에 실제로 쓸 피치 (thread.pitch 가 0 이면 표준값)
    static double effectivePitch(const ShaftThread &thread, double nominalDiameter);

    double totalLength() const;
    double maxDiameter() const;
};

#endif // SHAFTDATA_H
