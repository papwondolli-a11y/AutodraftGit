#ifndef DXFMANAGER_H
#define DXFMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <QSet>
#include <QTextStream>
#include <QPolygonF>
#include <QPainterPath>
#include <vector>
#include "dxfcommon.h"

class MyGraphicsScene;
class QTextStream;
class CadItem;
class EntityFactory;
class DxfDispatcher;
class QProgressDialog;

class DxfManager : public QObject {
    Q_OBJECT
public:
    explicit DxfManager(QObject* parent = nullptr);
    ~DxfManager();

    bool exportToDxf(const QString &fileName, MyGraphicsScene *scene);
    // progress: 널이 아니면 파싱 진행 중 라벨/값을 갱신하는 모달 진행 다이얼로그
    bool importFromDxf(const QString &fileName, MyGraphicsScene *scene,
                       QProgressDialog *progress = nullptr);

signals:
    void importFinished();
    void exportFinished();

private slots:
    void onImportCompleted();
    void onExportCompleted();

private:
    // ============================================================
    // [가져오기 진행 표시]
    //   importFromDxf() 실행 중에만 유효한 모달 진행 다이얼로그 포인터.
    //   reportProgress()가 라벨/값을 갱신하며, 모달 다이얼로그의
    //   setValue()가 내부적으로 processEvents()를 호출하므로
    //   파싱 도중에도 화면이 갱신된다.
    // ============================================================
    QProgressDialog* m_progressDlg = nullptr;
    int              m_progressCount = 0;   // 지금까지 읽은 엔티티 수(스로틀용)
    // phase가 비어있지 않으면 라벨을 교체하고, add만큼 카운트를 올린 뒤
    // 일정 간격마다 다이얼로그를 갱신한다.
    void reportProgress(const QString& phase, int add = 0);

    // ============================================================
    // [내부 구조체 1] TABLES 섹션 파싱 상태
    //   parseTablesSection() 내의 지역 변수들을 하나로 묶어
    //   finishTableRecord()에 한 번에 전달하기 위한 구조체.
    //   구 코드의 finishRecord 람다가 캡처하던 모든 변수를 대체.
    // ============================================================
    struct TableParseContext {
        QString currentTable;           // 현재 파싱 중인 테이블 종류 (LAYER/LTYPE/DIMSTYLE)

        // --- LAYER 임시 데이터 ---
        QString layerName;
        int     aciColor       = 7;
        double  weight         = 0.25;
        QString layerLinetype  = "Continuous";

        // --- LTYPE 임시 데이터 ---
        QString      ltName;
        QList<qreal> ltElements;        // 대시·도트 패턴 요소값 목록

        // --- DIMSTYLE 임시 데이터 ---
        QString dsName;
        int     dsTol      = 0, dsLim   = 0;
        int     dsDec      = 2, dsTdec  = 2;
        double  dsTp       = 0, dsTm    = 0;
        QString dsPost;
        int     dsZin      = 0, dsTzin  = 0;
        double  dsTextSize = 2.5;
        int     dsSe1      = 0, dsSe2   = 0; // DIMSE1(75)/DIMSE2(76): 치수선 억제 플래그
        int     dsSd1      = 0, dsSd2   = 0; // DIMSD1(281)/DIMSD2(282): 보조선 억제 플래그
        double  dsDimScale = 1.0;            // DIMSCALE (gc 40)
        QString dsFontName;                  // DIMTXSTY (gc 3 또는 340)
    };

    QMap<QString, QString> m_styleFontMap;   // 스타일 이름/핸들 -> 폰트 파일명 매핑

    // ============================================================
    // [내부 구조체 2] 엔티티 파싱 루프 공유 컨텍스트
    //   parseEntityList() 내의 지역 변수들을 참조로 묶어
    //   finalizeAndCreate()에 한 번에 전달하기 위한 구조체.
    //   구 코드의 finalizeAndCreate 람다가 캡처하던 모든 변수를 대체.
    // ============================================================
    struct EntityParseContext {
        DxfEntityData&   data;           // 현재 누적 중인 엔티티 필드값
        QList<CadItem*>& collectedItems; // 완성된 아이템 누적 목록
        EntityFactory&   factory;        // 아이템 생성 팩토리
        MyGraphicsScene* scene;          // 씬 (레이어 스타일 조회 등)
        QTextStream&     in;             // DXF 입력 스트림 (INSERT ATTRIB 파싱에 필요)
        int&             currentXDataApp;
        bool&            inDStyle;
        int&             currentDimVar;
        bool&            inAppData;      // AppData(102) 블록 내부 여부 플래그
        bool&            skipEntity;     // AUTODRAFT XDATA SKIP_IMPORT 마커가 있으면 엔티티 생성 건너뜀
    };

