#ifndef DOCUMENTSETTINGS_H
#define DOCUMENTSETTINGS_H

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

// ─────────────────────────────────────────────────────────────────────────────
// DocumentSettings
//   도면 하나에 귀속되는 도구/문서 기본값 저장소. MyGraphicsScene 가 소유한다.
//   키는 "<도구네임스페이스>/<항목>" 평면 문자열 (예: "planDoor/H", "hatch/pattern").
//   .cdb 의 file_metadata['doc_settings'] 로 직렬화되며, 템플릿(.cdt)이 그대로
//   운반한다. → AutoCAD 시스템 변수처럼 "값은 도면 안에" 산다.
//
//   조회 우선순위(ToolDefaults 가 구현): 이 저장소 → 전역 QSettings → 코드 기본값.
// ─────────────────────────────────────────────────────────────────────────────
class DocumentSettings {
public:
    bool contains(const QString &key) const { return m_map.contains(key); }
    QVariant value(const QString &key,
                   const QVariant &fallback = QVariant()) const {
        return m_map.value(key, fallback);
    }
    void setValue(const QString &key, const QVariant &v) { m_map.insert(key, v); }
    void remove(const QString &key) { m_map.remove(key); }
    void clear() { m_map.clear(); }
    bool isEmpty() const { return m_map.isEmpty(); }

    // 직렬화: file_metadata['doc_settings'] 에 통째로 저장/복원된다.
    QJsonObject toJson() const { return QJsonObject::fromVariantMap(m_map); }
    void fromJson(const QJsonObject &obj) { m_map = obj.toVariantMap(); }

private:
    QVariantMap m_map;
};

#endif // DOCUMENTSETTINGS_H
