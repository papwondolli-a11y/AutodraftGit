#include "dxfmanager.h"
#include "dxfdispatcher.h"
#include "mygraphicsscene.h"
#include "entityfactory.h"
#include "caditem.h"
//#include "lineitem.h"
//#include "circleitem.h"
//#include "arcitem.h"
//#include "polylineitem.h"
#include "textitem.h"
#include "hatchitem.h"
//#include "splineitem.h"
//#include "myspline.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "layer.h"
#include "iappcontext.h"
#include "cadutils.h"
//#include "dxfdimensionparser.h"
#include "gdtitem.h"
#include "leaderitem.h"
#include "blockitem.h"
#include "datumitem.h"
#include "basedimensionitem.h"
#include "mymath.h"
#include "multilinedata.h"
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <QProgressDialog>
#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#include <QGraphicsItem>
#include <QList>
#include <QDebug>
#include <QPen>
#include <QPointF>
#include <QLineF>
#include <QFont>
#include <cmath>
#include <climits>

// AutoDraft 기본 레이어명 "Default" → DXF 필수 레이어명 "0" 변환
static QString toDxfLayerName(const QString& name) {
    return (name.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0)
           ? QStringLiteral("0") : name;
}

// DXF 필수 레이어명 "0" → AutoDraft 기본 레이어명 "Default" 복원
static QString fromDxfLayerName(const QString& name) {
    return (name == QStringLiteral("0")) ? QStringLiteral("Default") : name;
}


DxfManager::DxfManager(QObject* parent) : QObject(parent), m_nextHandle(0x1000) {
    m_dispatcher = new DxfDispatcher();
    // 생성자: 기본 임포트/익스포트 완료 signal-slot 연결
    connect(this, &DxfManager::importFinished, this, &DxfManager::onImportCompleted);
    connect(this, &DxfManager::exportFinished, this, &DxfManager::onExportCompleted);
}

DxfManager::~DxfManager() {
    // 소멸자: 블록 정의 내의 모든 아이템 삭제
    for (const QList<CadItem*>& items : std::as_const(m_blockDefinitions)) {
        for (CadItem* item : items) {
            delete item;
        }
    }
    m_blockDefinitions.clear();
    delete m_dispatcher;
}

// 가져오기 진행 다이얼로그 갱신.
// - phase 가 비어있지 않으면 라벨을 교체한다(단계 전환).
// - add 만큼 엔티티 카운터를 올리고, 스로틀 간격마다 값을 갱신한다.
// 모달 QProgressDialog::setValue()는 내부적으로 processEvents()를 호출하므로
// 이 함수만으로 파싱 도중에도 화면이 살아있게 된다.
void DxfManager::reportProgress(const QString& phase, int add) {
    if (!m_progressDlg) return;

    m_progressCount += add;

    if (!phase.isEmpty()) {
        m_progressDlg->setLabelText(phase);
        m_progressDlg->setValue(m_progressDlg->value() + 1);
        return;
    }

    // 엔티티마다 다이얼로그를 갱신하면 느려지므로 16개 간격으로만 갱신
    if ((m_progressCount & 0x0F) == 0) {
        m_progressDlg->setLabelText(
            tr("도형 데이터를 읽는 중입니다... (%1개)").arg(m_progressCount));
        m_progressDlg->setValue(m_progressDlg->value() + 1);
    }
}

// R2007 미만 DXF의 $DWGCODEPAGE 에서 Windows 코드페이지 번호를 얻는다.
// "ANSI_949" → 949, "KOREAN" → 949. 알 수 없거나 없으면 0(시스템 ANSI 코드페이지 사용).
static int dxfLegacyCodePage(const QByteArray& bytes)
{
    const QByteArray head = bytes.left(65536);
    const int p = head.indexOf("$DWGCODEPAGE");
    if (p < 0) return 0;
    const QList<QByteArray> lines = head.mid(p, 128).split('\n');
    if (lines.size() < 3) return 0;
    const QByteArray name = lines.at(2).trimmed().toUpper();
    if (name.startsWith("ANSI_")) return name.mid(5).toInt();
    if (name == "KOREAN") return 949;
    return 0;
}

// DXF 파일 전체를 문자열로 읽는다.
// - UTF-8로 올바르게 해석되는 파일(AC1021 이상, ASCII 전용 포함)은 UTF-8로 읽는다.
// - 그렇지 않으면 R2007 미만 DXF로 보고 $DWGCODEPAGE 의 ANSI 코드페이지로 읽는다.
//   (한글 Windows에서 저장한 R2000 DXF의 한글·지름 기호(Ø)가 깨지는 문제 방지)
static bool readDxfFileText(const QString& fileName, QString* text)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = file.readAll();
    file.close();

    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);

    if (bytes.isValidUtf8()) {
        *text = QString::fromUtf8(bytes);
        return true;
    }

#ifdef Q_OS_WIN
    UINT cp = static_cast<UINT>(dxfLegacyCodePage(bytes));
    if (cp == 0) cp = GetACP();
    const int len = MultiByteToWideChar(cp, 0, bytes.constData(), bytes.size(), nullptr, 0);
    if (len > 0) {
        QString decoded(len, QChar());
        MultiByteToWideChar(cp, 0, bytes.constData(), bytes.size(),
                            reinterpret_cast<wchar_t*>(decoded.data()), len);
        *text = decoded;
        return true;
    }
#endif
    *text = QString::fromLocal8Bit(bytes);
    return true;
}

bool DxfManager::importFromDxf(const QString& fileName, MyGraphicsScene* scene,
                               QProgressDialog* progress) {
    if (!scene) return false;

    // 진행 다이얼로그는 이 함수 실행 동안에만 유효
    m_progressDlg = progress;
    m_progressCount = 0;
    reportProgress(tr("DXF 파일을 여는 중입니다..."));

    // 블록 정의 초기화 (기존 데이터 삭제)
    for (const QList<CadItem*>& items : std::as_const(m_blockDefinitions)) {
        for (CadItem* item : items) {
            delete item;
        }
    }
    m_blockDefinitions.clear();
    m_dimStyles.clear();
    m_importUnitFactor = 1.0; // [Phase 8] 임포트마다 초기화 (parseHeaderSection에서 재설정)

    // [Pass 1] HEADER, TABLES, BLOCKS 섹션 파싱
    reportProgress(tr("레이어·블록 정보를 읽는 중입니다..."));
    {
        QString text;
        if (!readDxfFileText(fileName, &text)) { m_progressDlg = nullptr; return false; }
        QTextStream in(&text);
        QString line;
        while (!in.atEnd()) {
            line = in.readLine().trimmed();
            QString value = in.readLine().trimmed();
            if (line == "0" && value == "SECTION") {
                QString nextCode = in.readLine().trimmed();
                QString secName = in.readLine().trimmed();
                if (nextCode == "2" && secName == "HEADER") {
                    parseHeaderSection(in, scene);
                } else if (nextCode == "2" && secName == "TABLES") {
                    parseTablesSection(in, scene);
                } else if (nextCode == "2" && secName == "BLOCKS") {
                    parseBlocksSection(in, scene);
                }
            }
        }
    }

    // [Pass 2] ENTITIES 섹션 파싱
    reportProgress(tr("도형 데이터를 읽는 중입니다..."));
    {
        QString text;
        if (!readDxfFileText(fileName, &text)) { m_progressDlg = nullptr; return false; }
        QTextStream in(&text);
        QString line;
        while (!in.atEnd()) {
            line = in.readLine().trimmed();
            QString value = in.readLine().trimmed();
            if (line == "0" && value == "SECTION") {
                QString nextCode = in.readLine().trimmed();
                QString secName = in.readLine().trimmed();
                if (nextCode == "2" && secName == "ENTITIES") {
                    parseEntitiesSection(in, scene);
                }
            }
        }
    }

    reportProgress(tr("화면에 배치하는 중입니다..."));
    emit importFinished();
    m_progressDlg = nullptr;
    return true;
}

