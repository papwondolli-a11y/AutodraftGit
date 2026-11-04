#ifndef BEAMSCHEDULE_H
#define BEAMSCHEDULE_H

#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────────
// 보 일람표 데이터 모델
//   기본 표 : blocks/etc/beamlist.cdb (축척 1 기준 748 x 525, 띠 5단 x 보 5열 = 보 25개)
//   사용자 표 : 도면에 이미 그려진 표 + 표 정보(칸 폭/높이, 도면 사이즈)
//   보 1개 = 열 1개를 3등분 → 단부(INT. END) / 중앙(CENTER) / 단부(EXT. END)  (왼쪽부터)
// ─────────────────────────────────────────────────────────────────────────────

// 철근 (상부근 / 하부근) : 1열(상) / 2열(하) 개수
struct BeamBars {
    int dia  = 16;   // 직경 (D16 → 16)
    int row1 = 0;    // 1열(상) 개수
    int row2 = 0;    // 2열(하) 개수

    int total() const { return row1 + row2; }
};

// 보 단면 1개
struct BeamSection {
    BeamBars top;                 // 상부근
    BeamBars bottom;              // 하부근
    int      stirrupDia   = 16;   // 스트럽 직경
    double   stirrupPitch = 300;  // 스트럽 간격
    bool     stirrupHigh  = true; // 고강도 (HD16@300 의 H)
};

struct BeamMark {
    QString     name;             // 부호 (FG1)
    double      width = 600.0;    // 보 폭 b
    double      depth = 800.0;    // 보 춤 D (슬래브 두께 포함)
    BeamSection sec[3];           // 왼쪽부터 0 = INT. END, 1 = CENTER, 2 = EXT. END

