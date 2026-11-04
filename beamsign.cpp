#include "beamsign.h"

#include <QByteArray>
#include <QCollator>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

namespace BeamSignBuilder {
const QString kBlockName = QStringLiteral("BEAMSIGN");
const QString kTagPrefix = QStringLiteral("PREFIX");
const QString kTagType   = QStringLiteral("TYPE");
const QString kTagNumber = QStringLiteral("NO");
const QString kOutLayer  = QStringLiteral("out");
const QString kTextLayer = QStringLiteral("signT");
}

// ─────────────────────────────────────────────────────────────────────────────
// 종류 / 층 구분 목록 (./data/beam_sign.json)
// ─────────────────────────────────────────────────────────────────────────────
static QString findSignJson()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << exeDir + QStringLiteral("/data/beam_sign.json")
               << QDir::cleanPath(exeDir + QStringLiteral("/../data/beam_sign.json"));
#ifdef APP_SOURCE_PATH
    candidates << QDir::cleanPath(QStringLiteral(APP_SOURCE_PATH)) + QStringLiteral("/data/beam_sign.json");
#endif
    for (int i = 0; i < candidates.size(); ++i) {
        if (QFile::exists(candidates.at(i))) return candidates.at(i);
    }
    return QString();
}

static QStringList readJsonList(const QString &key)
{
    const QString path = findSignJson();
    if (path.isEmpty()) return QStringList();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QStringList();
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QStringList out;
    const QJsonArray arr = doc.object().value(key).toArray();
    for (int i = 0; i < arr.size(); ++i) {
        const QString s = arr.at(i).toString().trimmed();
        if (!s.isEmpty() && !out.contains(s)) out << s;
    }
    return out;
}

QStringList BeamSignText::defaultTypes()
{
    const QStringList l = readJsonList(QStringLiteral("types"));
    if (!l.isEmpty()) return l;
    return QStringList() << "G" << "B" << "CG" << "SG";
}

QStringList BeamSignText::defaultPrefixes()
{
    const QStringList l = readJsonList(QStringLiteral("prefixes"));
    if (!l.isEmpty()) return l;
    return QStringList() << "1" << "2" << "3" << "R" << "F" << "B1" << "P";
}

// ─────────────────────────────────────────────────────────────────────────────
// 부호 문자열 분해 / 번호
// ─────────────────────────────────────────────────────────────────────────────
BeamMarkParts BeamSignText::parse(const QString &nameIn, const QStringList &knownTypes)
{
    BeamMarkParts p;
    const QString name = nameIn.trimmed();
    if (name.isEmpty()) return p;

    // 끝의 숫자 = 번호
    int digitsStart = name.size();
    while (digitsStart > 0 && name.at(digitsStart - 1).isDigit()) --digitsStart;
    p.number = name.mid(digitsStart);
    const QString head = name.left(digitsStart);   // 층 구분 + 종류
    if (head.isEmpty()) return p;

    // 종류 : 알려진 종류 중 head 의 끝과 일치하는 가장 긴 것
    QString best;
    for (int i = 0; i < knownTypes.size(); ++i) {
        const QString t = knownTypes.at(i);
        if (t.size() > best.size() && head.size() >= t.size()
            && head.endsWith(t, Qt::CaseSensitive))
            best = t;
    }
    if (best.isEmpty()) best = head.right(1);   // 모르는 종류 : 글자 하나

    p.type   = best;
    p.prefix = head.left(head.size() - best.size());
    return p;
}

