#include "bearinglayer.h"

#include "tooldefaults.h"

namespace {
const char *const kNamespace = "BearingCommon";
const char *const kKeyBody   = "BodyLayer";
}

QString BearingLayer::defaultName() {
    return QStringLiteral("BEARING");
}

QString BearingLayer::bodyLayer(MyGraphicsScene *scene) {
    const QString name = ToolDefaults(QString::fromLatin1(kNamespace), scene)
                             .getString(QString::fromLatin1(kKeyBody), defaultName());
    return name.isEmpty() ? defaultName() : name;
}

void BearingLayer::setBodyLayer(MyGraphicsScene *scene, const QString &name) {
    if (name.isEmpty()) return;
    ToolDefaults(QString::fromLatin1(kNamespace), scene).put(QString::fromLatin1(kKeyBody), name);
}
