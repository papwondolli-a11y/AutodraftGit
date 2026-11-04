#ifndef SETTINGSMIGRATION_H
#define SETTINGSMIGRATION_H

// ─────────────────────────────────────────────────────────────────────────────
// 설정 저장소 마이그레이션
//   AutoDraft 의 전역 설정은 QSettings 로 관리하며, 저장 포맷은 .ini 파일이다.
//     위치: <RoamingAppData>/Autodraft/Autodraft.ini
//           (Windows: C:\Users\<사용자>\AppData\Roaming\Autodraft\Autodraft.ini)
//
//   migrateLegacySettings() 는 과거 저장소의 값을 이 .ini 로 1회 이전한다.
//     - 직전 정규 저장소: 레지스트리 HKCU\Software\Autodraft\Autodraft
//     - 더 이전 레거시 루트: ("MyCompany","AutoDraft"/"basicForm"/"autodraft") 레지스트리
//   .ini 에 없는 키만 복사(비파괴), sentinel "migration/settingsSchema">=2 로 멱등.
//   레지스트리는 지우지 않는다(구버전으로 롤백 시 그대로 동작).
//
//   호출 위치: main() 에서
//     QSettings::setDefaultFormat(QSettings::IniFormat) +
//     QCoreApplication::setOrganizationName/setApplicationName 설정 "직후",
//     첫 QSettings 사용 전.
// ─────────────────────────────────────────────────────────────────────────────
void migrateLegacySettings();

#endif // SETTINGSMIGRATION_H
