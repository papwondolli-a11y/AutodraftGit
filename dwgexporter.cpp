#include "dwgexporter.h"
#include "dxfmanager.h"
#include "mygraphicsscene.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

namespace {
const int kConvertTimeoutMs = 300000;  // 큰 도면 대비 5분
}

DwgExporter::DwgExporter() {}

QString DwgExporter::converterPath() {
    const QString dir = QCoreApplication::applicationDirPath();
    const QString inTools = dir + QStringLiteral("/tools/dxf2dwg.exe");
    if (QFileInfo::exists(inTools))
        return inTools;
    return dir + QStringLiteral("/dxf2dwg.exe");
}

bool DwgExporter::isAvailable() {
    return QFileInfo::exists(converterPath());
}

bool DwgExporter::runConverter(const QString &dxfPath, const QString &outPath,
                               const QString &version, QString *errorMessage) {
    QProcess proc;
    QStringList args;
    args << dxfPath << outPath << QStringLiteral("--version") << version;
    proc.start(converterPath(), args);
    if (!proc.waitForStarted(5000)) {
        if (errorMessage)
            *errorMessage = tr("변환기를 실행할 수 없습니다: %1").arg(converterPath());
        return false;
    }

    // UI가 멈추지 않도록 100ms 단위로 대기하며 이벤트를 처리한다.
    int waited = 0;
    while (!proc.waitForFinished(100)) {
        QApplication::processEvents();
        waited += 100;
        if (waited >= kConvertTimeoutMs) {
            proc.kill();
            proc.waitForFinished(3000);
            if (errorMessage)
                *errorMessage = tr("변환 시간이 초과되었습니다.");
            return false;
        }
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (errorMessage) {
            const QString detail =
                QString::fromUtf8(proc.readAllStandardError()).trimmed();
            *errorMessage = detail.isEmpty()
                                ? tr("변환기가 오류 코드 %1로 종료되었습니다.").arg(proc.exitCode())
                                : detail;
        }
        return false;
    }
    return true;
}

bool DwgExporter::exportToDwg(const QString &dwgPath, MyGraphicsScene *scene,
                              const QString &version, QString *errorMessage) {
    if (!scene) {
        if (errorMessage)
            *errorMessage = tr("도면이 없습니다.");
        return false;
    }
    if (!isAvailable()) {
        if (errorMessage)
            *errorMessage = tr("DWG 변환기(tools/dxf2dwg.exe)를 찾을 수 없습니다.");
        return false;
    }

    // 임시 폴더(작업 후 자동 삭제)에서 변환하고, 성공했을 때만 대상 경로로 옮긴다.
    // 변환 도중 실패해도 기존 대상 파일이 손상되지 않게 하기 위함이다.
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMessage)
            *errorMessage = tr("임시 폴더를 만들 수 없습니다.");
        return false;
    }
    const QString tempDxf = tempDir.filePath(QStringLiteral("export.dxf"));
    const QString tempDwg = tempDir.filePath(QStringLiteral("export.dwg"));

    DxfManager manager;
    if (!manager.exportToDxf(tempDxf, scene)) {
        if (errorMessage)
            *errorMessage = tr("중간 DXF 파일 생성에 실패했습니다.");
        return false;
    }

    if (!runConverter(tempDxf, tempDwg, version, errorMessage))
        return false;

    if (!QFileInfo::exists(tempDwg)) {
        if (errorMessage)
            *errorMessage = tr("변환 결과 파일이 만들어지지 않았습니다.");
        return false;
    }

    if (QFile::exists(dwgPath) && !QFile::remove(dwgPath)) {
        if (errorMessage)
            *errorMessage = tr("기존 파일을 덮어쓸 수 없습니다. 다른 프로그램에서 열려 있는지 확인하세요.");
        return false;
    }
    if (!QFile::copy(tempDwg, dwgPath)) {
        if (errorMessage)
            *errorMessage = tr("결과 파일을 저장할 수 없습니다: %1").arg(QDir::toNativeSeparators(dwgPath));
        return false;
    }
    return true;
}
