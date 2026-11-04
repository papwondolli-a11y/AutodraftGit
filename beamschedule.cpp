#include "beamschedule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

namespace BeamScheduleBuilder {
const QString kTableLayer   = QStringLiteral("B-LIST");
const QString kLabelLayer   = QStringLiteral("T");
const QString kValueLayer   = QStringLiteral("text");
const QString kPaperLayer   = QStringLiteral("W1");
const QString kBeamLayer    = QStringLiteral("beam");
const QString kStirrupLayer = QStringLiteral("beam1");
const QString kBarLayer     = QStringLiteral("striup");
}

// ─────────────────────────────────────────────────────────────────────────────
// 표 정보
// ─────────────────────────────────────────────────────────────────────────────
BeamTableInfo BeamTableInfo::defaults(double scale)
{
    BeamTableInfo t;
    t.scale = scale > 1e-9 ? scale : 1.0;   // 길이 값은 축척 적용 전 샘플 값 그대로
    return t;
}

bool BeamTableInfo::paperMm(const QString &name, double &w, double &h)
{
    const QString n = name.trimmed().toUpper();
    if (n == QLatin1String("A0"))      { w = 1189.0; h = 841.0; return true; }
    if (n == QLatin1String("A1"))      { w = 841.0;  h = 594.0; return true; }
    if (n == QLatin1String("A2"))      { w = 594.0;  h = 420.0; return true; }
    if (n == QLatin1String("A3"))      { w = 420.0;  h = 297.0; return true; }
    if (n == QLatin1String("A4"))      { w = 297.0;  h = 210.0; return true; }
    return false;
}

double BeamTableInfo::bandPitch() const
{
    double sum = bandGap;
    for (int i = 0; i < RowCount; ++i) sum += rowH[i];
    return sum;
}

int BeamTableInfo::columns() const
{
    double pw = 0.0, ph = 0.0;
    if (!paperMm(paperSize, pw, ph) || beamColW < 1e-9) return 0;
    const double avail = pw - marginW - labelColW - rightColW;
    return avail <= 0.0 ? 0 : static_cast<int>(std::floor(avail / beamColW + 1e-6));
}

int BeamTableInfo::bands() const
{
    double pw = 0.0, ph = 0.0;
    const double pitch = bandPitch();
    if (!paperMm(paperSize, pw, ph) || pitch < 1e-9) return 0;
    const double avail = ph - marginH;
    return avail <= 0.0 ? 0 : static_cast<int>(std::floor(avail / pitch + 1e-6));
}