    // ============================================================
    // 메인 섹션 파싱 함수
    // ============================================================
    void parseHeaderSection(QTextStream& in, MyGraphicsScene* scene);
    void parseTablesSection(QTextStream& in, MyGraphicsScene* scene);
    void parseBlocksSection(QTextStream& in, MyGraphicsScene* scene);
    void parseEntitiesSection(QTextStream& in, MyGraphicsScene* scene);

    // 블록·엔티티 공통 파싱 루프
    QList<CadItem*> parseEntityList(QTextStream& in, MyGraphicsScene* scene,
                                    const QStringList& stopValues,
                                    const QString& initialType = "");

    // ============================================================
    // [구 람다 → 멤버 함수] TABLES 레코드 완료 처리
    //   ctx.currentTable에 따라 레이어·선종류·치수스타일을 씬에 등록.
    //   구 코드의 finishRecord 람다를 대체.
    // ============================================================
    void finishTableRecord(TableParseContext& ctx, MyGraphicsScene* scene);

    // ============================================================
    // [구 람다 → 멤버 함수] 엔티티 완료 처리
    //   ctx.data.type에 따라 CadItem을 생성하고 collectedItems에 추가한 뒤
    //   data를 초기화. 구 코드의 finalizeAndCreate 람다를 대체.
    // ============================================================
    void finalizeAndCreate(EntityParseContext& ctx);

    // ============================================================
    // [구 람다 → 멤버 함수] Hatch 경계 경로 빌더
    // ============================================================
public:
    // 엣지 기반(type 1~4) 해치 루프 → QPainterPath 변환.
    //   구 코드의 buildHatchEdgePath 람다를 대체.
    static QPainterPath buildHatchEdgePath(const QList<DxfEntityData::HatchEdge>& edges);

    // 폴리라인 기반 해치 루프 → QPainterPath 변환 (Bulge 완벽 처리).
    //   구 코드의 buildPolylinePath 람다를 대체.
    static QPainterPath buildPolylinePath(const QPolygonF& loop,
                                          const QList<double>& bulges);
private:
    // HatchItem 인스턴스 생성 후 collectedItems에 추가.
    //   구 코드의 createHatchItem 람다를 대체.
    void createHatchItem(QPainterPath&        hPath,
                         const DxfEntityData& data,
                         QList<CadItem*>&     collectedItems,
                         MyGraphicsScene*     scene);

    // ============================================================
    // [구 중첩 람다 → 정적 멤버 함수] 수학 헬퍼
    //   인스턴스 상태가 전혀 필요 없으므로 static으로 선언.
    // ============================================================
    // NURBS 유리 B-스플라인 곡선 위의 샘플 점 계산.
    //   구 코드의 evalBSpline 중첩 람다를 대체.
    static QList<QPointF> evalBSpline(const QList<QPointF>& cps,
                                      const QList<double>&  weights,
                                      int                   deg,
                                      int                   nPts,
                                      const QList<double>&  knots);

    // 타원 호 위 파라메트릭 점 계산 (t: 라디안).
    //   구 코드의 evalEll 중첩 람다를 대체.
    static QPointF evalEllipsePoint(double  cx,
                                    double  cy,
                                    QPointF majorAxis,
                                    double  ratio,
                                    double  t);

    // ============================================================
    // 후처리 / 공통 도우미 (기존 유지)
    // ============================================================
public:
    static void postProcessEntities(QList<CadItem*>& entities, MyGraphicsScene* scene);
    static void decomposeLwPolyline(const DxfEntityData& data,
                                    QList<CadItem*>& list,
                                    EntityFactory& factory,
                                    MyGraphicsScene* scene);
private:
    static void applyLayerStyle(CadItem* item, const QString& layer,
                                MyGraphicsScene* scene);
    CadItem* createEntityFromData(const DxfEntityData& data,
                                  EntityFactory& factory,
                                  MyGraphicsScene* scene);
    void     processInsertEntity(const DxfEntityData& data,
                                 QList<CadItem*>& list,
                                 EntityFactory& factory,
                                 MyGraphicsScene* scene);

