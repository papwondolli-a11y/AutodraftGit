#ifndef MECHPARTSTANDARDS_H
#define MECHPARTSTANDARDS_H

#include <QString>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────
// MechPartStandards — 기계부품(볼트/너트/와셔 등) 표준 규격 데이터
//
// 현재는 KS B 1002(육각볼트) 계열만 채워져 있다. "기계부품" 탭의 너트/와셔 등
// 다른 항목을 추가할 때도 동일한 호칭경(d) 목록을 공유해서 쓸 수 있도록
// 이 파일에 데이터를 모아둔다.
//
// 아래 5개 규격표(kKsB1002HexBolts 등)는 모두 "함수 호출" 형태다(예전엔
// extern const QVector<...>로 컴파일 시점에 고정된 전역 상수였음). 지금은
// Data/mechparts/*.json(mechpartsjsonloader.h)에서 재컴파일 없이 값을 읽어오고,
// 파일이 없거나 손상됐으면 이 파일에 남아있는 하드코딩 기본값으로 대체한다.
// 함수 호출 시점(다이얼로그 생성 등, main() 진입 이후)까지 로딩을 미루는 게
// 중요하다 — QCoreApplication::applicationDirPath()는 앱 인스턴스가 생성된
// 뒤에만 유효해서, 전역 변수 정적 초기화 시점에 즉시 읽으면 경로 탐색이 깨진다.
// ─────────────────────────────────────────────────────────────────────────

// 육각볼트 한 호칭경(예: M10)에 대응하는 규격 치수
// s/k/chamferD/filletConst 값은 Lisp\BOLTNUT.LSP 의 C:BOLT "A"(Hex.Bolt) 옵션에
// 내장된 규격표(an2,h,b,d,k) 값을 그대로 옮긴 것 — 도면 형상(모따기/필렛)이
// 이 수치들을 기준으로 계산되므로, 다른 출처의 표(예: 카탈로그 이미지)와 값이
// 살짝 달라도 이 표를 우선한다(형상 비례가 깨지지 않도록).
struct HexBoltSize {
    QString label;       // 호칭경 표기, 예: "M10"
    double  d;           // 호칭경(나사 바깥지름, mm) — Lisp an2
    double  pitch;       // 나사 피치(mm, 보통나사 기준)
    double  s;           // 머리 대변거리(across flats, mm) — Lisp b
    double  k;           // 머리 높이(mm) — Lisp h
    double  chamferD;    // 머리 상단 모따기 후 남는 원 지름(mm) — Lisp d
    double  filletConst; // 나사 끝단 모따기/필렛 계산에 쓰이는 상수(mm) — Lisp k

    // 머리 대각거리(across corners, mm) — 대변거리로부터 정육각형 기하로 계산
    double e() const { return s / 0.8660254037844387; } // s / cos(30°)
};

// KS B 1002 육각볼트 M6~M30 규격 목록 (실무에서 흔히 쓰는 사이즈 위주)
// — Data/mechparts/hexbolt.json에서 로드, 없으면 내장 기본값 사용
const QVector<HexBoltSize> &kKsB1002HexBolts();

// KS 표준 호칭길이(L) 계열, mm (일반 목적용 우선 길이 계열)
// — Data/mechparts/boltlengths.json에서 로드, 없으면 내장 기본값 사용
const QVector<int> &kKsPreferredBoltLengths();

// 나사부 길이(b) 계산 — KS B 1002 / ISO 4014 공통 공식
//   L<=125 : b = 2d+6
//   125<L<=200 : b = 2d+12
//   L>200 : b = 2d+25
// (전조볼트처럼 L 전체가 나사부인 경우를 대비해 결과가 L을 넘지 않도록 보정)
double hexBoltThreadLength(double d, double L);

// 렌치볼트(육각구멍붙이볼트, 소켓헤드캡스크류) 한 호칭경(예: M10)에 대응하는 규격 치수
// Lisp\wbolt.lsp 의 (defun c:wbolt ...) 에 내장된 규격표(an2,d,h,e,b,m,ph,k,t1,r1) 값을
// 그대로 옮긴 것 — k(나사 끝단 모따기용, 표에서만 정의되고 실제로는 쓰이지 않음)와
// t1(마찬가지로 lsp 안에서 미사용) 두 컬럼은 이식하지 않았다.
struct WrenchBoltSize {
    QString label;      // 호칭경 표기, 예: "M10"
    double  d;           // 호칭경(나사 바깥지름, mm) — Lisp an2
    double  pitch;        // 나사 피치(mm, 보통나사 기준) — Lisp ph
    double  headD;         // 머리(원통) 바깥지름(mm) — Lisp d
    double  headH;          // 머리 높이(mm) — Lisp h
    double  chamferE;        // 머리 상단/소켓 입구 모따기 계산 상수(mm) — Lisp e
    double  socketWidth;      // 육각 소켓 대변거리(mm) — Lisp b
    double  socketDepth;       // 육각 소켓 깊이(mm) — Lisp m
    double  filletR;            // 머리/몸통 경계 필렛 반지름(mm) — Lisp r1
};