bool DxfManager::exportToDxf(const QString& fileName, MyGraphicsScene* scene) {
    if (!scene) return false;
    m_nextHandle = 0x1000; // 내보내기마다 핸들 카운터 초기화

    // 블록 이름 → BLOCK_RECORD 핸들 매핑을 초기화한다.
    // writeTablesSection(BLOCK_RECORD 섹션)에서 채워지고,
    // writeBlocksSection에서 BLOCK/ENDBLK 의 gc330으로 참조된다.
    m_blockRecordHandles.clear();

    // OBJECTS 섹션에서 정의할 "Normal" 플롯 스타일 핸들을 미리 할당한다.
    m_normalPlotStyleHandle = getNextHandle();

    // TABLES(BLOCK_RECORD)와 BLOCKS 섹션이 동일한 블록 이름 집합을 참조하도록
    // 섹션 작성 전에 사용자 블록 이름을 미리 수집한다.
    // 수집 대상: m_blockDefinitions + 씬의 BlockItem.blockName()
    m_referencedBlockNames.clear();
    {
        QMapIterator<QString, QList<CadItem*>> it(m_blockDefinitions);
        while (it.hasNext()) {
            it.next();
            if (!it.key().startsWith('*')) m_referencedBlockNames.insert(it.key());
        }
        QList<QGraphicsItem*> allItems = scene->items();
        for (int i = 0; i < allItems.size(); ++i) {
            BlockItem* blk = dynamic_cast<BlockItem*>(allItems[i]);
            if (blk && !blk->blockName().isEmpty() && !blk->blockName().startsWith('*'))
                m_referencedBlockNames.insert(blk->blockName());
        }
    }

    // 씬의 치수 엔티티 수를 사전 집계한다.
    // TABLES/BLOCKS 섹션에서 *D1…*Dn 고유 블록을 미리 선언해야 하기 때문이다.
    m_dimBlockCount = 0;
    m_dimBlockAllocIdx = 0; 
    {
        QList<QGraphicsItem*> allItems = scene->items();
        // 1) 최상위 엔티티 섹션에 직접 기록될 치수들
        for (int i = 0; i < allItems.size(); ++i) {
            CadItem* item = dynamic_cast<CadItem*>(allItems[i]);
            if (dynamic_cast<BaseDimensionItem*>(item)) {
                if (!dynamic_cast<CadItem*>(item->parentItem())) {
                    ++m_dimBlockCount;
                }
            }
        }
        // 2) 각 블록 정의 내부에 포함되어 BLOCKS 섹션에서 기록될 치수들
        QSetIterator<QString> sit(m_referencedBlockNames);
        while (sit.hasNext()) {
            const QString bName = sit.next();
            QList<CadItem*> blockItems;
            if (m_blockDefinitions.contains(bName)) {
                blockItems = m_blockDefinitions[bName];
            } else {
                for (int i = 0; i < allItems.size(); ++i) {
                    BlockItem* blk = dynamic_cast<BlockItem*>(allItems[i]);
                    if (blk && blk->blockName() == bName) {
                        QList<QGraphicsItem*> children = blk->childItems();
                        for (int j = 0; j < children.size(); ++j) {
                            CadItem* c = dynamic_cast<CadItem*>(children[j]);
                            if (c) blockItems.append(c);
                        }
                        break;
                    }
                }
            }
            for (int i = 0; i < blockItems.size(); ++i) {
                if (dynamic_cast<BaseDimensionItem*>(blockItems[i]))
                    ++m_dimBlockCount;
            }
        }
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    // AC1015 DXF는 UTF-8이 아닌 시스템 ANSI 코드페이지로 저장해야 한다.
    // 한국어 Windows에서는 CP949(ANSI_949)가 시스템 코드페이지이다.
    out.setEncoding(QStringConverter::System);
    out.setRealNumberPrecision(6);

    // DXF 섹션 작성 순서: HEADER → CLASSES → TABLES → BLOCKS → ENTITIES → OBJECTS → EOF
    writeHeaderSection(out, scene);
    writeClassesSection(out);
    writeTablesSection(out, scene);
    writeBlocksSection(out, scene);      // 블록 정의 보존 (씬 스캔 포함)
    writeEntitiesSection(out, scene);
    writeObjectsSection(out);

    writeGroupCode(out, 0, "EOF");

    file.close();
    emit exportFinished();
    return true;
}

// ============================================================
// HEADER 섹션 파싱 — $DIMSCALE 값을 씬의 치수 스타일에 적용
// ============================================================
void DxfManager::parseHeaderSection(QTextStream& in, MyGraphicsScene* scene)
{
    QString lastVar;
    int insUnits = -1;     // [Phase 8] $INSUNITS (1 = Inches, 4 = Millimeters …)
    int measurement = -1;  // [Phase 8] $MEASUREMENT (0 = English, 1 = Metric)
    while (!in.atEnd()) {
        QString code  = in.readLine().trimmed();
        QString value = in.readLine().trimmed();
        if (code == "0" && value == "ENDSEC") break;
        if (code == "9") {
            lastVar = value;          // 변수 이름 ($DIMSCALE 등)
        } else if (lastVar == "$DIMSCALE" && code == "40") {
            bool ok = false;
            double scale = value.toDouble(&ok);
            if (ok && scale > 0.0 && !qFuzzyCompare(scale, 1.0)) {
                DimensionStyle ds = scene->dimensionStyle();
                ds.dimScale = scale;
                scene->setDimensionStyle(ds);
            }
            lastVar.clear();
        } else if (lastVar == "$INSUNITS" && code == "70") {
            insUnits = value.toInt();
            lastVar.clear();
        } else if (lastVar == "$MEASUREMENT" && code == "70") {
            measurement = value.toInt();
            lastVar.clear();
        }
    }

    // [Phase 8] 인치 도면 판정 → 좌표를 mm 모델 좌표계로 올리기 위한 배율 설정 +
    //   씬을 inch 표시 모드로 전환한다. $INSUNITS 가 우선, 없으면 $MEASUREMENT 로 폴백.
    //   (피트·야드 등 인치 외 임페리얼 단위는 이 앱에서 미지원 → mm 로 둔다.)
    const bool imperial = (insUnits == 1) || (insUnits <= 0 && measurement == 0);
    if (imperial) {
        m_importUnitFactor = 25.4;
        if (scene) scene->setLinearUnit(LinearUnit::Inch);
    }
}

void DxfManager::parseTablesSection(QTextStream& in, MyGraphicsScene* scene)
{
    if (!scene || !scene->context() || !scene->context()->layerManager()) return;

    QString code, value;

    // 모든 파싱 상태를 구조체 하나로 통합 (구 코드의 지역 변수 20여 개를 대체)
    TableParseContext ctx;

    while (!in.atEnd()) {
        code  = in.readLine().trimmed();
        value = in.readLine().trimmed();

        if (code == "0") {
            // 레코드 완료: 구 finishRecord 람다 → 멤버 함수 호출
            finishTableRecord(ctx, scene);

            if (value == "ENDTAB" || value == "ENDSEC") {
                ctx.currentTable = "";
                if (value == "ENDSEC") break;
                continue;
            }

            if (value == "TABLE") {
                in.readLine(); // 그룹 코드 "2" 스킵
                ctx.currentTable = in.readLine().trimmed();
                continue;
            }

            // 테이블 항목 시작: 해당 임시 변수 초기화
            if (ctx.currentTable == "LAYER" && value == "LAYER") {
                ctx.layerName     = "";
                ctx.aciColor      = 7;
                ctx.weight        = 0.25;
                ctx.layerLinetype = "Continuous";
            } else if (ctx.currentTable == "LTYPE" && value == "LTYPE") {
                ctx.ltName = "";
                ctx.ltElements.clear();
            } else if (ctx.currentTable == "DIMSTYLE" && value == "DIMSTYLE") {
                ctx.dsName     = "";
                ctx.dsTol      = 0;  ctx.dsLim   = 0;
                ctx.dsDec      = 2;  ctx.dsTdec  = 2;
                ctx.dsTp       = 0;  ctx.dsTm    = 0;
                ctx.dsPost     = "";
                ctx.dsZin      = 0;  ctx.dsTzin  = 0;
                ctx.dsTextSize = 2.5;
                ctx.dsSe1      = 0;  ctx.dsSe2   = 0;
                ctx.dsSd1      = 0;  ctx.dsSd2   = 0;
                ctx.dsDimScale = 1.0;
                ctx.dsFontName = "";
            } else if (ctx.currentTable == "STYLE" && value == "STYLE") {
                ctx.dsName     = ""; // 스타일 이름 재사용
                ctx.dsFontName = ""; // 폰트 이름 재사용
            }
            continue;
        }

        // 각 테이블 종류별 필드값 파싱
        if (ctx.currentTable == "LAYER") {
            if      (code == "2")   ctx.layerName     = fromDxfLayerName(value);
            else if (code == "62")  ctx.aciColor       = qAbs(value.toInt());
            else if (code == "6")   ctx.layerLinetype  = value;
            else if (code == "370") {
                int lw = value.toInt();
                if (lw > 0) ctx.weight = lw / 100.0;
            }
        } else if (ctx.currentTable == "LTYPE") {
            if      (code == "2")  ctx.ltName = value;
            else if (code == "49") ctx.ltElements.append(value.toDouble());
        } else if (ctx.currentTable == "DIMSTYLE") {
            int icode = code.toInt();
            if      (icode == 2)              ctx.dsName     = value;
            else if (icode == 3)              ctx.dsPost      = value;
            else if (icode == 71)             ctx.dsTol       = value.toInt();
            else if (icode == 72)             ctx.dsLim       = value.toInt();
            else if (icode == 47)             ctx.dsTp        = value.toDouble(); // DIMTP
            else if (icode == 48)             ctx.dsTm        = value.toDouble(); // DIMTM
            else if (icode == 271 || icode == 170) ctx.dsDec  = value.toInt();    // DIMDEC
            else if (icode == 272 || icode == 171) ctx.dsTdec = value.toInt();    // DIMTDEC
            else if (icode == 78)             ctx.dsZin       = value.toInt();
            else if (icode == 79)             ctx.dsTzin      = value.toInt();
            else if (icode == 40)             ctx.dsDimScale  = value.toDouble(); // DIMSCALE
            else if (icode == 140)            ctx.dsTextSize  = value.toDouble(); // DIMTXT
            else if (icode == 75)             ctx.dsSe1       = value.toInt();    // DIMSE1
            else if (icode == 76)             ctx.dsSe2       = value.toInt();    // DIMSE2
            else if (icode == 281)            ctx.dsSd1       = value.toInt();    // DIMSD1
            else if (icode == 282)            ctx.dsSd2       = value.toInt();    // DIMSD2
            else if (icode == 3 || icode == 340) ctx.dsFontName = value;          // DIMTXSTY
        } else if (ctx.currentTable == "STYLE") {
            if      (code == "2") ctx.dsName = value;
            else if (code == "3") ctx.dsFontName = value;
        }
    }
}

// ============================================================
// TABLES 레코드 완료 처리
//   ctx.currentTable 값에 따라 LAYER / LTYPE / DIMSTYLE을 씬에 등록.
//   구 코드의 finishRecord 람다를 대체.
// ============================================================
void DxfManager::finishTableRecord(TableParseContext& ctx, MyGraphicsScene* scene)
{
    LayerManager*    lm  = scene->context()->layerManager();
    LineTypeManager* ltm = scene->context()->lineTypeManager();

    if (ctx.currentTable == "LAYER" && !ctx.layerName.isEmpty() && lm) {
        // AutoCAD 선종류명("HIDDEN","CENTER2",…)을 Autodraft 내장 선종류명으로 정규화.
        // (파일 LTYPE 테이블에 정의가 함께 들어온 경우엔 그 이름이 그대로 유지된다.)
        QString ltName = ctx.layerLinetype;
        if (ltm) {
            QString canon = ltm->resolveLinetypeName(ltName);
            if (!canon.isEmpty())
                ltName = canon;
        }

        // 레이어 등록: 이름, ACI 색상 변환값, 선 두께, 선종류
        Layer newLayer(ctx.layerName,
                       aciToColor(ctx.aciColor, scene),
                       ctx.weight,
                       ltName);
        if (!lm->addLayer(newLayer)) {
            // 이미 존재하는 레이어(예: "0"→"Default" 기본 레이어)는 DXF 값으로 갱신한다.
            // (이 갱신이 없으면 해당 레이어의 선두께/색상이 DXF 파일 값이 아닌
            // 앱 기본값(weight=1.0)으로 남아버린다.)
            lm->editLayer(ctx.layerName, newLayer);
        }

    } else if (ctx.currentTable == "LTYPE" && !ctx.ltName.isEmpty() && ltm) {
        // 대시·도트 패턴 값을 쉼표 구분 문자열로 직렬화하여 선종류 등록
        QString patternStr;
        for (int i = 0; i < ctx.ltElements.size(); ++i) {
            if (i > 0) patternStr += ",";
            patternStr += QString::number(ctx.ltElements.at(i));
        }
        if (!patternStr.isEmpty())
            ltm->addLinetype(LineType(ctx.ltName, patternStr));

    } else if (ctx.currentTable == "STYLE" && !ctx.dsName.isEmpty()) {
        m_styleFontMap[ctx.dsName.toUpper()] = ctx.dsFontName;

    } else if (ctx.currentTable == "DIMSTYLE" && !ctx.dsName.isEmpty()) {
        // 수집된 치수 변수 전체를 DxfDimStyle 구조체에 채워 등록
        DxfDimStyle style;
        style.name      = ctx.dsName;
        style.dimTol    = ctx.dsTol;
        style.dimLim    = ctx.dsLim;
        style.dimTp     = ctx.dsTp;
        style.dimTm     = ctx.dsTm;
        style.dimDec    = ctx.dsDec;
        style.dimTdec   = ctx.dsTdec;
        style.dimZin    = ctx.dsZin;
        style.dimTzin   = ctx.dsTzin;
        style.dimPost   = ctx.dsPost;
        style.textSize  = ctx.dsTextSize;
        style.dimSe1    = ctx.dsSe1;
        style.dimSe2    = ctx.dsSe2;
        style.dimSd1    = ctx.dsSd1;
        style.dimSd2    = ctx.dsSd2;
        style.dimScale  = (ctx.dsDimScale > 0.0) ? ctx.dsDimScale : 1.0;

        // 폰트 이름 결정 (스타일 이름이나 핸들을 통해 m_styleFontMap에서 조회)
        if (!ctx.dsFontName.isEmpty()) {
            if (m_styleFontMap.contains(ctx.dsFontName.toUpper())) {
                style.fontName = m_styleFontMap.value(ctx.dsFontName.toUpper());
            } else {
                style.fontName = ctx.dsFontName; // 핸들이 아닌 실제 폰트명인 경우
            }
        }
        
        // 키는 대문자로 저장 → 대소문자 구분 없이 조회 가능
        m_dimStyles[ctx.dsName.toUpper()] = style;
    }
}

QColor DxfManager::aciToColor(int aci, MyGraphicsScene* scene) {
    // AutoCAD 표준 ACI(AutoCAD Color Index) 매핑
    switch (aci) {
        case 1: return Qt::red;
        case 2: return Qt::yellow;
        case 3: return Qt::green;
        case 4: return Qt::cyan;
        case 5: return Qt::blue;
        case 6: return Qt::magenta;
        case 7: {
            if (scene && scene->context() && scene->context()->themeManager()) {
                auto theme = scene->context()->themeManager()->currentTheme();
                if (theme == ThemeManager::ThemeType::Light || theme == ThemeManager::ThemeType::CADClassic)
                    return Qt::black;
            }
            return Qt::white;
        }
        case 8: return QColor(128, 128, 128);
        case 9: return QColor(192, 192, 192);
        case 256: return Qt::black;
        default: return Qt::white;
    }
}

void DxfManager::applyLayerStyle(CadItem* item, const QString& layer, MyGraphicsScene* scene) {
    if (!item || !scene) return;
    item->setLayerName(layer);
    LayerStyle style = scene->getLayerStyle(layer);
    QPen p(style.color);
    p.setCosmetic(true);
    p.setWidthF(style.weight);
    if (!style.dashPattern.isEmpty()) {
        p.setStyle(Qt::CustomDashLine);
        p.setDashPattern(style.dashPattern);
    }
    item->setPen(p);
}

CadItem* DxfManager::createEntityFromData(const DxfEntityData& data, EntityFactory& factory, MyGraphicsScene* scene) {
    CadItem* item = m_dispatcher->restore(data.type, data, factory, scene, m_dimStyles, m_blockDefinitions);
    if (item) applyLayerStyle(item, data.layer, scene);
    return item;
}

void DxfManager::decomposeLwPolyline(const DxfEntityData& data, QList<CadItem*>& list, EntityFactory& factory, MyGraphicsScene* scene) {
    if (data.polyVertices.size() < 2) return;
    QPen defaultPen(Qt::white);
    size_t n = data.polyVertices.size();
    size_t segments = data.isClosed ? n : n - 1;
    for (size_t i = 0; i < segments; ++i) {
        MyVector v1 = data.polyVertices[i];
        MyVector v2 = data.polyVertices[(i + 1) % n];
        double b = (i < data.polyBulges.size()) ? data.polyBulges[i] : 0.0;
        CadItem* subItem = nullptr;
        if (std::abs(b) < 1e-6) {
            subItem = dynamic_cast<CadItem*>(factory.createLine(QLineF(v1.x, v1.y, v2.x, v2.y), defaultPen));
        } else {
            double dx = v2.x - v1.x, dy = v2.y - v1.y, L = std::hypot(dx, dy);
            if (L > 1e-9) {
                double theta = 4.0 * std::atan(b), r = std::abs(L * (1.0 + b * b) / (4.0 * b)), distToC = L * (1.0 - b * b) / (4.0 * b);
                double nx = -dy / L, ny = dx / L, mx = (v1.x + v2.x) / 2.0, my = (v1.y + v2.y) / 2.0, cx = mx + nx * distToC, cy = my + ny * distToC;
                double startAngle = std::atan2(v1.y - cy, v1.x - cx) * 180.0 / MyMath::PI, spanAngle = theta * 180.0 / MyMath::PI;
                subItem = dynamic_cast<CadItem*>(factory.createArc(QPointF(cx, cy), r, startAngle, spanAngle, defaultPen));
            }
        }
        if (subItem) { applyLayerStyle(subItem, data.layer, scene); list.append(subItem); }
    }
}

QList<CadItem*> DxfManager::parseEntityList(QTextStream& in, MyGraphicsScene* scene, const QStringList& stopValues, const QString& initialType) {
    // 파싱 상태를 구조체 하나로 통합 (구 코드의 lambda 캡처 변수들을 대체)
    QList<CadItem*> collectedItems;
    QString code, value;
    EntityFactory factory;
    DxfEntityData data;
    data.reset(initialType);
    if (initialType == "ATTDEF" || initialType == "ATTRIB") {
        data.isAttribute = true;
    }
    int  currentXDataApp = 0;
    bool inDStyle        = false;
    int  currentDimVar   = -1;
    bool inAppData       = false; // AppData(102) 내부 필터링 플래그
    bool skipEntity      = false; // AUTODRAFT XDATA SKIP_IMPORT 마커 감지 시 엔티티 생성 건너뜀

    // 모든 지역 변수를 참조로 묶어 finalizeAndCreate()에 한 번에 전달
    EntityParseContext ctx{
        data, collectedItems, factory, scene, in,
        currentXDataApp, inDStyle, currentDimVar, inAppData, skipEntity
    };

    while (!in.atEnd()) {
        code  = in.readLine().trimmed();
        value = in.readLine().trimmed();

        if (code == "0") {
            // 엔티티 완료: 구 finalizeAndCreate 람다 → 멤버 함수 호출
            finalizeAndCreate(ctx);
            reportProgress(QString(), 1);   // 진행 다이얼로그 갱신(스로틀 내장)
            if (stopValues.contains(value)) break;
            data.reset(value);
            if (value == "ATTDEF" || value == "ATTRIB") {
                data.isAttribute = true;
            }
        } else {
            int gCode = code.toInt();

            // ==========================================
            // [수정점 1] AppData 무시 로직 적용
            // AutoCAD 2018 등 최신 DXF는 102 { ... } 형태의 사전을 가집니다.
            // 이 블록 내부의 2번, 70번 코드가 해치 패턴을 덮어씌우는 것을 방지합니다.
            // ==========================================
            if (gCode == 102) {
                if (value.startsWith("{")) {
                    inAppData = true;
                } else if (value == "}") {
                    inAppData = false;
                }
                continue;
            }
            // AppData 내부의 모든 그룹 코드는 무시
            if (inAppData) continue;


            // 치수선 위치???????????????????????????????????????????????????????
            // MULTILEADER 파싱: 특정 블록 그룹(300, 302, 304, 301, 303,
            // 305)을 감지하여 현재 파싱 중인 섹션(mlSection)을 구분합니다.
            //  mlSection: 0=기본, 1=CONTEXT_DATA, 2=LEADER, 3=LEADER_LINE
            // 치수선 위치???????????????????????????????????????????????????????
            if (data.type == "MULTILEADER") {
                // 엔티티 공통 헤더 코드(handle, layer)는 MULTILEADER에도 적용한다.
                // 이 코드들은 switch 문 이전에 continue로 빠져나가므로 여기서 명시적으로 처리한다.
                if (gCode == 5)  { data.handle = value; continue; }
                if (gCode == 8)  { data.layer = fromDxfLayerName(value); continue; }

                if (gCode == 300) {
                    // "CONTEXT_DATA{" 문구 발견(데이터 블록 시작)
                    if (value.startsWith("CONTEXT_DATA")) {
                        data.mlSection = 1;
                    } else if (value.startsWith("LEADER{")) {
                        // code 302 기반 "LEADER{" 인지 확인(멀티 리더의 리더 섹션)
                        data.mlSection = 2;
                    }
                    continue;
                } else if (gCode == 302) {
                    if (value.startsWith("LEADER{")) {
                        data.mlSection = 2;
                    } else if (value.startsWith("LEADER_LINE{")) {
                        data.mlSection = 3;
                    } else {
                        // 데이텀 상자용 문자 ("A", "B" 등)
                        data.text = value;
                    }
                    continue;
                } else if (gCode == 304) {
                    // "LEADER_LINE{" 문구 발견 시 리더선 파싱 모드 전환
                    if (value.startsWith("LEADER_LINE")) data.mlSection = 3;
                    else data.text = value;
                    continue;
                } else if (gCode == 301) {
                    // "CONTEXT_DATA{" 종료 (301 = "}")
                    if (data.mlSection == 1) data.mlSection = 0;
                    continue;
                } else if (gCode == 303) {
                    // "LEADER{" 종료
                    if (data.mlSection == 2) data.mlSection = 0;
                    continue;
                } else if (gCode == 305) {
                    // "LEADER_LINE{" 종료
                    if (data.mlSection == 3) data.mlSection = 2; // LEADER 섹션으로 복귀
                    continue;
                }

                // code 10 발견: 멀티리더 화살표 끝점 좌표 파싱
                if (gCode == 10) {
                    data.mlPendingX = value.toDouble();
                    continue;
                }
                // code 20 발견: X좌표 임시 저장분과 결합하여 점 생성
                if (gCode == 20) {
                    double px = data.mlPendingX;
                    double py = value.toDouble();
                    if (data.mlSection == 1 && !data.mlContentPosSet) {
                        // CONTEXT_DATA 섹션 내의 10/20 좌표(데이터 위치)
                        data.mlContentPos = MyVector(px, py);
                        data.mlContentPosSet = true;
                    } else if (data.mlSection == 2) {
                        // [추가] LEADER 섹션 내의 10/20 좌표는 Landing Point (Dogleg)
                        data.mlDoglegPos = MyVector(px, py);
                    } else if (data.mlSection == 3) {
                        // [수정] LEADER_LINE 섹션 내의 모든 10/20 좌표를 polyVertices에 수집한다.
                        data.polyVertices.push_back(MyVector(px, py));
                    }
                    data.mlPendingX = 0;
                    continue;
                }
                // [추가] code 310: 이진 데이터에서 데이텀 문자 추출 (A, B 등)
                if (gCode == 310 && data.text.isEmpty()) {
                    bool ok;
                    int val = value.toInt(&ok, 16);
                    if (ok && val >= 32 && val <= 126) {
                        data.text = QString(QChar(val));
                    }
                    continue;
                }
                // code 110/120: 도그레그 꺾임점 (Dogleg landing point - 예비용)
                if (gCode == 110) {
                    data.mlPendingX = value.toDouble();
                    continue;
                }
                if (gCode == 120) {
                    data.mlDoglegPos = MyVector(data.mlPendingX, value.toDouble());
                    data.mlPendingX = 0;
                    continue;
                }
                if (gCode == 140) {
                    data.fontSize = value.toDouble();
                    data.hasFontSize = true;
                    continue;
                }
                // 치수선 위치 MULTILEADER ?? ??? ?? ??? ???? ??? ??? continue
                continue;
            }
            // 치수선 위치???????????????????????????????????????????????????????

            switch (gCode) {
                case 5:  data.handle = value; break;
                case 340: data.associatedHandle = value; break;
                case 8:  data.layer = fromDxfLayerName(value); break;
                case 2:  
                    if (data.type == "HATCH") data.patternName = value;
                    else if (data.type == "ATTDEF" || data.type == "ATTRIB") data.attrTag = value;
                    else data.blockName = value; 
                    break;
                case 10: 
                    if (data.type == "DIMENSION") data.dx10 = value.toDouble();
                    else if (data.type == "HATCH") {
                        data.x1 = value.toDouble();
                    }
                    else data.x1 = value.toDouble();
                    break;
                case 20: 
                    if (data.type == "LWPOLYLINE") {
                        data.y1 = value.toDouble();
                        data.polyVertices.push_back(MyVector(data.x1, data.y1));
                        data.polyBulges.push_back(0.0);
                    } else if (data.type == "LEADER") {
                        data.y1 = value.toDouble();
                        data.polyVertices.push_back(MyVector(data.x1, data.y1));
                    } else if (data.type == "DIMENSION") {
                        data.dy10 = value.toDouble();
                    } else if (data.type == "HATCH") {
                        data.y1 = value.toDouble();
                        if (data.inHatchBoundary) {
                            if (data.currentHatchLoopType & 2) {
                                data.currentHatchLoop << QPointF(data.x1, data.y1);
                                data.currentHatchBulge << 0.0;
                            } else if (!data.currentHatchEdges.isEmpty()) {
                                auto& edge = data.currentHatchEdges.last();
                                if (edge.type == 1)      edge.p1 = QPointF(data.x1, data.y1);
                                else if (edge.type == 2 || edge.type == 3) edge.center = QPointF(data.x1, data.y1);
                                else if (edge.type == 4) edge.controlPoints.append(QPointF(data.x1, data.y1));
                            }
                        }
                    } else if (data.type == "SPLINE") {
                        data.y1 = value.toDouble();
                        data.splineControlPoints.push_back(MyVector(data.x1, data.y1));
                    } else {
                        data.y1 = value.toDouble();
                    }
                    break;
                case 30:
                    if (data.type == "HATCH") data.z1 = value.toDouble(); // Elevation
                    break;
                case 110:
                    data.dx13 = value.toDouble(); break;
                case 120:
                    data.dy13 = value.toDouble(); break;
                case 42:
                    if (data.type == "ELLIPSE") {
                        data.ellipseEndParam = value.toDouble(); // 끝 매개각(라디안)
                    } else if (data.type == "HATCH") {
                        if (data.inHatchBoundary && !data.currentHatchEdges.isEmpty() && data.currentHatchEdges.last().type == 4) {
                            data.currentHatchEdges.last().weights.append(value.toDouble());
                        } else if (!data.currentHatchBulge.isEmpty()) {
                            data.currentHatchBulge.back() = value.toDouble();
                        }
                    } else if (data.type == "LWPOLYLINE") {
                        if (!data.polyBulges.empty()) data.polyBulges.back() = value.toDouble();
                    } else data.scaleY = value.toDouble();
                    break;
                case 11: 
                    if (data.type == "SPLINE") data.x2 = value.toDouble();
                    else if (data.type == "HATCH") data.x2 = value.toDouble();
                    else { data.x2 = value.toDouble(); data.dx11 = value.toDouble(); }
                    break;
                case 21: 
                    if (data.type == "SPLINE") {
                        data.y2 = value.toDouble();
                        data.splineFitPoints.push_back(MyVector(data.x2, data.y2));
                    } else if (data.type == "HATCH") {
                        data.y2 = value.toDouble();
                        if (data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                            auto& edge = data.currentHatchEdges.last();
                            if (edge.type == 1)      edge.p2 = QPointF(data.x2, data.y2); // Line
                            else if (edge.type == 3) edge.majorAxis = QPointF(data.x2, data.y2); // Elliptical Arc
                            else if (edge.type == 4) edge.fitPoints.append(QPointF(data.x2, data.y2)); // Spline 피팅점
                        }
                    } else { data.y2 = value.toDouble(); data.dy11 = value.toDouble(); }
                    break;
                case 40:
                    data.radius = value.toDouble(); data.fontSize = value.toDouble();
                    if (data.type == "HATCH" && data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                        auto& edgeRef40 = data.currentHatchEdges.last();
                        if (edgeRef40.type == 4) {
                            // Spline 엣지: gc40은 노트 값 (반복) — 반지름과 구분 필수
                            edgeRef40.knotVector.append(value.toDouble());
                        } else if (edgeRef40.type == 3) {
                            edgeRef40.ratio = value.toDouble(); // Elliptical Arc ratio
                        } else {
                            // Arc 엣지 등: gc40은 반지름 (1회)
                            edgeRef40.radius = value.toDouble();
                        }
                    } else if (data.type == "SPLINE") {
                        // SPLINE 엔티티: gc40은 노트 벡터 값 (반복)
                        data.splineKnots.push_back(value.toDouble());
                    }
                    break;
                case 50: 
                    data.rotation = value.toDouble(); 
                    data.dimRotation = value.toDouble(); 
                    data.startAngle = value.toDouble(); 
                    if (data.type == "HATCH" && data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                        data.currentHatchEdges.last().startAngle = value.toDouble();
                    }
                    break;
                case 51:  
                    data.endAngle = value.toDouble(); 
                    if (data.type == "HATCH" && data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                        data.currentHatchEdges.last().endAngle = value.toDouble();
                    }
                    break;
                case 70:  
                    // if (data.type == "HATCH") data.solidFill = (value.toInt() == 1);
                    // else if (data.type == "SPLINE") {
                    //     data.splineClosed = (value.toInt() & 1);
                    // }
                    // else {
                    //     data.isClosed = (value.toInt() & 1); 
                    //     data.dimType = value.toInt(); 
                    // }
                    // break;
                    // gc70 HATCH: solidFill 플래그는 gc91(경계 수) 이전에 등장
                    // → inHatchBoundary=false 상태에서만 처리 (경계 내부의 gc70은 무관)
                    if (data.type == "HATCH" && !data.inHatchBoundary) {
                        data.solidFill = (value.toInt() == 1);
                    }
                    else if (data.type == "SPLINE") {
                        data.splineClosed = (value.toInt() & 1);
                    }
                    // 일반 엔티티 (AppData 체크 불필요)
                    else if (data.type != "HATCH") {
                        data.isClosed = (value.toInt() & 1); 
                        data.dimType = value.toInt(); 
                    }
                    break;
                case 75:
                    // 75번 코드는 해치 경계 데이터가 모두 끝난 직후에 항상 등장합니다.
                    // 여기서 플래그를 꺼주어야 뒤따라오는 41번, 52번 속성을 정상적으로 획득합니다.
                    if (data.type == "HATCH") {
                        data.inHatchBoundary = false;
                    } else if (data.type == "DIMENSION") {
                        data.dimSe1 = value.toInt(); // DIMSE1: 첫 번째 보조선 억제
                    }
                    break;
                case 76:
                    // DIMENSION 엔티티의 gc76: DIMSE2(두 번째 보조선 억제) 엔티티 오버라이드
                    if (data.type == "DIMENSION") {
                        data.dimSe2 = value.toInt();
                    }
                    break;
                case 52: // 패턴 이름
                    if (data.type == "HATCH") {
                        data.hatchAngle = value.toDouble();
                        data.inHatchBoundary = false; // 패턴 이름? ?? ? ?? ?? ??
                    }
                    break;
                case 41:
                    // ELLIPSE: gc41 = 시작 매개각(라디안)
                    if (data.type == "ELLIPSE") {
                        data.ellipseStartParam = value.toDouble();
                        break;
                    }
                    // gc41 HATCH 패턴 스케일: gc75 이후(inHatchBoundary=false)에 등장
                    // 경계 내부(inHatchBoundary=true)에서는 스플라인 가중치 등 다른 의미를
                    // 가질 수 있으므로 경계 외부에서만 스케일로 취급한다.
                    if (data.type == "HATCH" && !data.inHatchBoundary) {
                        data.hatchScale = value.toDouble();
                    }
                    // 일반 엔티티의 Scale X (이 경우 AppData 체크 불필요)
                    else if (data.type != "HATCH") {
                        data.scaleX = value.toDouble();
                    }
                    break;
                case 1:   data.text = value; break;
                case 3:
                    if (data.type == "ATTDEF") data.attrPrompt = value;
                    else data.dimStyleName = value;
                    break;
                case 91: // 경계 루프의 개수
                    if (data.type == "HATCH") data.inHatchBoundary = true;
                    break;
                case 92: // 새 경계 루프 시작 (Loop Type)
                    if (data.type == "HATCH") {
                        // 이전 루프 데이터 플러시 (폴리라인 우선)
                        if (!data.currentHatchLoop.isEmpty()) {
                            data.hatchPaths.append(buildPolylinePath(data.currentHatchLoop, data.currentHatchBulge));
                            data.hatchLoopTypes.append(data.currentHatchLoopType);
                            data.currentHatchLoop.clear();
                            data.currentHatchBulge.clear();
                        } else if (!data.currentHatchEdges.isEmpty()) {
                            data.hatchPaths.append(buildHatchEdgePath(data.currentHatchEdges));
                            data.hatchLoopTypes.append(data.currentHatchLoopType);
                        }
                        // 항상 엣지 목록 초기화: 폴리라인→엣지 루프 전환 시
                        // gc72(폴리라인의 has-bulge 플래그)로 생성된 가짜 엣지가
                        // 다음 엣지 루프로 넘어가는 것을 방지한다.
                        data.currentHatchEdges.clear();
                        data.currentHatchLoopType = value.toInt();
                        data.hatchEdgesStarted = false; // gc93 전까지 엣지 생성 금지
                        data.inHatchBoundary = true;
                    }
                    break;
                case 93: // 엣지 루프의 엣지 수 (폴리라인 루프의 경우 정점 수)
                    if (data.type == "HATCH" && data.inHatchBoundary)
                        data.hatchEdgesStarted = true; // 이후 gc72가 실제 엣지 타입임
                    break;
                case 71: 
                    if (data.type == "SPLINE") data.splineDegree = value.toInt();
                    else data.dimTol = value.toInt(); 
                    break;
                case 72:
                    if (data.type == "HATCH" && data.inHatchBoundary) {
                        // 폴리라인 루프(bit 1 set)에서 gc72는 "불지 포함 플래그(0/1)"이다.
                        // 엣지 루프에서만 gc72가 엣지 타입(1=Line,2=Arc,4=Spline)을 의미한다.
                        // 엣지 루프 헤더(gc93 이전)의 gc72는 폴리라인과 동일한 형식의 헤더 필드:
                        //   gc92 → gc72(has-bulge) → gc73(is-closed) → gc93(edge count) → [edges]
                        // gc93 수신 전까지 hatchEdgesStarted=false → 헤더 gc72를 엣지로 오해하지 않음
                        if (!(data.currentHatchLoopType & 2) && data.hatchEdgesStarted) {
                            data.hatchEdgeType = value.toInt();
                            data.currentHatchEdges.append(DxfEntityData::HatchEdge());
                            data.currentHatchEdges.last().type = data.hatchEdgeType;
                        }
                        // 폴리라인 루프의 gc72(has-bulge)는 실제 bulge를 gc42로 처리하므로 무시
                    } else {
                        data.dimLim = value.toInt();
                    }
                    break;
                case 73:
                    if (data.type == "HATCH" && data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                        auto& edge73 = data.currentHatchEdges.last();
                        if (edge73.type == 2)      edge73.ccw      = (value.toInt() == 1); // Arc: ccw 방향
                        else if (edge73.type == 4) edge73.rational = (value.toInt() == 1); // Spline: 유리형 여부
                    }
                    break;
                case 74:
                    if (data.type == "HATCH" && data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                        auto& edge74 = data.currentHatchEdges.last();
                        if (edge74.type == 4) edge74.periodic = (value.toInt() == 1); // Spline: 주기형 여부
                    }
                    break;
                case 95: // 스플라인 엣지 노트 벡터 수 (현재 사용 안 함, skip)
                    break;
                case 96: // 스플라인 엣지 제어점 수 (gc10/20 반복으로 자동 수집)
                    break;
                case 97: // 스플라인 엣지 피팅점 수 (gc11/21 반복으로 자동 수집)
                    break;
                case 94:
                    if (data.type == "HATCH" && data.inHatchBoundary && !data.currentHatchEdges.isEmpty()) {
                        data.currentHatchEdges.last().degree = value.toInt();
                    }
                    break;
                case 47:  data.dimTp = value.toDouble(); break;
                case 48:  data.dimTm = value.toDouble(); break;
                case 271: data.dimPrecision = value.toInt(); break;
                case 272: data.dimTdec = value.toInt(); break;

                // ==========================================
                // XData ?? (1001 "ACAD", 1000 "DSTYLE")
                // ==========================================
                case 1001:
                    if      (value == "ACAD")       currentXDataApp = 1;
                    else if (value == "AUTODRAFT")  currentXDataApp = 2;
                    else                            currentXDataApp = 0;
                    break;
                case 1000:
                    if      (currentXDataApp == 1 && value == "DSTYLE")
                        inDStyle = true;
                    else if (currentXDataApp == 2 && value == "SKIP_IMPORT")
                        skipEntity = true;
                    // 다중선 그룹 태그 복원 (AUTODRAFT XDATA)
                    else if (currentXDataApp == 2 && value.startsWith("ML_GROUP_ID:"))
                        data.mlGroupId = value.mid(12);
                    else if (currentXDataApp == 2 && value.startsWith("ML_ROLE:"))
                        data.mlRole = value.mid(8);
                    else if (currentXDataApp == 2 && value.startsWith("ML_SPACING:"))
                        data.mlSpacing = value.mid(11).toDouble();
                    break;
                case 1002:
                    if (value == "{") {
                        // 패턴 이름 ??
                    } else if (value == "}") { 
                        inDStyle = false; 
                        currentDimVar = -1; 
                    }
                    break;
                case 1070:
                case 1071:
                    if (inDStyle) {
                        if (currentDimVar == -1) {
                            currentDimVar = value.toInt();
                        } else {
                            int val = value.toInt();
                            if (currentDimVar == 71) { data.dimTol = val; data.hasDimTol = true; }
                            else if (currentDimVar == 72) { data.dimLim = val; data.hasDimLim = true; }
                            else if (currentDimVar == 271 || currentDimVar == 170) data.dimPrecision = val;
                            else if (currentDimVar == 272 || currentDimVar == 171) data.dimTdec = val;
                            else if (currentDimVar == 78) data.dimZin = val;
                            else if (currentDimVar == 75)  data.dimSe1 = val;
                            else if (currentDimVar == 76)  data.dimSe2 = val;
                            else if (currentDimVar == 281) data.dimSd1 = val;
                            else if (currentDimVar == 282) data.dimSd2 = val;
                            currentDimVar = -1;
                        }
                    }
                    break;
                case 1040:
                    if (inDStyle && currentDimVar != -1) {
                        double val = value.toDouble();
                        if (currentDimVar == 47) { data.dimTp = val; data.hasDimTp = true; }
                        else if (currentDimVar == 48) { data.dimTm = val; data.hasDimTm = true; }
                        else if (currentDimVar == 140) { data.fontSize = val; data.hasFontSize = true; }
                        currentDimVar = -1;
                    }
                    break;

                case 66:  data.entitiesFollow = value.toInt(); break;
                case 230: data.extZ = value.toDouble(); break;
                case 13:  data.dx13 = value.toDouble(); break;
                case 23:  data.dy13 = value.toDouble(); break;
                case 14:  data.dx14 = value.toDouble(); break;
                case 24:  data.dy14 = value.toDouble(); break;
                case 15:  data.dx15 = value.toDouble(); break;
                case 25:  data.dy15 = value.toDouble(); break;
                case 16:  data.dx16 = value.toDouble(); break;
                case 26:  data.dy16 = value.toDouble(); break;
                default:  break;
            }
        }
    }
    return collectedItems;
}

// ============================================================
// NURBS 유리 B-스플라인 곡선 위의 샘플 점 계산 (정적 멤버 함수)
//   cps     : 제어점 목록
//   weights : 각 제어점 가중치 (비어 있으면 1.0 사용)
//   deg     : 차수(degree)
//   nPts    : 샘플 분할 수
//   knots   : 노트 벡터 (비어 있으면 균일 클램프 노트 자동 생성)
//   구 코드의 evalBSpline 중첩 람다를 대체.
// ============================================================
QList<QPointF> DxfManager::evalBSpline(const QList<QPointF>& cps,
                                        const QList<double>&  weights,
                                        int                   deg,
                                        int                   nPts,
                                        const QList<double>&  knots_in)
{
    int n = cps.size();
    if (n < 2) return {};
    if (n < deg + 1) deg = n - 1; // 제어점 수보다 차수가 크면 내림

    // --- 노트 벡터 준비 ---
    QList<double> knots;
    if (!knots_in.isEmpty()) {
        // 제공된 노트 값 우선 사용; CAD 프로그램별로 개수가 다를 수 있으므로 n+deg+1에 맞게 조정
        knots = knots_in;
        int required = n + deg + 1;
        while (knots.size() < required) knots.append(knots.last());
        while (knots.size() > required) knots.removeLast();
    } else {
        // 균일 클램프 노트 벡터 자동 생성
        int m = n + deg + 1;
        for (int i = 0; i < m; ++i) {
            if      (i <= deg) knots.append(0.0);
            else if (i >= n)   knots.append(double(n - deg));
            else               knots.append(double(i - deg));
        }
    }

    double tMin = knots[deg];
    double tMax = knots[n];

    // de Boor 알고리즘용 동차 좌표 구조체 (x*w, y*w, w)
    struct Pt3 { double x, y, w; };

    QList<QPointF> result;
    for (int s = 0; s <= nPts; ++s) {
        double t = tMin + (tMax - tMin) * double(s) / double(nPts);
        if (s == nPts) t = tMax - 1e-10; // 마지막 노트 span 초과 방지

        // 현재 t가 속하는 노트 구간 k 탐색
        int k = deg;
        for (int i = deg; i < n; ++i) {
            if (t >= knots[i] && t < knots[i + 1]) { k = i; break; }
        }

        // de Boor 삼각 배열 초기화 (동차 좌표)
        QList<Pt3> d;
        for (int j = 0; j <= deg; ++j) {
            int    idx = k - deg + j;
            double w   = (weights.size() > idx) ? weights[idx] : 1.0;
            d.append({ cps[idx].x() * w, cps[idx].y() * w, w });
        }

        // de Boor 재귀 혼합
        for (int r = 1; r <= deg; ++r) {
            for (int j = deg; j >= r; --j) {
                int    idx   = k - deg + j;
                double denom = knots[idx + deg - r + 1] - knots[idx];
                double alpha = (denom < 1e-12) ? 0.0 : (t - knots[idx]) / denom;
                d[j] = {
                    (1.0 - alpha) * d[j-1].x + alpha * d[j].x,
                    (1.0 - alpha) * d[j-1].y + alpha * d[j].y,
                    (1.0 - alpha) * d[j-1].w + alpha * d[j].w
                };
            }
        }

        // 동차 좌표 → 유클리드 좌표 변환
        double finalW = d[deg].w;
        if (std::abs(finalW) > 1e-12)
            result.append(QPointF(d[deg].x / finalW, d[deg].y / finalW));
        else
            result.append(QPointF(d[deg].x, d[deg].y));
    }
    return result;
}

// ============================================================
// 타원 호 위의 파라메트릭 점 계산 (정적 멤버 함수)
//   (cx, cy)  : 타원 중심
//   majorAxis : 장축 벡터 (원점 기준 상대 좌표)
//   ratio     : 단축/장축 비율
//   t         : 파라메트릭 각도 (라디안)
//   구 코드의 evalEll 중첩 람다를 대체.
// ============================================================
QPointF DxfManager::evalEllipsePoint(double  cx,
                                      double  cy,
                                      QPointF majorAxis,
                                      double  ratio,
                                      double  t)
{
    // 장축·단축 길이 계산
    double a     = std::hypot(majorAxis.x(), majorAxis.y());
    double b     = a * ratio;
    // 장축이 X축과 이루는 회전 각도
    double angle = std::atan2(majorAxis.y(), majorAxis.x());
    double cosA  = std::cos(angle);
    double sinA  = std::sin(angle);
    // 파라메트릭 점 (회전 전 타원 좌표계)
    double x = a * std::cos(t);
    double y = b * std::sin(t);
    // 회전 변환 후 중심 오프셋 적용
    return QPointF(cx + x * cosA - y * sinA,
                   cy + x * sinA + y * cosA);
}

// ============================================================
// 엣지 기반 해치 루프 → QPainterPath 변환
//   edges: DXF HATCH 엔티티의 단일 경계 루프(gc92 단위)
//   내부에서 evalBSpline / evalEllipsePoint 정적 멤버 함수 사용.
//   구 코드의 buildHatchEdgePath 람다를 대체.
// ============================================================
QPainterPath DxfManager::buildHatchEdgePath(const QList<DxfEntityData::HatchEdge>& edges)
{
    QPainterPath path;
    if (edges.isEmpty()) return path;

    // 각 엣지를 위상 정렬에 필요한 기하 정보로 변환하는 내부 구조체
    struct EdgeGeom {
        int            type;
        QPointF        startPt, endPt;
        QPointF        p1, p2;           // type==1(Line) 끝점
        double         cx = 0, cy = 0, r = 0;
        double         startAngle = 0, endAngle = 0;
        bool           ccw = true;
        QList<QPointF> splinePts;
        bool           needsReverse = false;
        QPointF        majorAxis;
        double         ratio = 1.0;
    };

    QList<EdgeGeom> geoms;

    // --- 1단계: 각 HatchEdge → EdgeGeom 변환 ---
    for (int i = 0; i < edges.size(); ++i) {
        const DxfEntityData::HatchEdge& edge = edges[i];
        EdgeGeom g;
        g.type = edge.type;

        if (edge.type == 1) {
            // 직선(Line)
            g.p1 = edge.p1;  g.p2 = edge.p2;
            g.startPt = g.p1; g.endPt = g.p2;

        } else if (edge.type == 2) {
            // 원호(Arc): DXF CW 호(gc73=0)의 각도는 시계 방향 기준이므로
            // CCW 등가 각도로 변환하여 cos/sin 계산을 통일
            g.cx  = edge.center.x();  g.cy = edge.center.y();
            g.r   = edge.radius;      g.ccw = edge.ccw;
            double neg   = edge.ccw ? 1.0 : -1.0;
            g.startAngle = edge.startAngle * neg;
            g.endAngle   = edge.endAngle   * neg;
            double sr = g.startAngle * M_PI / 180.0;
            double er = g.endAngle   * M_PI / 180.0;
            g.startPt = QPointF(g.cx + g.r * std::cos(sr), g.cy + g.r * std::sin(sr));
            g.endPt   = QPointF(g.cx + g.r * std::cos(er), g.cy + g.r * std::sin(er));

        } else if (edge.type == 3) {
            // 타원 호(Elliptical Arc)
            g.cx = edge.center.x();  g.cy = edge.center.y();

            // 일부 DXF에서 장축 끝점이 절대 좌표로 들어오는 경우 감지 후 상대 벡터로 변환
            QPointF majorVec    = edge.majorAxis;
            double  distToOrigin = std::hypot(majorVec.x(), majorVec.y());
            double  distToCenter = QLineF(edge.center, majorVec).length();
            if (distToCenter < distToOrigin * 0.7)
                majorVec -= edge.center;

            g.majorAxis  = majorVec;
            g.ratio      = edge.ratio;
            g.startAngle = edge.startAngle; // 라디안
            g.endAngle   = edge.endAngle;   // 라디안
            g.ccw        = edge.ccw;
            // 구 evalEll 람다 → evalEllipsePoint 정적 멤버 함수 호출
            g.startPt = evalEllipsePoint(g.cx, g.cy, g.majorAxis, g.ratio, g.startAngle);
            g.endPt   = evalEllipsePoint(g.cx, g.cy, g.majorAxis, g.ratio, g.endAngle);

        } else if (edge.type == 4) {
            // B-스플라인(Spline)
            if (edge.controlPoints.size() >= 2) {
                int nSamples = (int)std::max<qsizetype>(50, edge.controlPoints.size() * 15);
                // 구 evalBSpline 람다 → 정적 멤버 함수 호출
                g.splinePts = evalBSpline(edge.controlPoints, edge.weights,
                                          edge.degree, nSamples, edge.knotVector);
                g.startPt = edge.controlPoints.first();
                g.endPt   = edge.controlPoints.last();
                if (!g.splinePts.isEmpty()) {
                    g.splinePts.first() = g.startPt;
                    g.splinePts.last()  = g.endPt;
                }
            } else if (edge.fitPoints.size() >= 2) {
                // 피팅점 기반 Catmull-Rom 근사
                const QList<QPointF>& fp = edge.fitPoints;
                int n = fp.size();
                g.splinePts.clear();
                for (int j = 0; j < n - 1; ++j) {
                    QPointF p0 = (j == 0)     ? fp[0]     : fp[j - 1];
                    QPointF p1 = fp[j];
                    QPointF p2 = fp[j + 1];
                    QPointF p3 = (j == n - 2) ? fp[n - 1] : fp[j + 2];
                    for (int k = 0; k < 10; ++k) {
                        double t  = k / 10.0;
                        double t2 = t * t, t3 = t2 * t;
                        double f1 = -0.5*t3 + t2        - 0.5*t;
                        double f2 =  1.5*t3 - 2.5*t2    + 1.0;
                        double f3 = -1.5*t3 + 2.0*t2    + 0.5*t;
                        double f4 =  0.5*t3 - 0.5*t2;
                        g.splinePts.append(p0*f1 + p1*f2 + p2*f3 + p3*f4);
                    }
                }
                g.splinePts.append(fp.last());
                // 피팅점의 first/last가 곡선의 정확한 시작/끝점
                g.startPt = fp.first();
                g.endPt   = fp.last();
                if (!g.splinePts.isEmpty()) {
                    g.splinePts.first() = g.startPt;
                    g.splinePts.last()  = g.endPt;
                }
            }
        }
        geoms.append(g);
    }

    // --- 2단계: 위상 정렬 (DXF 원본 순서 우선, 끊기면 최근접 탐색) ---
    QList<EdgeGeom> orderedGeoms;
    if (!geoms.isEmpty()) {
        orderedGeoms.append(geoms.takeFirst());

        while (!geoms.isEmpty()) {
            QPointF currentEnd = orderedGeoms.last().endPt;
            int    bestIdx = -1;
            bool   reverse = false;
            double minDist = 1e99;

            // 2a. DXF 원본 순서의 다음 엣지가 연결되는지 확인 (임계값: 0.1)
            double seqD1 = QLineF(currentEnd, geoms.first().startPt).length();
            double seqD2 = QLineF(currentEnd, geoms.first().endPt).length();

            if (seqD1 < 0.1) {
                bestIdx = 0; reverse = false;
            } else if (seqD2 < 0.1) {
                bestIdx = 0; reverse = true;
            } else {
                // 2b. 원본 순서가 끊겼다면 전체 남은 엣지 중 최근접 탐색
                //     구 코드의 check 람다(5줄)를 루프 내에 직접 인라인
                for (int j = 0; j < geoms.size(); ++j) {
                    double d1 = QLineF(currentEnd, geoms[j].startPt).length();
                    double d2 = QLineF(currentEnd, geoms[j].endPt).length();

                    // --- check(j, d1, false) 인라인 ---
                    if (d1 < minDist - 1e-4) {
                        minDist = d1; bestIdx = j; reverse = false;
                    } else if (bestIdx != -1 && d1 < minDist + 1e-4) {
                        // 거리가 비슷하면 구조가 복잡한 엣지(type 높은 것) 우선
                        if (geoms[j].type > geoms[bestIdx].type) {
                            minDist = d1; bestIdx = j; reverse = false;
                        }
                    }

                    // --- check(j, d2, true) 인라인 ---
                    if (d2 < minDist - 1e-4) {
                        minDist = d2; bestIdx = j; reverse = true;
                    } else if (bestIdx != -1 && d2 < minDist + 1e-4) {
                        if (geoms[j].type > geoms[bestIdx].type) {
                            minDist = d2; bestIdx = j; reverse = true;
                        }
                    }
                }
            }

            EdgeGeom nextG = geoms.takeAt(bestIdx);
            if (reverse) {
                std::swap(nextG.startPt, nextG.endPt);
                nextG.needsReverse = true;
            }
            orderedGeoms.append(nextG);
        }
    }

    // --- 3단계: 정렬된 엣지를 QPainterPath로 그리기 ---
    if (!orderedGeoms.isEmpty()) {
        path.moveTo(orderedGeoms.first().startPt);
        QPointF prevEnd = orderedGeoms.first().startPt;

        for (int i = 0; i < orderedGeoms.size(); ++i) {
            const EdgeGeom& g   = orderedGeoms[i];
            bool            rev = g.needsReverse;

            // 이전 끝점과 현재 시작점 사이 갭이 실제로 있을 때만 lineTo
            // (불필요한 대각선 방지)
            if (QLineF(prevEnd, g.startPt).length() > 1e-4)
                path.lineTo(g.startPt);

            if (g.type == 1) {
                path.lineTo(rev ? g.p1 : g.p2);

            } else if (g.type == 2) {
                // 원호: cos/sin 직접 분할 (arcTo 대신)
                double sAngle    = rev ? g.endAngle    : g.startAngle;
                double eAngle    = rev ? g.startAngle  : g.endAngle;
                bool   isCcw     = rev ? !g.ccw        : g.ccw;
                double spanAngle = eAngle - sAngle;
                if (isCcw)  { if (spanAngle < 0) spanAngle += 360.0; if (spanAngle == 0) spanAngle = 360.0; }
                else        { if (spanAngle > 0) spanAngle -= 360.0; if (spanAngle == 0) spanAngle = -360.0; }
                double sRad    = sAngle    * M_PI / 180.0;
                double spanRad = spanAngle * M_PI / 180.0;
                int    segs    = std::max(8, int(std::ceil(72.0 * std::abs(spanRad) / (2.0 * M_PI))));
                for (int k = 1; k <= segs; ++k) {
                    double a = sRad + spanRad * k / segs;
                    path.lineTo(QPointF(g.cx + g.r * std::cos(a), g.cy + g.r * std::sin(a)));
                }

            } else if (g.type == 3) {
                // 타원 호: evalEllipsePoint 정적 멤버 함수로 분할
                double sParam = rev ? g.endAngle   : g.startAngle;
                double eParam = rev ? g.startAngle : g.endAngle;
                bool   isCcw  = rev ? !g.ccw       : g.ccw;
                double span   = eParam - sParam;
                if (isCcw)  { if (span < 0) span += 2.0*M_PI; if (span == 0) span = 2.0*M_PI; }
                else        { if (span > 0) span -= 2.0*M_PI; if (span == 0) span = -2.0*M_PI; }
                int segs = std::max(12, int(std::ceil(72.0 * std::abs(span) / (2.0 * M_PI))));
                for (int k = 1; k <= segs; ++k) {
                    double t = sParam + span * k / segs;
                    path.lineTo(evalEllipsePoint(g.cx, g.cy, g.majorAxis, g.ratio, t));
                }

            } else if (g.type == 4) {
                // B-스플라인: 미리 계산된 샘플 점 순서대로(또는 역순으로) 연결
                if (!g.splinePts.isEmpty()) {
                    if (rev) {
                        for (int k = g.splinePts.size() - 2; k >= 0; --k)
                            path.lineTo(g.splinePts[k]);
                    } else {
                        for (int k = 1; k < g.splinePts.size(); ++k)
                            path.lineTo(g.splinePts[k]);
                    }
                }
            }

            prevEnd = g.endPt;
        }
        path.closeSubpath();
    }

    return path;
}

// ============================================================
// 폴리라인 기반 해치 루프 → QPainterPath 변환 (Bulge 완벽 처리)
//   loop  : 폴리라인 정점 목록
//   bulges: 각 세그먼트의 Bulge 값 (양수=CCW, 음수=CW, Y-up 기준)
//   구 코드의 buildPolylinePath 람다를 대체.
// ============================================================
QPainterPath DxfManager::buildPolylinePath(const QPolygonF&     loop,
                                            const QList<double>& bulges)
{
    QPainterPath path;
    if (loop.isEmpty()) return path;

    path.moveTo(loop.first());
    int n = loop.size();

    // j=1 부터 n까지 순회하여 마지막 닫힘 구간(→ 시작점)까지 모두 처리
    for (int j = 1; j <= n; ++j) {
        int    curr = j % n;
        int    prev = j - 1;

        // 이미 닫힌 폴리라인(last == first)이면 중복 세그먼트 스킵
        if (j == n && loop[prev] == loop[0]) continue;

        double bulge = (prev < bulges.size()) ? bulges[prev] : 0.0;

        if (bulge != 0.0) {
            double dx   = loop[curr].x() - loop[prev].x();
            double dy   = loop[curr].y() - loop[prev].y();
            double dist = std::hypot(dx, dy);

            if (dist > 1e-6) {
                // Bulge → 호 반지름·중심·시작각 계산 (Y-up 기준)
                // arcTo 대신 cos/sin 직접 분할로 끝점 오차 방지
                double sweep   = 4.0 * std::atan(bulge); // 양수=CCW, 음수=CW
                double radius  = dist / (2.0 * std::abs(std::sin(sweep / 2.0)));
                double midx    = (loop[prev].x() + loop[curr].x()) * 0.5;
                double midy    = (loop[prev].y() + loop[curr].y()) * 0.5;
                // sweep>0(CCW)일 때 중심은 현(chord) 좌측(Y-up 기준 위쪽)
                double cx       = midx + (-dy) / (2.0 * std::tan(sweep / 2.0));
                double cy       = midy + ( dx) / (2.0 * std::tan(sweep / 2.0));
                double startRad = std::atan2(loop[prev].y() - cy,
                                             loop[prev].x() - cx);
                int    segs     = std::max(8, int(std::ceil(72.0 * std::abs(sweep)
                                                            / (2.0 * M_PI))));
                path.lineTo(QPointF(cx + radius * std::cos(startRad),
                                    cy + radius * std::sin(startRad)));
                for (int s = 1; s <= segs; ++s) {
                    double a = startRad + sweep * s / segs;
                    path.lineTo(QPointF(cx + radius * std::cos(a),
                                        cy + radius * std::sin(a)));
                }
            } else {
                path.lineTo(loop[curr]);
            }
        } else {
            // Bulge == 0: 직선 세그먼트
            path.lineTo(loop[curr]);
        }
    }

    path.closeSubpath();
    return path;
}

// ============================================================
// HatchItem 인스턴스 생성 후 collectedItems에 추가
//   hPath         : 해치 외곽선 경로 (빈 경우 무시)
//   data          : 해치 엔티티 데이터 (패턴 이름·각도·스케일 등)
//   collectedItems: 완성 아이템 누적 목록
//   scene         : 레이어 스타일 조회에 사용
//   구 코드의 createHatchItem 람다를 대체.
// ============================================================
void DxfManager::createHatchItem(QPainterPath&        hPath,
                                  const DxfEntityData& data,
                                  QList<CadItem*>&     collectedItems,
                                  MyGraphicsScene*     scene)
{
    if (hPath.isEmpty()) return;

    hPath.setFillRule(Qt::OddEvenFill);

    HatchItem* hItem = nullptr;
    if (data.solidFill || data.patternName.toUpper() == "SOLID") {
        // 단색 채우기
        hItem = new HatchItem(hPath, Qt::SolidPattern);
    } else {
        // 패턴 채우기: 스케일이 0이면 1.0으로 보정
        double hScale = (data.hatchScale > 0) ? data.hatchScale : 1.0;
        hItem = new HatchItem(hPath, data.patternName, hScale, data.hatchAngle);
    }

    if (hItem) {
        hItem->setProperty("Solid", data.solidFill);
        applyLayerStyle(hItem, data.layer, scene);
        if (!data.handle.isEmpty())
            hItem->setData(0, data.handle);
        collectedItems.append(hItem);
    }
}

// ============================================================
// 엔티티 완료 처리
//   ctx.data.type에 따라 LWPOLYLINE / INSERT / HATCH / 기타를 생성하고
//   ctx.collectedItems에 추가한 뒤 ctx.data를 초기화한다.
//   buildPolylinePath / buildHatchEdgePath / createHatchItem 멤버 함수 사용.
//   구 코드의 finalizeAndCreate 람다를 대체.
// ============================================================
void DxfManager::finalizeAndCreate(EntityParseContext& ctx)
{
    if (ctx.data.type.isEmpty()) return;
    if (ctx.skipEntity) {
        ctx.data.reset();
        ctx.currentXDataApp = 0;
        ctx.inDStyle        = false;
        ctx.currentDimVar   = -1;
        ctx.inAppData       = false;
        ctx.skipEntity      = false;
        return;
    }

    if (ctx.data.type == "ATTRIB") {
        // ATTRIB: INSERT 뒤에 따라오는 속성 엔티티
        // collectedItems의 마지막 BlockItem에 ATTDEF TextItem 텍스트를 실제값으로 교체
        if (!ctx.collectedItems.isEmpty()) {
            BlockItem* bi = dynamic_cast<BlockItem*>(ctx.collectedItems.last());
            if (bi && !ctx.data.text.isEmpty()) {
                const QList<CadItem*>& items = bi->getItems();
                bool matched = false;
                // 1) 태그 기준 매칭
                for (int j = 0; j < items.size(); ++j) {
                    TextItem* ti = dynamic_cast<TextItem*>(items[j]);
                    if (!ti) continue;
                    if (!ti->getProperties().value("isAttribute", false).toBool()) continue;
                    QString defTag = ti->getProperties().value("tag", "").toString();
                    if (!ctx.data.attrTag.isEmpty() &&
                        defTag.compare(ctx.data.attrTag, Qt::CaseInsensitive) == 0) {
                        ti->setProperty("Content", CadUtils::processText(ctx.data.text));
                        matched = true;
                        break;
                    }
                }
                // 2) 태그 불일치: 첫 번째 미처리 ATTDEF 순서 기준 교체
                if (!matched) {
                    for (int j = 0; j < items.size(); ++j) {
                        TextItem* ti = dynamic_cast<TextItem*>(items[j]);
                        if (!ti) continue;
                        if (!ti->getProperties().value("isAttribute", false).toBool()) continue;
                        ti->setProperty("Content", CadUtils::processText(ctx.data.text));
                        break;
                    }
                }
            }
        }
        // ATTRIB 자체는 별도의 CadItem을 생성하지 않음

    } else if (ctx.data.type == "LWPOLYLINE") {
        // 폴리라인 분해 → 선분·호 등 개별 아이템으로 변환
        decomposeLwPolyline(ctx.data, ctx.collectedItems, ctx.factory, ctx.scene);

    } else if (ctx.data.type == "INSERT") {
        // 블록 참조: 블록 정의가 없으면 상태만 초기화 후 종료
        if (!m_blockDefinitions.contains(ctx.data.blockName)) {
            ctx.data.reset();
            ctx.currentXDataApp = 0;
            ctx.inDStyle        = false;
            ctx.currentDimVar   = -1;
            ctx.inAppData       = false;
            ctx.skipEntity      = false;
            return;
        }

        // 블록 내 아이템을 모두 복제
        const QList<CadItem*>& blockItems = m_blockDefinitions[ctx.data.blockName];
        QList<CadItem*> clonedItems;
        for (int i = 0; i < blockItems.size(); ++i) {
            CadItem* cloned = blockItems[i]->clone();
            if (cloned) {
                // 레이어 "0" 또는 빈 레이어는 INSERT 레이어 스타일 상속
                if (cloned->layerName() == "0" || cloned->layerName().isEmpty())
                    applyLayerStyle(cloned, ctx.data.layer, ctx.scene);
                clonedItems.append(cloned);
            }
        }

        // ATTRIB는 외부 parseEntityList 루프가 파싱하므로 여기서는 별도 처리 불필요
        // → finalizeAndCreate(ATTRIB) 시점에 마지막 BlockItem을 업데이트함

        CadItem* blockItem = dynamic_cast<CadItem*>(
            ctx.factory.createBlock(clonedItems,
                                    MyVector(ctx.data.x1, ctx.data.y1),
                                    ctx.data.blockName,
                                    ctx.data.scaleX,
                                    ctx.data.scaleY,
                                    ctx.data.rotation));
        if (blockItem) {
            applyLayerStyle(blockItem, ctx.data.layer, ctx.scene);
            ctx.collectedItems.append(blockItem);
        } else {
            for (int i = 0; i < clonedItems.size(); ++i) delete clonedItems[i];
        }

    } else if (ctx.data.type == "HATCH") {
        // 마지막 경계 루프 플러시
        // (폴리라인과 엣지를 독립적으로 확인하여 비정상 DXF도 안전하게 처리)
        if (!ctx.data.currentHatchLoop.isEmpty()) {
            ctx.data.hatchPaths.append(
                buildPolylinePath(ctx.data.currentHatchLoop,
                                  ctx.data.currentHatchBulge));
            ctx.data.hatchLoopTypes.append(ctx.data.currentHatchLoopType);
            ctx.data.currentHatchLoop.clear();
            ctx.data.currentHatchBulge.clear();
        }
        if (!ctx.data.currentHatchEdges.isEmpty()) {
            ctx.data.hatchPaths.append(
                buildHatchEdgePath(ctx.data.currentHatchEdges));
            ctx.data.hatchLoopTypes.append(ctx.data.currentHatchLoopType);
            ctx.data.currentHatchEdges.clear();
        }

        // 모든 경계 루프를 하나의 QPainterPath로 합산 후 HatchItem 생성
        QPainterPath currentPath;
        currentPath.setFillRule(Qt::OddEvenFill);
        for (int i = 0; i < ctx.data.hatchPaths.size(); ++i)
            currentPath.addPath(ctx.data.hatchPaths[i]);

        // 구 createHatchItem 람다 → 멤버 함수 호출
        createHatchItem(currentPath, ctx.data, ctx.collectedItems, ctx.scene);

    } else {
        // 나머지 모든 엔티티 타입 (LINE, CIRCLE, ARC, TEXT 등)
        CadItem* item = createEntityFromData(ctx.data, ctx.factory, ctx.scene);
        if (item) {
            if (!ctx.data.handle.isEmpty())
                item->setData(0, ctx.data.handle);
            if (!ctx.data.associatedHandle.isEmpty())
                item->setData(1, ctx.data.associatedHandle);
            // 다중선 그룹 태그 복원 (XDATA → QGraphicsItem::setData)
            if (!ctx.data.mlGroupId.isEmpty())
                item->setData(ML_DATA_GROUP_ID, ctx.data.mlGroupId);
            if (!ctx.data.mlRole.isEmpty())
                item->setData(ML_DATA_ROLE, ctx.data.mlRole);
            if (ctx.data.mlSpacing > 0.0)
                item->setData(ML_DATA_SPACING, ctx.data.mlSpacing);
            ctx.collectedItems.append(item);
        }
    }

    // 다음 엔티티를 위해 상태 초기화
    ctx.data.reset();
    ctx.currentXDataApp = 0;
    ctx.inDStyle        = false;
    ctx.currentDimVar   = -1;
    ctx.inAppData       = false;
    ctx.skipEntity      = false;
}


void DxfManager::parseBlocksSection(QTextStream& in, MyGraphicsScene* scene) {
    if (!scene) return;
    
    // seek()을 사용하여 블록 섹션 내의 엔티티들을 건너뛰며 필요한 블록 정보만 수집
    while (!in.atEnd()) {
        QString code = in.readLine().trimmed();
        QString value = in.readLine().trimmed();
        
        if (code == "0" && value == "ENDSEC") {
            break;
        }
        
        if (code == "0" && value == "BLOCK") {
            QString blockName;
            MyVector basePoint(0, 0);
            
            // 패턴 이름 ??
            while (!in.atEnd()) {
                QString c = in.readLine().trimmed();
                QString v = in.readLine().trimmed();
                
                if (c == "0") {
                    // 섹션 내 첫 번째 개체 유형 확인
                    if (v == "ENDBLK") {
                        m_blockDefinitions[blockName] = QList<CadItem*>();
                        break;
                    } else {
                        // 패턴 이름? ?? ?? (ENDBLK? ?? ???)
                        QList<CadItem*> blockItems = parseEntityList(in, scene, {"ENDBLK"}, v);
                        
                        // Base Point를 기준으로 상대 좌표 변환 준비
                        if (basePoint.magnitude() > 1e-6) {
                            for (int j = 0; j < blockItems.size(); ++j) {
                                blockItems[j]->move(basePoint * -1.0);
                            }
                        }
                        m_blockDefinitions[blockName] = blockItems;
                        break; // 패턴 이름 ?? ??, ?? 0 BLOCK ??? ??
                    }
                } else if (c == "2" && blockName.isEmpty()) {
                    // 패턴 이름 (?? 2? ?? ??? ? ???? ?? ?? ??)
                    blockName = v;
                } else if (c == "10") {
                    basePoint.x = v.toDouble();
                } else if (c == "20") {
                    basePoint.y = v.toDouble();
                }
            }
        }
    }
}


void DxfManager::parseEntitiesSection(QTextStream& in, MyGraphicsScene* scene) {
    if (!scene) return;
    QList<CadItem*> entities = parseEntityList(in, scene, {"ENDSEC"});

    // [Phase 8] inch DXF: 파일 좌표를 mm 모델 좌표계로 환산 (원점 기준 ×25.4).
    //   postProcessEntities(리더/GDT 기하 근접 매칭) 전에 적용해 이후 로직이
    //   모두 최종 좌표를 보게 한다.
    if (m_importUnitFactor != 1.0) {
        for (int i = 0; i < entities.size(); ++i)
            if (entities[i])
                entities[i]->scale(MyVector(0.0, 0.0), m_importUnitFactor);
    }

    // 치수선 위치 ????? ??? ?? ?? (??? ?? ?)
    postProcessEntities(entities, scene);
    
    // C++11 range-loop detach ??? ?? ??? ??
    // for (int i = 0; i < entities.size(); ++i) {
    //     CadItem* item = entities[i];
    //     QGraphicsItem* root = scene->getLayerRoot(item->layerName());
    //     item->setParentItem(root);
    // }
// C++11 range-loop detach ??? ?? ??? ??
    for (int i = 0; i < entities.size(); ++i) {
        CadItem* item = entities[i];
        QGraphicsItem* root = scene->getLayerRoot(item->layerName());
        
        // --- [메모리 관리] ---
        if (root) {
            item->setParentItem(root);
        } else {
            scene->addItem(item); // 테이블 항목별 파싱 ???? ?? ??
        }
        // -------------------
    }
}

void DxfManager::postProcessEntities(QList<CadItem*>& entities, MyGraphicsScene* scene) {
    Q_UNUSED(scene);

    // att1 블록은 표면 거칠기 기호(BlockItem)로 그대로 표시한다.
    // (이전에 DatumItem으로 잘못 변환하던 로직은 제거됨)

    QMap<QString, GdtItem*> gdtMap;
    QMap<QString, DatumItem*> datumMap;
    QMap<QString, CadItem*> textMap; // TextItem/MTextItem 저장 맵
    
    for (int i = 0; i < entities.size(); ++i) {
        QString handle = entities[i]->data(0).toString();
        if (handle.isEmpty()) continue;
        
        if (GdtItem* gdt = dynamic_cast<GdtItem*>(entities[i])) {
            gdtMap[handle] = gdt;
        } else if (DatumItem* datum = dynamic_cast<DatumItem*>(entities[i])) {
            datumMap[handle] = datum;
        } else {
            // TextItem="Content", MTextItem/DimensionItem="Text" 모두 수집
            QMap<QString, QVariant> props = entities[i]->getProperties();
            if (props.contains("Text") || props.contains("Content")) {
                textMap[handle] = entities[i];
            }
        }
    }
    
    QList<CadItem*> itemsToRemove;
    for (int i = 0; i < entities.size(); ++i) {
        if (LeaderItem* leader = dynamic_cast<LeaderItem*>(entities[i])) {
            QString assoc = leader->data(1).toString();
            
            GdtItem* targetGdt = nullptr;
            DatumItem* targetDatum = nullptr;
            CadItem* targetText = nullptr;

            if (!assoc.isEmpty()) {
                if (gdtMap.contains(assoc)) targetGdt = gdtMap[assoc];
                else if (datumMap.contains(assoc)) targetDatum = datumMap[assoc];
                else if (textMap.contains(assoc)) targetText = textMap[assoc];
            }

            // [Geometry Fallback - 1차]
            // DWG annotHandle가 상대 오프셋으로 저장된 경우 핸들 매칭이 실패한다.
            // 리더선 마지막 정점(landing 끝)과 확장 bounding box로 연관 객체를 찾는다.
            // minDist는 여러 프레임이 겹칠 때 가장 가까운 것을 선택하는 타이브레이커다.
            if (!targetGdt && !targetDatum && !targetText) {
                QList<QPointF> gripPts = leader->getGripPoints();
                if (!gripPts.isEmpty()) {
                    QPointF leaderEnd = gripPts.last();
                    double minDist = 1e9; // r.contains()가 주 필터; 이 값은 타이브레이커

                    // GDT 검사 (bounding box 기반)
                    for (auto it = gdtMap.begin(); it != gdtMap.end(); ++it) {
                        QRectF r = it.value()->sceneBoundingRect().adjusted(-20, -20, 20, 20);
                        if (r.contains(leaderEnd)) {
                            double d = QLineF(r.center(), leaderEnd).length();
                            if (d < minDist) { minDist = d; targetGdt = it.value(); }
                        }
                    }
                    if (!targetGdt) {
                        minDist = 1e9;
                        // Datum 검사 (bounding box 기반)
                        for (auto it = datumMap.begin(); it != datumMap.end(); ++it) {
                            QRectF r = it.value()->sceneBoundingRect().adjusted(-20, -20, 20, 20);
                            if (r.contains(leaderEnd)) {
                                double d = QLineF(r.center(), leaderEnd).length();
                                if (d < minDist) { minDist = d; targetDatum = it.value(); }
                            }
                        }
                    }
                    if (!targetGdt && !targetDatum) {
                        minDist = 1e9;
                        // Text 검사 (bounding box 기반)
                        for (auto it = textMap.begin(); it != textMap.end(); ++it) {
                            QRectF r = it.value()->sceneBoundingRect().adjusted(-20, -20, 20, 20);
                            if (r.contains(leaderEnd)) {
                                double d = QLineF(r.center(), leaderEnd).length();
                                if (d < minDist) { minDist = d; targetText = it.value(); }
                            }
                        }
                    }
                }
            }

            // [Geometry Fallback - 2차]
            // DWG에서 TOLERANCE 삽입점과 LEADER 마지막 정점 간의 거리가
            // bounding box 확장(20) 범위를 초과하는 경우를 대비한 보완 로직.
            // LEADER의 모든 정점과 각 GDT의 position 사이의 최소 거리로 매칭한다.
            // 임계값(50 유닛): 기하공차 박스 너비는 일반적으로 20~40 유닛이므로 충분한 여유.
            if (!targetGdt && !targetDatum && !targetText) {
                QList<QPointF> gripPts = leader->getGripPoints();
                if (!gripPts.isEmpty()) {
                    const double kMaxMatchDist = 50.0;
                    double minDistGdt  = kMaxMatchDist;

                    // GDT: gdtData().position을 직접 사용하여 정확도 향상
                    for (auto it = gdtMap.begin(); it != gdtMap.end(); ++it) {
                        QPointF gdtPos = it.value()->gdtData().position.toPointF();
                        for (int gi = 0; gi < gripPts.size(); ++gi) {
                            double d = QLineF(gdtPos, gripPts[gi]).length();
                            if (d < minDistGdt) {
                                minDistGdt = d;
                                targetGdt  = it.value();
                            }
                        }
                    }

                    // GDT 2차 fallback 실패 시 Datum 검사
                    if (!targetGdt) {
                        double minDistDatum = kMaxMatchDist;
                        for (auto it = datumMap.begin(); it != datumMap.end(); ++it) {
                            QPointF dtPos = it.value()->datumData().startPoint.toPointF();
                            for (int gi = 0; gi < gripPts.size(); ++gi) {
                                double d = QLineF(dtPos, gripPts[gi]).length();
                                if (d < minDistDatum) {
                                    minDistDatum  = d;
                                    targetDatum   = it.value();
                                }
                            }
                        }
                    }

                    // annotHandle=0(parseDwg 부분 실패)로 핸들 매칭 불가 시 Text 근접 검사
                    // LEADER 화살촉(첫 정점)으로부터 가장 가까운 TextItem/MTextItem을 연결.
                    // 임계값(300 유닛): 지시선은 텍스트와 멀리 떨어질 수 있으므로 넉넉히.
                    if (!targetGdt && !targetDatum && assoc.isEmpty()) {
                        QPointF arrowPt = gripPts.first();
                        double minDistText = 300.0;
                        for (auto it = textMap.begin(); it != textMap.end(); ++it) {
                            QPointF tPos = it.value()->pos();
                            double d = QLineF(arrowPt, tPos).length();
                            if (d < minDistText) {
                                minDistText = d;
                                targetText  = it.value();
                            }
                        }
                    }
                }
            }

            // 1) GDT에 리더선 연결
            if (targetGdt) {
                QList<QPointF> gripPts = leader->getGripPoints();
                QList<MyVector> newPts;
                for (int j = 0; j < gripPts.size(); ++j) {
                    newPts.append(MyVector(gripPts[j]));
                }
                targetGdt->setLeaderPoints(newPts, false); // false로 해야 회전(0) 유지됨
                itemsToRemove.append(leader);
            }
            // 1.1) 데이텀에 리더선 연결
            else if (targetDatum) {
                QList<QPointF> gripPts = leader->getGripPoints();
                QList<MyVector> newPts;
                for (int j = 0; j < gripPts.size(); ++j) {
                    newPts.append(MyVector(gripPts[j]));
                }
                
                // 데이텀 지시선은 수직 또는 수평으로 일직선이어야 하므로 축 정렬을 강제한다.
                if (newPts.size() >= 2) {
                    MyVector first = newPts.first();
                    MyVector last  = newPts.last();
                    double dx = std::abs(last.x - first.x);
                    double dy = std::abs(last.y - first.y);
                    if (dy > dx) {
                        last.x = first.x;
                    } else {
                        last.y = first.y;
                    }

                    // [신규] 화살촉과 박스 간의 최소 거리 확보 로직
                    double bS = targetDatum->datumData().textHeight;
                    double arrowSize = bS * 1.5;
                    double triHeight = arrowSize * std::sqrt(3.0) / 2.0;
                    double boxSize = 2.2 * bS;
                    double minAllowedDist = triHeight + (boxSize / 2.0) + (bS * 1.5);
                    
                    MyVector dir = last - first;
                    double curL = dir.magnitude();
                    if (curL > 1e-6) {
                        dir = dir.normalized();
                        if (curL < minAllowedDist) {
                            last = first + dir * minAllowedDist;
                        }
                    } else {
                        last = first + MyVector(0, minAllowedDist);
                    }
                    
                    newPts[0] = first;
                    newPts[newPts.size() - 1] = last;
                }

                targetDatum->setLeaderPoints(newPts, true); // true로 전달하여 startPoint/endPoint 동기화
                itemsToRemove.append(leader);
            }
            // 2) 일반 텍스트에 리더선 연결
            else if (targetText) {
                QList<QPointF> gripPts = leader->getGripPoints();
                if (!gripPts.isEmpty()) {
                    // 텍스트 삽입점을 지시선 마지막 점(shoulder start)으로 교정한다.
                    // DWG LEADER는 화살촉 근방 정점만 저장하고 텍스트까지의 경로를
                    // AutoCAD가 동적으로 계산하므로, 텍스트 위치를 마지막 정점으로
                    // 대체해 전체 대각선 경로가 렌더링되도록 한다.
                    QPointF textPos = targetText->pos();
                    QPointF leaderEnd = gripPts.last();
                    const double kSnapDist = 5.0; // 이미 근접한 경우 교정 불필요
                    if (QLineF(textPos, leaderEnd).length() > kSnapDist) {
                        leader->applyGripMove(gripPts.size() - 1,
                                              MyVector(textPos.x(), textPos.y()));
                    }
                }
                // MTEXT는 서식 보존을 위해 별도 렌더링하므로 병합하지 않는다.
            }
        }
    }
    
    // [참고] 정렬 치수 좌표 최종 확인 (TOLERANCE를 이용해 다차원 치수 위치 정밀 조정)
    QList<DatumItem*> datumList = datumMap.values();
    for (int i = 0; i < datumList.size(); ++i) {
        for (int j = i + 1; j < datumList.size(); ++j) {
            DatumItem* d1 = datumList[i];
            DatumItem* d2 = datumList[j];
            if (itemsToRemove.contains(d1) || itemsToRemove.contains(d2)) continue;

            // 치수선 위치? ?? ?? ??(endPoint)? ?? ??? ?? ???? ??
            if (d1->datumData().text == d2->datumData().text) {
                if (d1->datumData().endPoint.distanceTo(d2->datumData().endPoint) < 1e-3) {
                    // 정점 개수 정보? ? ???(???? ??) ???? ??
                    if (d1->datumData().leaderPoints.size() >= d2->datumData().leaderPoints.size()) {
                        itemsToRemove.append(d2);
                    } else {
                        itemsToRemove.append(d1);
                    }
                }
            }
        }
    }

    // [신규] DatumItem과 GdtItem 간의 정밀 밀착 보정
    QList<GdtItem*> gdtList = gdtMap.values();
    for (int i = 0; i < datumList.size(); ++i) {
        DatumItem* datum = datumList[i];
        if (itemsToRemove.contains(datum)) continue;
        
        MyVector pStart = datum->datumData().startPoint;
        GdtItem* nearestGdt = nullptr;
        double minDist = 15.0; // 허용 반경 15.0 이내
        
        for (int j = 0; j < gdtList.size(); ++j) {
            GdtItem* gdt = gdtList[j];
            if (itemsToRemove.contains(gdt)) continue;
            
            QRectF gdtRect = gdt->sceneBoundingRect();
            if (gdtRect.adjusted(-15, -15, 15, 15).contains(pStart.toPointF())) {
                double d = QLineF(gdtRect.center(), pStart.toPointF()).length();
                if (d < minDist) {
                    minDist = d;
                    nearestGdt = gdt;
                }
            }
        }
        
        if (nearestGdt) {
            QRectF gdtRect = nearestGdt->sceneBoundingRect();
            // setupBottomLeftOrigin()에 의해 Y가 위로 갈수록 증가함.
            // QRectF의 top()은 Y가 작은 쪽(아래), bottom()은 Y가 큰 쪽(위).
            double topY = gdtRect.bottom();
            double bottomY = gdtRect.top();
            
            QList<MyVector> pts = datum->datumData().leaderPoints;
            if (pts.isEmpty()) pts << datum->datumData().startPoint << datum->datumData().endPoint;
            
            if (pts.size() >= 2) {
                double distToTop = std::abs(pts.first().y - topY);
                double distToBottom = std::abs(pts.first().y - bottomY);
                
                if (distToTop < distToBottom) {
                    pts[0].y = topY;
                } else {
                    pts[0].y = bottomY;
                }
                
                // 수직선 정렬 유지
                if (std::abs(pts.last().x - pts.first().x) < 5.0) {
                    pts.last().x = pts.first().x;
                }
                
                datum->setLeaderPoints(pts, true);
            }
        }
    }

    for (int i = 0; i < itemsToRemove.size(); ++i) {
        entities.removeAll(itemsToRemove[i]);
        delete itemsToRemove[i];
    }
}

// Windows ANSI 코드페이지 번호 → AutoCAD $DWGCODEPAGE 이름 변환
// 알 수 없는 코드페이지는 ANSI_1252(서유럽)를 기본값으로 반환한다.
static QString codepageForSystem()
{
#ifdef Q_OS_WIN
    UINT cp = GetACP();
    switch (cp) {
    case 932:  return "ANSI_932";  // 일본어
    case 936:  return "ANSI_936";  // 중국어 간체
    case 949:  return "ANSI_949";  // 한국어
    case 950:  return "ANSI_950";  // 중국어 번체
    case 1250: return "ANSI_1250"; // 중앙유럽
    case 1251: return "ANSI_1251"; // 키릴 문자
    case 1252: return "ANSI_1252"; // 서유럽 (기본)
    case 1253: return "ANSI_1253"; // 그리스어
    case 1254: return "ANSI_1254"; // 터키어
    case 1255: return "ANSI_1255"; // 히브리어
    case 1256: return "ANSI_1256"; // 아랍어
    default:   return QString("ANSI_%1").arg(cp);
    }
#else
    return "ANSI_949"; // 비-Windows 환경 기본값
#endif
}

QString DxfManager::getNextHandle() {
    // 접두사 'A'를 붙여 0x101과 같은 낮은 번호와의 충돌을 원천 차단
    return QString("A%1").arg(m_nextHandle++, 4, 16, QChar('0')).toUpper();
}

QString DxfManager::allocateHandle() {
    return getNextHandle();
}

QString DxfManager::allocateDimBlockName() {
    // TABLES/BLOCKS 섹션에 선언된 *D1…*Dn 이름을 순서대로 반환한다.
    // ENTITIES 섹션의 각 DIMENSION 엔티티가 호출하여 고유 블록 이름을 얻는다.
    return QString("*D%1").arg(++m_dimBlockAllocIdx);
}

// AC1015 엔티티 공통 헤더: 0=타입, 5=핸들, 100 AcDbEntity, 8=레이어
void DxfManager::writeEntityAcDbHeader(QTextStream& out,
                                        const QString& entityType,
                                        const QString& layerName)
{
    writeGroupCode(out, 0,   entityType);
    writeGroupCode(out, 5,   getNextHandle());
    // gc330: 엔티티 소유자 — *Model_Space BLOCK_RECORD 핸들을 가리킨다.
    // 이 값이 없으면 AutoCAD가 엔티티를 모델 스페이스로 귀속하지 못한다.
    if (!m_modelSpaceHandle.isEmpty())
        writeGroupCode(out, 330, m_modelSpaceHandle);
    writeGroupCode(out, 100, QString("AcDbEntity"));
    writeGroupCode(out, 8,   toDxfLayerName(layerName.isEmpty() ? QStringLiteral("0") : layerName));
}

void DxfManager::writeClassesSection(QTextStream& out) {
    writeGroupCode(out, 0, "SECTION");
    writeGroupCode(out, 2, "CLASSES");
    writeGroupCode(out, 0, "ENDSEC");
}

void DxfManager::writeHeaderSection(QTextStream& out, MyGraphicsScene* scene) {
    writeGroupCode(out, 0, "SECTION");
    writeGroupCode(out, 2, "HEADER");

    // AC1015 = AutoCAD 2000 (MTEXT, MULTILEADER 등 지원 최소 버전)
    writeGroupCode(out, 9, "$ACADVER");
    writeGroupCode(out, 1, "AC1015");

    // 파일 인코딩 코드페이지: 시스템 ANSI 코드페이지와 일치해야 한다.
    // 한국어 Windows = ANSI_949, 서유럽 = ANSI_1252
    writeGroupCode(out, 9, "$DWGCODEPAGE");
    writeGroupCode(out, 3, codepageForSystem());

    // 핸들 시드 (다음에 할당할 핸들값. 파일 내 최대 핸들보다 커야 함)
    writeGroupCode(out, 9, "$HANDSEED");
    writeGroupCode(out, 5, "FFFFFF"); 

    // 삽입 기준점 (0,0,0)
    writeGroupCode(out, 9,  "$INSBASE");
    writeGroupCode(out, 10, 0.0);
    writeGroupCode(out, 20, 0.0);
    writeGroupCode(out, 30, 0.0);

    // 도면 범위: 씬의 bounding rect를 사용한다.
    // AutoCAD는 $EXTMIN/$EXTMAX 없이는 ZOOM EXTENTS가 동작하지 않는다.
    QRectF ext = scene ? scene->itemsBoundingRect() : QRectF(0, 0, 594, 420);
    if (ext.isNull() || ext.isEmpty()) ext = QRectF(0, 0, 594, 420);

    writeGroupCode(out, 9,  "$EXTMIN");
    writeGroupCode(out, 10, ext.left());
    writeGroupCode(out, 20, ext.top());
    writeGroupCode(out, 30, 0.0);

    writeGroupCode(out, 9,  "$EXTMAX");
    writeGroupCode(out, 10, ext.right());
    writeGroupCode(out, 20, ext.bottom());
    writeGroupCode(out, 30, 0.0);

    // 도면 한계 (용지 크기)
    writeGroupCode(out, 9,  "$LIMMIN");
    writeGroupCode(out, 10, 0.0);
    writeGroupCode(out, 20, 0.0);

    writeGroupCode(out, 9,  "$LIMMAX");
    writeGroupCode(out, 10, ext.right());
    writeGroupCode(out, 20, ext.bottom());

    // 삽입 단위: 4 = 밀리미터. AutoDraft 모델 좌표는 항상 mm 로 저장·출력하므로
    // 도면이 inch 표시 모드여도 지오메트리 자체는 mm 크기로 내보낸다(물리 크기 보존).
    writeGroupCode(out, 9,  "$INSUNITS");
    writeGroupCode(out, 70, QString::number(4));

    // 측정 단위: 1 = 미터법
    writeGroupCode(out, 9,  "$MEASUREMENT");
    writeGroupCode(out, 70, QString::number(1));

    writeGroupCode(out, 0, "ENDSEC");
}

// ============================================================
// QColor → ACI 색상 인덱스 역변환
//   임포트의 aciToColor()와 대칭 관계.
//   표준 9색 이외의 색은 RGB 거리로 가장 가까운 ACI를 선택한다.
// ============================================================
int DxfManager::colorToAci(const QColor& color)
{
    // ACI 1~9의 기준 색상 테이블 (aciToColor와 동일한 매핑)
    static const struct { int aci; int r, g, b; } kAciTable[] = {
        { 1, 255,   0,   0 }, // red
        { 2, 255, 255,   0 }, // yellow
        { 3,   0, 255,   0 }, // green
        { 4,   0, 255, 255 }, // cyan
        { 5,   0,   0, 255 }, // blue
        { 6, 255,   0, 255 }, // magenta
        { 7, 255, 255, 255 }, // white (또는 black — 화면 테마 무관하게 7로 저장)
        { 8, 128, 128, 128 }, // dark gray
        { 9, 192, 192, 192 }, // light gray
    };

    int   bestAci  = 7;      // 매칭 실패 시 기본값 white
    long  bestDist = LONG_MAX;

    int cr = color.red(), cg = color.green(), cb = color.blue();
    for (const auto& entry : kAciTable) {
        long dr = cr - entry.r;
        long dg = cg - entry.g;
        long db = cb - entry.b;
        long dist = dr*dr + dg*dg + db*db; // 유클리드 거리² (RGB 공간)
        if (dist < bestDist) {
            bestDist = dist;
            bestAci  = entry.aci;
            if (dist == 0) break; // 완전 일치면 조기 종료
        }
    }
    return bestAci;
}

// ============================================================
// TABLES 섹션 작성
//   AC1015 필수 9개 테이블을 표준 순서로 출력한다.
//   순서: VPORT → LTYPE → LAYER → STYLE → VIEW → UCS → APPID → DIMSTYLE → BLOCK_RECORD
// ============================================================
void DxfManager::writeTablesSection(QTextStream& out, MyGraphicsScene* scene)
{
    if (!scene || !scene->context()) return;

    LayerManager*    lm  = scene->context()->layerManager();
    LineTypeManager* ltm = scene->context()->lineTypeManager();

    QRectF ext = scene->itemsBoundingRect();
    if (ext.isNull() || ext.isEmpty()) ext = QRectF(0, 0, 594, 420);

    writeGroupCode(out, 0, "SECTION");
    writeGroupCode(out, 2, "TABLES");

    // ==========================================================
    // 1. VPORT — 활성 뷰포트
    // ==========================================================
    {
        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "VPORT");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  1); // 항목 수

        writeGroupCode(out, 0,   "VPORT");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbViewportTableRecord");
        writeGroupCode(out, 2,   "*ACTIVE");
        writeGroupCode(out, 70,  0);
        writeGroupCode(out, 10,  0.0); // 뷰 하단-좌측 X
        writeGroupCode(out, 20,  0.0); // 뷰 하단-좌측 Y
        writeGroupCode(out, 11,  1.0); // 뷰 상단-우측 X
        writeGroupCode(out, 21,  1.0); // 뷰 상단-우측 Y
        writeGroupCode(out, 12,  ext.center().x()); // 뷰 중심 X
        writeGroupCode(out, 22,  ext.center().y()); // 뷰 중심 Y
        writeGroupCode(out, 40,  ext.height() > 0 ? ext.height() : 420.0); // 뷰 높이
        writeGroupCode(out, 41,  ext.width() / (ext.height() > 0 ? ext.height() : 420.0)); // 가로세로비

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 2. LTYPE — 선종류
    // ==========================================================
    {
        QList<LineType> linetypes = ltm ? ltm->getAllLinetypes() : QList<LineType>();

        // 사용자 선종류 중 기본 3종 중복 제거
        QList<LineType> userLtypes;
        for (int i = 0; i < linetypes.size(); ++i) {
            QString u = linetypes[i].name.toUpper();
            if (u != "BYLAYER" && u != "BYBLOCK" && u != "CONTINUOUS")
                userLtypes.append(linetypes[i]);
        }

        // AC1015 필수: ByLayer(0) + ByBlock(0) + Continuous(64) + 사용자 선종류(64)
        int totalCount = 3 + userLtypes.size();

        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "LTYPE");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  totalCount);

        // ByLayer — gc70=0, 패턴 없음
        writeGroupCode(out, 0,   "LTYPE");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbLinetypeTableRecord");
        writeGroupCode(out, 2,   "ByLayer");
        writeGroupCode(out, 70,  0);
        writeGroupCode(out, 3,   "");
        writeGroupCode(out, 72,  65);
        writeGroupCode(out, 73,  0);
        writeGroupCode(out, 40,  0.0);

        // ByBlock — gc70=0, 패턴 없음
        writeGroupCode(out, 0,   "LTYPE");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbLinetypeTableRecord");
        writeGroupCode(out, 2,   "ByBlock");
        writeGroupCode(out, 70,  0);
        writeGroupCode(out, 3,   "");
        writeGroupCode(out, 72,  65);
        writeGroupCode(out, 73,  0);
        writeGroupCode(out, 40,  0.0);

        // Continuous — gc70=64, 패턴 없음 (실선)
        writeGroupCode(out, 0,   "LTYPE");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbLinetypeTableRecord");
        writeGroupCode(out, 2,   "Continuous");
        writeGroupCode(out, 70,  64);
        writeGroupCode(out, 3,   "Solid line");
        writeGroupCode(out, 72,  65);
        writeGroupCode(out, 73,  0);
        writeGroupCode(out, 40,  0.0);

        // 사용자 정의 선종류
        for (int i = 0; i < userLtypes.size(); ++i) {
            const LineType& lt = userLtypes[i];

            QList<double> elements;
            if (!lt.pattern.isEmpty()) {
                const QStringList parts = lt.pattern.split(',');
                for (int k = 0; k < parts.size(); ++k) {
                    bool ok = false;
                    double v = parts[k].trimmed().toDouble(&ok);
                    if (ok) elements.append(v);
                }
            }
            double totalLen = 0.0;
            for (int k = 0; k < elements.size(); ++k) totalLen += std::abs(elements[k]);

            writeGroupCode(out, 0,   "LTYPE");
            writeGroupCode(out, 5,   getNextHandle());
            writeGroupCode(out, 330, tblH);
            writeGroupCode(out, 100, "AcDbSymbolTableRecord");
            writeGroupCode(out, 100, "AcDbLinetypeTableRecord");
            writeGroupCode(out, 2,   lt.name);
            writeGroupCode(out, 70,  64);
            writeGroupCode(out, 3,   lt.name);
            writeGroupCode(out, 72,  65);
            writeGroupCode(out, 73,  elements.size());
            writeGroupCode(out, 40,  totalLen);
            for (int j = 0; j < elements.size(); ++j) {
                writeGroupCode(out, 49, elements[j]);
                writeGroupCode(out, 74, 0);
                writeGroupCode(out, 75, 0);
            }
        }

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 3. LAYER — 레이어
    // ==========================================================
    {
        QList<Layer> layers = lm ? lm->getAllLayers() : QList<Layer>();

        // 씬 엔티티가 참조하지만 LayerManager에 없는 레이어를 보완한다.
        // LAYER TABLE에 누락된 레이어가 있으면 AutoCAD가 해당 엔티티를 레이어 "0"으로
        // 강제 재배치하므로, 씬을 스캔하여 참조 레이어를 모두 수집한다.
        if (scene) {
            QSet<QString> lmNames;
            for (int i = 0; i < layers.size(); ++i)
                lmNames.insert(layers[i].name());

            QList<QGraphicsItem*> sceneItems = scene->items();
            for (int i = 0; i < sceneItems.size(); ++i) {
                CadItem* ci = dynamic_cast<CadItem*>(sceneItems[i]);
                if (!ci) continue;
                QString ln = ci->layerName();
                if (!ln.isEmpty() && ln != "Default" && !lmNames.contains(ln)) {
                    // LayerManager에 없는 레이어 → 기본 속성으로 보완
                    layers.append(Layer(ln, Qt::white, 0.0, "Continuous",
                                        false, true, true, false));
                    lmNames.insert(ln);
                }
            }
        }

        // 실제 작성될 고유 레이어 수를 먼저 계산한다 (gc70 과 실제 레코드 수가
        // 일치해야 AutoCAD가 테이블을 올바르게 읽는다).
        QSet<QString> uniqueLayerNames;
        for (int i = 0; i < layers.size(); ++i)
            uniqueLayerNames.insert(toDxfLayerName(layers[i].name()));

        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "LAYER");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  uniqueLayerNames.size());

        QSet<QString> writtenNames;
        for (int i = 0; i < layers.size(); ++i) {
            const Layer& layer = layers[i];
            QString dxfName = toDxfLayerName(layer.name());
            if (writtenNames.contains(dxfName)) continue;
            writtenNames.insert(dxfName);

            int flags    = 64;
            int aciColor = colorToAci(layer.color());
            if (!layer.isLayerOn()) aciColor = -aciColor;
            QString ltName = layer.linetype().isEmpty() ? "Continuous" : layer.linetype();
            int plotFlag = (layer.name().toUpper() == "DEFPOINTS") ? 0 : 1;

            writeGroupCode(out, 0,   "LAYER");
            writeGroupCode(out, 5,   getNextHandle());
            writeGroupCode(out, 330, tblH);
            writeGroupCode(out, 100, "AcDbSymbolTableRecord");
            writeGroupCode(out, 100, "AcDbLayerTableRecord");
            writeGroupCode(out, 2,   dxfName);
            writeGroupCode(out, 70,  flags);
            writeGroupCode(out, 62,  aciColor);
            writeGroupCode(out, 6,   ltName);
            writeGroupCode(out, 290, plotFlag);
            writeGroupCode(out, 370, -3);
            writeGroupCode(out, 390, m_normalPlotStyleHandle);
        }

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 4. STYLE — 텍스트 스타일 (도면 내 사용 폰트 감지)
    // ==========================================================
    QString standardStyleHandle;
    {
        // 도면 내에서 사용된 대표 폰트 검색 (GDT 등)
        QString representativeFont = "arial.ttf"; 
        if (scene) {
            QList<QGraphicsItem*> items = scene->items();
            for (int i = 0; i < items.size(); ++i) {
                GdtItem* gdt = dynamic_cast<GdtItem*>(items[i]);
                if (gdt && !gdt->gdtData().fontName.isEmpty()) {
                    QString fn = gdt->gdtData().fontName.toLower();
                    if (fn.contains("돋움") || fn.contains("dotum")) representativeFont = "dotum.ttf";
                    else if (fn.contains("굴림") || fn.contains("gulim")) representativeFont = "gulim.ttc";
                    else if (fn.contains("맑은") || fn.contains("malgun")) representativeFont = "malgun.ttf";
                    else if (fn.endsWith(".ttf") || fn.endsWith(".shx") || fn.endsWith(".ttc")) representativeFont = fn;
                    else representativeFont = fn + ".ttf";
                    break;
                }
            }
        }

        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "STYLE");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  1);

        standardStyleHandle = getNextHandle();
        writeGroupCode(out, 0,   "STYLE");
        writeGroupCode(out, 5,   standardStyleHandle);
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbTextStyleTableRecord");
        writeGroupCode(out, 2,   "Standard");
        writeGroupCode(out, 70,  0);
        writeGroupCode(out, 40,  0.0);
        writeGroupCode(out, 41,  1.0);
        writeGroupCode(out, 50,  0.0);
        writeGroupCode(out, 71,  0);
        writeGroupCode(out, 42,  2.5);
        writeGroupCode(out, 3,   representativeFont); 
        writeGroupCode(out, 4,   ""); 

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 5. VIEW — 명명된 뷰 (비어 있어도 테이블 자체는 필수)
    // ==========================================================
    {
        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "VIEW");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  0); // 항목 없음

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 6. UCS — 사용자 좌표계 (비어 있어도 테이블 자체는 필수)
    // ==========================================================
    {
        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "UCS");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  0);

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 7. APPID — 응용 프로그램 ID
    // ==========================================================
    {
        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "APPID");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  2);

        writeGroupCode(out, 0,   "APPID");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbRegAppTableRecord");
        writeGroupCode(out, 2,   "ACAD");
        writeGroupCode(out, 70,  0);

        writeGroupCode(out, 0,   "APPID");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbRegAppTableRecord");
        writeGroupCode(out, 2,   "AUTODRAFT");
        writeGroupCode(out, 70,  0);

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 8. DIMSTYLE — 치수 스타일 (STANDARD 하나만 출력)
    // ==========================================================
    {
        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "DIMSTYLE");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  1);
        writeGroupCode(out, 100, "AcDbDimStyleTable");
        writeGroupCode(out, 71,  0);

        writeGroupCode(out, 0,   "DIMSTYLE");
        writeGroupCode(out, 105, getNextHandle()); 
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbDimStyleTableRecord");
        writeGroupCode(out, 2,   "STANDARD");
        writeGroupCode(out, 70,  0);
        writeGroupCode(out, 3,   "");   // DIMPOST
        writeGroupCode(out, 4,   "");   // DIMAPOST
        writeGroupCode(out, 40,  1.0);  // DIMSCALE
        writeGroupCode(out, 41,  2.5);  // DIMASZ
        writeGroupCode(out, 42,  0.625);// DIMEXO
        writeGroupCode(out, 43,  3.75); // DIMDLI
        writeGroupCode(out, 44,  1.25); // DIMEXE
        writeGroupCode(out, 140, 2.5);  // DIMTXT
        writeGroupCode(out, 141, 2.5);  // DIMCEN
        writeGroupCode(out, 143, 0.03937); // DIMALTF
        writeGroupCode(out, 147, 0.625);// DIMGAP
        writeGroupCode(out, 73,  0);    // DIMTIH
        writeGroupCode(out, 74,  0);    // DIMTOH
        writeGroupCode(out, 77,  1);    // DIMTAD (1: Text above line)
        writeGroupCode(out, 78,  8);    // DIMZIN
        writeGroupCode(out, 171, 3);    // DIMALTU
        writeGroupCode(out, 172, 1);    // DIMTOFL
        writeGroupCode(out, 271, 2);    // DIMDEC
        writeGroupCode(out, 272, 2);    // DIMTDEC
        writeGroupCode(out, 274, 3);    // DIMALTTZ
        writeGroupCode(out, 275, 0);    // DIMADEC
        writeGroupCode(out, 280, 0);    // DIMJUST
        writeGroupCode(out, 281, 0);    // DIMSD1
        writeGroupCode(out, 282, 0);    // DIMSD2
        writeGroupCode(out, 283, 0);    // DIMTOL
        writeGroupCode(out, 284, 0);    // DIMLIM
        writeGroupCode(out, 285, 0);    // DIMSOXD
        writeGroupCode(out, 286, 0);    // DIMSAH
        writeGroupCode(out, 340, standardStyleHandle); // 텍스트 스타일 지정 (AC1015 필수)

        writeGroupCode(out, 0, "ENDTAB");
    }

    // ==========================================================
    // 9. BLOCK_RECORD — 블록 레코드 (AC1015 필수)
    //    m_referencedBlockNames: exportToDxf에서 사전 수집한 사용자 블록 이름 집합
    //    BLOCKS 섹션과 반드시 동일한 이름 집합을 출력해야 한다.
    // ==========================================================
    {
        int blockCount = 2 + m_dimBlockCount + m_referencedBlockNames.size();
        if (!m_referencedBlockNames.contains("_DatumFilled")) {
            blockCount++;
        }

        QString tblH = getNextHandle();
        writeGroupCode(out, 0,   "TABLE");
        writeGroupCode(out, 2,   "BLOCK_RECORD");
        writeGroupCode(out, 5,   tblH);
        writeGroupCode(out, 100, "AcDbSymbolTable");
        writeGroupCode(out, 70,  blockCount);

        // *Model_Space — 핸들을 멤버에 저장하여 엔티티 gc330에 사용한다.
        // m_blockRecordHandles에도 저장하여 writeBlocksSection에서 BLOCK/ENDBLK
        // 의 gc330 소유자 핸들을 올바르게 기록할 수 있도록 한다.
        m_modelSpaceHandle = getNextHandle();
        m_blockRecordHandles["*Model_Space"] = m_modelSpaceHandle;
        writeGroupCode(out, 0,   "BLOCK_RECORD");
        writeGroupCode(out, 5,   m_modelSpaceHandle);
        writeGroupCode(out, 330, tblH);
        writeGroupCode(out, 100, "AcDbSymbolTableRecord");
        writeGroupCode(out, 100, "AcDbBlockTableRecord");
        writeGroupCode(out, 2,   "*Model_Space");

        // *Paper_Space
        {
            // 핸들을 로컬 변수에 저장한 후 맵과 출력 스트림 양쪽에 기록한다.
            QString h = getNextHandle();
            m_blockRecordHandles["*Paper_Space"] = h;
            writeGroupCode(out, 0,   "BLOCK_RECORD");
            writeGroupCode(out, 5,   h);
            writeGroupCode(out, 330, tblH);
            writeGroupCode(out, 100, "AcDbSymbolTableRecord");
            writeGroupCode(out, 100, "AcDbBlockTableRecord");
            writeGroupCode(out, 2,   "*Paper_Space");
        }

        // ----------------------------------------------------------
        // gc2를 생략하여 익명 블록을 AutoCAD에 위임하므로,
        // 여기서 *D 블록 레코드를 생성하지 않습니다.
        // ----------------------------------------------------------
        // 사용자 정의 블록 (exportToDxf에서 수집된 집합 사용)
        QSetIterator<QString> sit(m_referencedBlockNames);
        while (sit.hasNext()) {
            const QString bName = sit.next();
            QString h = getNextHandle();
            m_blockRecordHandles[bName] = h;   // BLOCKS 섹션에서 330으로 사용
            writeGroupCode(out, 0,   "BLOCK_RECORD");
            writeGroupCode(out, 5,   h);
            writeGroupCode(out, 330, tblH);
            writeGroupCode(out, 100, "AcDbSymbolTableRecord");
            writeGroupCode(out, 100, "AcDbBlockTableRecord");
            writeGroupCode(out, 2,   bName);
        }

        // --- 데이텀 전용 화살촉 블록 레코드 (자동 생성 방어 로직) ---
        if (!m_referencedBlockNames.contains("_DatumFilled")) {
            QString h = getNextHandle();
            m_blockRecordHandles["_DatumFilled"] = h;
            m_datumArrowHandle = h; // LEADER XDATA DIMLDRBLK(341) 참조용 핸들 저장
            writeGroupCode(out, 0,   "BLOCK_RECORD");
            writeGroupCode(out, 5,   h);
            writeGroupCode(out, 330, tblH);
            writeGroupCode(out, 100, "AcDbSymbolTableRecord");
            writeGroupCode(out, 100, "AcDbBlockTableRecord");
            writeGroupCode(out, 2,   "_DatumFilled");
        } else {
            // 사용자 블록으로 이미 등록된 경우 핸들을 가져온다.
            m_datumArrowHandle = m_blockRecordHandles.value("_DatumFilled");
        }

        writeGroupCode(out, 0, "ENDTAB");
    }

    writeGroupCode(out, 0, "ENDSEC");
}

