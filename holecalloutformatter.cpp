#include "holecalloutformatter.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace {
QString formatNum(double v) {
    QString s = QString::number(v, 'f', 2);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

// 지름 기호 변형(%%c, ø, ∅, ⌀)과 곱하기 기호(×)를 포매터가 쓰는 형태로 통일
QString normalizeLine(QString line) {
    line = line.trimmed();
    line.replace(QStringLiteral("%%c"), QStringLiteral("Ø"), Qt::CaseInsensitive);
    line.replace(QChar(0x00F8), QChar(0x00D8)); // ø
    line.replace(QChar(0x2205), QChar(0x00D8)); // ∅
    line.replace(QChar(0x2300), QChar(0x00D8)); // ⌀
    line.replace(QChar(0x00D7), QLatin1Char('x')); // ×
    return line;
}

QString prettyThreadSpec(const QString &spec) {
    QString s = spec;
    s.replace(QLatin1Char('x'), QChar(0x00D7));
    s.replace(QLatin1Char('X'), QChar(0x00D7));
    return s;
}
}

QString HoleCalloutFormatter::formatSingle(const MyHole &hole) {
    QStringList lines;

    // 1. 기본 라인: 탭이면 나사 규격, 아니면 지름
    if (hole.isThreaded) {
        QString base = prettyThreadSpec(hole.threadSpec);
        if (!hole.threadClass.isEmpty()) base += QLatin1Char('-') + hole.threadClass;
        base += hole.throughHole ? QStringLiteral(" THRU")
                                 : QStringLiteral(" X %1 DP").arg(formatNum(hole.threadDepth));
        lines << base;
    } else {
        QString base = QStringLiteral("Ø%1").arg(formatNum(hole.diameter));
        if (!hole.toleranceClass.isEmpty()) base += QLatin1Char(' ') + hole.toleranceClass;
        base += hole.throughHole ? QStringLiteral(" THRU")
                                 : QStringLiteral(" X %1 DP").arg(formatNum(hole.depth));
        lines << base;
    }

    // 2. 카운터보어
    if (hole.hasCounterbore) {
        lines << QStringLiteral("C/B Ø%1 X %2 DP").arg(formatNum(hole.cbDiameter), formatNum(hole.cbDepth));
    }

    // 3. 카운터싱크
    if (hole.hasCountersink) {
        lines << QStringLiteral("C/S Ø%1 X %2°").arg(formatNum(hole.csDiameter), formatNum(hole.csAngle));
    }

    return lines.join(QLatin1Char('\n'));
}

bool HoleCalloutFormatter::specsMatch(const MyHole &a, const MyHole &b) {
    return a.diameter == b.diameter
        && a.throughHole == b.throughHole
        && a.depth == b.depth
        && a.isThreaded == b.isThreaded
        && a.threadSpec == b.threadSpec
        && a.threadClass == b.threadClass
        && a.threadDepth == b.threadDepth
        && a.toleranceClass == b.toleranceClass
        && a.hasCounterbore == b.hasCounterbore
        && a.cbDiameter == b.cbDiameter
        && a.cbDepth == b.cbDepth
        && a.hasCountersink == b.hasCountersink
        && a.csDiameter == b.csDiameter
        && a.csAngle == b.csAngle;
}

QString HoleCalloutFormatter::formatGrouped(const QList<MyHole> &holes, bool &ok) {
    ok = false;
    if (holes.isEmpty()) return QString();

    for (int i = 1; i < holes.size(); ++i) {
        if (!specsMatch(holes[0], holes[i])) return QString();
    }
    ok = true;

    QString text = formatSingle(holes[0]);
    if (holes.size() > 1) {
        QStringList lines = text.split(QLatin1Char('\n'));
        if (!lines.isEmpty()) lines[0] = QStringLiteral("%1x %2").arg(holes.size()).arg(lines[0]);
        text = lines.join(QLatin1Char('\n'));
    }
    return text;
}