// KS(=ISO 4762) 렌치볼트(육각구멍붙이볼트) M3~M52 규격 목록 — Lisp\wbolt.lsp 표 그대로
// — Data/mechparts/wrenchbolt.json에서 로드, 없으면 내장 기본값 사용
const QVector<WrenchBoltSize> &kKsWrenchBolts();

// 접시머리렌치볼트(육각구멍붙이 접시머리볼트, 플랫헤드 소켓캡스크류) 한 호칭경(예: M10)에
// 대응하는 규격 치수. Lisp\hhbolt.lsp 의 (defun c:hhbolt ...) 에 내장된 규격표
// (an2,ph,k,a,h,m,c,g,sl) 값을 그대로 옮긴 것 — sl(나사부길이) 컬럼은 표에 있지만 lsp
// 안에서 실제로 쓰이지 않던 값이라(전체 길이를 항상 전나사로 그림) 이식하지 않았다.
// 대신 HHBoltTool은 BoltTool/WrenchBoltTool과 동일하게 hexBoltThreadLength()로 나사부
// 길이를 계산해 전나사/부분나사를 선택할 수 있게 새로 지원한다.
struct HHBoltSize {
    QString label;       // 호칭경 표기, 예: "M10"
    double  d;            // 호칭경(나사 바깥지름, mm) — Lisp an2
    double  pitch;         // 나사 피치(mm, 보통나사 기준) — Lisp ph
    double  tipChamferK;    // 나사 끝단 모따기 높이(mm) — Lisp k
    double  headD;            // 머리(접시머리) 지름(mm) — Lisp a
    double  headH;             // 머리 높이(원추 깊이, mm) — Lisp h
    double  socketDepth;        // 육각 소켓 깊이(mm) — Lisp m
    double  cornerH;              // 머리 상단 모서리 높이(및 소켓 바닥 처짐량, mm) — Lisp c
    double  socketWidth;           // 육각 소켓 대변거리(mm) — Lisp g
};

// hhbolt.lsp 표에 정의된 M3~M16 규격 목록 (그 외 호칭경은 원본 lsp에도 없음)
// — Data/mechparts/hhbolt.json에서 로드, 없으면 내장 기본값 사용
const QVector<HHBoltSize> &kKsHHBolts();

// 둥근머리렌치볼트(육각구멍붙이 둥근머리볼트, 버튼헤드 소켓캡스크류) 한 호칭경(예: M10)에
// 대응하는 규격 치수. Lisp\hb.lsp 의 (defun c:hb ...) 에 내장된 규격표
// (an2,ph,k,a,h,h1,g,h2) 값을 그대로 옮긴 것.
// hb.lsp 는 hhbolt.lsp 와 달리 나사부 길이(b) 표가 아예 없고, 대신 머리 바로 아래
// 무나사부 목 길이(h2)가 규격표에 고정값으로 정의돼 있어 항상 그 길이만큼만 골지름선을
// 그린다(다른 볼트류의 "전나사/부분나사" 토글과 달리 이 볼트는 애초에 토글 개념이 없다 —
// HBBoltTool에도 전나사 옵션을 넣지 않았다).
struct HBBoltSize {
    QString label;       // 호칭경 표기, 예: "M10"
    double  d;            // 호칭경(나사 바깥지름, mm) — Lisp an2
    double  pitch;         // 나사 피치(mm, 보통나사 기준) — Lisp ph
    double  tipChamferK;    // 나사 끝단 모따기 높이(mm) — Lisp k
    double  headD;            // 머리(둥근머리) 지름(mm) — Lisp a
    double  headH;             // 머리 높이(mm) — Lisp h
    double  socketDepth;        // 육각 소켓 깊이(mm) — Lisp h1
    double  socketWidth;         // 육각 소켓 대변거리(mm) — Lisp g
    double  neckLen;              // 머리 바로 아래 무나사부 목 길이(mm) — Lisp h2
};

// hb.lsp 표에 정의된 M3~M16 규격 목록 (그 외 호칭경은 원본 lsp에도 없음)
// — Data/mechparts/hbbolt.json에서 로드, 없으면 내장 기본값 사용
const QVector<HBBoltSize> &kKsHBBolts();

#endif // MECHPARTSTANDARDS_H
