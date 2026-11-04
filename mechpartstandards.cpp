#include "mechpartstandards.h"
#include "mechpartsjsonloader.h"

#include <algorithm>

namespace {

// ── 하드코딩 기본값(fallback) ────────────────────────────────────────────
// Data/mechparts/*.json이 없거나 손상됐을 때만 쓰인다. 예전에는 이 값들이
// 공개 상수(kKsB1002HexBolts 등) 그 자체였다 — 지금은 JSON 로드 실패 시의
// 대체값으로만 남아있다. 값 자체는 그대로 유지(변경 없음).

// 육각볼트 규격표 — Lisp\BOLTNUT.LSP 의 (defun C:BOLT ...) "A"(Hex.Bolt) 옵션에
// 내장된 규격표(an2,h,b,d,k) 값을 그대로 옮김. 정면도의 모따기/필렛 계산식이 이
// 수치를 전제로 만들어져 있어서, 다른 출처(카탈로그 이미지)와 값이 살짝 달라도
// 이 LISP 표를 우선한다.
//   pitch      : 보통나사 피치 — 표준 규격값 그대로 사용
//   s          : 머리 대변거리 (Lisp b)
//   k          : 머리 높이 (Lisp h)
//   chamferD   : 머리 상단 모따기 후 남는 원 지름 (Lisp d)
//   filletConst: 나사 끝단 모따기 계산 상수 (Lisp k)
// M22는 LISP 원본표의 대변거리(32)가 ISO 4014 표준값(34)과 달라 표준값(34)으로 보정함.
const QVector<HexBoltSize> &hexBoltDefaults() {
    static const QVector<HexBoltSize> table = {
        { QStringLiteral("M3"),  3.0,  0.50, 5.5,  2.0,  5.3,  0.6 },
        { QStringLiteral("M4"),  4.0,  0.70, 7.0,  2.8,  6.8,  0.8 },
        { QStringLiteral("M5"),  5.0,  0.80, 8.0,  3.5,  7.8,  0.9 },
        { QStringLiteral("M6"),  6.0,  1.00, 10.0, 4.0,  9.8,  1.0 },
        { QStringLiteral("M8"),  8.0,  1.25, 13.0, 5.5,  12.6, 1.2 },
        { QStringLiteral("M10"), 10.0, 1.50, 17.0, 7.0,  16.5, 1.5 },
        { QStringLiteral("M12"), 12.0, 1.75, 19.0, 8.0,  18.0, 2.0 },
        { QStringLiteral("M14"), 14.0, 2.00, 22.0, 9.0,  21.0, 2.0 },
        { QStringLiteral("M16"), 16.0, 2.00, 24.0, 10.0, 23.0, 2.0 },
        { QStringLiteral("M18"), 18.0, 2.50, 27.0, 12.0, 26.0, 2.5 },
        { QStringLiteral("M20"), 20.0, 2.50, 30.0, 13.0, 29.0, 2.5 },
        { QStringLiteral("M22"), 22.0, 2.50, 34.0, 14.0, 31.0, 2.5 },
        { QStringLiteral("M24"), 24.0, 3.00, 36.0, 15.0, 34.0, 3.0 },
        { QStringLiteral("M27"), 27.0, 3.00, 41.0, 17.0, 39.0, 3.0 },
        { QStringLiteral("M30"), 30.0, 3.50, 46.0, 19.0, 44.0, 3.5 },
        { QStringLiteral("M33"), 33.0, 3.50, 50.0, 21.0, 48.0, 3.5 },
        { QStringLiteral("M36"), 36.0, 4.00, 55.0, 23.0, 53.0, 4.0 },
        { QStringLiteral("M39"), 39.0, 4.00, 60.0, 25.0, 57.0, 4.0 },
        { QStringLiteral("M42"), 42.0, 4.50, 65.0, 26.0, 62.0, 4.5 },
    };
    return table;
}

// KS 표준 호칭길이 계열(mm) — 일반 목적 육각볼트에서 흔히 쓰는 우선 길이
const QVector<int> &preferredBoltLengthsDefaults() {
    static const QVector<int> table = {
        10, 12, 16, 20, 25, 30, 35, 40, 45, 50,
        55, 60, 65, 70, 80, 90, 100, 110, 120, 130,
        140, 150, 160, 180, 200
    };
    return table;
}

// 렌치볼트 규격표 — Lisp\wbolt.lsp 의 cond 블록(an2별 size 리스트) 값을 그대로 옮김.
// 원본 리스트 순서: (an2 d h e b m ph k t1 r1) — k, t1은 lsp 안에서도 실제로 쓰이지
// 않는 값이라 이식하지 않았다(WrenchBoltSize에 필드 자체가 없음).
const QVector<WrenchBoltSize> &wrenchBoltDefaults() {
    static const QVector<WrenchBoltSize> table = {
        { QStringLiteral("M3"),  3.0,  0.50, 5.5,  3.0,  0.2, 2.5,  1.6,  0.3  },
        { QStringLiteral("M4"),  4.0,  0.70, 7.0,  4.0,  0.3, 3.0,  2.2,  0.35 },
        { QStringLiteral("M5"),  5.0,  0.80, 8.5,  5.0,  0.3, 4.0,  2.5,  0.35 },
        { QStringLiteral("M6"),  6.0,  1.00, 10.0, 6.0,  0.4, 5.0,  3.0,  0.4  },
        { QStringLiteral("M8"),  8.0,  1.25, 13.0, 8.0,  0.5, 6.0,  4.0,  0.6  },
        { QStringLiteral("M10"), 10.0, 1.50, 16.0, 10.0, 0.6, 8.0,  5.0,  0.6  },
        { QStringLiteral("M12"), 12.0, 1.75, 18.0, 12.0, 0.7, 10.0, 6.0,  1.0  },
        { QStringLiteral("M14"), 14.0, 2.00, 21.0, 14.0, 0.8, 12.0, 7.0,  1.0  },
        { QStringLiteral("M16"), 16.0, 2.00, 24.0, 16.0, 1.0, 14.0, 8.0,  1.0  },
        { QStringLiteral("M18"), 18.0, 2.50, 27.0, 18.0, 1.0, 14.0, 9.0,  1.0  },
        { QStringLiteral("M20"), 20.0, 2.50, 30.0, 20.0, 1.0, 17.0, 10.0, 1.2  },
        { QStringLiteral("M22"), 22.0, 2.50, 33.0, 22.0, 1.0, 19.0, 12.0, 1.2  },
        { QStringLiteral("M24"), 24.0, 3.00, 36.0, 24.0, 1.0, 19.0, 12.0, 1.2  },
        { QStringLiteral("M27"), 27.0, 3.00, 40.0, 27.0, 1.5, 19.0, 13.5, 1.2  },
        { QStringLiteral("M30"), 30.0, 3.50, 45.0, 30.0, 1.5, 22.0, 15.0, 1.5  },
        { QStringLiteral("M33"), 33.0, 3.50, 50.0, 33.0, 1.5, 24.0, 16.5, 1.5  },
        { QStringLiteral("M36"), 36.0, 4.00, 54.0, 36.0, 1.5, 27.0, 18.0, 1.5  },
        { QStringLiteral("M39"), 39.0, 4.00, 58.0, 39.0, 1.5, 27.0, 20.0, 1.6  },
        { QStringLiteral("M42"), 42.0, 4.50, 63.0, 42.0, 2.0, 32.0, 21.0, 1.6  },
        { QStringLiteral("M45"), 45.0, 4.50, 68.0, 45.0, 2.0, 32.0, 23.0, 1.6  },
        { QStringLiteral("M48"), 48.0, 5.00, 72.0, 48.0, 2.0, 36.0, 24.0, 1.6  },
        { QStringLiteral("M52"), 52.0, 5.00, 78.0, 52.0, 2.5, 36.0, 26.0, 1.6  },
    };
    return table;
}

// 접시머리렌치볼트 규격표 — Lisp\hhbolt.lsp 의 cond 블록(an2별 size 리스트) 값을 그대로 옮김.
// 원본 리스트 순서: (an2 ph k a h m c g sl) — 마지막 sl(나사부길이)은 이식하지 않음(HHBoltSize.h 주석 참고).
const QVector<HHBoltSize> &hhBoltDefaults() {
    static const QVector<HHBoltSize> table = {
        { QStringLiteral("M3"),   3.0, 0.50, 0.6,  6.0, 1.7, 1.2, 0.25, 2.0  },
        { QStringLiteral("M4"),   4.0, 0.70, 0.8,  8.0, 2.3, 1.8, 0.3,  2.5  },
        { QStringLiteral("M5"),   5.0, 0.80, 0.9, 10.0, 2.8, 2.3, 0.3,  3.0  },
        { QStringLiteral("M6"),   6.0, 1.00, 1.0, 12.0, 3.3, 2.5, 0.3,  4.0  },
        { QStringLiteral("M8"),   8.0, 1.25, 1.2, 16.0, 4.4, 3.5, 0.4,  5.0  },
        { QStringLiteral("M10"), 10.0, 1.50, 1.5, 20.0, 5.5, 4.4, 0.5,  6.0  },
        { QStringLiteral("M12"), 12.0, 1.75, 2.0, 24.0, 6.5, 4.6, 0.5,  8.0  },
        { QStringLiteral("M16"), 16.0, 2.00, 2.0, 30.0, 7.5, 5.3, 0.5, 10.0  },
    };
    return table;
}

// 둥근머리렌치볼트 규격표 — Lisp\hb.lsp 의 cond 블록(an2별 size 리스트) 값을 그대로 옮김.
// 원본 리스트 순서: (an2 ph k a h h1 g h2)
const QVector<HBBoltSize> &hbBoltDefaults() {
    static const QVector<HBBoltSize> table = {
        { QStringLiteral("M3"),   3.0, 0.50, 0.6,  5.7, 1.65, 1.1, 2.0, 1.0 },
        { QStringLiteral("M4"),   4.0, 0.70, 0.8,  7.6, 2.2,  1.3, 2.5, 1.4 },
        { QStringLiteral("M5"),   5.0, 0.80, 0.9,  9.5, 2.75, 1.6, 3.0, 1.6 },
        { QStringLiteral("M6"),   6.0, 1.00, 1.0, 10.5, 3.3,  2.1, 4.0, 2.0 },
        { QStringLiteral("M8"),   8.0, 1.25, 1.2, 14.0, 4.4,  2.6, 5.0, 2.5 },
        { QStringLiteral("M10"), 10.0, 1.50, 1.5, 17.5, 5.5,  3.2, 6.0, 3.0 },
        { QStringLiteral("M12"), 12.0, 1.75, 2.0, 21.0, 6.6,  4.2, 8.0, 3.5 },
        { QStringLiteral("M16"), 16.0, 2.00, 2.0, 28.0, 8.8,  5.2, 10.0, 4.0 },
    };
    return table;
}

// ── JSON 로드 + 기본값 대체 (지연 초기화용 free function — 람다 금지 규칙 준수) ──
QVector<HexBoltSize> computeHexBoltTable() {
    QVector<HexBoltSize> loaded = loadHexBoltSizesFromJson();
    return loaded.isEmpty() ? hexBoltDefaults() : loaded;
}
QVector<int> computePreferredBoltLengths() {
    QVector<int> loaded = loadBoltLengthsFromJson();
    return loaded.isEmpty() ? preferredBoltLengthsDefaults() : loaded;
}
QVector<WrenchBoltSize> computeWrenchBoltTable() {
    QVector<WrenchBoltSize> loaded = loadWrenchBoltSizesFromJson();
    return loaded.isEmpty() ? wrenchBoltDefaults() : loaded;
}
QVector<HHBoltSize> computeHHBoltTable() {
    QVector<HHBoltSize> loaded = loadHHBoltSizesFromJson();
    return loaded.isEmpty() ? hhBoltDefaults() : loaded;
}
QVector<HBBoltSize> computeHBBoltTable() {
    QVector<HBBoltSize> loaded = loadHBBoltSizesFromJson();
    return loaded.isEmpty() ? hbBoltDefaults() : loaded;
}

} // namespace

