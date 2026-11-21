#include "shaftdata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ShaftParams ShaftParams::sample() {
    ShaftParams p;

    ShaftSegment s1;
    s1.diameter = 12; s1.length = 18; s1.fit = QStringLiteral("g6"); s1.chamferLeft = 1;

    ShaftSegment s2;
    s2.diameter = 16; s2.length = 14; s2.fit = QStringLiteral("g6");

    ShaftSegment s3;
    s3.diameter = 20; s3.length = 19; s3.fit = QStringLiteral("k5"); s3.chamferLeft = 1;

    ShaftSegment s4; // 칼라
    s4.diameter = 25; s4.length = 4; s4.dimDiameter = true;

    ShaftSegment s5;
    s5.diameter = 20; s5.length = 59; s5.fit = QStringLiteral("k5"); s5.chamferRight = 1;

    ShaftSegment s6;
    s6.diameter = 14; s6.length = 24; s6.fit = QStringLiteral("g6"); s6.chamferRight = 1;

    p.segments << s1 << s2 << s3 << s4 << s5 << s6;
    return p;
}

double ShaftParams::standardPitch(double d) {
    // 호칭경 이하 중 가장 큰 표준 거친나사 피치
    static const double table[][2] = {
        {3, 0.5}, {4, 0.7}, {5, 0.8}, {6, 1.0}, {8, 1.25}, {10, 1.5}, {12, 1.75},
        {14, 2.0}, {16, 2.0}, {18, 2.5}, {20, 2.5}, {22, 2.5}, {24, 3.0}, {27, 3.0},
        {30, 3.5}, {33, 3.5}, {36, 4.0}, {39, 4.0}, {42, 4.5}, {45, 4.5}, {48, 5.0},
        {52, 5.0}, {56, 5.5}, {60, 5.5}, {64, 6.0}
    };
    double pitch = 0.35;
    const int n = int(sizeof(table) / sizeof(table[0]));
    for (int i = 0; i < n; ++i) {
        if (d + 1e-9 >= table[i][0]) pitch = table[i][1];
    }
    return pitch;
}

double ShaftParams::effectivePitch(const ShaftThread &thread, double nominalDiameter) {
    return thread.pitch > 0.0 ? thread.pitch : standardPitch(nominalDiameter);
}

bool ShaftParams::hasThread(int side) const {
    if (threadSide == ShaftThreadSide::Both) return true;
    return side == 0 ? threadSide == ShaftThreadSide::Left : threadSide == ShaftThreadSide::Right;
}

// ── 키홈 JSON ──────────────────────────────────────────────────────────────
QJsonObject ShaftKeyway::toJson() const {
    QJsonObject o;
    o.insert(QStringLiteral("on"),   enabled);
    o.insert(QStringLiteral("type"), int(type));
    o.insert(QStringLiteral("ref"),  int(ref));
    o.insert(QStringLiteral("off"),  offset);
    o.insert(QStringLiteral("len"),  length);
    o.insert(QStringLiteral("up"),   upper);
    o.insert(QStringLiteral("cen"),  center);
    o.insert(QStringLiteral("low"),  lower);
    o.insert(QStringLiteral("sec"),  showSection);
    o.insert(QStringLiteral("dep"),  sectionDepth);
    return o;
}

ShaftKeyway ShaftKeyway::fromJson(const QJsonObject &o, const ShaftKeyway &def) {
    ShaftKeyway k = def;
    if (o.isEmpty()) return k;
    k.enabled = o.value(QStringLiteral("on")).toBool(k.enabled);
    k.type    = (o.value(QStringLiteral("type")).toInt(int(k.type)) == 0) ? ShaftKeywayType::Open
                                                                          : ShaftKeywayType::Closed;
    const int r = o.value(QStringLiteral("ref")).toInt(int(k.ref));
    k.ref = (r == 1) ? ShaftKeywayRef::Left : (r == 2) ? ShaftKeywayRef::Right : ShaftKeywayRef::Center;
    k.offset       = o.value(QStringLiteral("off")).toDouble(k.offset);
    k.length       = o.value(QStringLiteral("len")).toDouble(k.length);
    k.upper        = o.value(QStringLiteral("up")).toDouble(k.upper);
    k.center       = o.value(QStringLiteral("cen")).toDouble(k.center);
    k.lower        = o.value(QStringLiteral("low")).toDouble(k.lower);
    k.showSection  = o.value(QStringLiteral("sec")).toBool(k.showSection);
    k.sectionDepth = o.value(QStringLiteral("dep")).toDouble(k.sectionDepth);
    return k;
}

