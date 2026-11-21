#ifndef THRUSTBEARINGDATA_H
#define THRUSTBEARINGDATA_H

#include <QList>
#include <QString>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────
// ThrustBearingSpec / Params — "스러스트베어링(평면자리 단식)" 툴의 규격표와 입력값
//
// 규격표: C:\mlsp\LISP_DAT\BEARING2.DAT (BE3.LSP 의 데이터) — 호칭번호, d, de, D, H (KS B 2022 511~514 계열)
//   d  = 축 와셔(축에 끼는 쪽) 안지름,  de = 하우징 와셔 안지름,  D = 바깥지름,  H = 전체 높이(평면자리형)
//   c  = 와셔 모서리 둥근 반경 — DAT 에 없어 KS 표의 경향으로 채운 추정값(대화상자에서 수정 가능)
//
// 베어링 종류 (KS 표 "평면자리형 / 조심자리형 / 조심자리와셔형"):
//   0 평면자리형        51xxx   : 하우징 와셔 바닥이 평평
//   1 조심자리형        53xxx   : 하우징 와셔 바닥이 구면(볼록) — 높이 H1
//   2 조심자리와셔형    53xxxU  : 조심자리형 + 구면 자리 와셔(오목, 바닥 평평) — 높이 H2
//   조심자리형은 512·513·514 계열에만 있다 (511 계열은 평면자리형만).
//   H1·H2 는 KS 표 값이 없어 H 의 비율(H1≈1.08H, H2≈1.2H)로 구한 추정값 — 수정 가능.
// 복식(KS B 2022 522~524 + 제조사 표): A 평면자리 52xxx / B 조심자리 54xxx / C 조심자리와셔 54xxxU.
//   d·di·D·H·r·r1 는 제조사 표, a 는 d≤70 은 표 값·d≥75 도 표를 읽은 값 (읽기 정확도 한계 있음), 조심자리 높이는 추정.
// 기준점(클릭 위치)은 베어링 전체 높이의 가운데, 축선 위. 축선은 수직(Y 방향, KS 그림과 동일).
// ─────────────────────────────────────────────────────────────────────────
struct ThrustBearingSpec {
    QString name;    // 예: "51205"
    double  d  = 0;
    double  de = 0;
    double  D  = 0;
    double  H  = 0;
    double  c  = 0;
    // 복식(522·523·524 계열)만: di=가운데 와셔 안지름, a=가운데 와셔 두께, c1=가운데 와셔 모서리 반경
    //   (d=바깥 두 와셔 안지름, c=바깥 와셔 모서리 반경)
    bool    dbl = false;
    double  di = 0;
    double  a  = 0;
    double  c1 = 0;
};

struct ThrustBearingParams {
    QString name       = QStringLiteral("51205");
    int     type       = 0;      // 0 평면자리형, 1 조심자리형, 2 조심자리와셔형
    double  d          = 25.0;
    double  de         = 27.0;
    double  D          = 47.0;
    double  H          = 15.0;
    double  c          = 0.6;
    double  H1         = 16.2;   // 조심자리형 전체 높이
    double  H2         = 18.0;   // 조심자리와셔형 전체 높이
    bool    dbl        = false;  // 복식 (522·523·524 계열: 가운데 와셔를 공유하는 양방향 2단 볼)
    double  di         = 20.0;   // 복식: 가운데 와셔 안지름
    double  a          = 7.0;    // 복식: 가운데 와셔 두께
    double  c1         = 0.5;    // 복식: 가운데 와셔 모서리 반경
    bool    drawDims   = true;   // d, de, D, 높이 치수
    bool    drawCenter = true;   // 축선 + 볼 중심선
    bool    drawHatch  = true;   // 와셔 단면 해칭
    bool    drawFull   = true;   // 전체 그리기: 구멍 건너 면선 연결 (BE3.LSP 방식)
    double  hatchScale = 0.0;    // ANSI31 해칭 스케일 (0 이하면 자동)
};

namespace ThrustBearingTable {
const QList<ThrustBearingSpec> &all();
ThrustBearingSpec find(const QString &name);
// 계열 목록: 단식 ("511","512","513","514") / 복식 ("522","523","524")
QStringList families(bool dbl = false);
bool isDoubleName(const QString &name);       // 52xxx = 복식
QString familyOf(const QString &name);          // 51205 → "512"
QList<ThrustBearingSpec> byFamily(const QString &family);
// 조심자리형(53xxx)이 있는 계열인가 (512·513·514)
bool hasSelfAligning(const QString &family);
// 종류 표시 이름과 호칭 (type 1: 53205, type 2: 53205U)
QString typeLabel(int type);
QString displayName(const QString &name, int type);   // 복식은 54xxx / 54xxxU
// H 로부터 H1, H2 추정 (0.1 단위 반올림). 복식은 H 에 대한 비율이 다르다.
double estimateH1(double H, bool dbl = false);
double estimateH2(double H, bool dbl = false);
// 와셔 두께에 맞춘 ANSI31 해칭 스케일 자동 계산 (복식은 a 를 넘기면 한쪽 단 높이로 계산)
double autoHatchScale(double H, double a = -1.0);
}

#endif // THRUSTBEARINGDATA_H
