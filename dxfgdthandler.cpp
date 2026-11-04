#include "dxfgdthandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "mymath.h"
#include "mygdt.h"       // GdtData, GdtSymbol 열거형
#include "datumitem.h"   // DatumData 구조체
#include "gdtitem.h"
#include "gdtcalculator.h"
#include <QtMath>

// =============================================================================
// DxfGdtHandler — TOLERANCE 엔티티 복원
//
// DXF TOLERANCE 텍스트는 "%%v" 구분자로 구분된 섹션으로 구성된다:
//   섹션[0]: 기호 코드 (예: "{\\Fgdt;j}")
//   섹션[1]: 공차값
//   섹션[2]: 데이텀 레퍼런스 A
//   섹션[3]: (여백)
//   섹션[4]: 데이텀 레퍼런스 B ...
//
// 판별 로직:
//  - 섹션[0]이 비어있고 섹션[2] 이후에 텍스트가 있으면 → DatumItem (데이텀 기호)
//  - 그 외 → GdtItem (GDT 공차 프레임)
// =============================================================================

CadItem* DxfGdtHandler::restore(const DxfEntityData& data,
                                  EntityFactory& factory,
                                  MyGraphicsScene* /*scene*/,
                                  const QMap<QString, DxfDimStyle>& dimStyles,
                                  const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 입력 텍스트 복사 및 0x1D 제어 문자를 %%v로 치환 (이중 안전장치)
    QString cleanText = data.text;
    cleanText.replace(QChar(0x1D), "%%v");

    // %%v 구분자로 텍스트를 분리한다.
    QStringList p = cleanText.split("%%v", Qt::KeepEmptyParts);

    // 첫 번째 섹션에서 GDT 기호 문자를 추출한다.
    // 형식 예: "{\\Fgdt;j}" → 세미콜론 뒤, 닫는 중괄호 앞 문자가 기호이다.
    QString sym;
    if (!p.isEmpty()) {
        int i = p[0].indexOf(';');
        sym = (i != -1 && p[0].endsWith('}'))
            ? p[0].mid(i + 1, p[0].length() - i - 2)
            : p[0];
    }

    // 기호가 비어 있고, 섹션[1]도 비어 있으면 데이텀 기호로 판별한다.
    bool isD = false;
    QString dTxt;
    if (sym.isEmpty() && p.size() > 1 && p[1].isEmpty()) {
        for (int i = 2; i < p.size(); ++i) {
            if (!p[i].isEmpty()) {
                isD  = true;
                dTxt = p[i].trimmed();
                break;
            }
        }
    }
    
    // DWG import시 %%v 없이 순수 문자열만 넘어오는 경우 데이텀 처리
    if (!isD && p.size() == 1 && !sym.isEmpty()) {
        // GDT 기호는 "{\\Fgdt;j}" 등과 같이 포맷팅되어 들어온다.
        // 포맷팅 문자가 없으면 데이텀 기호로 간주한다.
        if (!p[0].contains("\\Fgdt;", Qt::CaseInsensitive)) {
            isD = true;
            dTxt = sym.trimmed();
        }
    }

    // dx11, dy11이 방향 벡터로 주어진 경우 회전각을 계산한다.
    double rot = (std::abs(data.dx11) > 1e-6 || std::abs(data.dy11) > 1e-6)
               ? std::atan2(data.dy11, data.dx11) * 180.0 / MyMath::PI
               : 0.0;

    QPen pen(Qt::white);

    if (isD) {
        // 데이텀 기호(DatumItem)로 복원한다.
        DatumData d;
        d.text       = dTxt;
        d.startPoint = MyVector(data.x1, data.y1);

        // 텍스트 높이: 개별 오버라이드 → DimStyle → 기본값 2.5 순으로 결정한다.
        double tH = data.hasFontSize ? data.fontSize
                  : (dimStyles.contains(data.dimStyleName)
                      ? dimStyles[data.dimStyleName].textSize
                      : 2.5);
        d.textHeight = tH;

        // dx11, dy11 방향으로 끝점을 설정하여 데이텀 꼬리를 맞춥니다.
        MyVector dir(data.dx11, data.dy11);
        if (dir.magnitude() < 1e-6) dir = MyVector(0, 1); // 방향이 없으면 위로
        d.endPoint = d.startPoint + dir.normalized() * (tH * 1.0);
        d.leaderPoints << d.startPoint << d.endPoint;

        return dynamic_cast<CadItem*>(factory.createDatum(d, pen));

    } else {
        // GDT 공차 프레임(GdtItem)으로 복원한다.
        GdtData g;
        g.position  = MyVector(data.x1, data.y1);
        g.rotation  = rot;
        g.useLeader = false;

        // [tol-autocad.dxf 전체 14개 기호 실증 확인 - 최종]
        if      (sym == "j") g.symbol = GdtSymbol::Position;         // j=위치도 ⊕
        else if (sym == "r") g.symbol = GdtSymbol::Concentricity;    // r=동심도 ◎
        else if (sym == "i") g.symbol = GdtSymbol::Symmetry;         // i=대칭도 ≡
        else if (sym == "f") g.symbol = GdtSymbol::Parallelism;      // f=평행도 //
        else if (sym == "b") g.symbol = GdtSymbol::Perpendicularity; // b=직각도 ⊥
        else if (sym == "a") g.symbol = GdtSymbol::Angularity;       // a=경사도 ∠
        else if (sym == "g") g.symbol = GdtSymbol::Cylindricity;     // g=원통도
        else if (sym == "c") g.symbol = GdtSymbol::Flatness;         // c=평면도
        else if (sym == "e") g.symbol = GdtSymbol::Circularity;      // e=진원도 ○
        else if (sym == "u") g.symbol = GdtSymbol::Straightness;     // u=직진도 —
        else if (sym == "d") g.symbol = GdtSymbol::SurfaceProfile;  // d=면의윤곽도 ⌓
        else if (sym == "k") g.symbol = GdtSymbol::LineProfile;      // k=선의윤곽도 ⌒
        else if (sym == "h") g.symbol = GdtSymbol::CircularRunout;   // h=원주흔들림 ↗
        else if (sym == "t") g.symbol = GdtSymbol::TotalRunout;      // t=전체흔들림 ↗↗
        else                 g.symbol = GdtSymbol::Position;

        g.textHeight = data.hasFontSize ? data.fontSize
                     : (dimStyles.contains(data.dimStyleName)
                         ? dimStyles[data.dimStyleName].textSize
                         : 2.5);

        // 폰트 이름 추출: DimStyle에서 가져오되, 한글 폰트가 아닌 경우(SHX 등)
        // Autodraft 기본 폰트로 대체하여 새로 생성한 GDT와 박스 크기를 통일한다.
        if (dimStyles.contains(data.dimStyleName)) {
            g.fontName = dimStyles[data.dimStyleName].fontName;
        }
        QString fn = g.fontName.toLower();
        bool isKorean = fn.contains("dotum") || fn.contains("돋움") || fn.contains("gulim") || fn.contains("굴림") || fn.contains("malgun") || fn.contains("맑은");
        if (!isKorean) {
            g.fontName = "Arial";
        }

        // 공차값 섹션을 파싱한다. n(표준), {\\Fgdt;n} 또는 %%c가 있으면 지름 기호이다.
        if (p.size() > 1) {
            QString t = p[1];
            if (t.startsWith('n') || t.startsWith("%%c") || t.contains("{\\Fgdt;n}") || t.contains("%%c")) {
                g.hasDiameter = true;
                if (t.startsWith('n')) t = t.mid(1);
                else if (t.startsWith("%%c")) t = t.mid(3);
                t.replace("{\\Fgdt;n}", "");
                t.replace("%%c", "");
            }
            g.toleranceValue = t;
        }

        // 데이텀 레퍼런스는 홀수 인덱스(3, 5, 7...)에 위치한다.
        for (int i = 3; i < p.size(); i += 2) {
            if (!p[i].isEmpty()) g.datumRefs.append(p[i]);
        }

        // [중요] AutoCAD DXF의 TOLERANCE 삽입점은 이미 지시선 부착 지점이므로 
        // 90~270도 구간에서 중복으로 위치 보정을 할 필요가 없습니다. (GdtItem이 내부적으로 처리)
        // 이 로직을 제거하여 복원된 객체와 새로 생성된 객체의 지시선 부착 위치를 통일합니다.

        return dynamic_cast<CadItem*>(factory.createGdt(g, pen));
    }
}

