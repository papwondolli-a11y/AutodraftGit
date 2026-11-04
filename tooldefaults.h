#ifndef TOOLDEFAULTS_H
#define TOOLDEFAULTS_H

#include <QString>
#include <QVariant>

class MyGraphicsScene;

// ─────────────────────────────────────────────────────────────────────────────
// ToolDefaults
//   도구 기본값의 3단 조회 / 이중 기록 어댑터. 각 도구의 load/saveXxxSettings()
//   를 최소 diff 로 교체하기 위한 얇은 래퍼.
//
//   get(key, factory):
//     ① 현재 도면 DocumentSettings 의 "<ns>/<key>" 가 있으면 그 값
//     ② 없으면 전역 QSettings(정규 루트) group("<ns>") 의 key (없으면 factory)
//     ③ 둘 다 없으면 factory
//
//   put(key, value):
//     현재 도면 DocumentSettings + 전역 QSettings 둘 다에 기록.
//     (전역 기록 → 기존 "마지막 값이 다음 새 도면에 이어짐" UX 유지)
//     scene 이 null 이면 전역에만 기록한다.
// ─────────────────────────────────────────────────────────────────────────────
class ToolDefaults {
public:
    ToolDefaults(const QString &toolNamespace, MyGraphicsScene *scene);

    QVariant get(const QString &key, const QVariant &factoryDefault) const;
    void     put(const QString &key, const QVariant &value);

    // 편의 타입 접근자 (JSON 왕복으로 int↔double 이 섞여도 안전하게 변환)
    double  getDouble(const QString &key, double def) const;
    int     getInt(const QString &key, int def) const;
    bool    getBool(const QString &key, bool def) const;
    QString getString(const QString &key, const QString &def) const;

private:
    QString m_ns;
    MyGraphicsScene *m_scene;
};

#endif // TOOLDEFAULTS_H
