#include "helpdialog.h"

#include <QFile>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QSplitter>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QHeaderView>
#include <QVector>

namespace {

struct HelpTopic {
    const char *title;
    const char *anchor;
};

struct HelpChapter {
    const char *title;
    const char *file;
    std::initializer_list<HelpTopic> topics;
};

// 실제 메뉴바/탭바 명령어 체계(appcommandregistry.cpp)를 그대로 반영한 목차.
// 각 anchor는 help/ko/*.html 안의 id와 1:1로 대응한다.
const HelpChapter kChapters[] = {
    {"시작하기", "00_start.html", {
        {"화면 구성", "start.layout"},
        {"타이틀바","titlebar.exp" },
        {"데시보드","dashboard.exp" },
        {"도크창","dock.exp" },
        {"상태줄","statur.exp" },
        {"공통 동작 (실행취소/다시실행)", "start.common"},
        {"그립/크로스헤어","gsnap.exp"},
    }},
    {"파일", "01_file.html", {
        {"새로만들기", "file.new"},
        {"템플릿 보기...", "file.newfromtemplate"},
        {"템플릿으로 저장...", "file.saveastemplate"},
        {"열기", "file.open"},
        {"저장하기", "file.save"},
        {"다른이름 저장하기", "file.saveas"},
        {"일괄 출력...", "file.batchprint"},
        {"출력 스타일 편집...", "file.printstyle"},
        {"DXF 가져오기...", "file.dxfimport"},
        {"DXF 내보내기...", "file.dxfexport"},
        {"DWG 가져오기...", "file.dwgimport"},
        {"끝내기", "file.exit"},
    }},
    {"환경설정", "02_settings.html", {
        {"도면설정", "menu.settings.drawingsettings"},
        {"치수 설정", "menu.settings.dimstyle"},
        {"언어 / Language", "settings.language"},
        {"화면 요소 표시", "settings.viewToggles"},
        {"테마", "settings.theme"},
    }},
    {"그리기", "03_draw.html", {
        {"라인", "draw.line"},
        {"원", "draw.circle"},
        {"호", "draw.arc"},
        {"3점 호", "draw.arc3point"},
        {"폴리라인", "draw.polyline"},
        {"다각형", "draw.polygon"},
        {"해칭", "draw.hatch"},
        {"스플라인", "draw.spline"},
        {"멀티중심선", "draw.gridcenter"},
        {"평행중심", "draw.parallelcenter"},
        {"박스 중심선", "draw.boxcenter"},
        {"원 중심", "draw.circlecenter"},
        {"주열번호", "draw.gridbubble"},
        {"거칠기", "draw.surfacefinish"},
        {"구멍 R", "draw.hole"},
        {"구멍 P", "draw.holep"},
        {"구멍 단면", "draw.holesection"},
    }},
    {"편집하기", "04_modify.html", {
        {"이동", "modify.move"},
        {"복사", "modify.copy"},
        {"거울상", "modify.mirror"},
        {"4거울상", "modify.quadmirror"},
        {"회전", "modify.rotate"},
        {"확대/축소", "modify.scale"},
        {"지우기", "modify.erase"},
        {"옵셋", "modify.offset"},
        {"자르기", "modify.trim"},
        {"연장", "modify.extend"},
        {"둥근모따기", "modify.fillet"},
        {"직각모따기", "modify.chamfer"},
        {"배열하기", "modify.array"},
        {"늘리기", "modify.stretch"},
        {"끊기", "modify.sever"},
        {"연결하기", "modify.join"},
        {"분해하기", "modify.explode"},
        {"중복선제거", "modify.overkill"},
        {"상세도", "modify.detailview"},
    }},
    {"화면보기", "05_view.html", {
        {"도면틀", "view.limits"},
        {"모두보기", "view.zoomall"},
        {"맞게보기", "view.zoomextend"},
        {"상자보기", "view.zoomwindow"},
        {"마우스 휠", "view.wheel"},
    }},
    {"치수선 · 기하공차", "07_dimension.html", {
        {"스타일", "draw.dimstyle"},
        {"수평/수직", "draw.dimlinear_hv"},
        {"정렬치수", "draw.linearDim"},
        {"원,호 치수", "draw.radDim"},
        {"각도(P)", "draw.angDim"},
        {"각도(E)", "draw.dimangular"},
        {"기준점", "draw.dimbaseline"},
        {"연결치수", "draw.dimcontinue"},
        {"연속치수", "draw.autocontinuousdim"},
        {"지시선", "draw.leader"},
        {"구멍 콜아웃", "draw.holecallout"},
        {"대칭치수", "draw.halfdim"},
        {"영역치수", "draw.areadim"},
        {"치수지시선", "draw.dimlea"},
        {"치수이동", "draw.dimMoveText"},
        {"치수편집", "draw.dimTextEdit"},
        {"기하공차", "draw.gdt"},
        {"데이텀", "draw.datum"},
        {"기하편집", "draw.gdtedittool"},
    }},
    {"글자쓰기", "06_text.html", {
       {"여러줄", "text.text"},
       {"한줄", "text.dtext"},
       {"박스글자", "text.boxtext"},
     }},
    {"다중선(벽체)", "08_multiline.html", {
        {"2-Line", "multiline.twoline"},
        {"4-Line", "multiline.fourline"},
        {"6-Line", "multiline.sixline"},
        {"L-코너(R)", "multiline.raycast_lcorner"},
        {"T-코너(R)", "multiline.raycast_tcorner"},
        {"C-코너", "multiline.ccorner"},
        {"편심2-Line", "multiline.eccentrictwoline"},
        {"편심4-Line", "multiline.eccentricfourline"},
        {"편심6-Line", "multiline.eccentricsixline"},
    }},
    {"블록", "09_block.html", {
        {"만들기", "block.wblock"},
        {"불러오기", "block.insert"},
        {"가구", "block.lib.furniture"},
        {"싱크대", "block.lib.sink"},
        {"화장실", "block.lib.toilet"},
        {"나무", "block.lib.tree"},
    }},
    {"평면 도어 · 창문", "10_plan.html", {
        {"외여닫이(F)", "plandoor.type1"},
        {"쌍여닫이(F)", "plandoor.double"},
        {"양방여닫이(F)", "plandoor.biswing"},
        {"양방쌍여닫이(F)", "plandoor.doublebiswing"},
        {"회전문(F)", "plandoor.revolving"},
        {"단순문(F)", "plandoor.simple"},
        {"단순쌍여닫이(F)", "plandoor.simpledouble"},
        {"단순양방여닫이(F)", "plandoor.simplebiswing"},
        {"단순양방쌍여닫이(F)", "plandoor.simpledoublebiswing"},
        {"미서기창(DF)", "planwindow.type1"},
        {"미서기창 3짝(DF)", "planwindow.type3"},
        {"미서기창 4짝(DF)", "planwindow.type4"},
        {"미서기창 상단부(DF)", "planwindow.top"},
        {"미서기창 3짝 상단부(DF)", "planwindow.top3"},
        {"미서기창 4짝 상단부(DF)", "planwindow.top4"},
        {"미서기창 상단부 연결(DF)", "planwindow.toplink"},
    }},
    {"기계부품", "11_mech.html", {
        {"볼트", "mech.bolt"},
        {"렌치볼트", "mech.wrenchbolt"},
        {"접시머리렌치볼트", "mech.hhbolt"},
        {"둥근머리렌치볼트", "mech.hbbolt"},
        {"준비 중인 항목", "mech.notimplemented"},
    }},
};

QString loadResourceHtml(const QString &qrcUrl)
{
    QString path = qrcUrl;
    path.replace(QStringLiteral("qrc:"), QStringLiteral(":"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString wrapFragment(const QString &innerHtml, bool isListItem)
{
    const QString body = isListItem ? (QStringLiteral("<ul>") + innerHtml + QStringLiteral("</ul>")) : innerHtml;
    return QStringLiteral(
        "<html><body style=\"font-family:'Malgun Gothic','Segoe UI',sans-serif; "
        "font-size:14px; line-height:1.5;\">%1</body></html>").arg(body);
}

// html 문서에서 id=anchor를 가진 헤딩(h1~h4) 또는 li 태그를 찾아,
// 다음에 나오는 같은 레벨(또는 그 이하 깊이)의 헤딩 직전까지를 하나의
// "섹션"으로 잘라서 돌려준다. 목차의 세부 항목을 클릭했을 때 그 항목의
// 설명만 보여주기 위한 용도.
QString extractSection(const QString &html, const QString &anchor)
{
    static const QRegularExpression headingRe(
        QStringLiteral(R"re(<(h[1-4]|li)\b[^>]*\bid="([^"]+)"[^>]*>)re"),
        QRegularExpression::CaseInsensitiveOption);

    struct Heading { QString tag; QString id; int start; };
    QVector<Heading> headings;

    QRegularExpressionMatchIterator it = headingRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        headings.append({m.captured(1).toLower(), m.captured(2), static_cast<int>(m.capturedStart(0))});
    }

    int targetIndex = -1;
    for (int i = 0; i < headings.size(); ++i) {
        if (headings[i].id == anchor) {
            targetIndex = i;
            break;
        }
    }
    if (targetIndex < 0)
        return QString();

    auto levelOf = [](const QString &tag) {
        return tag == QLatin1String("li") ? 5 : (tag.length() >= 2 ? tag.at(1).digitValue() : 0);
    };

    const int level = levelOf(headings[targetIndex].tag);
    const int start = headings[targetIndex].start;

    int end = html.length();
    const int bodyEnd = html.indexOf(QStringLiteral("</body>"), start, Qt::CaseInsensitive);
    if (bodyEnd >= 0)
        end = bodyEnd;

    for (int i = targetIndex + 1; i < headings.size(); ++i) {
        if (levelOf(headings[i].tag) <= level) {
            end = headings[i].start;
            break;
        }
    }

    const QString inner = html.mid(start, end - start).trimmed();
    return wrapFragment(inner, headings[targetIndex].tag == QLatin1String("li"));
}

} // namespace

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("사용자 매뉴얼"));
    resize(1000, 640);

    m_tocTree = new QTreeWidget(this);
    m_tocTree->setHeaderHidden(true);
    m_tocTree->setColumnCount(1);

    m_contentBrowser = new QTextBrowser(this);
    m_contentBrowser->setOpenExternalLinks(true);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_tocTree);
    splitter->addWidget(m_contentBrowser);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 710});

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(splitter);

    connect(m_tocTree, &QTreeWidget::itemClicked, this, &HelpDialog::onTocItemClicked);

    buildToc();

    // 첫 화면으로 "시작하기" 챕터를 바로 보여준다.
    m_contentBrowser->setSource(QUrl(QStringLiteral("qrc:/help/ko/00_start.html")));
}

