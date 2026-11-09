#ifndef SHAFTDATA_H
#define SHAFTDATA_H

#include <QList>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
// ShaftSegment / ShaftParams — "축(Shaft)" 툴의 입력값
//
// 축은 왼쪽 끝에서 오른쪽 끝으로 이어지는 "구간(원통) 리스트"로 정의한다.
//   - 구간마다 지름·길이·끼워맞춤 기호(g6, k5 …)·좌/우 모따기
//   - 칼라(플랜지)·홈 폭 같은 것도 지름이 다른 짧은 구간으로 표현한다.
// 기준점(클릭 위치)은 축 왼쪽 끝면의 중심(축선 위의 점)이다. 축선은 수평(X축 방향).
// ─────────────────────────────────────────────────────────────────────────
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
};

// 축 양쪽 끝단에 붙일 수 있는 형상
enum class ShaftEndType {
    None = 0,
    Keyway,   // 키홈
    Thread    // 나사부 (육각볼트와 같은 골지름선 표현)
};

// 키홈(측면도): 끝면에서 안쪽으로 파인 홈. 안쪽 끝은 반원, 끝면 쪽은 열려 있다.
//   상단/하단 = 홈의 위쪽·아래쪽 벽이 "홈 중앙선"에서 떨어진 거리,
//   중앙      = 홈 중앙선이 축 중심선에서 위(+)/아래(-)로 벗어난 양.
struct ShaftKeyway {
    double length = 14.0; // 끝면에서 홈 안쪽 끝까지
    double upper  = 2.5;  // 상단
    double center = 0.0;  // 중앙
    double lower  = 2.5;  // 하단
    bool   showSection  = false; // 단면(키홈 단면도)을 도형 상단에 함께 그림
    int    sectionPos   = -1;    // 단면 위치: 0=구간 앞(왼쪽) 1=중앙 2=뒤(오른쪽), -1=끝단 쪽(자동)
    double sectionDepth = 3.0;   // 단면도의 홈 깊이 (축 윗면에서 홈 바닥까지)
};

// 나사부: 끝단 구간의 지름이 나사 호칭경. 골지름 = d - 1.082532 * pitch
struct ShaftThread {
    double length = 12.0; // 끝면에서 나사 시작선까지
    double pitch  = 0.0;  // 0 이면 호칭경에 맞는 표준 거친나사 피치 사용
};

struct ShaftEnd {
    ShaftEndType type = ShaftEndType::None;
    ShaftKeyway  keyway;
    ShaftThread  thread;
};

struct ShaftParams {
    QList<ShaftSegment> segments;
    ShaftEnd leftEnd;    // 첫 구간의 왼쪽 끝
    ShaftEnd rightEnd;   // 마지막 구간의 오른쪽 끝
    bool   drawCenterLine = true;
    bool   diameterDims   = true;   // 지름 치수 전체 on/off
    bool   lengthDims     = true;   // 길이 치수(구간별 + 전체) 전체 on/off
    double dimGap         = 8.0;    // 도형에서 첫 치수선까지 거리
    double dimStep        = 7.0;    // 치수선 단 간격
    double centerExtend   = 4.0;    // 중심선이 양 끝에서 튀어나오는 길이
    QString bodyLayer;              // 축 외형을 그릴 레이어 (비우면 기본 "SHAFT")

    // 예시 기본값(138mm 계단축: Ø12·Ø16·Ø20·칼라 Ø25·Ø20·Ø14)
    static ShaftParams sample();

    // 저장/복원 (ToolDefaults 와 프리셋용 JSON 문자열)
    QString toJsonString() const;
    static ShaftParams fromJsonString(const QString &json, const ShaftParams &fallback);

    // 표준 거친나사 피치 (호칭경 → 피치)
    static double standardPitch(double nominalDiameter);
    // 나사부에 실제로 쓸 피치 (thread.pitch 가 0 이면 표준값)
    static double effectivePitch(const ShaftThread &thread, double nominalDiameter);

    double totalLength() const;
    double maxDiameter() const;
};

#endif // SHAFTDATA_H