// ── 공개 접근자 — 함수 호출 시점(첫 호출)에 한 번만 JSON을 읽어 캐시한다 ──
// (QCoreApplication 인스턴스가 이미 생성된 뒤에 처음 호출되므로 경로 탐색이 안전하다.
//  함수-로컬 static은 C++11부터 스레드세이프한 초기화가 보장된다.)
const QVector<HexBoltSize> &kKsB1002HexBolts() {
    static const QVector<HexBoltSize> cache = computeHexBoltTable();
    return cache;
}

const QVector<int> &kKsPreferredBoltLengths() {
    static const QVector<int> cache = computePreferredBoltLengths();
    return cache;
}

const QVector<WrenchBoltSize> &kKsWrenchBolts() {
    static const QVector<WrenchBoltSize> cache = computeWrenchBoltTable();
    return cache;
}

const QVector<HHBoltSize> &kKsHHBolts() {
    static const QVector<HHBoltSize> cache = computeHHBoltTable();
    return cache;
}

const QVector<HBBoltSize> &kKsHBBolts() {
    static const QVector<HBBoltSize> cache = computeHBBoltTable();
    return cache;
}

double hexBoltThreadLength(double d, double L) {
    double b;
    if (L <= 125.0)      b = 2.0 * d + 6.0;
    else if (L <= 200.0) b = 2.0 * d + 12.0;
    else                 b = 2.0 * d + 25.0;
    // 전조볼트(나사부가 머리 바로 아래까지 이어짐)처럼 b가 L보다 커지는 경우 보정
    return std::min(b, L);
}
