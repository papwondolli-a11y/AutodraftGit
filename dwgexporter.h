#ifndef DWGEXPORTER_H
#define DWGEXPORTER_H

#include <QString>
#include <QCoreApplication>

class MyGraphicsScene;

// =============================================================================
// DwgExporter — DWG 파일 저장 진입점
//
// DWG 바이너리를 직접 쓰지 않고, 기존 DxfManager로 임시 DXF를 만든 뒤
// 외부 변환기(tools/dxf2dwg.exe, acadrust 기반 Rust 프로그램)를 실행해
// DWG로 변환한다. 변환기가 실패해도 본 프로그램은 영향받지 않는다.
//
// 사용 예:
//   DwgExporter exporter;
//   QString err;
//   if (!exporter.exportToDwg("out.dwg", scene, "2018", &err)) { ... }
// =============================================================================
class DwgExporter {
    Q_DECLARE_TR_FUNCTIONS(DwgExporter)

public:
    DwgExporter();

    // 변환기 exe 경로 (실행 파일 옆 tools/ 폴더, 없으면 실행 파일 폴더)
    static QString converterPath();

    // 변환기 exe가 존재하는지 확인
    static bool isAvailable();

    // scene을 DWG로 저장한다.
    // version: "2000" / "2004" / "2010" / "2018" 등 dxf2dwg --version 값
    // errorMessage: 실패 시 사유 (널 가능)
    bool exportToDwg(const QString &dwgPath, MyGraphicsScene *scene,
                     const QString &version, QString *errorMessage);

private:
    // 변환기를 실행하고 끝날 때까지 이벤트 루프를 돌리며 기다린다.
    bool runConverter(const QString &dxfPath, const QString &outPath,
                      const QString &version, QString *errorMessage);
};

#endif // DWGEXPORTER_H