void DxfManager::writeObjectsSection(QTextStream& out) {
    writeGroupCode(out, 0, "SECTION");
    writeGroupCode(out, 2, "OBJECTS");

    // ----------------------------------------------------------
    // AC1015 OBJECTS 섹션 필수 구조 (참조 파일 기준):
    //
    //  (1) 루트 명명된 객체 딕셔너리 (DICTIONARY, 330=0)
    //        └─ "ACAD_PLOTSTYLENAME" → plotDictHandle
    //  (2) 플롯 스타일 딕셔너리 (ACDBDICTIONARYWDFLT, 330=rootHandle)
    //        └─ "Normal" → placeholderHandle
    //  (3) "Normal" 플롯 스타일 본체 (ACDBPLACEHOLDER, 330=plotDictHandle)
    //
    // 주의:
    //  - DICTIONARY / ACDBDICTIONARYWDFLT / ACDBPLACEHOLDER 에는
    //    "100 AcDbObject" 서브클래스 마커를 쓰지 않는다.
    //    참조 DXF 파일에서 이들은 100 AcDbDictionary 로 바로 시작한다.
    //  - 소유자 핸들(330)은 반드시 100 이전에 위치해야 한다.
    // ----------------------------------------------------------

    QString rootDictHandle = getNextHandle();
    QString plotDictHandle = getNextHandle();

    // (1) 루트 명명된 객체 딕셔너리
    writeGroupCode(out, 0,   "DICTIONARY");
    writeGroupCode(out, 5,   rootDictHandle);
    writeGroupCode(out, 330, 0);               // 소유자 없음 (문서 루트)
    writeGroupCode(out, 100, "AcDbDictionary");
    writeGroupCode(out, 281, 1);
    writeGroupCode(out, 3,   "ACAD_PLOTSTYLENAME");
    writeGroupCode(out, 350, plotDictHandle);

    // (2) 플롯 스타일 딕셔너리 with default
    writeGroupCode(out, 0,   "ACDBDICTIONARYWDFLT");
    writeGroupCode(out, 5,   plotDictHandle);
    writeGroupCode(out, 330, rootDictHandle);  // 소유자: 루트 딕셔너리
    writeGroupCode(out, 100, "AcDbDictionary");
    writeGroupCode(out, 281, 1);
    writeGroupCode(out, 3,   "Normal");
    writeGroupCode(out, 350, m_normalPlotStyleHandle);
    writeGroupCode(out, 100, "AcDbDictionaryWithDefault");
    writeGroupCode(out, 340, m_normalPlotStyleHandle);

    // (3) "Normal" 플롯 스타일 본체 — 서브클래스 마커 없이 소유자 핸들만 기록
    writeGroupCode(out, 0,   "ACDBPLACEHOLDER");
    writeGroupCode(out, 5,   m_normalPlotStyleHandle);
    writeGroupCode(out, 330, plotDictHandle);  // 소유자: 플롯 딕셔너리

    writeGroupCode(out, 0, "ENDSEC");
}



