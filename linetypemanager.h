#ifndef LINETYPEMANAGER_H
#define LINETYPEMANAGER_H

#include <QString>
#include <QMap>
#include <QList>
#include <QVector>
#include <QSize>
#include <QIcon>

struct LineType {
    QString name;
    QString pattern;

    LineType() {}
    LineType(const QString& n, const QString& p) : name(n), pattern(p) {}
};

class LineTypeManager
{
public:
    LineTypeManager();
    ~LineTypeManager();

    // 라인타입 표시 그룹 — 새 레이어 다이얼로그의 라디오 버튼과 매핑.
    //  AutoDraft     : Autodraft 고유 선종류 (BorderLine, DashLine, Continous ...)
    //  AcadStandard  : AutoCAD 표준 선종류    (BORDER, HIDDEN, CENTER2 ...)
    //  AcadIso       : AutoCAD ISO 선종류     (ACAD_ISO02W100 ...)
    enum class LinetypeGroup { AutoDraft, AcadStandard, AcadIso };

    // 이름 규칙으로 그룹을 판별한다(ACAD_ISO* → ISO, 전부 대문자 → Standard, 그 외 → AutoDraft).
    static LinetypeGroup groupOf(const QString& name);

    // 현재 UI 라인타입 그룹 — "새 레이어 만들기" 다이얼로그에서 라디오로 설정하며,
    // 레이어 dock 의 라인타입 변경 다이얼로그도 이 그룹의 선종류만 표시한다.
    // QSettings 로 세션 간 유지(기본값 AutoDraft).
    static LinetypeGroup currentGroup();
    static void setCurrentGroup(LinetypeGroup group);

    QString getDefaultFilePath() const;
    bool loadFromFile(const QString& filename);
    bool saveToFile(const QString& filename);

    bool addLinetype(const LineType& linetype);
    bool editLinetype(const QString& oldName, const LineType& newLinetype);
    bool removeLinetype(const QString& name);

    LineType getLinetype(const QString& name) const;
    QList<LineType> getAllLinetypes() const;

    // 특정 그룹에 속하는 선종류만 반환 (이름 오름차순).
    QList<LineType> getLinetypesInGroup(LinetypeGroup group) const;

    // 임의의 선종류명(대소문자 차이·"HIDDEN" 등 AutoCAD 표준명 포함)을 실제로 등록된
    // Autodraft 선종류명으로 정규화한다. 해당 없음/연속선이면 빈 문자열을 반환한다.
    // DXF/DWG 임포트 시 레이어 선종류명을 앱 내부 이름으로 맞추는 데 사용.
    QString resolveLinetypeName(const QString& name) const;

    // [신규] 라인타입 이름을 입력받아 Qt용 Dash 패턴으로 변환해 반환하는 함수
    QVector<qreal> getQtDashPattern(const QString& typeName) const;

    // 선종류 모양 미리보기 아이콘을 QPainter 로 그린다 (준비된 PNG 불필요).
    //  makePreviewIcon : 대시 패턴(getQtDashPattern 결과)을 직접 넘길 때
    //  previewIconFor  : 이름만으로 (내부에서 getQtDashPattern 호출)
    static QIcon makePreviewIcon(const QVector<qreal>& dashPattern,
                                 const QSize& size = QSize(32, 12));
    QIcon previewIconFor(const QString& typeName,
                         const QSize& size = QSize(32, 12)) const;

private:
    QMap<QString, LineType> m_linetypes;
};

#endif // LINETYPEMANAGER_H
