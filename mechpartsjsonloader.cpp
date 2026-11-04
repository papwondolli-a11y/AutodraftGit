#include "mechpartsjsonloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

QString resolveMechPartsDataDir() {
    QString dir = QCoreApplication::applicationDirPath() + "/Data/mechparts";
    if (!QDir(dir).exists()) {
        QString alt = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../Data/mechparts");
        if (QDir(alt).exists()) {
            dir = alt;
        }
#ifdef APP_SOURCE_PATH
        else {
            QString devDir = QDir::cleanPath(APP_SOURCE_PATH) + QDir::separator()
                            + "Data" + QDir::separator() + "mechparts";
            if (QDir(devDir).exists())
                dir = devDir;
        }
#endif
    }
    return dir;
}

// <resolveMechPartsDataDir()>/<fileName>을 읽어 QJsonDocument로 반환한다.
// 파일이 없거나 열 수 없으면 isNull() 상태의 빈 문서를 반환한다.
static QJsonDocument readMechPartsJson(const QString &fileName) {
    QFile f(QDir(resolveMechPartsDataDir()).filePath(fileName));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QJsonDocument();
    return QJsonDocument::fromJson(f.readAll());
}

QVector<HexBoltSize> loadHexBoltSizesFromJson(const QString &fileName) {
    QVector<HexBoltSize> out;
    QJsonDocument doc = readMechPartsJson(fileName);
    if (!doc.isArray()) return out;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        HexBoltSize sz;
        sz.label       = o.value("label").toString();
        sz.d           = o.value("d").toDouble();
        sz.pitch       = o.value("pitch").toDouble();
        sz.s           = o.value("s").toDouble();
        sz.k           = o.value("k").toDouble();
        sz.chamferD    = o.value("chamferD").toDouble();
        sz.filletConst = o.value("filletConst").toDouble();
        if (sz.label.isEmpty()) continue; // 필수 필드(label) 누락 시 건너뜀
        out.append(sz);
    }
    return out;
}

QVector<WrenchBoltSize> loadWrenchBoltSizesFromJson(const QString &fileName) {
    QVector<WrenchBoltSize> out;
    QJsonDocument doc = readMechPartsJson(fileName);
    if (!doc.isArray()) return out;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        WrenchBoltSize sz;
        sz.label       = o.value("label").toString();
        sz.d           = o.value("d").toDouble();
        sz.pitch       = o.value("pitch").toDouble();
        sz.headD       = o.value("headD").toDouble();
        sz.headH       = o.value("headH").toDouble();
        sz.chamferE    = o.value("chamferE").toDouble();
        sz.socketWidth = o.value("socketWidth").toDouble();
        sz.socketDepth = o.value("socketDepth").toDouble();
        sz.filletR     = o.value("filletR").toDouble();
        if (sz.label.isEmpty()) continue;
        out.append(sz);
    }
    return out;
}

QVector<HHBoltSize> loadHHBoltSizesFromJson(const QString &fileName) {
    QVector<HHBoltSize> out;
    QJsonDocument doc = readMechPartsJson(fileName);
    if (!doc.isArray()) return out;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        HHBoltSize sz;
        sz.label        = o.value("label").toString();
        sz.d            = o.value("d").toDouble();
        sz.pitch        = o.value("pitch").toDouble();
        sz.tipChamferK  = o.value("tipChamferK").toDouble();
        sz.headD        = o.value("headD").toDouble();
        sz.headH        = o.value("headH").toDouble();
        sz.socketDepth  = o.value("socketDepth").toDouble();
        sz.cornerH      = o.value("cornerH").toDouble();
        sz.socketWidth  = o.value("socketWidth").toDouble();
        if (sz.label.isEmpty()) continue;
        out.append(sz);
    }
    return out;
}

QVector<HBBoltSize> loadHBBoltSizesFromJson(const QString &fileName) {
    QVector<HBBoltSize> out;
    QJsonDocument doc = readMechPartsJson(fileName);
    if (!doc.isArray()) return out;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        HBBoltSize sz;
        sz.label       = o.value("label").toString();
        sz.d           = o.value("d").toDouble();
        sz.pitch       = o.value("pitch").toDouble();
        sz.tipChamferK = o.value("tipChamferK").toDouble();
        sz.headD       = o.value("headD").toDouble();
        sz.headH       = o.value("headH").toDouble();
        sz.socketDepth = o.value("socketDepth").toDouble();
        sz.socketWidth = o.value("socketWidth").toDouble();
        sz.neckLen     = o.value("neckLen").toDouble();
        if (sz.label.isEmpty()) continue;
        out.append(sz);
    }
    return out;
}

QVector<int> loadBoltLengthsFromJson(const QString &fileName) {
    QVector<int> out;
    QJsonDocument doc = readMechPartsJson(fileName);
    if (!doc.isArray()) return out;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (v.isDouble()) out.append(v.toInt());
    }
    return out;
}
