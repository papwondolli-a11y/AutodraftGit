#ifndef TEMPLATEMANAGER_H
#define TEMPLATEMANAGER_H

#include <QImage>
#include <QList>
#include <QString>

class IAppContext;

// 하나의 템플릿 파일에 대한 표시용 메타데이터
struct TemplateInfo {
    QString path;      // 절대 경로
    QString name;      // 확장자를 뺀 표시 이름
    QImage  thumbnail; // .cdb file_metadata 의 'preview' PNG (없으면 null)
};

// ─────────────────────────────────────────────────────────────────────────────
// TemplateManager
//   AutoCAD 의 acad.dwt 개념. 템플릿은 "확장자만 .cdt 인 일반 .cdb 도면"이며,
//   별도 포맷이 아니다. "새 도면"은 템플릿을 빈 무제 문서로 복사해 시작한다.
//   (레이어 / 치수 스타일 / 단위·축척 / 도곽 블록 등이 그대로 복제된다.)
//   템플릿 파일은 GD&T 프리셋과 같은 data 폴더(<exe>/../data)를 공유한다.
// ─────────────────────────────────────────────────────────────────────────────
class TemplateManager {
public:
    // 템플릿이 놓이는 폴더. GD&T 프리셋과 동일한 data 폴더를 쓴다.
    //   1) <exe>/data  2) <exe>/../data  3) 소스 루트/data
    //   아무 것도 없으면 <exe>/../data 를 만들어 반환한다.
    static QString templateDir();

    // templateDir 안의 *.cdt 목록 (썸네일 포함, 이름 오름차순)
    static QList<TemplateInfo> availableTemplates();

    // QNEW 대응: "새로만들기"가 사용할 기본 템플릿 경로.
    // 빈 문자열이거나 파일이 사라졌으면 빈 도면으로 동작한다.
    static QString defaultTemplatePath();
    static void    setDefaultTemplatePath(const QString &path);

    // [만들기/확인] 템플릿으로 새 무제 창을 만든다. 성공 시 true.
    //   templatePath 가 비었거나 존재하지 않으면 빈 도면으로 생성한다.
    //   생성된 창에는 filePath 속성을 설정하지 않으므로 첫 저장은 "다른 이름으로".
    //   asPrototype: "템플릿 보기 > 만들기/확인"으로 명시적으로 만든 경우에만
    //     true로 넘긴다. templatePath 유무와 무관하게 창 이름이 "Drawing N"
    //     대신 "원형도면"이 되고, "템플릿으로 저장"이 가능해진다.
    //     Ctrl+N(QNEW, 기본 템플릿 자동 적용) 경로는 항상 false로 호출해
    //     "Drawing N" 명명을 그대로 유지하고 템플릿 저장은 허용하지 않는다.
    static bool createWindowFromTemplate(IAppContext *ctx,
                                         const QString &templatePath,
                                         bool asPrototype = false);

    // [편집] 템플릿 파일 자체를 새 창에서 연다. 성공 시 true.
    //   filePath 속성을 그 .cdt 로 걸어 두므로 Ctrl+S 가 원본 템플릿에 되쓴다.
    //   설정값을 비우지 않고 파일에 저장된 그대로 복원한다.
    static bool openTemplateForEdit(IAppContext *ctx, const QString &templatePath);
};

#endif // TEMPLATEMANAGER_H
