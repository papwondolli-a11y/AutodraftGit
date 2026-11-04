#include "settingsmigration.h"

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace {

struct LegacyRoot {
    const char *org;
    const char *app;
};

// 아주 옛날의 레거시 (org, app) 목록. 전부 레지스트리에만 존재했다.
const LegacyRoot kLegacyRoots[] = {
    {"MyCompany", "AutoDraft"}, // hatchtool, filemanager(recentFileList), batchprint
    {"MyCompany", "basicForm"}, // blockpalettedock
    {"MyCompany", "autodraft"}, // custommenubar(recentFileList - 소문자 분기)
};

// src 의 모든 키를 dst 로 복사한다. dst 에 이미 있는 키는 건드리지 않는다.
int copyMissingKeys(QSettings &src, QSettings &dst) {
    int copied = 0;
    const QStringList keys = src.allKeys();
    for (int i = 0; i < keys.size(); ++i) {
        const QString &k = keys.at(i);
        if (dst.contains(k))
            continue;
        dst.setValue(k, src.value(k));
        ++copied;
    }
    return copied;
}

} // namespace

void migrateLegacySettings() {
    // main() 에서 setDefaultFormat(IniFormat) + org/app 이 이미 설정돼 있어야 한다.
    // → canonical 은 이제 <RoamingAppData>/Autodraft/Autodraft.ini 를 가리킨다.
    QSettings canonical;

    // 스키마 2 = "레지스트리 → .ini 이전 완료". (스키마 1 = 3-0 의 레지스트리 루트 통합)
    const QString sentinel = QStringLiteral("migration/settingsSchema");
    if (canonical.value(sentinel).toInt() >= 2)
        return;

    // 1) 직전 정규 저장소: 레지스트리 HKCU\Software\Autodraft\Autodraft
    //    (3-0 이후 대부분의 설정이 여기 쌓여 있다 — 최근파일·테마·도구 기본값·template/defaultPath 등)
    {
        QSettings prevRegistry(QSettings::NativeFormat, QSettings::UserScope,
                               QStringLiteral("Autodraft"),
                               QStringLiteral("Autodraft"));
        copyMissingKeys(prevRegistry, canonical);
    }

    // 2) 그보다 더 이전의 레거시 루트들 (전부 레지스트리)
    const int n = int(sizeof(kLegacyRoots) / sizeof(kLegacyRoots[0]));
    for (int i = 0; i < n; ++i) {
        QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                         QString::fromLatin1(kLegacyRoots[i].org),
                         QString::fromLatin1(kLegacyRoots[i].app));
        if (legacy.allKeys().isEmpty())
            continue;
        copyMissingKeys(legacy, canonical);
    }

    // 레거시(레지스트리) 저장소는 지우지 않는다 — 롤백 여지 + 구버전 병행 실행 대비.
    canonical.setValue(sentinel, 2);
    canonical.sync();
}