// =============================================================================
// DxfGdtHandler — 내보내기
// =============================================================================

// GdtSymbol 열거형을 AutoCAD gdt 폰트 문자로 변환한다.
static QChar gdtSymbolToChar(GdtSymbol sym)
{
    // [tol-autocad.dxf 전체 14개 기호 실증 확인 - 최종]
    switch (sym) {
    case GdtSymbol::Position:         return 'j'; // j=위치도
    case GdtSymbol::Concentricity:    return 'r'; // r=동심도
    case GdtSymbol::Symmetry:         return 'i'; // i=대칭도
    case GdtSymbol::Parallelism:      return 'f'; // f=평행도
    case GdtSymbol::Perpendicularity: return 'b'; // b=직각도
    case GdtSymbol::Angularity:       return 'a'; // a=경사도
    case GdtSymbol::Cylindricity:     return 'g'; // g=원통도
    case GdtSymbol::Flatness:         return 'c'; // c=평면도
    case GdtSymbol::Circularity:      return 'e'; // e=진원도
    case GdtSymbol::Straightness:     return 'u'; // u=직진도
    case GdtSymbol::SurfaceProfile:   return 'd'; // d=면의윤곽도
    case GdtSymbol::LineProfile:      return 'k'; // k=선의윤곽도
    case GdtSymbol::CircularRunout:   return 'h'; // h=원주흔들림
    case GdtSymbol::TotalRunout:      return 't'; // t=전체흔들림
    default:                          return 'j';
    }
}