    // ============================================================
    // DXF 내보내기
    // ============================================================
    void writeHeaderSection(QTextStream& out, MyGraphicsScene* scene);
    void writeClassesSection(QTextStream& out);
    void writeTablesSection(QTextStream& out, MyGraphicsScene* scene);
    void writeBlocksSection(QTextStream& out, MyGraphicsScene* scene);
    void writeEntitiesSection(QTextStream& out, MyGraphicsScene* scene);
    void writeObjectsSection(QTextStream& out);
    void writeItemToDxf(QTextStream& out, CadItem* item);

    // 핸들러가 공용으로 호출하는 AC1015 엔티티 공통 헤더 작성
    // (0=type, 5=handle, 100 AcDbEntity, 8=layer 순서로 출력)
public:
    void writeEntityAcDbHeader(QTextStream& out,
                               const QString& entityType,
                               const QString& layerName);

    // 치수 내보내기 시 고유 익명 블록 이름(*D1, *D2 …)을 순서대로 반환한다.
    // TABLES/BLOCKS 섹션과 ENTITIES 섹션이 같은 이름을 사용하도록 카운터를 공유한다.
    QString allocateDimBlockName();

    // 핸들러가 핸들을 미리 할당해야 할 때 사용 (LEADER↔TOLERANCE 교차 참조 등)
    QString allocateHandle();

    // LEADER gc330 소유자 핸들 — 엔티티 헤더를 수동으로 작성할 때 사용
    QString modelSpaceHandle() const { return m_modelSpaceHandle; }
    QString datumArrowHandle() const { return m_datumArrowHandle; }

private:

    // QColor → ACI 색상 인덱스 역변환 (writeTablesSection에서 사용)
    static int colorToAci(const QColor& color);

    // Group Code 쓰기 인라인 헬퍼
    inline void writeGroupCode(QTextStream& out, int code, const QString& value) {
        out << code << "\n" << value << "\n";
    }
    inline void writeGroupCode(QTextStream& out, int code, double value) {
        out << code << "\n" << value << "\n";
    }

    QColor aciToColor(int aci, MyGraphicsScene* scene);
    QString getNextHandle();


    // ============================================================
    // 멤버 변수
    // ============================================================
    QMap<QString, QList<CadItem*>> m_blockDefinitions;      // 블록 이름 → CadItem 목록
    QMap<QString, DxfDimStyle>     m_dimStyles;             // 치수 스타일 이름(대문자) → 스타일
    double                         m_importUnitFactor = 1.0; // [Phase 8] inch DXF 임포트 시 25.4 (mm 환산)
    DxfDispatcher*                 m_dispatcher;            // 엔티티 디스패처
    int                            m_nextHandle;            // DXF 내보내기용 핸들 카운터
    QString                        m_normalPlotStyleHandle; // "Normal" 플롯 스타일 핸들 (LAYER→390 참조)
    QSet<QString>                  m_referencedBlockNames;  // export 전 수집된 사용자 블록 이름 집합
    QString                        m_modelSpaceHandle;      // *Model_Space BLOCK_RECORD 핸들 (엔티티 gc330 참조)
    int                            m_dimBlockCount;         // export 전 집계한 치수 엔티티 수
    int                            m_dimBlockAllocIdx;      // 치수 블록 할당 카운터 (1부터 시작)
    // 블록 이름 → BLOCK_RECORD 핸들 매핑
    // writeTablesSection(BLOCK_RECORD 섹션)에서 핸들을 할당하면서 채우고,
    // writeBlocksSection에서 BLOCK/ENDBLK 의 gc330 소유자 핸들로 참조한다.
    // gc330 이 없으면 AutoCAD 가 *D 블록 정의를 BLOCK_RECORD 와 연결하지 못해
    // DIMENSION 엔티티의 geometry 재생성이 실패한다.
    QMap<QString, QString>         m_blockRecordHandles;   // 블록 이름 → BLOCK_RECORD 핸들
    QString                        m_datumArrowHandle;     // 데이텀 전용 화살촉(_DATUM) 블록 핸들
};

#endif // DXFMANAGER_H