// ============================================================
// BLOCKS 섹션 작성
//   임포트 시 parseBlocksSection()이 읽는 블록 정의를 역으로 생성.
//   m_blockDefinitions가 비어 있어도 규격상 최소 Model/Paper Space
//   더미 블록이 필요하므로 항상 섹션 자체는 출력한다.
// ============================================================
void DxfManager::writeBlocksSection(QTextStream& out, MyGraphicsScene* scene)
{
    writeGroupCode(out, 0, "SECTION");
    writeGroupCode(out, 2, "BLOCKS");

    // ----------------------------------------------------------
    // DXF AC1015 필수 시스템 블록: *Model_Space
    // BLOCK 엔티티의 gc330: 대응하는 BLOCK_RECORD 핸들을 가리켜야 한다.
    // 이 값이 없으면 AutoCAD가 블록 정의를 BLOCK_RECORD와 연결하지 못한다.
    // ----------------------------------------------------------
    {
        // m_blockRecordHandles["*Model_Space"] = m_modelSpaceHandle 이므로 동일
        const QString& ownerH = m_modelSpaceHandle;
        writeGroupCode(out, 0,   "BLOCK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, ownerH);           // ← BLOCK_RECORD 핸들 (gc330)
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockBegin"));
        writeGroupCode(out, 2,   QString("*Model_Space"));
        writeGroupCode(out, 70,  QString("0"));
        writeGroupCode(out, 10,  0.0);
        writeGroupCode(out, 20,  0.0);
        writeGroupCode(out, 30,  0.0);
        writeGroupCode(out, 3,   QString("*Model_Space"));
        writeGroupCode(out, 1,   QString(""));
        writeGroupCode(out, 0,   "ENDBLK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, ownerH);           // ← ENDBLK 도 동일 BLOCK_RECORD 핸들
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockEnd"));
    }

    // ----------------------------------------------------------
    // DXF AC1015 필수 시스템 블록: *Paper_Space
    // ----------------------------------------------------------
    {
        const QString ownerH = m_blockRecordHandles.value("*Paper_Space");
        writeGroupCode(out, 0,   "BLOCK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, ownerH);           // ← BLOCK_RECORD 핸들 (gc330)
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockBegin"));
        writeGroupCode(out, 2,   QString("*Paper_Space"));
        writeGroupCode(out, 70,  QString("0"));
        writeGroupCode(out, 10,  0.0);
        writeGroupCode(out, 20,  0.0);
        writeGroupCode(out, 30,  0.0);
        writeGroupCode(out, 3,   QString("*Paper_Space"));
        writeGroupCode(out, 1,   QString(""));
        writeGroupCode(out, 0,   "ENDBLK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, ownerH);           // ← ENDBLK 도 동일 BLOCK_RECORD 핸들
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockEnd"));
    }

    // ----------------------------------------------------------
    // gc2를 생략하므로 여기에서 빈 *D 블록을 생성하지 않습니다.
    // AutoCAD가 파일을 열 때 치수 파라미터를 기반으로 블록을 완벽하게 자동 생성합니다.
    // ----------------------------------------------------------

    // ----------------------------------------------------------
    // 사용자 정의 블록: m_referencedBlockNames 집합 기준으로 출력
    // (exportToDxf에서 m_blockDefinitions + 씬 BlockItem 모두 수집해 놓음)
    // BLOCK_RECORD 테이블과 동일한 집합이므로 항상 일치가 보장된다.
    // ----------------------------------------------------------

    // 씬 BlockItem → blockName → CadItem 자식 목록 인덱스 (씬 스캔 결과)
    QMap<QString, QList<CadItem*>> sceneBlockItems;
    if (scene) {
        QList<QGraphicsItem*> allItems = scene->items();
        for (int i = 0; i < allItems.size(); ++i) {
            BlockItem* blk = dynamic_cast<BlockItem*>(allItems[i]);
            if (!blk || blk->blockName().isEmpty() || blk->blockName().startsWith('*')) continue;
            if (sceneBlockItems.contains(blk->blockName())) continue; // 첫 번째 인스턴스만 사용

            QList<CadItem*> children;
            QList<QGraphicsItem*> childItems = blk->childItems();
            for (int j = 0; j < childItems.size(); ++j) {
                CadItem* c = dynamic_cast<CadItem*>(childItems[j]);
                if (c) children.append(c);
            }
            sceneBlockItems.insert(blk->blockName(), children);
        }
    }

    QSetIterator<QString> sit(m_referencedBlockNames);
    while (sit.hasNext()) {
        const QString bName = sit.next();

        // 블록 내용 결정: m_blockDefinitions 우선, 없으면 씬 BlockItem 자식
        QList<CadItem*> blockItems;
        if (m_blockDefinitions.contains(bName))
            blockItems = m_blockDefinitions[bName];
        else if (sceneBlockItems.contains(bName))
            blockItems = sceneBlockItems[bName];

        // BLOCK/ENDBLK 의 gc330 = 해당 BLOCK_RECORD 핸들
        // 이 값이 없으면 AutoCAD가 블록 정의와 BLOCK_RECORD를 연결하지 못한다.
        const QString ownerH = m_blockRecordHandles.value(bName);
        writeGroupCode(out, 0,   "BLOCK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, ownerH);           // ← BLOCK_RECORD 핸들 (gc330)
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockBegin"));
        writeGroupCode(out, 2,   bName);
        writeGroupCode(out, 70,  QString("0"));
        writeGroupCode(out, 10,  0.0);
        writeGroupCode(out, 20,  0.0);
        writeGroupCode(out, 30,  0.0);
        writeGroupCode(out, 3,   bName);
        writeGroupCode(out, 1,   QString(""));

        for (int i = 0; i < blockItems.size(); ++i) {
            writeItemToDxf(out, blockItems[i]);
        }

        writeGroupCode(out, 0,   "ENDBLK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, ownerH);           // ← ENDBLK 도 동일 BLOCK_RECORD 핸들
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockEnd"));
    }

    // ----------------------------------------------------------
    // 데이텀 전용 화살촉 블록 정의 (_DatumFilled)
    // ----------------------------------------------------------
    if (!m_referencedBlockNames.contains("_DatumFilled")) {
        QString handle = m_blockRecordHandles.value("_DatumFilled");
        writeGroupCode(out, 0,   "BLOCK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, handle);
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockBegin"));
        writeGroupCode(out, 2,   QString("_DatumFilled"));
        writeGroupCode(out, 70,  QString("0"));
        writeGroupCode(out, 10,  0.0);
        writeGroupCode(out, 20,  0.0);
        writeGroupCode(out, 30,  0.0);
        writeGroupCode(out, 3,   QString("_DatumFilled"));
        writeGroupCode(out, 1,   QString(""));

        // 데이텀 삼각형 형상 (SOLID) — AutoCAD 참조 파일(_DatumFilled) 기준
        // 꼭짓점 배치: 밑변(폭 1.155)이 삽입점(x=0)에, 정점이 x=-1에 위치.
        // 블록 삽입 시 삽입점이 지시선 vertex[0](표면 접촉점)에 놓이고,
        // 정점(-1,0)이 지시선 방향으로 정렬 → 올바른 ISO 데이텀 삼각형 외관.
        writeGroupCode(out, 0,   "SOLID");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, handle);           // 소유자: _DatumFilled BLOCK_RECORD 핸들
        writeGroupCode(out, 100, "AcDbEntity");
        writeGroupCode(out, 8,   "0");
        writeGroupCode(out, 6,   "ByBlock");        // 선종류 ByBlock — 삽입 색상 상속
        writeGroupCode(out, 62,  0);                // 색상 ByBlock (0)
        writeGroupCode(out, 100, "AcDbTrace");
        writeGroupCode(out, 10,  0.0);              // 꼭짓점1: 밑변 상단 (x=0)
        writeGroupCode(out, 20,  0.5773502691896258);
        writeGroupCode(out, 30,  0.0);
        writeGroupCode(out, 11,  -1.0);             // 꼭짓점2: 정점 (x=-1, 지시선 방향)
        writeGroupCode(out, 21,  0.0);
        writeGroupCode(out, 31,  0.0);
        writeGroupCode(out, 12,  0.0);              // 꼭짓점3: 밑변 하단 (x=0)
        writeGroupCode(out, 22,  -0.5773502691896258);
        writeGroupCode(out, 32,  0.0);
        writeGroupCode(out, 13,  0.0);              // 꼭짓점4: 꼭짓점3과 동일 (3각형 처리)
        writeGroupCode(out, 23,  -0.5773502691896258);
        writeGroupCode(out, 33,  0.0);

        writeGroupCode(out, 0,   "ENDBLK");
        writeGroupCode(out, 5,   getNextHandle());
        writeGroupCode(out, 330, handle);
        writeGroupCode(out, 100, QString("AcDbEntity"));
        writeGroupCode(out, 8,   QString("0"));
        writeGroupCode(out, 100, QString("AcDbBlockEnd"));
    }

    writeGroupCode(out, 0, "ENDSEC");
}

void DxfManager::writeEntitiesSection(QTextStream& out, MyGraphicsScene* scene) {
    // ENTITIES 섹션에서 allocateDimBlockName()이 *D1부터 다시 시작하도록 카운터를 초기화한다.
    // BLOCKS 섹션에서 쓴 순서와 동일하게 매핑된다.
    // m_dimBlockAllocIdx = 0; // 중복 초기화 제거 (카운터가 BLOCKS 섹션에서 이어서 와야 함)

    writeGroupCode(out, 0, "SECTION");
    writeGroupCode(out, 2, "ENTITIES");

    QList<QGraphicsItem*> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem* baseItem = items[i];
        if (!baseItem) continue;

        CadItem* item = dynamic_cast<CadItem*>(baseItem);
        if (!item) continue;

        // 부모가 CadItem(= BlockItem 내부 구성요소)이면 INSERT 엔티티로 대표되므로 생략한다.
        // 레이어 루트는 CadItem이 아니므로, 레이어 루트의 직계 자식은 이 검사를 통과한다.
        if (dynamic_cast<CadItem*>(item->parentItem())) continue;

        writeItemToDxf(out, item);
    }

    writeGroupCode(out, 0, "ENDSEC");
}

void DxfManager::writeItemToDxf(QTextStream& out, CadItem* item) {
    if (!item) return;

    // BlockItem은 디스패처 핸들러가 없으므로 직접 INSERT 엔티티로 직렬화한다.
    if (BlockItem* blk = dynamic_cast<BlockItem*>(item)) {
        MyVector ins = blk->insertionPoint();
        writeGroupCode(out, 0,  "INSERT");
        writeGroupCode(out, 5,  getNextHandle());
        writeGroupCode(out, 100, "AcDbEntity");
        writeGroupCode(out, 8,  blk->layerName().isEmpty() ? QString("0") : blk->layerName());
        writeGroupCode(out, 100, "AcDbBlockReference");
        writeGroupCode(out, 2,  blk->blockName());
        writeGroupCode(out, 10, ins.x);
        writeGroupCode(out, 20, ins.y);
        writeGroupCode(out, 30, 0.0);
        writeGroupCode(out, 41, blk->blockScaleX());
        writeGroupCode(out, 42, blk->blockScaleY());
        writeGroupCode(out, 50, blk->blockRotation());
        return;
    }

    // 나머지 엔티티는 디스패처가 타입별 핸들러를 선택하여 출력한다.
    // BaseDimensionItem도 DxfDimensionHandler가 직접 DIMENSION 엔티티로 직렬화한다.
    m_dispatcher->exportDxf(out, item, this);
}

void DxfManager::onImportCompleted() {
    qDebug() << "DXF Import Completed.";
}

void DxfManager::onExportCompleted() {
    qDebug() << "DXF Export Completed.";
}
// mergeDimensionProperties는 DxfEntityRestorer로 이관됨
