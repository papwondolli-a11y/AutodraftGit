#include "linetypemanager.h"
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QHash>
#include <QStringList>
#include <QtMath> // qAbs 사용을 위해
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSettings>

// ── AutoCAD 표준 선종류명 → Autodraft(linetype.lin) 선종류명 매핑 ────────────────
// AutoCAD DXF/DWG 의 레이어는 "HIDDEN", "CENTER2", "ACAD_ISO04W100" 처럼 Autodraft 와
// 다른 이름으로 선종류를 참조한다. 파일의 LTYPE 테이블에 정의가 함께 들어오면 그대로
// 등록되지만(finishTableRecord), 정의 없이 이름만 참조하는 파일도 많아 그 경우엔 여기
// 매핑으로 가장 가까운 내장 패턴에 연결한다. 빈 문자열은 "연속선(실선)"을 뜻한다.
// 반환: true = 매핑 존재(outCanonical 채움; 빈 문자열이면 "실선"), false = 아는 별칭 아님.
static bool acadLinetypeAlias(const QString& name, QString& outCanonical)
{
    static const QHash<QString, QString> kMap = {
        {"CONTINUOUS", ""}, {"BYLAYER", ""}, {"BYBLOCK", ""}, {"SOLID", ""},

        {"HIDDEN",   "DashLine(small)"}, {"HIDDEN2",  "DashLine(tiny)"},  {"HIDDENX2",  "DashLine"},
        {"DASHED",   "DashLine"},        {"DASHED2",  "DashLine(small)"}, {"DASHEDX2",  "DashLine(large)"},
        {"DASH",     "DashLine"},

        {"CENTER",   "CenterLine"},      {"CENTER2",  "CenterLine(small)"},{"CENTERX2", "CenterLine(large)"},

        {"PHANTOM",  "DivideLine"},      {"PHANTOM2", "DivideLine(small)"},{"PHANTOMX2","DivideLine(large)"},
        {"DIVIDE",   "DivideLine"},      {"DIVIDE2",  "DivideLine(small)"},{"DIVIDEX2", "DivideLine(large)"},

        {"DASHDOT",  "DashDotLine"},     {"DASHDOT2", "DashDotLine(tiny)"},{"DASHDOTX2","DashDotLine(large)"},
        {"DOT",      "DotLine"},         {"DOT2",     "DotLine(tiny)"},    {"DOTX2",    "DotLine(large)"},
        {"BORDER",   "BorderLine"},      {"BORDER2",  "BorderLine(small)"},{"BORDERX2", "BorderLine(large)"},

        // ISO 128 계열
        {"ACAD_ISO02W100", "DashLine"},        {"ACAD_ISO03W100", "DashLine(large)"},
        {"ACAD_ISO04W100", "DashDotLine"},     {"ACAD_ISO05W100", "DivideLine"},
        {"ACAD_ISO07W100", "DotLine"},         {"ACAD_ISO08W100", "DashLine(large)"},
        {"ACAD_ISO10W100", "DashDotLine"},     {"ACAD_ISO12W100", "DivideLine"},
    };
    const QString key = name.trimmed().toUpper();
    auto it = kMap.constFind(key);
    if (it == kMap.constEnd())
        return false;
    outCanonical = it.value();
    return true;
}

LineTypeManager::LineTypeManager()
{
}

LineTypeManager::~LineTypeManager()
{
}

QString LineTypeManager::getDefaultFilePath() const
{
    QString appDirPath = QCoreApplication::applicationDirPath();

    // 1순위: 배포 환경 — 실행 파일(.exe) 옆 Data 폴더
    QString path = QDir(appDirPath).absoluteFilePath("Data/linetype.lin");
    // 2순위: qmake 빌드 — exe가 release/ 또는 debug/ 하위일 때 한 단계 위 확인
    if (!QFile::exists(path)) {
        path = QDir::cleanPath(appDirPath + "/../Data/linetype.lin");
    }
    // 3순위: CMake 개발 환경 — APP_SOURCE_PATH(소스 루트)에서 탐색
#ifdef APP_SOURCE_PATH
    if (!QFile::exists(path)) {
        path = QDir(APP_SOURCE_PATH).absoluteFilePath("Data/linetype.lin");
    }
#endif
    return path;
}

bool LineTypeManager::loadFromFile(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    m_linetypes.clear();
    QTextStream in(&file);

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith('#') || line.trimmed().isEmpty())
            continue;

        if (line.length() > 50) {
            QString name = line.left(50).trimmed();
            QString pattern = line.mid(50).trimmed();
            if (!name.isEmpty() && !pattern.isEmpty()) {
                m_linetypes.insert(name, LineType(name, pattern));
            }
        }
    }
    return true;
}