bool ShaftKeyway::standardSize(double d, double &width, double &depth) {
    // KS B 1311 평행키: 적용 축지름 초과~이하, 키 폭 b, 축 홈 깊이 t1
    static const double table[][4] = {
        {6, 8, 2, 1.2},     {8, 10, 3, 1.8},    {10, 12, 4, 2.5},   {12, 17, 5, 3.0},
        {17, 22, 6, 3.5},   {22, 30, 8, 4.0},   {30, 38, 10, 5.0},  {38, 44, 12, 5.0},
        {44, 50, 14, 5.5},  {50, 58, 16, 6.0},  {58, 65, 18, 7.0},  {65, 75, 20, 7.5},
        {75, 85, 22, 9.0},  {85, 95, 25, 9.0},  {95, 110, 28, 10.0}, {110, 130, 32, 11.0}
    };
    const int n = int(sizeof(table) / sizeof(table[0]));
    for (int i = 0; i < n; ++i) {
        if (d > table[i][0] + 1e-9 && d <= table[i][1] + 1e-9) {
            width = table[i][2];
            depth = table[i][3];
            return true;
        }
    }
    return false;
}

// ── 옛 끝단 형식 변환 ─────────────────────────────────────────────────────
//   type 1(키홈): 끝 구간의 키홈. 단면 표시가 아니면 끝 열림, 단면이면 옛 단면 위치(앞/중앙/뒤)를 기준으로 바꾼다.
//   type 2(나사): threadSide 에 그 끝을 더한다.
void ShaftParams::applyLegacyEnd(int side, const QJsonObject &o) {
    if (o.isEmpty() || segments.isEmpty()) return;
    const int t = o.value(QStringLiteral("type")).toInt(0);
    if (t == 2) {
        ShaftThread &th = (side == 0) ? leftThread : rightThread;
        th.length = o.value(QStringLiteral("thLen")).toDouble(th.length);
        th.pitch  = o.value(QStringLiteral("thPitch")).toDouble(th.pitch);
        const bool other = hasThread(1 - side);
        threadSide = other ? ShaftThreadSide::Both : (side == 0 ? ShaftThreadSide::Left : ShaftThreadSide::Right);
        return;
    }
    if (t != 1) return;

    ShaftSegment &seg = (side == 0) ? segments.first() : segments.last();
    ShaftKeyway k;
    k.enabled      = true;
    k.length       = o.value(QStringLiteral("kwLen")).toDouble(k.length);
    k.upper        = o.value(QStringLiteral("kwUp")).toDouble(k.upper);
    k.center       = o.value(QStringLiteral("kwCen")).toDouble(k.center);
    k.lower        = o.value(QStringLiteral("kwLow")).toDouble(k.lower);
    k.showSection  = o.value(QStringLiteral("kwSec")).toBool(false);
    k.sectionDepth = o.value(QStringLiteral("kwDep")).toDouble(k.sectionDepth);
    const ShaftKeywayRef endRef = (side == 0) ? ShaftKeywayRef::Left : ShaftKeywayRef::Right;
    if (!k.showSection) {
        k.type = ShaftKeywayType::Open;
        k.ref  = endRef;
    } else {
        int pos = o.value(QStringLiteral("kwPos")).toInt(-1);
        if (pos < 0) pos = (side == 0) ? 0 : 2;
        if (pos == 1) {
            k.type = ShaftKeywayType::Closed;
            k.ref  = ShaftKeywayRef::Center;
        } else {
            k.ref  = (pos == 0) ? ShaftKeywayRef::Left : ShaftKeywayRef::Right;
            k.type = (k.ref == endRef) ? ShaftKeywayType::Open : ShaftKeywayType::Closed;
            k.offset = 0.0;
        }
    }
    seg.keyway = k;
}

double ShaftParams::totalLength() const {
    double sum = 0.0;
    for (int i = 0; i < segments.size(); ++i) sum += segments.at(i).length;
    return sum;
}

double ShaftParams::maxDiameter() const {
    double m = 0.0;
    for (int i = 0; i < segments.size(); ++i) m = qMax(m, segments.at(i).diameter);
    return m;
}