void HelpDialog::buildToc()
{
    for (const HelpChapter &chapter : kChapters) {
        auto *chapterItem = new QTreeWidgetItem(m_tocTree, {QString::fromUtf8(chapter.title)});
        const QString chapterUrl = QStringLiteral("qrc:/help/ko/%1").arg(QString::fromUtf8(chapter.file));
        chapterItem->setData(0, Qt::UserRole, chapterUrl);

        for (const HelpTopic &topic : chapter.topics) {
            auto *topicItem = new QTreeWidgetItem(chapterItem, {QString::fromUtf8(topic.title)});
            const QString topicUrl = chapterUrl + QStringLiteral("#") + QString::fromUtf8(topic.anchor);
            topicItem->setData(0, Qt::UserRole, topicUrl);
        }
    }
    m_tocTree->expandAll();
}

void HelpDialog::onTocItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;

    const QString target = item->data(0, Qt::UserRole).toString();
    if (target.isEmpty())
        return;

    const int hashIdx = target.indexOf(QLatin1Char('#'));
    if (hashIdx < 0) {
        // 챕터(최상위 목차) 클릭: 해당 챕터 파일 전체를 보여준다.
        m_contentBrowser->setSource(QUrl(target));
        return;
    }

    // 세부 항목 클릭: 그 항목의 설명 구간만 잘라서 보여준다.
    const QString fileUrl = target.left(hashIdx);
    const QString anchor = target.mid(hashIdx + 1);
    const QString html = loadResourceHtml(fileUrl);
    const QString section = html.isEmpty() ? QString() : extractSection(html, anchor);

    if (section.isEmpty())
        m_contentBrowser->setSource(QUrl(target)); // 파싱 실패 시 기존 방식으로 대체
    else
        m_contentBrowser->setHtml(section);
}
