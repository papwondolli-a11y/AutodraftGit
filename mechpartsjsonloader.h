#ifndef MECHPARTSJSONLOADER_H
#define MECHPARTSJSONLOADER_H

#include "mechpartstandards.h"

#include <QString>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────
// mechpartsjsonloader.h — 기계부품 규격표를 외부 JSON 파일에서 읽어오는 로더.
// 재컴파일 없이 사용자가 Data/mechparts/*.json 파일을 직접 편집해 규격을
// 추가/수정할 수 있게 하기 위한 것 (gdtdialog.cpp의 resolveGdtDataDir() +
// QJsonDocument 프리셋 저장/불러오기 패턴을 그대로 따름).
//
// 여기 있는 함수들은 순수하게 "파일 읽기/JSON 파싱"만 담당한다 — 파일이 없거나
// 손상됐을 때 어떤 기본값으로 대체할지는 mechpartstandards.cpp의 공개 접근자
// 함수(kKsB1002HexBolts() 등)가 결정한다(항상 빈 QVector를 반환해 실패를 알림).
// ─────────────────────────────────────────────────────────────────────────

// Data/mechparts 폴더 위치 탐색: 1) exe 옆 Data/mechparts, 2) 상위 폴더의
// Data/mechparts(qmake debug/release 빌드), 3) 소스 루트의 Data/mechparts
// (개발 환경, APP_SOURCE_PATH가 정의된 디버그 빌드에서만).
QString resolveMechPartsDataDir();

// 각 규격표를 <resolveMechPartsDataDir()>/<fileName> 에서 읽어 반환한다.
// 파일이 없거나 JSON 파싱에 실패하거나(배열이 아니거나) 배열이 비어있으면
// 빈 QVector를 반환한다 — 호출부가 하드코딩된 기본값으로 대체해야 한다.
QVector<HexBoltSize>    loadHexBoltSizesFromJson(const QString &fileName = QStringLiteral("hexbolt.json"));
QVector<WrenchBoltSize> loadWrenchBoltSizesFromJson(const QString &fileName = QStringLiteral("wrenchbolt.json"));
QVector<HHBoltSize>     loadHHBoltSizesFromJson(const QString &fileName = QStringLiteral("hhbolt.json"));
QVector<HBBoltSize>     loadHBBoltSizesFromJson(const QString &fileName = QStringLiteral("hbbolt.json"));
// kKsPreferredBoltLengths는 구조체 배열이 아니라 정수 배열이라 JSON 형태가 다르다
// (예: [10, 12, 16, ...]) — 별도 파서로 분리.
QVector<int>            loadBoltLengthsFromJson(const QString &fileName = QStringLiteral("boltlengths.json"));

#endif // MECHPARTSJSONLOADER_H
