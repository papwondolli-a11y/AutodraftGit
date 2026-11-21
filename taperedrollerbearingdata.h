#ifndef TAPEREDROLLERBEARINGDATA_H
#define TAPEREDROLLERBEARINGDATA_H

#include <QList>
#include <QString>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────
// TaperedRollerBearingSpec / Params — "테이퍼롤러베어링" 툴의 규격표와 입력값
//
// 규격표: C:\mlsp\LISP_DAT\BEARING1.DAT (BE2.LSP 의 데이터) — 호칭번호, d, D, B, T, C
//   d=콘 안지름, D=컵 바깥지름, B=콘 폭, T=전체 폭, C=컵 폭
// r(컵 후면 반경)·R(콘 후면 반경)은 DAT 에 없어 ISO 355 경향으로 채운 추정값이다.
//   (확인한 값: 30204·30206 = 1/1, 30210 = 1.5/1.5, 30310 = 2.5/2, 32010X = 1/1) → 대화상자에서 수정 가능.
// 기준점(클릭 위치)은 베어링 전체 폭(T)의 가운데, 축선 위. 축선은 수평(X 방향).
// ─────────────────────────────────────────────────────────────────────────
struct TaperedRollerBearingSpec {
    QString name;   // 예: "30206"
    double  d = 0;  // 콘 안지름
    double  D = 0;  // 컵 바깥지름
    double  B = 0;  // 콘 폭
    double  T = 0;  // 전체 폭
    double  C = 0;  // 컵 폭
    double  r = 0;  // 컵 후면 반경 (r)
    double  R = 0;  // 콘 후면 반경 (R)
};

struct TaperedRollerBearingParams {
    QString name       = QStringLiteral("30206");
    double  d          = 30.0;
    double  D          = 62.0;
    double  B          = 16.0;
    double  T          = 17.25;
    double  C          = 14.0;
    double  r          = 1.0;
    double  R          = 1.0;
    double  angle      = 15.0;   // 롤러(레이스) 경사각 — BE2.LSP 는 15° 고정
    bool    drawDims   = true;   // T, C, B, D, d 치수
    bool    drawCenter = true;   // 베어링 중심선 + 롤러 축선
    bool    drawHatch  = true;   // 컵·콘 단면 해칭
    bool    drawFull   = true;   // 전체 그리기: 면선을 베어링 전체 높이로 연결 (BE2.LSP 방식)
    double  hatchScale = 0.0;    // ANSI31 해칭 스케일 (0 이하면 자동 계산값 사용)
};

namespace TaperedRollerBearingTable {
const QList<TaperedRollerBearingSpec> &all();
TaperedRollerBearingSpec find(const QString &name);
// 계열 목록 ("320","302","322","303","323")
QStringList families();
// 호칭번호의 계열 (30206 → "302", 32010 → "320")
QString familyOf(const QString &name);
QList<TaperedRollerBearingSpec> byFamily(const QString &family);
// 컵·콘 단면 두께에 맞춘 ANSI31 해칭 스케일 자동 계산
double autoHatchScale(double d, double D);
}

#endif // TAPEREDROLLERBEARINGDATA_H