int BeamSignText::nextNumber(const QStringList &names, const QString &prefix, const QString &type,
                             const QStringList &knownTypes)
{
    int maxNo = 0;
    for (int i = 0; i < names.size(); ++i) {
        const BeamMarkParts p = parse(names.at(i), knownTypes);
        if (p.prefix != prefix || p.type != type || p.number.isEmpty()) continue;
        bool ok = false;
        const int n = p.number.toInt(&ok);
        if (ok) maxNo = std::max(maxNo, n);
    }
    return maxNo + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// 부호 도형 (g.cdb : 높이 6 타원형 + 문자 3개, 중심 (0,0))
// ─────────────────────────────────────────────────────────────────────────────
BeamSignGeom BeamSignBuilder::build(const BeamMarkParts &parts, const BeamSignSizes &sizes,
                                    const BeamSignMetrics &prefix, const BeamSignMetrics &type,
                                    const BeamSignMetrics &number)
{
    const double kTypeSize = sizes.type;
    const double kHeight   = std::max(6.0, kTypeSize * 2.0);   // g.cdb : 종류 글자 3 → 높이 6
    const double kRadius   = kHeight / 2.0;
    const double kMinStraight = 6.0;     // g.cdb : 직선부 길이 6 (전체 폭 12)
    const double kGap      = 0.8;        // 종류 글자 획과 앞/뒤 문자 획 사이 (양쪽 동일)
    const double kMargin   = 0.4;        // 직선부 안쪽 여유
    const double kBaseline = -0.377 * kTypeSize;   // g.cdb : 문자 베이스라인 = 중심 y - 1.13 (종류 글자 3)

    BeamSignGeom g;
    g.height = kHeight;

    const bool hasPrefix = !parts.prefix.isEmpty();

    // 종류 글자는 가운데 (가운데 정렬 기준 중심 0). 실제 획의 좌우 끝
    const double typeInkL = -type.advance / 2.0 + type.inkLeft;
    const double typeInkR = -type.advance / 2.0 + type.inkRight;

    // 가운데 정렬 문자의 중심 x : 획 사이 간격이 kGap 이 되도록
    //   prefix 획 오른쪽 끝 = typeInkL - kGap,  number 획 왼쪽 끝 = typeInkR + kGap
    const double prefixCx = typeInkL - kGap - prefix.inkRight + prefix.advance / 2.0;
    const double numberCx = typeInkR + kGap - number.inkLeft + number.advance / 2.0;

    // 외곽 폭 : 가장 바깥 획 기준
    const double leftExtent  = hasPrefix ? -(prefixCx - prefix.advance / 2.0 + prefix.inkLeft)
                                         : -typeInkL;
    const double rightExtent = numberCx - number.advance / 2.0 + number.inkRight;
    const double half = std::max(std::max(leftExtent, rightExtent), std::max(-typeInkL, typeInkR));
    const double straight = std::max(kMinStraight, 2.0 * half + kMargin);
    g.width = straight + 2.0 * kRadius;

    g.lines.append(QLineF(-straight / 2.0,  kRadius, straight / 2.0,  kRadius));
    g.lines.append(QLineF(-straight / 2.0, -kRadius, straight / 2.0, -kRadius));

    BeamSignArc left;
    left.center = QPointF(-straight / 2.0, 0.0);
    left.radius = kRadius;
    left.startAngle = 90.0;
    left.spanAngle  = 180.0;
    g.arcs.append(left);
    BeamSignArc right;
    right.center = QPointF(straight / 2.0, 0.0);
    right.radius = kRadius;
    right.startAngle = 270.0;
    right.spanAngle  = 180.0;
    g.arcs.append(right);

    if (hasPrefix) {
        BeamSignLabel t;
        t.text   = parts.prefix;
        t.tag    = kTagPrefix;
        t.prompt = QStringLiteral("층 구분");
        t.pos    = QPointF(prefixCx, kBaseline);
        t.size   = sizes.prefix;
        g.texts.append(t);
    }
    {
        BeamSignLabel t;
        t.text   = parts.type;
        t.tag    = kTagType;
        t.prompt = QStringLiteral("종류");
        t.pos    = QPointF(0.0, kBaseline);
        t.size   = kTypeSize;
        g.texts.append(t);
    }
    {
        BeamSignLabel t;
        t.text   = parts.number;
        t.tag    = kTagNumber;
        t.prompt = QStringLiteral("번호");
        t.pos    = QPointF(numberCx, kBaseline);
        t.size   = sizes.number;
        g.texts.append(t);
    }
    return g;
}

// ─────────────────────────────────────────────────────────────────────────────
// 구조도 .cdb 에서 부호 읽기
//   entities(type='Block', properties.Name='BEAMSIGN') 의 geometry 블롭 :
//   FileManager 직렬화와 동일 (QDataStream Qt_6_0)
//   QString 이름, double 축척X/Y, 회전, 삽입점 x/y, quint32 자식 수,
//   자식마다 { QString 종류, QString 레이어, QByteArray 속성 JSON, QByteArray 형상 }
// ─────────────────────────────────────────────────────────────────────────────
QMap<QString, int> BeamSignReader::readCdb(const QString &path, QString *error)
{
    QMap<QString, int> result;
    static int s_seq = 0;
    const QString conn = QStringLiteral("beamsign_read_%1").arg(++s_seq);
    QString err;

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            err = db.lastError().text();
        } else {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral(
                    "SELECT e.properties, g.data FROM entities e "
                    "JOIN geometry g ON g.entity_id = e.id WHERE e.type = 'Block'"))) {
                err = q.lastError().text();
            } else {
                while (q.next()) {
                    const QJsonObject props = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
                    if (props.value("Name").toString() != BeamSignBuilder::kBlockName) continue;

                    QByteArray blob = q.value(1).toByteArray();
                    QDataStream in(&blob, QIODevice::ReadOnly);
                    in.setVersion(QDataStream::Qt_6_0);

                    QString bName;
                    double sx = 0, sy = 0, rot = 0, ix = 0, iy = 0;
                    quint32 count = 0;
                    in >> bName >> sx >> sy >> rot >> ix >> iy >> count;
                    if (in.status() != QDataStream::Ok) continue;

                    BeamMarkParts parts;
                    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
                        QString childType, childLayer;
                        QByteArray childProps, childData;
                        in >> childType >> childLayer >> childProps >> childData;
                        if (childType != QLatin1String("Text")) continue;

                        const QJsonObject o = QJsonDocument::fromJson(childProps).object();
                        const QString tag = o.value("tag").toString();
                        QString content = o.value("Content").toString();
                        if (content.isEmpty()) content = o.value("content").toString();
                        if (tag == BeamSignBuilder::kTagPrefix)      parts.prefix = content;
                        else if (tag == BeamSignBuilder::kTagType)   parts.type = content;
                        else if (tag == BeamSignBuilder::kTagNumber) parts.number = content;
                    }
                    if (parts.isValid()) result[parts.compose()] += 1;
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);

    if (error) *error = err;
    return result;
}

namespace {
struct NaturalLess {
    const QCollator *collator;
    bool operator()(const QString &a, const QString &b) const { return collator->compare(a, b) < 0; }
};
}

QStringList BeamSignReader::naturalSorted(const QStringList &names)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    QStringList out = names;
    NaturalLess less;
    less.collator = &collator;
    std::sort(out.begin(), out.end(), less);
    return out;
}