bool HoleCalloutFormatter::parse(const QString &text, const MyHole &base, MyHole &out,
                                 int &count, QString &error) {
    out = base;
    count = 1;
    error.clear();

    QStringList lines;
    const QStringList raw = text.split(QLatin1Char('\n'));
    for (const QString &r : raw) {
        QString line = normalizeLine(r);
        if (!line.isEmpty()) lines << line;
    }
    if (lines.isEmpty()) {
        error = QCoreApplication::translate("HoleCalloutFormatter", "콜아웃 텍스트가 비어 있습니다.");
        return false;
    }

    const QRegularExpression::PatternOptions ci = QRegularExpression::CaseInsensitiveOption;
    static const QString num = QStringLiteral("(\\d+(?:\\.\\d+)?|\\.\\d+)");

    // 1. 첫 줄: [Nx ] + (Ø지름 [공차] | 나사규격[-등급]) + (THRU | X 깊이 DP)
    QString first = lines.takeFirst();
    QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^(\\d+)\\s*x\\s+(.+)$"), ci).match(first);
    if (m.hasMatch()) {
        count = m.captured(1).toInt();
        first = m.captured(2).trimmed();
        if (count < 1) {
            error = QCoreApplication::translate("HoleCalloutFormatter", "개수는 1 이상이어야 합니다.");
            return false;
        }
    }

    bool through = false;
    double depthVal = 0.0;
    QString body;
    m = QRegularExpression(QStringLiteral("^(.+?)\\s+THRU$"), ci).match(first);
    if (m.hasMatch()) {
        through = true;
        body = m.captured(1).trimmed();
    } else {
        m = QRegularExpression(QStringLiteral("^(.+?)\\s+x\\s*") + num + QStringLiteral("\\s*DP$"), ci).match(first);
        if (!m.hasMatch()) {
            error = QCoreApplication::translate("HoleCalloutFormatter", "첫 줄 끝에 'THRU' 또는 'X 깊이 DP'가 필요합니다: %1").arg(first);
            return false;
        }
        body = m.captured(1).trimmed();
        depthVal = m.captured(2).toDouble();
        if (depthVal <= 0.0) {
            error = QCoreApplication::translate("HoleCalloutFormatter", "깊이는 0보다 커야 합니다.");
            return false;
        }
    }

    m = QRegularExpression(QStringLiteral("^\\x{00D8}\\s*") + num + QStringLiteral("(?:\\s+(\\S+))?$")).match(body);
    if (m.hasMatch()) {
        // 일반 드릴 구멍
        out.isThreaded = false;
        out.diameter = m.captured(1).toDouble();
        out.toleranceClass = m.captured(2);
        if (out.diameter <= 0.0) {
            error = QCoreApplication::translate("HoleCalloutFormatter", "지름은 0보다 커야 합니다.");
            return false;
        }
        out.throughHole = through;
        if (!through) out.depth = depthVal;
    } else {
        // 탭(나사) 구멍: 규격[-등급]
        QString spec = body;
        QString cls;
        m = QRegularExpression(QStringLiteral("^(\\S+)-(\\d+[A-Za-z]+(?:\\d+[A-Za-z]+)?)$")).match(body);
        if (m.hasMatch()) {
            spec = m.captured(1);
            cls = m.captured(2);
        }
        double major = 0.0, pitch = 0.0;
        if (spec.contains(QLatin1Char(' ')) || !MyHole::parseThreadSpec(spec, major, pitch)) {
            error = QCoreApplication::translate("HoleCalloutFormatter", "지름(Ø...) 또는 나사 규격(예: M10x1.5)으로 해석할 수 없습니다: %1").arg(body);
            return false;
        }
        bool specChanged = !base.isThreaded || base.threadSpec != spec;
        out.isThreaded = true;
        out.threadSpec = spec;
        out.threadClass = cls;
        // 규격이 바뀌면 속성창(ThreadSpec 변경)과 같은 탭드릴 근사식으로 드릴지름도 맞춘다
        if (specChanged && major - pitch > 0.0) out.diameter = major - pitch;
        out.throughHole = through;
        if (!through) {
            // 막힌 탭 구멍의 표기 깊이는 나사산 깊이 — 드릴 깊이는 그 이상이어야 한다
            out.threadDepth = depthVal;
            if (out.depth < depthVal) out.depth = depthVal;
        }
    }

    // 2. 나머지 줄: 카운터보어 / 카운터싱크. 줄이 없으면 해당 가공을 끈다.
    out.hasCounterbore = false;
    out.hasCountersink = false;
    const QRegularExpression cbRe(QStringLiteral("^C/B\\s*\\x{00D8}\\s*") + num + QStringLiteral("\\s*x\\s*") + num + QStringLiteral("\\s*DP$"), ci);
    const QRegularExpression csRe(QStringLiteral("^C/S\\s*\\x{00D8}\\s*") + num + QStringLiteral("\\s*x\\s*") + num + QStringLiteral("\\s*\\x{00B0}?$"), ci);
    for (const QString &line : std::as_const(lines)) {
        QRegularExpressionMatch lm = cbRe.match(line);
        if (lm.hasMatch() && !out.hasCounterbore) {
            out.hasCounterbore = true;
            out.cbDiameter = lm.captured(1).toDouble();
            out.cbDepth = lm.captured(2).toDouble();
            continue;
        }
        lm = csRe.match(line);
        if (lm.hasMatch() && !out.hasCountersink) {
            out.hasCountersink = true;
            out.csDiameter = lm.captured(1).toDouble();
            out.csAngle = lm.captured(2).toDouble();
            continue;
        }
        error = QCoreApplication::translate("HoleCalloutFormatter", "해석할 수 없는 줄입니다: %1").arg(line);
        return false;
    }

    // 3. 속성창과 같은 유효성 규칙: 카운터보어/카운터싱크 지름은 구멍(탭이면 나사 호칭지름)보다 커야 함
    double minDiameter = out.isThreaded ? out.threadMajorRadius() * 2.0 : out.diameter;
    if (out.hasCounterbore && (out.cbDiameter <= minDiameter || out.cbDepth <= 0.0)) {
        error = QCoreApplication::translate("HoleCalloutFormatter", "카운터보어 지름(%1)은 %2보다 커야 하고 깊이는 0보다 커야 합니다.")
                    .arg(formatNum(out.cbDiameter), formatNum(minDiameter));
        return false;
    }
    if (out.hasCountersink && (out.csDiameter <= minDiameter || out.csAngle <= 0.0 || out.csAngle >= 180.0)) {
        error = QCoreApplication::translate("HoleCalloutFormatter", "카운터싱크 지름(%1)은 %2보다 커야 하고 각도는 0~180° 사이여야 합니다.")
                    .arg(formatNum(out.csDiameter), formatNum(minDiameter));
        return false;
    }
    return true;
}