bool LineTypeManager::saveToFile(const QString& filename)
{
    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    QList<QString> keys = m_linetypes.keys();

    // 인덱스 기반 루프 적용
    for (int i = 0; i < keys.size(); ++i) {
        const LineType& lt = m_linetypes.value(keys.at(i));
        out << lt.name.leftJustified(50, ' ', true) << lt.pattern << "\n";
    }
    return true;
}

bool LineTypeManager::addLinetype(const LineType& linetype)
{
    if(m_linetypes.contains(linetype.name))
        return false;
    m_linetypes.insert(linetype.name, linetype);
    return true;
}

bool LineTypeManager::editLinetype(const QString& oldName, const LineType& newLinetype)
{
    if(!m_linetypes.contains(oldName))
        return false;
    if (oldName != newLinetype.name) {
        m_linetypes.remove(oldName);
    }
    m_linetypes.insert(newLinetype.name, newLinetype);
    return true;
}

bool LineTypeManager::removeLinetype(const QString& name)
{
    return m_linetypes.remove(name) > 0;
}

LineType LineTypeManager::getLinetype(const QString& name) const
{
    // 1) 정확히 일치
    if (m_linetypes.contains(name)) {
        return m_linetypes.value(name);
    }

    // 2) 대소문자 무시 매칭 — DXF/DWG 임포트나 예전 도면은 "dashline", "DASHLINE",
    //    "HIDDEN" 처럼 등록명과 대소문자가 다른 이름을 저장하고 있어, 정확 매칭만
    //    하면 라인타입이 통째로 무시되어(패턴 공백 → 실선) 버린다.
    for (auto it = m_linetypes.constBegin(); it != m_linetypes.constEnd(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
            return it.value();
        }
    }

    // 3) AutoCAD 표준명 별칭 — "HIDDEN" → "DashLine(small)" 등. 매핑된 canonical 이름으로
    //    다시 조회한다. canonical 이 빈 문자열이면 실선(CONTINUOUS/ByLayer 등)이므로 빈 값.
    QString canonical;
    if (acadLinetypeAlias(name, canonical)) {
        if (canonical.isEmpty())
            return LineType();
        if (m_linetypes.contains(canonical))
            return m_linetypes.value(canonical);
        for (auto it = m_linetypes.constBegin(); it != m_linetypes.constEnd(); ++it) {
            if (it.key().compare(canonical, Qt::CaseInsensitive) == 0)
                return it.value();
        }
    }

    // 4) "Continuous"(u 포함) 오타 대비 — linetype.lin 에는 "Continous" 로 등록돼 있다.
    if (name.compare("Continuous", Qt::CaseInsensitive) == 0)
        return LineType();

    return LineType(); // 없으면 빈 값
}

QString LineTypeManager::resolveLinetypeName(const QString& name) const
{
    if (name.trimmed().isEmpty())
        return QString();

    if (m_linetypes.contains(name))
        return name;

    for (auto it = m_linetypes.constBegin(); it != m_linetypes.constEnd(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return it.key();
    }

    QString canonical;
    if (acadLinetypeAlias(name, canonical)) {
        if (canonical.isEmpty())
            return QString(); // 연속선
        if (m_linetypes.contains(canonical))
            return canonical;
        for (auto it = m_linetypes.constBegin(); it != m_linetypes.constEnd(); ++it) {
            if (it.key().compare(canonical, Qt::CaseInsensitive) == 0)
                return it.key();
        }
    }

    if (name.compare("Continuous", Qt::CaseInsensitive) == 0)
        return QString();

    return QString(); // 못 찾음
}

QList<LineType> LineTypeManager::getAllLinetypes() const
{
    return m_linetypes.values();
}

LineTypeManager::LinetypeGroup LineTypeManager::groupOf(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.toUpper().startsWith("ACAD_ISO"))
        return LinetypeGroup::AcadIso;
    // 대문자만으로 이루어진 이름(소문자가 하나도 없음) = AutoCAD 표준 선종류.
    // Autodraft 고유 선종류는 "DashLine", "BorderLine", "Continous" 처럼 대소문자 혼용.
    if (!trimmed.isEmpty() && trimmed == trimmed.toUpper() && trimmed != trimmed.toLower())
        return LinetypeGroup::AcadStandard;
    return LinetypeGroup::AutoDraft;
}