// "Default" 레이어명을 DXF 필수 레이어명 "0"으로 변환한다.
static QString toGdtDxfLayerName(const QString& name)
{
    return (name.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0)
           ? QStringLiteral("0") : name;
}

// 엔티티 헤더를 미리 할당된 핸들로 수동 출력한다.
// writeEntityAcDbHeader 는 내부에서 핸들을 자동 할당하므로,
// 교차 참조(LEADER↔TOLERANCE)가 필요할 때는 이 함수를 사용한다.
static void writeHeaderWithHandle(QTextStream& out, DxfManager* manager,
                                  const QString& entityType,
                                  const QString& handle,
                                  const QString& layerName)
{
    writeGC(out, 0,   entityType);
    writeGC(out, 5,   handle);
    QString msHandle = manager->modelSpaceHandle();
    if (!msHandle.isEmpty())
        writeGC(out, 330, msHandle);
    writeGC(out, 100, QString("AcDbEntity"));
    writeGC(out, 8,   toGdtDxfLayerName(layerName.isEmpty() ? QStringLiteral("0") : layerName));
}

bool DxfGdtHandler::canExport(CadItem* item) const
{
    return (dynamic_cast<GdtItem*>(item) != nullptr) ||
           (dynamic_cast<DatumItem*>(item) != nullptr);
}

void DxfGdtHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    GdtItem* gdtItem = dynamic_cast<GdtItem*>(item);
    DatumItem* datumItem = dynamic_cast<DatumItem*>(item);

    // DatumItem: AutoCAD 시각용으로 순수 기하 엔티티(SOLID+LINE+LWPOLYLINE+TEXT)를 출력하고,
    // AutoDraft 재임포트용으로 LEADER+TOLERANCE(gc60=1, AutoCAD에서 비가시)를 추가 출력한다.
    // 재임포트 시 dxfmanager의 SKIP_IMPORT XDATA로 기하 엔티티를 건너뛰어 중복을 방지한다.
    if (datumItem) {
        const DatumData& d = datumItem->datumData();
        QString lyr = item->layerName();
        double tH = d.textHeight;
        double h  = tH;
        double arrowSize = tH * 0.8;
        double triHeight = arrowSize * qSqrt(3.0) / 2.0;
        double autocad_w_d = GdtCalculator::datumBoxWidth(tH, d.text, true);

        QList<MyVector> pts;
        if (d.leaderPoints.size() >= 2) pts = d.leaderPoints;
        else pts << d.startPoint << d.endPoint;

        // 퇴화 케이스(삼각형~박스 거리 0): 박스+텍스트만 출력 (AutoCAD 시각용)
        if (pts.size() < 2 || (pts.last() - pts.first()).magnitude() < 1e-6) {
            MyVector pc = pts.isEmpty() ? d.endPoint : pts.last();
            manager->writeEntityAcDbHeader(out, "LWPOLYLINE", lyr);
            writeGC(out, 100, QString("AcDbPolyline"));
            writeGC(out, 90, 4); writeGC(out, 70, 1);
            writeGC(out, 10, pc.x - h); writeGC(out, 20, pc.y - h);
            writeGC(out, 10, pc.x + h); writeGC(out, 20, pc.y - h);
            writeGC(out, 10, pc.x + h); writeGC(out, 20, pc.y + h);
            writeGC(out, 10, pc.x - h); writeGC(out, 20, pc.y + h);
            writeGC(out, 1001, QString("AUTODRAFT")); writeGC(out, 1000, QString("SKIP_IMPORT"));
            manager->writeEntityAcDbHeader(out, "TEXT", lyr);
            writeGC(out, 100, QString("AcDbText"));
            writeGC(out, 10, pc.x); writeGC(out, 20, pc.y); writeGC(out, 30, 0.0);
            writeGC(out, 40, tH); writeGC(out, 1, d.text); writeGC(out, 72, 1);
            writeGC(out, 11, pc.x); writeGC(out, 21, pc.y); writeGC(out, 31, 0.0);
            writeGC(out, 100, QString("AcDbText")); writeGC(out, 73, 2);
            writeGC(out, 1001, QString("AUTODRAFT")); writeGC(out, 1000, QString("SKIP_IMPORT"));
            // AutoDraft 재임포트: LEADER 없는 독립 TOLERANCE (gc60=1 → AutoCAD 비가시)
            manager->writeEntityAcDbHeader(out, "TOLERANCE", lyr);
            writeGC(out, 60, 1);
            writeGC(out, 100, QString("AcDbFcf"));
            writeGC(out,   3, QString("Standard"));
            writeGC(out,  10, pc.x - autocad_w_d / 2.0);
            writeGC(out,  20, pc.y);
            writeGC(out,  30, 0.0);
            writeGC(out,  11, 1.0); writeGC(out, 21, 0.0); writeGC(out, 31, 0.0);
            writeGC(out,   1, QString("%%v%%v%1%%v%%v").arg(d.text.trimmed()));
            return;
        }

        MyVector pStart   = pts.first();
        MyVector pEnd     = pts.last();
        MyVector firstDir = (pts[1] - pts[0]).normalized();
        MyVector lastDir  = (pts.last() - pts[pts.size()-2]).normalized();
        MyVector nFirst(-firstDir.y, firstDir.x);
        MyVector nLast (-lastDir.y,  lastDir.x);
        MyVector triBase1 = pStart + nFirst * (arrowSize / 2.0);
        MyVector triBase2 = pStart - nFirst * (arrowSize / 2.0);
        MyVector triTip   = pStart + firstDir * triHeight;
        MyVector lineEnd  = pEnd - lastDir * h;

        // 1. SOLID — AutoCAD 시각 전용 (재임포트 시 SKIP_IMPORT XDATA로 건너뜀)
        manager->writeEntityAcDbHeader(out, "SOLID", lyr);
        writeGC(out, 100, QString("AcDbTrace"));
        writeGC(out, 10, triBase1.x); writeGC(out, 20, triBase1.y); writeGC(out, 30, 0.0);
        writeGC(out, 11, triBase2.x); writeGC(out, 21, triBase2.y); writeGC(out, 31, 0.0);
        writeGC(out, 12, triTip.x);   writeGC(out, 22, triTip.y);   writeGC(out, 32, 0.0);
        writeGC(out, 13, triTip.x);   writeGC(out, 23, triTip.y);   writeGC(out, 33, 0.0);
        writeGC(out, 1001, QString("AUTODRAFT")); writeGC(out, 1000, QString("SKIP_IMPORT"));

        // 2. LINE — AutoCAD 시각 전용
        QList<MyVector> linePts;
        linePts << triTip;
        for (int i = 1; i < pts.size() - 1; ++i) linePts << pts[i];
        linePts << lineEnd;
        for (int i = 0; i < linePts.size() - 1; ++i) {
            if ((linePts[i+1] - linePts[i]).magnitude() < 1e-6) continue;
            manager->writeEntityAcDbHeader(out, "LINE", lyr);
            writeGC(out, 100, QString("AcDbLine"));
            writeGC(out, 10, linePts[i].x);   writeGC(out, 20, linePts[i].y);   writeGC(out, 30, 0.0);
            writeGC(out, 11, linePts[i+1].x); writeGC(out, 21, linePts[i+1].y); writeGC(out, 31, 0.0);
            writeGC(out, 1001, QString("AUTODRAFT")); writeGC(out, 1000, QString("SKIP_IMPORT"));
        }

        // 3. LWPOLYLINE — AutoCAD 시각 전용
        MyVector v1 = pEnd - nLast * h - lastDir * h;
        MyVector v2 = pEnd + nLast * h - lastDir * h;
        MyVector v3 = pEnd + nLast * h + lastDir * h;
        MyVector v4 = pEnd - nLast * h + lastDir * h;
        manager->writeEntityAcDbHeader(out, "LWPOLYLINE", lyr);
        writeGC(out, 100, QString("AcDbPolyline"));
        writeGC(out, 90, 4); writeGC(out, 70, 1);
        writeGC(out, 10, v1.x); writeGC(out, 20, v1.y);
        writeGC(out, 10, v2.x); writeGC(out, 20, v2.y);
        writeGC(out, 10, v3.x); writeGC(out, 20, v3.y);
        writeGC(out, 10, v4.x); writeGC(out, 20, v4.y);
        writeGC(out, 1001, QString("AUTODRAFT")); writeGC(out, 1000, QString("SKIP_IMPORT"));

        // 4. TEXT — AutoCAD 시각 전용
        manager->writeEntityAcDbHeader(out, "TEXT", lyr);
        writeGC(out, 100, QString("AcDbText"));
        writeGC(out, 10, pEnd.x); writeGC(out, 20, pEnd.y); writeGC(out, 30, 0.0);
        writeGC(out, 40, tH);
        writeGC(out,  1, d.text);
        writeGC(out, 72, 1);
        writeGC(out, 11, pEnd.x); writeGC(out, 21, pEnd.y); writeGC(out, 31, 0.0);
        writeGC(out, 100, QString("AcDbText"));
        writeGC(out, 73, 2);
        writeGC(out, 1001, QString("AUTODRAFT")); writeGC(out, 1000, QString("SKIP_IMPORT"));

        // 5. LEADER+TOLERANCE (AutoCAD에서 gc60=1로 비가시, AutoDraft 재임포트용)
        QString leaderHandle = manager->allocateHandle();
        QString tolHandle    = manager->allocateHandle();

        // LEADER: gc73=3(데이텀), 정점, gc340→TOLERANCE 참조
        // gc60=1: AutoCAD에서 비가시 (기하 엔티티가 시각 담당)
        writeHeaderWithHandle(out, manager, "LEADER", leaderHandle, lyr);
        writeGC(out, 60, 1);
        writeGC(out, 100, QString("AcDbLeader"));
        writeGC(out,   3, QString("Standard"));
        writeGC(out,  73, 3);
        writeGC(out,  75, 0);
        writeGC(out,  40, autocad_w_d);
        writeGC(out,  41, 0.0);
        writeGC(out,  76, static_cast<int>(pts.size()));
        for (int i = 0; i < pts.size(); ++i) {
            writeGC(out, 10, pts[i].x);
            writeGC(out, 20, pts[i].y);
            writeGC(out, 30, 0.0);
        }
        writeGC(out, 340, tolHandle);
        writeGC(out, 210, 0.0); writeGC(out, 220, 0.0); writeGC(out, 230, 1.0);
        writeGC(out, 211, 0.0); writeGC(out, 221, 1.0); writeGC(out, 231, 0.0);
        writeGC(out, 212, 0.0); writeGC(out, 222, 0.0); writeGC(out, 232, 0.0);
        writeGC(out, 213, 0.0); writeGC(out, 223, 0.0); writeGC(out, 233, 0.0);

        // TOLERANCE: gc60=1 비가시, gc1 형식 "%%v%%vB%%v%%v" → restore()에서 DatumItem 판별
        writeGC(out, 0,   QString("TOLERANCE"));
        writeGC(out, 5,   tolHandle);
        writeGC(out, 102, QString("{ACAD_REACTORS"));
        writeGC(out, 330, leaderHandle);
        writeGC(out, 102, QString("}"));
        {
            QString msHandle = manager->modelSpaceHandle();
            if (!msHandle.isEmpty()) writeGC(out, 330, msHandle);
        }
        writeGC(out, 100, QString("AcDbEntity"));
        writeGC(out, 8,   toGdtDxfLayerName(lyr.isEmpty() ? QStringLiteral("0") : lyr));
        writeGC(out,  60, 1);
        writeGC(out, 100, QString("AcDbFcf"));
        writeGC(out,   3, QString("Standard"));
        writeGC(out,  10, pEnd.x - autocad_w_d / 2.0);
        writeGC(out,  20, pEnd.y);
        writeGC(out,  30, 0.0);
        writeGC(out,  11, 1.0); writeGC(out, 21, 0.0); writeGC(out, 31, 0.0);
        writeGC(out,   1, QString("%%v%%v%1%%v%%v").arg(d.text.trimmed()));
        return;
    }

    QString gc1Text;
    MyVector position;
    double textHeight = 0.0;
    double autocad_w = 0.0;
    QList<MyVector> leaderPoints;
    bool useLeader = false;
    QString layerName = item->layerName();

    if (gdtItem) {
        const GdtData& g = gdtItem->gdtData();
        // --- gc1 텍스트 조립 ---
        QStringList sections;
        sections << QString("{\\Fgdt;%1}").arg(gdtSymbolToChar(g.symbol));
        sections << (g.hasDiameter ? QString("%%c%1").arg(g.toleranceValue) : g.toleranceValue);
        sections << QString();
        for (int i = 0; i < g.datumRefs.size(); ++i) {
            sections << g.datumRefs[i];
            sections << QString();
        }
        while (sections.size() > 3 && sections.last().isEmpty()) sections.removeLast();
        gc1Text = sections.join("%%v");
        textHeight = g.textHeight;
        leaderPoints = gdtItem->getAdjustedLeaderPoints();
        useLeader = g.useLeader && (leaderPoints.size() >= 2);
        position = useLeader ? leaderPoints.last() : g.position;
        // AutoCAD용 너비 계산
        QString paddedTol = " " + g.toleranceValue + " "; 
        QList<QString> paddedDatums;
        int datumCount = g.datumRefs.size();
        for (int i = 0; i < datumCount; ++i)
            paddedDatums << " " + g.datumRefs.at(i) + " ";
        autocad_w = GdtCalculator::totalGdtWidth(textHeight, paddedTol, g.hasDiameter, paddedDatums, g.symbol, true);

    } else if (datumItem) {
        const DatumData& d = datumItem->datumData();
        // [수정] 정방형 확보를 위해 텍스트 앞뒤에 공백을 두 칸씩 추가합니다.
        gc1Text = QString("%%v%%v  %1  ").arg(d.text.trimmed());
        textHeight = d.textHeight;
        autocad_w = GdtCalculator::datumBoxWidth(textHeight, d.text, true);

        if (d.leaderPoints.size() >= 2) leaderPoints = d.leaderPoints;
        else leaderPoints << d.startPoint << d.endPoint;
        useLeader = (leaderPoints.size() >= 2);
        position = leaderPoints.last();
    }

    bool hasLeader = useLeader && !leaderPoints.isEmpty();

    MyVector exportPos = position;
    if (hasLeader && datumItem) {
        // [수정] 아래 공통 처리 블록(line 323 부근)에서 이미 처리하므로 여기서는 초기화만 유지하여 이중 감산을 방지합니다.
        exportPos = position; 
    }

    if (hasLeader) {
        // --- 1. 기하학적 정점 보정 (사선/비틀림 원천 차단) ---
        if (leaderPoints.size() == 2) {
            MyVector p0 = leaderPoints[0];
            MyVector p1 = leaderPoints[1];
            
            if (gdtItem) {
                // Autodraft에서 그린 2점 GDT 지시선(대각선)을 직각 3점 리더로 강제 변환
                if (std::abs(p0.x - p1.x) > 1e-4 && std::abs(p0.y - p1.y) > 1e-4) {
                    MyVector bendPoint(p0.x, p1.y); // 수직 상승 후 수평 꺾임
                    leaderPoints.insert(1, bendPoint);
                }
            } else if (datumItem) {
                // 데이텀 지시선이 비스듬해지는 사선 오류 방지 (완벽한 수직선 강제)
                leaderPoints[1].x = p0.x;
            }
        }

        // --- 2. 삽입점 보정 (파고듦 방지) ---
        exportPos = leaderPoints.last();
        if (gdtItem && leaderPoints.size() >= 2) {
            double dx = leaderPoints.last().x - leaderPoints[leaderPoints.size() - 2].x;
            double rot = MyMath::normalizeAngle(gdtItem->gdtData().rotation);
            bool isRotated180 = (rot > 135.0 && rot < 225.0);

            if (dx < -1e-4) { 
                // 지시선이 오른쪽에서 왼쪽으로 진행 (박스의 오른쪽 테두리에 닿음)
                // TOLERANCE 삽입점 = 박스 왼쪽 끝, LEADER 마지막 vertex도 동일하게 이동
//                exportPos.x = leaderPoints.last().x ;
                exportPos.x = leaderPoints.last().x - autocad_w;

            } else if (dx > 1e-4) {
                // 지시선이 왼쪽에서 오른쪽으로 진행 (박스의 왼쪽 테두리에 닿음)
                if (isRotated180) {
                    exportPos.x += autocad_w;
                }
            }
        }
    }

    // 데이텀 박스: gc10/20 = 박스 왼쪽 끝, 수직 중심
    // leaderPoints.last() = datumitem.cpp 에서 박스 center, gc20은 그대로 사용
    // if (datumItem) {
    //     exportPos = hasLeader ? leaderPoints.last() : position;
    //     exportPos.x -= (autocad_w / 2.0);
    // }

    if (datumItem) {
        // [??] ?? ??? ??? exportPos? ?? position?? ??
        exportPos = hasLeader ? leaderPoints.last() : position;
        exportPos.x -= (autocad_w / 2.0);
        if (hasLeader && leaderPoints.size() >= 2) {
            double dy = leaderPoints.last().y - leaderPoints[leaderPoints.size() - 2].y;
            // AutoCAD TOLERANCE ???? ???? ?? ?????, 
            // ?? ???? ??? ??/?? ??? ??? y? ?? ??
            exportPos.y += (dy > 0 ? textHeight : -textHeight);
        }
    }


    // 211 수평/수직 벡터 미리 계산 (정점 제거 안함)
    double hdir_x = 1.0;
    double hdir_y = 0.0;
    if (hasLeader && leaderPoints.size() >= 2) {
        double dx = leaderPoints.last().x - leaderPoints[leaderPoints.size() - 2].x;
        double dy = leaderPoints.last().y - leaderPoints[leaderPoints.size() - 2].y;
        if (std::abs(dx) > 1e-4) {
            hdir_x = (dx > 0) ? 1.0 : -1.0;
            hdir_y = 0.0;
        } else if (std::abs(dy) > 1e-4) {
            hdir_x = 0.0;
            hdir_y = (dy > 0) ? 1.0 : -1.0;
        }
    }

    if (hasLeader) {
        QString leaderHandle = manager->allocateHandle();
        QString tolHandle    = manager->allocateHandle();

        // --- LEADER 엔티티 ---
        writeHeaderWithHandle(out, manager, "LEADER", leaderHandle, layerName);
        writeGC(out, 100, QString("AcDbLeader"));
        writeGC(out,   3, QString("Standard"));
        writeGC(out,  73, datumItem ? 3 : 1);                   // 데이텀은 연결 없음(None=3), GDT는 연결(Tolerance=1)
     //   writeGC(out,  74, 0);                   // 후크라인 방향 
        writeGC(out,  75, 0);                   // 후크라인 자동 생성 끄기 (명시된 정점 그대로 사용)
        writeGC(out,  40, gdtItem ? textHeight : autocad_w); // GDT: 텍스트높이, 데이텀: 프레임너비
        writeGC(out,  41, datumItem ? 0.0 : autocad_w);    // 데이텀: 0(훅라인 없음), GDT: 프레임너비

        // LEADER 정점 출력 (GDT, 데이텀 공통)
        writeGC(out, 76, static_cast<int>(leaderPoints.size()));
        for (int i = 0; i < leaderPoints.size(); ++i) {
            writeGC(out, 10, leaderPoints[i].x);
            writeGC(out, 20, leaderPoints[i].y);
            writeGC(out, 30, 0.0);
        }
        if (!datumItem) {
            writeGC(out, 340, tolHandle);     // GDT만 TOLERANCE 핸들 참조 유지
        }

        // 오프셋/벡터 지정 (AutoCAD 원점 튀는 현상 및 후크라인 방향 반전 방지)
        writeGC(out, 210, 0.0); writeGC(out, 220, 0.0); writeGC(out, 230, 1.0);
        writeGC(out, 211, hdir_x); writeGC(out, 221, hdir_y); writeGC(out, 231, 0.0);
        writeGC(out, 212, 0.0); writeGC(out, 222, 0.0); writeGC(out, 232, 0.0);
        writeGC(out, 213, 0.0); writeGC(out, 223, 0.0); writeGC(out, 233, 0.0);

        // 화살촉 및 텍스트 스타일 오버라이드 (XDATA DSTYLE)
        writeGC(out, 1001, QString("ACAD"));
        writeGC(out, 1000, QString("DSTYLE"));
        writeGC(out, 1002, QString("{"));
        if (datumItem) {
            QString dtH = manager->datumArrowHandle();
            if (!dtH.isEmpty()) {
                writeGC(out, 1070, 341);
                writeGC(out, 1005, dtH);
            } else {
                writeGC(out, 1070, 341);
                writeGC(out, 1000, QString("_DatumFilled"));
            }
        }
        // DIMTXT(140) 및 DIMASZ(41) 오버라이드
        writeGC(out, 1070, 140);
        writeGC(out, 1040, textHeight);
        writeGC(out, 1070, 41);
        writeGC(out, 1040, textHeight);
        writeGC(out, 1002, QString("}"));

        // --- TOLERANCE 엔티티 헤더 — REACTORS로 LEADER와 역방향 연결 ---
        writeGC(out, 0,   QString("TOLERANCE"));
        writeGC(out, 5,   tolHandle);
        if (!datumItem) {
            writeGC(out, 102, QString("{ACAD_REACTORS"));
            writeGC(out, 330, leaderHandle);
            writeGC(out, 102, QString("}"));
        }
        QString msHandle = manager->modelSpaceHandle();
        if (!msHandle.isEmpty())
            writeGC(out, 330, msHandle);
        writeGC(out, 100, QString("AcDbEntity"));
        writeGC(out, 8,   toGdtDxfLayerName(layerName.isEmpty() ? QStringLiteral("0") : layerName));
    } else {
        // --- TOLERANCE 엔티티 헤더 (지시선 없음) ---
        manager->writeEntityAcDbHeader(out, "TOLERANCE", layerName);
    }


    // --- TOLERANCE 본체 (AcDbFcf) ---
    writeGC(out, 100, QString("AcDbFcf"));
    writeGC(out,   3, QString("Standard"));
    writeGC(out,  10, exportPos.x);
    writeGC(out,  20, exportPos.y);
    writeGC(out,  30, 0.0);
    
    // GDT 박스는 항상 수평으로 렌더링되도록 X축 벡터(11)를 명시
    writeGC(out,  11, 1.0);
    writeGC(out,  21, 0.0);
    writeGC(out,  31, 0.0);

    writeGC(out,   1, gc1Text);

    // XDATA: 텍스트 높이 및 화살촉 크기 오버라이드
    writeGC(out, 1001, QString("ACAD"));
    writeGC(out, 1000, QString("DSTYLE"));
    writeGC(out, 1002, QString("{"));
    writeGC(out, 1070, 140); // 텍스트 높이
    writeGC(out, 1040, textHeight);
    writeGC(out, 1070, 41);  // 화살촉 크기 복원
    writeGC(out, 1040, textHeight);
    writeGC(out, 1002, QString("}"));
}
