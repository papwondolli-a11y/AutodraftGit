#include "templatemanager.h"

#include "drawsubwindow.h"
#include "filemanager.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUndoStack>

QString TemplateManager::templateDir() {
    const QString exeDir = QCoreApplication::applicationDirPath();

    // GD&T 프리셋(resolveGdtDataDir)과 같은 우선순위로 data 폴더를 찾는다.
    QStringList candidates;
    candidates << exeDir + QStringLiteral("/data");
    candidates << QDir::cleanPath(exeDir + QStringLiteral("/../data"));
#ifdef APP_SOURCE_PATH
    candidates << QDir::cleanPath(QStringLiteral(APP_SOURCE_PATH) +
                                  QStringLiteral("/data"));
#endif

    for (int i = 0; i < candidates.size(); ++i) {
        if (QDir(candidates.at(i)).exists())
            return candidates.at(i);
    }

    // 아무 것도 없으면 사용자 지정 기본값(<exe>/../data)을 만들어 쓴다.
    const QString fallback = QDir::cleanPath(exeDir + QStringLiteral("/../data"));
    QDir().mkpath(fallback);
    return fallback;
}

QList<TemplateInfo> TemplateManager::availableTemplates() {
    QList<TemplateInfo> result;

    QDir dir(templateDir());
    QStringList filters;
    filters << QStringLiteral("*.cdt");
    const QFileInfoList entries =
        dir.entryInfoList(filters, QDir::Files, QDir::Name);

    // 썸네일은 .cdb 스키마의 file_metadata['preview'] 를 그대로 재사용한다.
    // (loadThumbnail 은 m_ctx 를 쓰지 않으므로 ctx 없이 호출해도 안전하다.)
    FileManager fm(nullptr);
    for (int i = 0; i < entries.size(); ++i) {
        const QFileInfo &fi = entries.at(i);
        TemplateInfo info;
        info.path = fi.absoluteFilePath();
        info.name = fi.completeBaseName();
        info.thumbnail = fm.loadThumbnail(info.path);
        result.append(info);
    }
    return result;
}

QString TemplateManager::defaultTemplatePath() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    const QString path =
        settings.value(QStringLiteral("template/defaultPath")).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QString();
    return path;
}

void TemplateManager::setDefaultTemplatePath(const QString &path) {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    settings.setValue(QStringLiteral("template/defaultPath"), path);
}

bool TemplateManager::createWindowFromTemplate(IAppContext *ctx,
                                               const QString &templatePath,
                                               bool asPrototype) {
    if (!ctx)
        return false;

    const bool useTemplate =
        !templatePath.isEmpty() && QFileInfo::exists(templatePath);
    // "템플릿 보기 > 만들기/확인"으로 명시적으로 만든 경우에만 "원형도면"
    // 명명을 적용한다 — 만들기는 항상 빈 창(templatePath 없음)이어도 원형도면
    // 취급을 받는다. Ctrl+N(QNEW) 의 기본 템플릿 자동 적용 경로는
    // asPrototype=false 로 들어와 내용은 템플릿을 따르되 이름은 종전과 같은
    // "Drawing N"을 유지한다.
    // "템플릿 보기"는 대시보드 상태에서만 열 수 있고, 창이 만들어지는 즉시
    // 작업 영역으로 전환되며 자동으로 잠기므로(showWorkspace() 참고) 여기서
    // 별도로 비활성화/재활성화할 필요가 없다.
    DrawSubWindow *window = new DrawSubWindow();
    ctx->addSubWindow(window); // 여기서 setContext() → 기본 레이어 세팅까지 완료됨

    if (asPrototype) {
        window->setWindowTitle(QObject::tr("원형도면"));
        // 템플릿 만들기/확인으로 연 도면만 "템플릿으로 저장"이 가능하도록 표시.
        window->setProperty("isTemplateOrigin", true);
    }

    if (useTemplate && ctx->fileManager()) {
        // asTemplate=true : 최근 파일 목록·테마 강제 적용을 건너뛴다.
        const bool ok =
            ctx->fileManager()->loadDrawing(templatePath, window->scene(), true);
        if (ok) {
            // 무제 문서로 시작: filePath 속성을 두지 않아 첫 저장이 "다른 이름으로".
            if (window->undoStack())
                window->undoStack()->clear();
        }
        // 로드 실패해도 빈 창은 그대로 사용 (사용자 흐름을 끊지 않음)
    }

    window->scheduleInitialViewFit();
    return true;
}

bool TemplateManager::openTemplateForEdit(IAppContext *ctx,
                                          const QString &templatePath) {
    if (!ctx || !ctx->fileManager())
        return false;
    if (templatePath.isEmpty() || !QFileInfo::exists(templatePath))
        return false;

    DrawSubWindow *window = new DrawSubWindow();
    ctx->addSubWindow(window);

    // asTemplate=true : 편집 세션이 사용자 테마를 갈아엎거나 최근 파일을
    // 어지럽히지 않게 한다. 씬 설정값 자체는 파일에 저장된 그대로 전부 복원된다.
    if (!ctx->fileManager()->loadDrawing(templatePath, window->scene(), true)) {
        window->close();
        return false;
    }

    // 템플릿 파일 자체를 편집: filePath 를 걸어 Ctrl+S 가 이 .cdt 에 되쓰도록.
    window->setProperty("filePath", templatePath);
    window->setWindowTitle(QFileInfo(templatePath).fileName());
    // 템플릿 편집으로 연 도면도 "템플릿으로 저장"이 가능하도록 표시.
    window->setProperty("isTemplateOrigin", true);
    if (window->undoStack())
        window->undoStack()->clear();

    if (window->view())
        QTimer::singleShot(100, window->view(), &MyGraphicsView::zoomAll);
    return true;
}