LineTypeManager::LinetypeGroup LineTypeManager::currentGroup()
{
    QSettings s; // 정규 .ini (main() 지정)
    const int v = s.value("LineType/CurrentGroup",
                          int(LinetypeGroup::AutoDraft)).toInt();
    if (v == int(LinetypeGroup::AcadStandard)) return LinetypeGroup::AcadStandard;
    if (v == int(LinetypeGroup::AcadIso))      return LinetypeGroup::AcadIso;
    return LinetypeGroup::AutoDraft;
}

void LineTypeManager::setCurrentGroup(LinetypeGroup group)
{
    QSettings s; // 정규 .ini (main() 지정)
    s.setValue("LineType/CurrentGroup", int(group));
}

QList<LineType> LineTypeManager::getLinetypesInGroup(LinetypeGroup group) const
{
    QList<LineType> out;
    for (auto it = m_linetypes.constBegin(); it != m_linetypes.constEnd(); ++it) {
        if (groupOf(it.key()) == group)
            out.append(it.value());
    }
    return out;
}

QVector<qreal> LineTypeManager::getQtDashPattern(const QString& typeName) const
{
    QVector<qreal> dashPattern;

    // 0. 연속선(가는 실선)은 대시 패턴이 아예 없어야 한다. 이름이 continuous 계열이면
    //    파일에 "11.0" 같은 값이 들어 있어도 무시하고 빈 패턴(=Qt::SolidLine) 반환.
    const QString low = typeName.trimmed().toLower();
    if (low.isEmpty() || low == "continuous" || low == "continous" ||
        low == "bylayer" || low == "byblock" || low == "solid")
        return dashPattern;

    // 1. 해당 라인타입 객체를 찾아 패턴 문자열 획득 (예: "12.0,-5.,0.2,-5.")
    LineType lt = getLinetype(typeName);
    QString patternStr = lt.pattern;

    // 2. 콤마(,)를 기준으로 문자열 자르기
    QStringList parts = patternStr.split(',');

    // 3. 인덱스 기반 루프로 숫자 파싱 및 절대값 변환
    for (int i = 0; i < parts.size(); ++i) {
        QString part = parts.at(i).trimmed();
        if (part.isEmpty()) continue;

        qreal val = part.toDouble();

        // [핵심] Qt는 선과 공백 모두 길이를 '양수'로 받으므로 qAbs 처리
        dashPattern.append(qAbs(val));
    }

    // 4. 유효한 대시 패턴은 '선분'과 '간격'이 필요 → 0이 아닌 값이 2개 이상이어야 한다.
    //    "11.0" / "11.0,0.0" 처럼 0 아닌 값이 1개뿐이면 실선 의도이므로 패턴 없이 반환.
    int nonZero = 0;
    for (int i = 0; i < dashPattern.size(); ++i)
        if (dashPattern.at(i) > 0.0) ++nonZero;
    if (nonZero < 2)
        dashPattern.clear();

    return dashPattern;
}

QIcon LineTypeManager::makePreviewIcon(const QVector<qreal>& dashPattern, const QSize& size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);

    const qreal x0   = 1.0;
    const qreal x1   = size.width() - 1.0;
    const qreal midY = size.height() / 2.0;

    QPen pen(Qt::black);
    pen.setWidth(1);
    pen.setCosmetic(true);

    QVector<qreal> pat = dashPattern;
    // 0(점) 요소는 화면에서 사라지므로 아주 짧은 대시로 치환
    for (int i = 0; i < pat.size(); ++i)
        if (pat.at(i) <= 0.0) pat[i] = 0.5;
    // Qt 는 짝수 길이 패턴만 그린다 — 홀수면 마지막 값 반복
    if (pat.size() % 2 != 0 && !pat.isEmpty())
        pat.append(pat.last());

    if (pat.size() >= 2) {
        qreal cycle = 0.0;
        for (int i = 0; i < pat.size(); ++i) cycle += pat.at(i);
        if (cycle > 0.0) {
            // 아이콘 폭에 약 2.5 주기가 들어가도록 스케일 (특징이 보이게)
            const qreal scale = (x1 - x0) / (cycle * 2.5);
            QVector<qreal> scaled;
            for (int i = 0; i < pat.size(); ++i)
                scaled.append(qMax<qreal>(pat.at(i) * scale, 0.2));
            pen.setStyle(Qt::CustomDashLine);
            pen.setDashPattern(scaled); // 단위 = pen width(1px)
        }
    }

    p.setPen(pen);
    p.drawLine(QPointF(x0, midY), QPointF(x1, midY));
    p.end();
    return QIcon(pm);
}

QIcon LineTypeManager::previewIconFor(const QString& typeName, const QSize& size) const
{
    return makePreviewIcon(getQtDashPattern(typeName), size);
}