QString ShaftParams::toJsonString() const {
    QJsonArray arr;
    for (int i = 0; i < segments.size(); ++i) {
        const ShaftSegment &s = segments.at(i);
        QJsonObject o;
        o.insert(QStringLiteral("d"),   s.diameter);
        o.insert(QStringLiteral("l"),   s.length);
        o.insert(QStringLiteral("fit"), s.fit);
        o.insert(QStringLiteral("cl"),  s.chamferLeft);
        o.insert(QStringLiteral("cr"),  s.chamferRight);
        o.insert(QStringLiteral("dd"),  s.dimDiameter);
        o.insert(QStringLiteral("sf"),  s.showFit);
        o.insert(QStringLiteral("dl"),  s.dimLength);
        o.insert(QStringLiteral("ltu"), s.lenTolUse);
        o.insert(QStringLiteral("ltp"), s.lenTolUpper);
        o.insert(QStringLiteral("ltl"), s.lenTolLower);
        if (s.keyway.enabled) o.insert(QStringLiteral("kw"), s.keyway.toJson());
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("segments"),  arr);
    QJsonObject th;
    th.insert(QStringLiteral("side"),   int(threadSide));
    th.insert(QStringLiteral("lLen"),   leftThread.length);
    th.insert(QStringLiteral("lPitch"), leftThread.pitch);
    th.insert(QStringLiteral("rLen"),   rightThread.length);
    th.insert(QStringLiteral("rPitch"), rightThread.pitch);
    root.insert(QStringLiteral("thread"), th);
    root.insert(QStringLiteral("center"),    drawCenterLine);
    root.insert(QStringLiteral("diaDims"),   diameterDims);
    root.insert(QStringLiteral("lenDims"),   lengthDims);
    root.insert(QStringLiteral("dimGap"),    dimGap);
    root.insert(QStringLiteral("dimStep"),   dimStep);
    root.insert(QStringLiteral("centerExt"), centerExtend);
    root.insert(QStringLiteral("bodyLayer"), bodyLayer);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

ShaftParams ShaftParams::fromJsonString(const QString &json, const ShaftParams &fallback) {
    if (json.trimmed().isEmpty()) return fallback;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return fallback;
    const QJsonObject root = doc.object();

    ShaftParams p = fallback;
    const QJsonArray arr = root.value(QStringLiteral("segments")).toArray();
    if (!arr.isEmpty()) {
        p.segments.clear();
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject o = arr.at(i).toObject();
            ShaftSegment s;
            s.diameter     = o.value(QStringLiteral("d")).toDouble(s.diameter);
            s.length       = o.value(QStringLiteral("l")).toDouble(s.length);
            s.fit          = o.value(QStringLiteral("fit")).toString();
            s.chamferLeft  = o.value(QStringLiteral("cl")).toDouble(0.0);
            s.chamferRight = o.value(QStringLiteral("cr")).toDouble(0.0);
            s.dimDiameter  = o.value(QStringLiteral("dd")).toBool(true);
            s.showFit      = o.value(QStringLiteral("sf")).toBool(true);
            s.dimLength    = o.value(QStringLiteral("dl")).toBool(true);
            s.lenTolUse    = o.value(QStringLiteral("ltu")).toBool(false);
            s.lenTolUpper  = o.value(QStringLiteral("ltp")).toDouble(0.0);
            s.lenTolLower  = o.value(QStringLiteral("ltl")).toDouble(0.0);
            s.keyway       = ShaftKeyway::fromJson(o.value(QStringLiteral("kw")).toObject(), ShaftKeyway());
            p.segments.append(s);
        }
    }
    if (root.contains(QStringLiteral("thread"))) {
        const QJsonObject th = root.value(QStringLiteral("thread")).toObject();
        const int side = th.value(QStringLiteral("side")).toInt(0);
        p.threadSide = (side == 1) ? ShaftThreadSide::Left : (side == 2) ? ShaftThreadSide::Right
                     : (side == 3) ? ShaftThreadSide::Both : ShaftThreadSide::None;
        p.leftThread.length  = th.value(QStringLiteral("lLen")).toDouble(p.leftThread.length);
        p.leftThread.pitch   = th.value(QStringLiteral("lPitch")).toDouble(p.leftThread.pitch);
        p.rightThread.length = th.value(QStringLiteral("rLen")).toDouble(p.rightThread.length);
        p.rightThread.pitch  = th.value(QStringLiteral("rPitch")).toDouble(p.rightThread.pitch);
    } else if (!arr.isEmpty()) {
        // 옛 형식: 양 끝단(leftEnd/rightEnd) → 끝 구간 키홈 / 나사부
        p.threadSide = ShaftThreadSide::None;
        p.applyLegacyEnd(0, root.value(QStringLiteral("leftEnd")).toObject());
        p.applyLegacyEnd(1, root.value(QStringLiteral("rightEnd")).toObject());
    }
    p.drawCenterLine = root.value(QStringLiteral("center")).toBool(p.drawCenterLine);
    p.diameterDims   = root.value(QStringLiteral("diaDims")).toBool(p.diameterDims);
    p.lengthDims     = root.value(QStringLiteral("lenDims")).toBool(p.lengthDims);
    p.dimGap         = root.value(QStringLiteral("dimGap")).toDouble(p.dimGap);
    p.dimStep        = root.value(QStringLiteral("dimStep")).toDouble(p.dimStep);
    p.centerExtend   = root.value(QStringLiteral("centerExt")).toDouble(p.centerExtend);
    p.bodyLayer      = root.value(QStringLiteral("bodyLayer")).toString(p.bodyLayer);
    return p;
}