QVector<BeamMark> BeamScheduleData::packedBeams() const
{
    QVector<BeamMark> out;
    for (int i = 0; i < beams.size(); ++i) {
        if (!beams.at(i).isEmpty()) out.append(beams.at(i));
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 표기 문자열
// ─────────────────────────────────────────────────────────────────────────────
static QString numText(double v)
{
    return QString::number(v, 'f', (v == static_cast<int>(v)) ? 0 : 1);
}

QString BeamScheduleText::stirrup(const BeamSection &s)
{
    return QStringLiteral("%1D%2@%3")
        .arg(s.stirrupHigh ? QStringLiteral("H") : QString())
        .arg(s.stirrupDia)
        .arg(numText(s.stirrupPitch));
}

QString BeamScheduleText::bars(const BeamBars &b)
{
    if (b.total() <= 0) return QString();
    return QStringLiteral("D%1 - %2").arg(b.dia).arg(b.total());
}

QString BeamScheduleText::size(const BeamMark &m)
{
    return QStringLiteral("%1 x %2").arg(numText(m.width)).arg(numText(m.depth));
}

QString BeamScheduleText::position(int sectionIndex)
{
    static const char *kNames[3] = {"INT. END", "CENTER", "EXT. END"};
    if (sectionIndex < 0 || sectionIndex > 2) return QString();
    return QString::fromLatin1(kNames[sectionIndex]);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON 저장 / 불러오기
// ─────────────────────────────────────────────────────────────────────────────
static QJsonObject barsToJson(const BeamBars &b)
{
    QJsonObject o;
    o["dia"]  = b.dia;
    o["row1"] = b.row1;
    o["row2"] = b.row2;
    return o;
}

static BeamBars barsFromJson(const QJsonObject &o, const BeamBars &def)
{
    BeamBars b = def;
    b.dia  = o.value("dia").toInt(def.dia);
    b.row1 = o.value("row1").toInt(def.row1);
    b.row2 = o.value("row2").toInt(def.row2);
    return b;
}

static QJsonObject tableToJson(const BeamTableInfo &t)
{
    QJsonObject o;
    o["paperSize"] = t.paperSize;
    o["scale"]     = t.scale;
    o["marginW"]   = t.marginW;
    o["marginH"]   = t.marginH;
    o["labelColW"] = t.labelColW;
    o["beamColW"]  = t.beamColW;
    o["rightColW"] = t.rightColW;
    o["bandGap"]   = t.bandGap;
    QJsonArray rows;
    for (int i = 0; i < BeamTableInfo::RowCount; ++i) rows.append(t.rowH[i]);
    o["rowH"] = rows;
    return o;
}

static BeamTableInfo tableFromJson(const QJsonObject &o, const BeamTableInfo &def)
{
    BeamTableInfo t = def;
    t.paperSize = o.value("paperSize").toString(def.paperSize);
    t.scale     = o.value("scale").toDouble(def.scale);
    t.marginW   = o.value("marginW").toDouble(def.marginW);
    t.marginH   = o.value("marginH").toDouble(def.marginH);
    t.labelColW = o.value("labelColW").toDouble(def.labelColW);
    t.beamColW  = o.value("beamColW").toDouble(def.beamColW);
    t.rightColW = o.value("rightColW").toDouble(def.rightColW);
    t.bandGap   = o.value("bandGap").toDouble(def.bandGap);
    const QJsonArray rows = o.value("rowH").toArray();
    for (int i = 0; i < BeamTableInfo::RowCount && i < rows.size(); ++i)
        t.rowH[i] = rows.at(i).toDouble(def.rowH[i]);
    return t;
}

QString BeamScheduleText::toJson(const BeamScheduleData &d)
{
    QJsonObject root;
    root["useCustomTable"] = d.useCustomTable;
    root["tableVersion"]   = 2;   // 2 : 표 정보 길이 값은 축척 적용 전 크기
    root["table"]          = tableToJson(d.table);
    QJsonObject layers;
    layers["beam"]    = d.beamLayer;
    layers["stirrup"] = d.stirrupLayer;
    layers["bar"]     = d.barLayer;
    root["layers"]    = layers;
    QJsonArray arr;
    for (int i = 0; i < d.beams.size(); ++i) {
        const BeamMark &m = d.beams.at(i);
        QJsonObject o;
        o["name"]  = m.name;
        o["width"] = m.width;
        o["depth"] = m.depth;
        QJsonArray secs;
        for (int j = 0; j < 3; ++j) {
            const BeamSection &s = m.sec[j];
            QJsonObject so;
            so["top"]          = barsToJson(s.top);
            so["bottom"]       = barsToJson(s.bottom);
            so["stirrupDia"]   = s.stirrupDia;
            so["stirrupPitch"] = s.stirrupPitch;
            so["stirrupHigh"]  = s.stirrupHigh;
            secs.append(so);
        }
        o["sections"] = secs;
        arr.append(o);
    }
    root["beams"] = arr;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

BeamScheduleData BeamScheduleText::fromJson(const QString &json, double drawingScale)
{
    BeamScheduleData d;
    d.table = BeamTableInfo::defaults(drawingScale);
    if (json.isEmpty()) return d;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return d;

    const QJsonObject root = doc.object();
    d.useCustomTable = root.value("useCustomTable").toBool(false);
    if (root.contains("layers")) {
        const QJsonObject lo = root.value("layers").toObject();
        d.beamLayer    = lo.value("beam").toString(d.beamLayer);
        d.stirrupLayer = lo.value("stirrup").toString(d.stirrupLayer);
        d.barLayer     = lo.value("bar").toString(d.barLayer);
    }
    if (root.contains("table") && root.value("tableVersion").toInt(1) >= 2)
        d.table = tableFromJson(root.value("table").toObject(), d.table);
    else if (root.contains("table"))       // 이전 버전(축척 적용값) 저장 → 용지 크기만 유지, 치수는 기본값
        d.table.paperSize = root.value("table").toObject().value("paperSize").toString(d.table.paperSize);
    else if (root.contains("paperSize"))
        d.table.paperSize = root.value("paperSize").toString(d.table.paperSize);

    d.table.scale = drawingScale > 1e-9 ? drawingScale : 1.0;   // 항상 현재 도면 축척

    const QJsonArray arr = root.value("beams").toArray();
    d.beams.resize(qMax(arr.size(), BeamScheduleData::kDefaultSlots));
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject o = arr.at(i).toObject();
        BeamMark &m = d.beams[i];
        m.name  = o.value("name").toString();
        m.width = o.value("width").toDouble(m.width);
        m.depth = o.value("depth").toDouble(m.depth);
        const QJsonArray secs = o.value("sections").toArray();
        for (int j = 0; j < 3 && j < secs.size(); ++j) {
            const QJsonObject so = secs.at(j).toObject();
            BeamSection &s = m.sec[j];
            s.top          = barsFromJson(so.value("top").toObject(), s.top);
            s.bottom       = barsFromJson(so.value("bottom").toObject(), s.bottom);
            s.stirrupDia   = so.value("stirrupDia").toInt(s.stirrupDia);
            s.stirrupPitch = so.value("stirrupPitch").toDouble(s.stirrupPitch);
            s.stirrupHigh  = so.value("stirrupHigh").toBool(s.stirrupHigh);
        }
    }
    d.normalizeSlots();
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// 도형 생성
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// 기본 표 (beamlist.cdb, 축척 1 기준)
const double kTotalW      = 748.0;
const double kTotalH      = 525.0;
const double kTableRight  = 678.0;   // 보 열 영역 오른쪽 끝 (그 오른쪽은 도면 사이즈 칸)
const double kLabelW      = 53.0;
const double kColW        = 125.0;
const double kBandH       = 105.0;

const double kLabelSize   = 4.0;
const double kValueSize   = 2.5;
const double kPaperSize   = 23.08;

struct LabelDef { const char *text; double x; double y; };
const LabelDef kLabels[] = {
    {"스트럽", 11.9, 2.6},
    {"하부근", 11.9, 12.4},
    {"상부근", 11.9, 22.6},
    {"크 기",  11.9, 32.6},
    {"위치",   12.7, 41.8},
    {"형태",   12.7, 69.4},
    {"부 호",  12.9, 94.5},
};
const int kLabelCount = sizeof(kLabels) / sizeof(kLabels[0]);

QPointF at(const QPointF &origin, double scale, double x, double y)
{
    return QPointF(origin.x() + x * scale, origin.y() + y * scale);
}

void addLine(ScheduleGeom &g, const QString &layer, double x1, double y1, double x2, double y2)
{
    SchedLine l;
    l.line  = QLineF(x1, y1, x2, y2);
    l.layer = layer;
    g.lines.append(l);
}

void addText(ScheduleGeom &g, const QString &layer, const QString &text,
             const QPointF &pos, double size, int align)
{
    if (text.isEmpty()) return;
    SchedText t;
    t.text  = text;
    t.pos   = pos;
    t.size  = size;
    t.align = align;
    t.layer = layer;
    g.texts.append(t);
}

// 철근 1열 : n 개를 xL ~ xR 사이에 균등 배치
void addBarRow(ScheduleGeom &g, int n, double xL, double xR, double y, double radius,
               const QString &layer)
{
    for (int k = 0; k < n; ++k) {
        SchedCircle c;
        c.radius = radius;
        c.layer  = layer;
        const double x = (n == 1) ? (xL + xR) / 2.0 : xL + (xR - xL) * k / (n - 1);
        c.center = QPointF(x, y);
        g.circles.append(c);
    }
}

void addSection(ScheduleGeom &g, const BeamMark &m, const BeamSection &s,
                double cx, double cy, const BeamShapeStyle &style)
{
    using namespace BeamScheduleBuilder;
    // 형태란보다 클 때 축소 배율 적용 (1 = 실치수)
    const double f = style.shapeScale > 1e-9 ? style.shapeScale : 1.0;
    BeamShapeStyle st = style;
    st.slabThickness  *= f;
    st.flangeOverhang *= f;
    st.stirrupInset   *= f;
    st.barFromFace    *= f;
    st.rowPitch       *= f;
    st.barSideInset   *= f;
    const double width = m.width * f;
    const double depth = m.depth * f;

    const double yBot  = cy - depth / 2.0;
    const double yTop  = cy + depth / 2.0;
    const double ySlab = yTop - st.slabThickness;
    const double xl    = cx - width / 2.0;
    const double xr    = cx + width / 2.0;
    const double ov    = st.flangeOverhang;

    // 외곽 : 웹 + 슬래브 (슬래브 양끝은 열린 선)
    addLine(g, st.beamLayer, xl, yBot, xr, yBot);
    addLine(g, st.beamLayer, xl, yBot, xl, ySlab);
    addLine(g, st.beamLayer, xr, yBot, xr, ySlab);
    addLine(g, st.beamLayer, xl - ov, ySlab, xl, ySlab);
    addLine(g, st.beamLayer, xr, ySlab, xr + ov, ySlab);
    addLine(g, st.beamLayer, xl - ov, yTop, xr + ov, yTop);

    // 스트럽
    const double sx1 = xl + st.stirrupInset;
    const double sx2 = xr - st.stirrupInset;
    const double sy1 = yBot + st.stirrupInset;
    const double sy2 = yTop - st.stirrupInset;
    addLine(g, st.stirrupLayer, sx1, sy1, sx2, sy1);
    addLine(g, st.stirrupLayer, sx2, sy1, sx2, sy2);
    addLine(g, st.stirrupLayer, sx2, sy2, sx1, sy2);
    addLine(g, st.stirrupLayer, sx1, sy2, sx1, sy1);

    // 철근 : 1열(상) / 2열(하)
    const double bx1 = sx1 + st.barSideInset;
    const double bx2 = sx2 - st.barSideInset;

    const double topRow2Y = yTop - st.barFromFace - st.rowPitch;
    const double topRow1Y = topRow2Y + st.rowPitch;
    addBarRow(g, s.top.row1, bx1, bx2, topRow1Y, s.top.dia / 2.0 * f, st.barLayer);
    addBarRow(g, s.top.row2, bx1, bx2, topRow2Y, s.top.dia / 2.0 * f, st.barLayer);

    const double botRow2Y = yBot + st.barFromFace;
    const double botRow1Y = botRow2Y + st.rowPitch;
    addBarRow(g, s.bottom.row1, bx1, bx2, botRow1Y, s.bottom.dia / 2.0 * f, st.barLayer);
    addBarRow(g, s.bottom.row2, bx1, bx2, botRow2Y, s.bottom.dia / 2.0 * f, st.barLayer);
}

// 보 1개 내용 : cellLL = 그 보의 형태란 좌하단, 행 높이/열 폭은 표 정보
void addBeamContent(ScheduleGeom &g, const BeamMark &m, const QPointF &cellLL,
                    const BeamTableInfo &tableInfo, const BeamShapeStyle &style)
{
    using namespace BeamScheduleBuilder;
    // 표 정보 값(축척 적용 전)에 도면 축척을 곱해 실제 도면 크기로
    BeamTableInfo t = tableInfo;
    t.beamColW *= t.scale;
    for (int i = 0; i < BeamTableInfo::RowCount; ++i) t.rowH[i] *= t.scale;
    const double fs     = kValueSize * t.scale;
    const double shapeH = t.rowH[BeamTableInfo::Shape];

    // 행 중심 y (형태란 기준 위/아래로 쌓음)
    double yPos     = cellLL.y() - t.rowH[BeamTableInfo::Position];
    double yPosMid  = yPos + t.rowH[BeamTableInfo::Position] / 2.0;
    double ySize    = yPos - t.rowH[BeamTableInfo::Size];
    double ySizeMid = ySize + t.rowH[BeamTableInfo::Size] / 2.0;
    double yTop     = ySize - t.rowH[BeamTableInfo::Top];
    double yTopMid  = yTop + t.rowH[BeamTableInfo::Top] / 2.0;
    double yBot     = yTop - t.rowH[BeamTableInfo::Bottom];
    double yBotMid  = yBot + t.rowH[BeamTableInfo::Bottom] / 2.0;
    double yStir    = yBot - t.rowH[BeamTableInfo::Stirrup];
    double yStirMid = yStir + t.rowH[BeamTableInfo::Stirrup] / 2.0;
    double yNameMid = cellLL.y() + shapeH + t.rowH[BeamTableInfo::Name] / 2.0;

    addText(g, kValueLayer, m.name, QPointF(cellLL.x() + t.beamColW / 2.0, yNameMid), fs, 1);

    for (int j = 0; j < 3; ++j) {
        const BeamSection &s = m.sec[j];
        const double subX = cellLL.x() + (j + 0.5) * t.beamColW / 3.0;

        addText(g, kValueLayer, BeamScheduleText::stirrup(s), QPointF(subX, yStirMid), fs, 1);
        addText(g, kValueLayer, BeamScheduleText::bars(s.bottom), QPointF(subX, yBotMid), fs, 1);
        addText(g, kValueLayer, BeamScheduleText::bars(s.top), QPointF(subX, yTopMid), fs, 1);
        addText(g, kValueLayer, BeamScheduleText::size(m), QPointF(subX, ySizeMid), fs, 1);
        addText(g, kValueLayer, BeamScheduleText::position(j), QPointF(subX, yPosMid), fs, 1);

        // 단면 도형은 축척을 곱하지 않은 실치수
        addSection(g, m, s, subX, cellLL.y() + shapeH / 2.0, style);
    }
}
} // namespace

QPointF BeamScheduleBuilder::tableSize(double scale)
{
    return QPointF(kTotalW * scale, kTotalH * scale);
}

QVector<SchedLine> BeamScheduleBuilder::frameLines(const QPointF &origin, double scale)
{
    ScheduleGeom g;

    // 세로선
    const double xs[8] = {0.0, 53.0, 178.0, 303.0, 428.0, 553.0, 678.0, 748.0};
    for (int i = 0; i < 8; ++i) {
        const QPointF a = at(origin, scale, xs[i], 0.0);
        const QPointF b = at(origin, scale, xs[i], kTotalH);
        addLine(g, kTableLayer, a.x(), a.y(), b.x(), b.y());
    }

    // 가로선 : 띠마다 0,10,20,30,40,50,90 (+ 아래 띠와의 경계 104), 맨 아래/위는 전체 폭
    const double ys[7] = {0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 90.0};
    for (int band = 0; band < BeamScheduleData::kDefaultBands; ++band) {
        const double base = band * kBandH;
        for (int i = 0; i < 7; ++i) {
            const double y = base + ys[i];
            const double right = (y == 0.0) ? kTotalW : kTableRight;
            const QPointF a = at(origin, scale, 0.0, y);
            const QPointF b = at(origin, scale, right, y);
            addLine(g, kTableLayer, a.x(), a.y(), b.x(), b.y());
        }
        if (band < BeamScheduleData::kDefaultBands - 1) {
            const QPointF a = at(origin, scale, 0.0, base + 104.0);
            const QPointF b = at(origin, scale, kTableRight, base + 104.0);
            addLine(g, kTableLayer, a.x(), a.y(), b.x(), b.y());
        }
    }
    const QPointF ta = at(origin, scale, 0.0, kTotalH);
    const QPointF tb = at(origin, scale, kTotalW, kTotalH);
    addLine(g, kTableLayer, ta.x(), ta.y(), tb.x(), tb.y());

    return g.lines;
}

ScheduleGeom BeamScheduleBuilder::build(const BeamScheduleData &data, const QPointF &origin,
                                        double scale, const BeamShapeStyle &baseStyle)
{
    BeamShapeStyle style = baseStyle;
    style.beamLayer    = data.beamLayer;
    style.stirrupLayer = data.stirrupLayer;
    style.barLayer     = data.barLayer;
    style.shapeScale   = data.shapeScale;
    ScheduleGeom g;
    g.lines = frameLines(origin, scale);

    // 행 라벨 (띠마다)
    for (int band = 0; band < BeamScheduleData::kDefaultBands; ++band) {
        for (int i = 0; i < kLabelCount; ++i) {
            const QPointF p = at(origin, scale, kLabels[i].x, band * kBandH + kLabels[i].y);
            addText(g, kLabelLayer, QString::fromUtf8(kLabels[i].text), p, kLabelSize * scale, 0);
        }
    }

    // 오른쪽 칸 : 도면 사이즈
    addText(g, kPaperLayer, data.table.paperSize,
            at(origin, scale, (kTableRight + kTotalW) / 2.0,
               (BeamScheduleData::kDefaultBands - 1) * kBandH + 66.3),
            kPaperSize * scale, 1);

    // 보 : 입력된 보를 순서대로 (위 띠 왼쪽부터)
    const BeamTableInfo layout = BeamTableInfo::defaults(scale);
    const QVector<BeamMark> beams = data.packedBeams();
    for (int k = 0; k < beams.size(); ++k) {
        if (k >= BeamScheduleData::kDefaultSlots) {
            g.dropped = beams.size() - k;
            break;
        }
        const int bandFromTop = k / BeamScheduleData::kDefaultColumns;
        const int col         = k % BeamScheduleData::kDefaultColumns;
        const int band        = BeamScheduleData::kDefaultBands - 1 - bandFromTop;
        const QPointF cellLL  = at(origin, scale, kLabelW + col * kColW, band * kBandH + 50.0);
        addBeamContent(g, beams.at(k), cellLL, layout, style);
    }
    return g;
}

ScheduleGeom BeamScheduleBuilder::buildCustom(const BeamScheduleData &data, const QPointF &cellLL,
                                              const BeamShapeStyle &baseStyle)
{
    BeamShapeStyle style = baseStyle;
    style.beamLayer    = data.beamLayer;
    style.stirrupLayer = data.stirrupLayer;
    style.barLayer     = data.barLayer;
    style.shapeScale   = data.shapeScale;
    ScheduleGeom g;
    const BeamTableInfo &t = data.table;
    const int cols = t.columns();
    const int bands = t.bands();
    const int capacity = cols * bands;
    const double pitch = t.bandPitch() * t.scale;

    const QVector<BeamMark> beams = data.packedBeams();
    for (int k = 0; k < beams.size(); ++k) {
        if (k >= capacity) {
            g.dropped = beams.size() - k;
            break;
        }
        const int bandFromTop = k / cols;
        const int col         = k % cols;
        const QPointF ll(cellLL.x() + col * t.beamColW * t.scale, cellLL.y() - bandFromTop * pitch);
        addBeamContent(g, beams.at(k), ll, t, style);
    }
    return g;
}

QRectF BeamScheduleBuilder::customArea(const BeamTableInfo &t, const QPointF &cellLL)
{
    const int cols = t.columns();
    const int bands = t.bands();
    const double S = t.scale;
    double below = 0.0;
    for (int i = 0; i < BeamTableInfo::Shape; ++i) below += t.rowH[i] * S;
    const double left   = cellLL.x();
    const double right  = cellLL.x() + cols * t.beamColW * S;
    const double bottom = cellLL.y() - below - (bands > 0 ? (bands - 1) * t.bandPitch() * S : 0.0);
    const double top    = cellLL.y() + (t.rowH[BeamTableInfo::Shape] + t.rowH[BeamTableInfo::Name]) * S;
    return QRectF(QPointF(left, bottom), QPointF(right, top));
}
