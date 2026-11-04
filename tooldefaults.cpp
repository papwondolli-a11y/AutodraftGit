#include "tooldefaults.h"

#include "documentsettings.h"
#include "mygraphicsscene.h"

#include <QSettings>

ToolDefaults::ToolDefaults(const QString &toolNamespace, MyGraphicsScene *scene)
    : m_ns(toolNamespace), m_scene(scene) {}

QVariant ToolDefaults::get(const QString &key,
                           const QVariant &factoryDefault) const {
    const QString docKey = m_ns + QLatin1Char('/') + key;

    // ① 현재 도면
    if (m_scene && m_scene->documentSettings() &&
        m_scene->documentSettings()->contains(docKey)) {
        return m_scene->documentSettings()->value(docKey);
    }

    // ② 전역 QSettings 정규 루트
    QSettings s; // (Autodraft/Autodraft) — main() 에서 org/app 설정됨
    s.beginGroup(m_ns);
    const QVariant v = s.value(key, factoryDefault);
    s.endGroup();
    return v;
}

void ToolDefaults::put(const QString &key, const QVariant &value) {
    const QString docKey = m_ns + QLatin1Char('/') + key;

    // 도면 스코프
    if (m_scene && m_scene->documentSettings())
        m_scene->documentSettings()->setValue(docKey, value);

    // 전역 스코프 (다음 새 도면의 씨앗)
    QSettings s;
    s.beginGroup(m_ns);
    s.setValue(key, value);
    s.endGroup();
}

double ToolDefaults::getDouble(const QString &key, double def) const {
    return get(key, def).toDouble();
}
int ToolDefaults::getInt(const QString &key, int def) const {
    return get(key, def).toInt();
}
bool ToolDefaults::getBool(const QString &key, bool def) const {
    return get(key, def).toBool();
}
QString ToolDefaults::getString(const QString &key, const QString &def) const {
    return get(key, def).toString();
}
