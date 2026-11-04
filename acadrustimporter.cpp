#include "acadrustimporter.h"
#include "dxfmanager.h"
#include "mygraphicsscene.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QTemporaryDir>

namespace {
const int kConvertTimeoutMs = 300000;  // 큰 도면 대비 5분
}

AcadrustImporter::AcadrustImporter() {}

QString AcadrustImporter::converterPath() {
    const QString dir = QCoreApplication::applicationDirPath();
    const QString inTools = dir + QStringLiteral("/tools/dwg2dxf.exe");
    if (QFileInfo::exists(inTools))
        return inTools;
    const QString besideExe = dir + QStringLiteral("/dwg2dxf.exe");
    if (QFileInfo::exists(besideExe))
        return besideExe;
#ifdef APP_SOURCE_PATH
    // 개발(Debug) 빌드: 빌드 폴더에는 tools/ 가 없으므로 소스 폴더의 tools/ 를 사용한다.
    const QString inSource =
        QDir::cleanPath(QStringLiteral(APP_SOURCE_PATH)) + QStringLiteral("/tools/dwg2dxf.exe");
    if (QFileInfo::exists(inSource))
        return inSource;
#endif
    return inTools;
}

bool AcadrustImporter::isAvailable() {
    return QFileInfo::exists(converterPath());
}

bool AcadrustImporter::runConverter(const QString &dwgPath, const QString &dxfPath,
                                    QString *errorMessage) {
    QProcess proc;
    QStringList args;
    // 항상 2018 형식(UTF-8) DXF로 쓴다. 원본 DWG가 R2007 미만이어도 코드페이지
    // 변환 없이 한글·지름 기호(Ø)가 그대로 전달된다.
    args << dwgPath << dxfPath << QStringLiteral("--version") << QStringLiteral("2018");
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

bool AcadrustImporter::importFromDwg(const QString &dwgPath, MyGraphicsScene *scene,
                                     QProgressDialog *progress, QString *errorMessage) {
    if (!scene) {
        if (errorMessage)
            *errorMessage = tr("도면이 없습니다.");
        return false;
    }
    if (!isAvailable()) {
        if (errorMessage)
            *errorMessage = tr("DWG 변환기(tools/dwg2dxf.exe)를 찾을 수 없습니다.");
        return false;
    }

    // 임시 폴더(작업 후 자동 삭제)에서 변환한다.
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMessage)
            *errorMessage = tr("임시 폴더를 만들 수 없습니다.");
        return false;
    }
    const QString tempDxf = tempDir.filePath(QStringLiteral("import.dxf"));

    if (!runConverter(dwgPath, tempDxf, errorMessage))
        return false;

    if (!QFileInfo::exists(tempDxf)) {
        if (errorMessage)
            *errorMessage = tr("변환 결과 파일이 만들어지지 않았습니다.");
        return false;
    }

    if (progress)
        progress->setLabelText(tr("변환된 도면을 읽는 중입니다..."));

    DxfManager manager;
    if (!manager.importFromDxf(tempDxf, scene, progress)) {
        if (errorMessage)
            *errorMessage = tr("변환된 DXF를 가져오지 못했습니다.");
        return false;
    }
    return true;
}
