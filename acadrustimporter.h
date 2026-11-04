#ifndef ACADRUSTIMPORTER_H
#define ACADRUSTIMPORTER_H

#include <QString>
#include <QCoreApplication>

class MyGraphicsScene;
class QProgressDialog;

// =============================================================================
// AcadrustImporter — acadrust 기반 DWG 가져오기 진입점
//
// 외부 변환기(tools/dwg2dxf.exe, acadrust 기반 Rust 프로그램)로 DWG를 임시 DXF로
// 변환한 뒤, 기존 DxfManager 가져오기 경로로 도면에 추가한다.
// 변환기가 실패해도 본 프로그램은 영향받지 않는다.
//
// 사용 예:
//   AcadrustImporter importer;
//   QString err;
//   if (!importer.importFromDwg("in.dwg", scene, &progress, &err)) { ... }
// =============================================================================
class AcadrustImporter {
    Q_DECLARE_TR_FUNCTIONS(AcadrustImporter)

public:
    AcadrustImporter();

    // 변환기 exe 경로 (실행 파일 옆 tools/ 폴더 → 실행 파일 폴더 → 소스 폴더 tools/)
    static QString converterPath();

    // 변환기 exe가 존재하는지 확인
    static bool isAvailable();

    // dwgPath 의 DWG를 scene 에 가져온다.
    // progress: 널이 아니면 DXF 파싱 단계의 진행 표시에 사용 (널 가능)
    // errorMessage: 실패 시 사유 (널 가능)
    bool importFromDwg(const QString &dwgPath, MyGraphicsScene *scene,
                       QProgressDialog *progress, QString *errorMessage);

private:
    // 변환기를 실행하고 끝날 때까지 이벤트 루프를 돌리며 기다린다.
    bool runConverter(const QString &dwgPath, const QString &dxfPath,
                      QString *errorMessage);
};

#endif // ACADRUSTIMPORTER_H
