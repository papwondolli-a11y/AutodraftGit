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

namespace {
QJsonObject endToJson(const ShaftEnd &e) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), int(e.type));
    o.insert(QStringLiteral("kwLen"),   e.keyway.length);
    o.insert(QStringLiteral("kwUp"),    e.keyway.upper);
    o.insert(QStringLiteral("kwCen"),   e.keyway.center);
    o.insert(QStringLiteral("kwLow"),   e.keyway.lower);
    o.insert(QStringLiteral("kwSec"),   e.keyway.showSection);
    o.insert(QStringLiteral("kwDep"),   e.keyway.sectionDepth);
    o.insert(QStringLiteral("kwPos"),   e.keyway.sectionPos);
    o.insert(QStringLiteral("thLen"),   e.thread.length);
    o.insert(QStringLiteral("thPitch"), e.thread.pitch);
    return o;
}

ShaftEnd endFromJson(const QJsonObject &o, const ShaftEnd &def) {
    ShaftEnd e = def;
    if (o.isEmpty()) return e;
    const int t = o.value(QStringLiteral("type")).toInt(0);
    e.type = (t == 1) ? ShaftEndType::Keyway : (t == 2) ? ShaftEndType::Thread : ShaftEndType::None;
    e.keyway.length = o.value(QStringLiteral("kwLen")).toDouble(e.keyway.length);
    e.keyway.upper  = o.value(QStringLiteral("kwUp")).toDouble(e.keyway.upper);
    e.keyway.center = o.value(QStringLiteral("kwCen")).toDouble(e.keyway.center);
    e.keyway.lower  = o.value(QStringLiteral("kwLow")).toDouble(e.keyway.lower);
    e.keyway.showSection  = o.value(QStringLiteral("kwSec")).toBool(e.keyway.showSection);
    e.keyway.sectionDepth = o.value(QStringLiteral("kwDep")).toDouble(e.keyway.sectionDepth);
    e.keyway.sectionPos   = o.value(QStringLiteral("kwPos")).toInt(e.keyway.sectionPos);
    e.thread.length = o.value(QStringLiteral("thLen")).toDouble(e.thread.length);
    e.thread.pitch  = o.value(QStringLiteral("thPitch")).toDouble(e.thread.pitch);
    return e;
}
} // namespace

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
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("segments"),  arr);
    root.insert(QStringLiteral("leftEnd"),   endToJson(leftEnd));
    root.insert(QStringLiteral("rightEnd"),  endToJson(rightEnd));
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
            p.segments.append(s);
        }
    }
    p.leftEnd  = endFromJson(root.value(QStringLiteral("leftEnd")).toObject(),  p.leftEnd);
    p.rightEnd = endFromJson(root.value(QStringLiteral("rightEnd")).toObject(), p.rightEnd);
    p.drawCenterLine = root.value(QStringLiteral("center")).toBool(p.drawCenterLine);
    p.diameterDims   = root.value(QStringLiteral("diaDims")).toBool(p.diameterDims);
    p.lengthDims     = root.value(QStringLiteral("lenDims")).toBool(p.lengthDims);
    p.dimGap         = root.value(QStringLiteral("dimGap")).toDouble(p.dimGap);
    p.dimStep        = root.value(QStringLiteral("dimStep")).toDouble(p.dimStep);
    p.centerExtend   = root.value(QStringLiteral("centerExt")).toDouble(p.centerExtend);
    p.bodyLayer      = root.value(QStringLiteral("bodyLayer")).toString(p.bodyLayer);
    return p;
}
