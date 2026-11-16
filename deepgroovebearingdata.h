#ifndef DEEPGROOVEBEARINGDATA_H
#define DEEPGROOVEBEARINGDATA_H

#include <QList>
#include <QString>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────
// DeepGrooveBearingSpec / Params — "깊은 홈 볼베어링" 툴의 규격표와 입력값
//
// 규격표(KS B 2023 / ISO 15 의 69 계열): 호칭번호, 안지름 d, 바깥지름 D, 폭 B, 최소 모따기 r
// 기준점(클릭 위치)은 베어링 중심(축선 위, 폭의 가운데). 축선은 수평(X 방향).
// ─────────────────────────────────────────────────────────────────────────
struct DeepGrooveBearingSpec {
    QString name;   // 예: "6900"
    double  d = 0;  // 안지름
    double  D = 0;  // 바깥지름
    double  B = 0;  // 폭
    double  r = 0;  // 최소 모따기
};

struct DeepGrooveBearingParams {
    QString name       = QStringLiteral("6900");
    double  d          = 10.0;
    double  D          = 22.0;
    double  B          = 6.0;
    double  r          = 0.3;
    bool    drawDims   = true;   // B, ød, øD 치수
    bool    drawCenter = true;   // 중심선
    bool    drawHatch  = true;   // 단면 해칭
    bool    drawFull   = true;   // 전체 그리기: 위·아래 연결선(BE1.LSP 방식), 끄면 단면 두 개만
    double  hatchScale = 0.0;    // ANSI31 해칭 스케일 (0 이하면 자동 계산값 사용)
};

namespace DeepGrooveBearingTable {
// 69 계열 규격표 (693 ~ 6940)
const QList<DeepGrooveBearingSpec> &all();
// 호칭번호로 찾기 (없으면 name 이 빈 규격)
DeepGrooveBearingSpec find(const QString &name);
// 계열 목록 ("60","62","63","64","68","69")
QStringList families();
// 호칭번호의 계열 (603 → "60", 693 → "69", 6205 → "62")
QString familyOf(const QString &name);
// 해당 계열의 규격 목록
QList<DeepGrooveBearingSpec> byFamily(const QString &family);
// 링 높이 (D-d)/2 에 비례한 ANSI31 해칭 스케일 자동 계산 (해칭 간격 ≈ 링 높이/6)
double autoHatchScale(double d, double D);
}

#endif // DEEPGROOVEBEARINGDATA_H