    bool isEmpty() const { return name.trimmed().isEmpty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// 표 정보 (사용자 표) — 길이 값은 "축척을 곱하기 전" 크기(샘플 양식 기준)이며,
// 실제 도면 크기 = 값 x scale (도면 축척은 현재 도면의 축척이 자동 적용된다)
//   행 구성 (아래 → 위) : 스트럽, 하부근, 상부근, 크기, 위치, 형태, 부호
// ─────────────────────────────────────────────────────────────────────────────
struct BeamTableInfo {
    enum Row { Stirrup = 0, Bottom, Top, Size, Position, Shape, Name, RowCount };

    QString paperSize = QStringLiteral("A1");   // 출력용 도면 사이즈
    double  scale     = 1.0;                    // 도면 축척 (1:scale) — 현재 도면 축척이 적용됨
    double  marginW   = 93.0;                   // 도면 가로 여백 합 (도면 사이즈 - 표 바깥 폭)
    double  marginH   = 69.0;                   // 도면 세로 여백 합
    double  labelColW = 53.0;                   // 라벨(스트럽/하부근...) 열 폭
    double  beamColW  = 125.0;                  // 보 1개 열 폭 (형태란 폭)
    double  rightColW = 70.0;                   // 오른쪽 도면사이즈 칸 폭
    double  rowH[RowCount] = {10, 10, 10, 10, 10, 40, 15};   // 행 높이
    double  bandGap   = 0.0;                    // 띠와 띠 사이 추가 간격

    // 샘플 양식(beamlist.cdb) 값 + 도면 축척
    static BeamTableInfo defaults(double scale);

    // 도면 사이즈 (mm) — 알 수 없는 이름이면 false
    static bool paperMm(const QString &name, double &w, double &h);

    double bandPitch() const;      // 띠 1개의 높이 (행 높이 합 + 띠 간격)
    int    columns() const;        // 도면 사이즈 안에 들어가는 보 열 수
    int    bands() const;          // 도면 사이즈 안에 들어가는 띠(단) 수
    int    slotTotal() const { return columns() * bands(); }
};

struct BeamScheduleData {
    static constexpr int kDefaultBands   = 5;   // 기본 표 띠 단수
    static constexpr int kDefaultColumns = 5;   // 기본 표 보 열 수
    static constexpr int kDefaultSlots   = kDefaultBands * kDefaultColumns;

    bool                useCustomTable = false;   // false = 기본 표 삽입, true = 사용자 표 사용
    BeamTableInfo       table;                    // 표 정보 (사용자 표 / 오른쪽 도면사이즈 표기)
    QVector<BeamMark>   beams;                    // 슬롯 순서 : 위 띠 왼쪽부터

    // 보 단면 도형에 쓰는 레이어 3개
    QString             beamLayer    = QStringLiteral("beam");     // 보 외곽
    QString             stirrupLayer = QStringLiteral("beam1");    // 스트럽
    QString             barLayer     = QStringLiteral("striup");   // 철근
    double              shapeScale = 1.0;                           // (저장 안 함) 단면 도형 축소 배율
    QStringList         replaceLayers;                              // (저장 안 함) 다시 만들 때 함께 지울 이전 레이어

    BeamScheduleData() : beams(kDefaultSlots) {}

    int slotCount() const { return useCustomTable ? table.slotTotal() : kDefaultSlots; }
    // 슬롯 수를 모드에 맞춘다 (입력된 보가 뒤쪽에 남아 있으면 그 위치까지는 유지)
    void normalizeSlots() {
        int last = -1;
        for (int i = 0; i < beams.size(); ++i)
            if (!beams.at(i).isEmpty()) last = i;
        const int need = slotCount();
        beams.resize(need > last + 1 ? need : last + 1);
    }

    // 입력된 보만 순서대로 (빈 슬롯 제외)
    QVector<BeamMark> packedBeams() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// 표기 문자열 / 저장
// ─────────────────────────────────────────────────────────────────────────────
namespace BeamScheduleText {
    QString stirrup(const BeamSection &s);              // HD16@300
    QString bars(const BeamBars &b);                    // D16 - 4  (개수 0 이면 빈 문자열)
    QString size(const BeamMark &m);                    // 600 x 800
    QString position(int sectionIndex);                 // INT. END / CENTER / EXT. END

    QString toJson(const BeamScheduleData &d);
    BeamScheduleData fromJson(const QString &json, double drawingScale);
}

// ─────────────────────────────────────────────────────────────────────────────
// 생성될 도형 (레이어별)
// ─────────────────────────────────────────────────────────────────────────────
struct SchedLine   { QLineF line;     QString layer; };
struct SchedCircle { QPointF center;  double radius = 0.0; QString layer; };
struct SchedText   { QString text;    QPointF pos; double size = 0.0; int align = 0; QString layer; };

struct ScheduleGeom {
    QVector<SchedLine>   lines;
    QVector<SchedCircle> circles;
    QVector<SchedText>   texts;
    int                  dropped = 0;   // 용량 초과로 생성하지 못한 보 개수
};

// 단면 도형 치수 (beam-list.cdb 샘플, 실치수 mm)
struct BeamShapeStyle {
    double slabThickness = 200.0;   // 슬래브 두께
    double flangeOverhang = 100.0;  // 웹 좌우로 내민 슬래브 길이
    double stirrupInset  = 40.0;    // 외곽 → 스트럽 간격
    double barFromFace   = 65.0;    // 보 면 → 1단 철근 중심
    double rowPitch      = 45.0;    // 1열 ↔ 2열 간격
    double barSideInset  = 25.0;    // 스트럽 → 양끝 철근 중심

    // 단면 도형 레이어 (보 외곽 / 스트럽 / 철근)
    QString beamLayer    = QStringLiteral("beam");
    QString stirrupLayer = QStringLiteral("beam1");
    QString barLayer     = QStringLiteral("striup");

    double  shapeScale   = 1.0;     // 단면 도형 배율 (형태란보다 클 때 축소용, 1 = 실치수)
};

namespace BeamScheduleBuilder {
    extern const QString kTableLayer;    // "B-LIST"
    extern const QString kLabelLayer;    // "T"
    extern const QString kValueLayer;    // "text"
    extern const QString kPaperLayer;    // "W1"
    extern const QString kBeamLayer;     // "beam"
    extern const QString kStirrupLayer;  // "beam1"
    extern const QString kBarLayer;      // "striup"

    // 기본 표 전체 크기 (축척 1 기준 748 x 525)
    QPointF tableSize(double scale);

    // 기본 표 틀 선 (origin = 좌하단)
    QVector<SchedLine> frameLines(const QPointF &origin, double scale);

    // 기본 표 : 틀 + 라벨 + 도면 사이즈 + 보 내용 (origin = 표 좌하단)
    ScheduleGeom build(const BeamScheduleData &data, const QPointF &origin, double scale,
                       const BeamShapeStyle &style = BeamShapeStyle());

    // 사용자 표 : 보 내용만 (cellLL = 첫 번째 보 형태란의 좌하단)
    ScheduleGeom buildCustom(const BeamScheduleData &data, const QPointF &cellLL,
                             const BeamShapeStyle &style = BeamShapeStyle());

    // 사용자 표에 보 내용이 차지하는 영역 (기존 내용 삭제용)
    QRectF customArea(const BeamTableInfo &t, const QPointF &cellLL);
}

#endif // BEAMSCHEDULE_H
