#include "dxfdimensionhandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "mydimension.h"
#include "basedimensionitem.h"
#include "dxfdimensionparser.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "myline.h"
#include "fittolerancecalculator.h"
#include <QtMath>
#include <cmath>

// =============================================================================
// DxfDimensionHandler — DIMENSION 엔티티 복원
// dimType & 0x07 값에 따라 치수 종류를 결정한다:
//   0,1 → 선형(Linear) 또는 반치수(HalfDimension)
//   2,5 → 각도(Angular)
//   3   → 지름(Diametric)
//   4   → 반지름(Radial)
// =============================================================================

CadItem* DxfDimensionHandler::restore(const DxfEntityData& data,
                                       EntityFactory& factory,
                                       MyGraphicsScene* scene,
                                       const QMap<QString, DxfDimStyle>& dimStyles,
                                       const QMap<QString, QList<CadItem*>>& blocks)
{
    // DXF $DIMSCALE / DWG DIMSCALE 헤더값. 임포트 시 parseHeaderSection() 또는
    // DwgInterface::addHeader()가 씬의 치수 스타일 dimScale에 미리 반영해 둔다.
    // DimStyle 레코드에 개별 DIMSCALE(gc40)이 없을 때 도면 전체 축척으로 사용한다.
    const double sceneDimScale = scene ? scene->dimensionStyle().dimScale : 0.0;

    // 블록에서 텍스트를 보완한다. 텍스트가 비어있거나 "<>"이면
    // 연관 블록 엔티티에서 실제 텍스트를 찾아 채운다.
    DxfEntityData mData = data;
    if ((mData.text.isEmpty() || mData.text == "<>") &&
        !mData.blockName.isEmpty() &&
        blocks.contains(mData.blockName))
    {
        const QList<CadItem*>& bItems = blocks[mData.blockName];
        for (int i = 0; i < bItems.size(); ++i) {
            QMap<QString, QVariant> props = bItems[i]->getProperties();
            if (props.contains("Text")) {
                QString bTxt = props.value("Text").toString();
                if (!bTxt.isEmpty() && bTxt != "<>") {
                    mData.text = bTxt;
                    break;
                }
            }
        }
    }

    MyDimension dim;
    int type = data.dimType & 0x07; // 하위 3비트가 치수 종류

    if (type == 0 || type == 1) {
        // 선형 치수 또는 반치수
        double x13 = data.dx13, y13 = data.dy13; // 정의점 1
        double x14 = data.dx14, y14 = data.dy14; // 정의점 2
        double xdl = data.dx10, ydl = data.dy10; // 치수선 위치

        bool isV = std::abs(x13 - x14) < 1e-3;
 //       bool isH = std::abs(y13 - y14) < 1e-3;

        // Se/Sd 플래그: DIMSTYLE 기본값 → 엔티티 XDATA 오버라이드 순으로 적용.
        // geometry(gc10 vs p13/p14) 기반 판별은 AutoCAD가 일반 치수에서도
        // gc10을 p14 위치에 배치하는 관행 때문에 오판을 유발하므로 사용하지 않는다.
        QString sKey = data.dimStyleName.toUpper();
        int fSe1 = 0, fSd1 = 0, fSe2 = 0, fSd2 = 0;
        if (dimStyles.contains(sKey)) {
            fSe1 = dimStyles[sKey].dimSe1;  fSd1 = dimStyles[sKey].dimSd1;
            fSe2 = dimStyles[sKey].dimSe2;  fSd2 = dimStyles[sKey].dimSd2;
        }
        if (data.dimSe1 != -1) fSe1 = data.dimSe1;
        if (data.dimSd1 != -1) fSd1 = data.dimSd1;
        if (data.dimSe2 != -1) fSe2 = data.dimSe2;
        if (data.dimSd2 != -1) fSd2 = data.dimSd2;
        bool halfByP14 = (fSe2 == 1) || (fSd2 == 1); // p2(x14) 억제: Se2/Sd2=1
        bool halfByP13 = (fSe1 == 1) || (fSd1 == 1); // p1(x13) 억제: Se1/Sd1=1

        if (halfByP14) {
            // Se2 케이스: p1(x13)이 보이는 쪽(보조선 있음), p2(x14)가 억제된 중심
            dim.dimensionType = DimensionType::HalfDimension;
            dim.start   = MyVector(x13, y13);
            dim.end     = isV ? MyVector(x13, (y13 + y14) * 0.5)
                              : MyVector((x13 + x14) * 0.5, y13);
            dim.dimLine = MyVector(xdl, ydl);
            dim.fixedAngle    = data.dimRotation;
            dim.useFixedAngle = (type == 0);
        } else if (halfByP13) {
            // Se1 케이스: p2(x14)가 보이는 쪽(보조선 있음), p1(x13)이 억제된 중심
            dim.dimensionType = DimensionType::HalfDimension;
            dim.start   = MyVector(x14, y14);
            dim.end     = isV ? MyVector(x14, (y13 + y14) * 0.5)
                              : MyVector((x13 + x14) * 0.5, y14);
            dim.dimLine = MyVector(xdl, ydl);
            dim.fixedAngle    = data.dimRotation;
            dim.useFixedAngle = (type == 0);
        } else {
            // 일반 선형 치수
            dim.dimensionType = DimensionType::Linear;
            dim.start   = MyVector(x13, y13);
            dim.end     = MyVector(x14, y14);
            dim.dimLine = MyVector(xdl, ydl);
            dim.fixedAngle    = data.dimRotation;
            dim.useFixedAngle = (type == 0);
        }

    } else if (type == 2 || type == 5) {
        // 각도 치수
        dim.dimensionType = DimensionType::Angular;

        if (type == 5) {
            // 3-Point Angular (AcDb3PointAngularDimension)
            // gc15 = 꼭짓점(vertex), gc13 = 첫번째 팔 끝점, gc14 = 두번째 팔 끝점
            // gc10 = 치수선 호 정의점, gc16 = 호 정의점(대체용)
            dim.start   = MyVector(data.dx15, data.dy15);
            dim.end     = MyVector(data.dx13, data.dy13);
            dim.end2    = MyVector(data.dx14, data.dy14);
            
            bool hasArcPt16 = (data.dx16 != 0.0 || data.dy16 != 0.0);
            bool hasArcPt10 = (data.dx10 != 0.0 || data.dy10 != 0.0);
            if (hasArcPt16) {
                dim.dimLine = MyVector(data.dx16, data.dy16);
            } else if (hasArcPt10) {
                dim.dimLine = MyVector(data.dx10, data.dy10);
            } else {
                dim.dimLine = MyVector(data.dx11, data.dy11);
            }
        } else {
            // 2-Line Angular (AcDb2LineAngularDimension)
            // 오토캐드 DXF 스펙:
            // gc13/14 = 첫번째 선의 시작/끝
            // gc10/15 = 두번째 선의 시작/끝 (15/16이 아님!)
            // gc16 = 치수선 호 정의점
            MyLine l1(MyVector(data.dx13, data.dy13), MyVector(data.dx14, data.dy14));
            MyLine l2(MyVector(data.dx10, data.dy10), MyVector(data.dx15, data.dy15));
            std::vector<MyVector> inters = MyMath::intersectLineLineInfinite(l1, l2);
            
            if (!inters.empty()) {
                dim.start = inters[0]; // 꼭짓점(Vertex)은 두 선의 교차점
            } else {
                dim.start = l1.start; // 평행할 경우 예외 처리
            }
            
            bool hasArcPt16 = (data.dx16 != 0.0 || data.dy16 != 0.0);
            dim.dimLine = hasArcPt16 ? MyVector(data.dx16, data.dy16) : MyVector(data.dx11, data.dy11);
            
            // 호 정의점(dimLine)이 포함된 사분면을 감싸도록 두 방향(ray) 벡터를 결정합니다.
            MyVector v1 = l1.end - l1.start;
            MyVector v2 = l2.end - l2.start;
            MyVector arcVec = dim.dimLine - dim.start;
            
            // 행렬식 (Determinant)으로 v1, v2 기저(Basis)에서 arcVec의 부호를 판별합니다.
            double det = v1.x * v2.y - v1.y * v2.x;
            if (std::abs(det) > 1e-6) {
                double a = (arcVec.x * v2.y - arcVec.y * v2.x) / det;
                double b = (v1.x * arcVec.y - v1.y * arcVec.x) / det;
                
                MyVector ray1 = (a >= 0) ? v1 : (v1 * -1.0);
                MyVector ray2 = (b >= 0) ? v2 : (v2 * -1.0);
                
                dim.end = dim.start + ray1;
                dim.end2 = dim.start + ray2;
            } else {
                dim.end = l1.end;
                dim.end2 = l2.end;
            }
        }

    } else if (type == 3 || type == 4) {
        if (type == 4) {
            // 반지름(Radial): gc10=중심, gc15=호 위의 점, gc11=텍스트 위치
            dim.dimensionType = DimensionType::Radial;
            dim.start   = MyVector(data.dx10, data.dy10); // center
            dim.end     = MyVector(data.dx15, data.dy15); // arc point (radius magnitude)
            dim.dimLine = MyVector(data.dx11, data.dy11); // text position
        } else {
            // 지름(Diametric): gc10=반대쪽 호 점, gc15=텍스트쪽 호 점, gc11=텍스트 위치
            MyVector farPt  = MyVector(data.dx10, data.dy10);
            MyVector nearPt = MyVector(data.dx15, data.dy15);
            double   radius = (nearPt - farPt).magnitude() * 0.5;
            MyVector center = (farPt + nearPt) * 0.5;
            dim.dimensionType = DimensionType::Diametric;
            dim.start   = center;
            dim.end     = center + MyVector(radius, 0.0); // 반지름 크기만 보존
            dim.dimLine = MyVector(data.dx11, data.dy11); // text position
        }
    }

    // 실측값(measurement)을 계산한다. 텍스트 파싱 시 기준값으로 사용된다.
    double measurement = 0.0;
    if (dim.dimensionType == DimensionType::Linear) {
        if (dim.useFixedAngle) {
            // 고정 각도 방향 투영 거리
            double rad = qDegreesToRadians(dim.fixedAngle);
            measurement = std::abs(
                MyMath::dotProduct(dim.end - dim.start, MyVector(qCos(rad), qSin(rad))));
        } else {
            measurement = (dim.end - dim.start).magnitude();
        }
    } else if (dim.dimensionType == DimensionType::HalfDimension) {
        // 반치수는 측정값의 2배가 전체 길이다.
        measurement = (dim.end - dim.start).magnitude() * 2.0;
    } else if (dim.dimensionType == DimensionType::Radial ||
               dim.dimensionType == DimensionType::Diametric) {
        double r = (dim.end - dim.start).magnitude();
        measurement = (type == 3) ? r * 2.0 : r; // 지름 또는 반지름
    }

    // DimStyle 및 엔티티 오버라이드 값을 MyDimension에 병합한다.
    mergeProperties(dim, mData, measurement, dimStyles, sceneDimScale);

    QPen pen(Qt::white);
    return dynamic_cast<CadItem*>(factory.createDimension(dim, pen));
}

// =============================================================================
// DxfDimensionHandler::mergeProperties — 스타일·오버라이드 병합
// DxfDimStyle(글로벌)과 엔티티별 오버라이드를 우선순위에 따라 병합한다.
// 텍스트 파싱(DxfDimensionParser)을 통해 접두사·접미사·공차를 분리한다.
// =============================================================================

void DxfDimensionHandler::mergeProperties(MyDimension& target,
                                           const DxfEntityData& source,
                                           double measurement,
                                           const QMap<QString, DxfDimStyle>& dimStyles,
                                           double sceneDimScale)
{
    QString sKey = source.dimStyleName.toUpper();

    // 1단계: DimStyle 기본값을 적용한다.
    if (dimStyles.contains(sKey)) {
        const DxfDimStyle& ds = dimStyles[sKey];
        target.precision               = ds.dimDec;
        target.tolPrecision            = ds.dimTdec;
        target.suppressTrailingZeros    = (ds.dimZin  & 8);
        target.suppressTrailingZerosTol = (ds.dimTzin & 8);
        if (ds.dimScale > 0.0 && !qFuzzyCompare(ds.dimScale, 1.0))
            target.dimScaleOverride = ds.dimScale;

        // DimStyle에 공차가 설정되어 있으면 적용한다.
        if (ds.dimTol == 1 || ds.dimLim == 1) {
            target.tolType = ds.dimLim == 1
                ? ToleranceType::Limits
                : (ds.dimTp == ds.dimTm && ds.dimTp != 0
                   ? ToleranceType::Symmetrical
                   : ToleranceType::Deviation);
            target.tolUpper = ds.dimTp;
            target.tolLower = std::abs(ds.dimTm);
        }
    }

    // 1-b단계: DimStyle 레코드에 개별 DIMSCALE(gc40)이 없으면
    // 도면 전체 DIMSCALE($DIMSCALE 헤더값)을 치수 축척으로 적용한다.
    // AutoCAD가 저장한 DXF/DWG는 DIMSTYLE 테이블에 gc40을 생략하고
    // 헤더 $DIMSCALE에만 기록하는 경우가 많다(예: 1:50 건축 도면 → $DIMSCALE=50).
    if (target.dimScaleOverride <= 0.0 &&
        sceneDimScale > 0.0 && !qFuzzyCompare(sceneDimScale, 1.0)) {
        target.dimScaleOverride = sceneDimScale;
    }

    // 2단계: 텍스트에 DimStyle의 dimPost(접미사 형식)를 적용한다.
    QString fTxt = source.text.isEmpty() ? "<>" : source.text;
    if (dimStyles.contains(sKey)) {
        const DxfDimStyle& ds = dimStyles[sKey];
        if (!ds.dimPost.isEmpty() && fTxt.contains("<>")) {
            int idx = ds.dimPost.indexOf("<>");
            if (idx >= 0) {
                // dimPost가 "<>"를 포함하면 앞뒤로 감싼다.
                fTxt.replace("<>", ds.dimPost.left(idx) + "<>" + ds.dimPost.mid(idx + 2));
            } else if (ds.dimPost == "()") {
                fTxt.replace("<>", "(<>)");
            } else {
                fTxt.replace("<>", "<>" + ds.dimPost);
            }
        }
    }

    // 3단계: 텍스트를 파싱하여 접두사·접미사·공차·자동측정 여부를 분리한다.
    DxfDimensionParser::ParseResult res =
        DxfDimensionParser::parse(fTxt, measurement, target.precision);

    target.text   = res.hasAutoMeasurement ? "" : res.cleanText;
    target.prefix = res.prefix;
    target.suffix = res.suffix;

    if (res.tolType != ToleranceType::None) {
        // 텍스트 내 인라인 공차가 있으면 DimStyle보다 우선한다.
        target.tolType  = res.tolType;
        target.tolUpper = res.tolUpper;
        target.tolLower = res.tolLower;
        if (res.tolPrecision >= 0) target.tolPrecision = res.tolPrecision;
    } else {
        // 엔티티 오버라이드 값을 적용한다.
        bool overrideTol = source.hasDimTol ? (source.dimTol == 1) : false;
        bool overrideLim = source.hasDimLim ? (source.dimLim == 1) : false;

        if (target.tolType != ToleranceType::None || overrideTol || overrideLim) {
            if (overrideLim || (!source.hasDimTol && target.tolType == ToleranceType::Limits)) {
                target.tolType = ToleranceType::Limits;
            } else {
                double tp = source.hasDimTp ? source.dimTp : target.tolUpper;
                double tm = source.hasDimTm ? std::abs(source.dimTm) : target.tolLower;
                target.tolType = (tp == tm && tp != 0)
                    ? ToleranceType::Symmetrical
                    : ToleranceType::Deviation;
            }
            if (source.hasDimTp) target.tolUpper = source.dimTp;
            if (source.hasDimTm) target.tolLower = std::abs(source.dimTm);
        }

        // 엔티티 단위 정밀도 오버라이드 적용
        if (source.dimPrecision >= 0) target.precision               = source.dimPrecision;
        if (source.dimZin      >= 0) target.suppressTrailingZeros    = (source.dimZin  & 8);
        if (source.dimTdec     >= 0) target.tolPrecision             = source.dimTdec;
        if (source.dimTzin     >= 0) target.suppressTrailingZerosTol = (source.dimTzin & 8);
    }
}

// =============================================================================
// DxfDimensionHandler — 내보내기
// restore의 역방향으로 MyDimension → DXF DIMENSION 그룹코드를 생성한다.
//
// restore 역방향 매핑 요약:
//   LINEAR   : dimLine → gc10/20, start → gc13/23, end → gc14/24
//   HALF     : dimLine → gc10/20, start → gc13/23, fullEnd(=2*end-start) → gc14/24
//   ANGULAR  : start → gc10/20, end → gc13/23, end2 → gc14/24, dimLine → gc15/25
//   DIAMETRIC: dimLine(=start=center) → gc10/20, end → gc15/25
//   RADIAL   : dimLine(=start=center) → gc10/20, end → gc15/25
//
// DXF 텍스트 중심점(gc11/21) 계산:
//   LINEAR/HALF: 두 치수선 발(foot)의 중점 = (start+end)/2 + perp*h
//   ANGULAR    : dimLine 위치 근사
//   RADIAL/DIA : (start+end)/2
// =============================================================================

bool DxfDimensionHandler::canExport(CadItem* item) const
{
    // BaseDimensionItem 파생 타입이면 내보내기 가능으로 표시한다.
    return dynamic_cast<BaseDimensionItem*>(item) != nullptr;
}

// ─── 내부 헬퍼: 측정 방향과 수직 방향을 계산한다 ─────────────────────────────
// useFixedAngle이 true이면 fixedAngle(°)에서, false이면 start→end 벡터에서 계산한다.
static void calcDimDirections(const MyDimension& d,
                               MyVector& outDir, MyVector& outPerp)
{
    if (d.useFixedAngle) {
        // 고정 각도(rotated dimension)
        double rad = qDegreesToRadians(d.fixedAngle);
        outDir = MyVector(qCos(rad), qSin(rad));
    } else {
        // 정렬 치수(aligned): start→end 방향을 단위 벡터로 정규화한다.
        MyVector diff = d.end - d.start;
        double mag = diff.magnitude();
        outDir = (mag > 1e-9) ? diff.normalized() : MyVector(1.0, 0.0);
    }
    // 수직 방향: dir을 반시계 90도 회전
    outPerp = MyVector(-outDir.y, outDir.x);
}

// ─── 내부 헬퍼: 선형 치수 텍스트 중심점을 계산한다 ─────────────────────────────
// foot1 = defPt1 + perp*h, foot2 = defPt2 + perp*h
// textMid = (foot1 + foot2) / 2 = (defPt1 + defPt2)/2 + perp*h
// 여기서 h = dimLine에서 defPt1까지의 perp 방향 투영 거리
static MyVector calcLinearTextMid(const MyVector& defPt1, const MyVector& defPt2,
                                   const MyVector& dimLineRef,
                                   const MyVector& perp)
{
    double h = MyMath::dotProduct(dimLineRef - defPt1, perp);
    return MyVector((defPt1.x + defPt2.x) * 0.5 + perp.x * h,
                    (defPt1.y + defPt2.y) * 0.5 + perp.y * h);
}

// ─── 내부 헬퍼: DXF 텍스트 문자열을 구성한다 ────────────────────────────────────
// text가 비어 있으면 자동 측정("<>"), 아니면 커스텀 텍스트.
// prefix/suffix가 있으면 앞뒤에 붙인다.
// [수정] 끼워맞춤 등급기호(H7 등)는 UI에서 선택 시 이미 suffix에 반영되므로 여기서
// 별도로 더 붙이지 않는다(중복 방지). AutoCAD에는 결과적으로 일반 꼬리말 텍스트로 나간다.
static QString buildDxfText(const MyDimension& d)
{
    QString base = d.text.isEmpty() ? "<>" : d.text;
    QString full = d.prefix + base + d.suffix;
    return full.isEmpty() ? "<>" : full;
}

// ─── 내부 헬퍼: 치수 XDATA 블록을 출력한다 ──────────────────────────────────
// R2000+(AC1015)에서는 치수 스타일 오버라이드(공차·연장선 억제 등)를
// 평문 그룹코드가 아닌 "1001 ACAD / 1000 DSTYLE" XDATA 블록으로 표현한다.
// 엔티티 당 ACAD XDATA는 하나여야 하므로, 모든 오버라이드를 단일 블록에 합친다.
//
// suppressExt2: true이면 gc1070/76=1 (DIMSE2, 두 번째 보조선 억제)를 포함한다.
// measurement: 이 치수의 측정값(gc42와 동일한 값). 끼워맞춤(FitCode) 공차의 상/하한을
// 내보내기 직전에 계산하는 데 사용한다(AutoCAD는 "H7" 등급기호 자체를 모르므로
// 계산된 숫자 공차로 변환해서 DIMTP/DIMTM에 실어 보낸다).
static void writeDimXdata(QTextStream& out, const MyDimension& d, bool suppressExt2, double measurement)
{
    // 쓸 내용이 없으면 XDATA 블록 자체를 생략한다.
    // [주의] 끼워맞춤(FitCode)인데 fitShowValues==false("30H7"처럼 기호만 표기하는 기본 모드)면
    // DIMTOL/DIMTP/DIMTM을 쓰지 않는다 - 이 플래그를 쓰면 AutoCAD가 자체적으로 텍스트 뒤에
    // 숫자 공차를 자동으로 덧붙여 그리므로, 화면 표기(기호만)와 DXF 내보내기 결과가 어긋난다.
    bool hasTol  = (d.tolType != ToleranceType::None) &&
                   !(d.tolType == ToleranceType::FitCode && !d.fitShowValues);
    bool hasPrec = (d.precision >= 0);
    if (!suppressExt2 && !hasTol && !hasPrec) return;

    writeGC(out, 1001, QString("ACAD"));
    writeGC(out, 1000, QString("DSTYLE"));
    writeGC(out, 1002, QString("{"));

    // ── 치수선 및 연장선 억제 (반치수용) ──
    if (suppressExt2) {
        // 보조선 억제
        writeGC(out, 1070, 76); // DIMSE2
        writeGC(out, 1070, 1);
        // 치수선 억제
        writeGC(out, 1070, 282); // DIMSD2
        writeGC(out, 1070, 1);
    }

    // ── 주치수 정밀도 및 0 억제 ──
    if (hasPrec) {
        writeGC(out, 1070, 271); // DIMDEC
        writeGC(out, 1070, d.precision);
        writeGC(out, 1070, 78);  // DIMZIN
        writeGC(out, 1070, d.suppressTrailingZeros ? 8 : 0);
    }

    // ── 공차 오버라이드 ──
    if (hasTol) {
        // [신규] 끼워맞춤(FitCode)이면 등급기호를 IT등급표로 조회해 실제 숫자 공차로 변환한다.
        double effUpper = d.tolUpper;
        double effLower = d.tolLower;
        if (d.tolType == ToleranceType::FitCode) {
            FitToleranceResult fitResult = FitToleranceCalculator::calculate(measurement, d.fitCode);
            if (fitResult.valid) {
                effUpper = fitResult.upper;
                effLower = fitResult.lower;
            }
        }

        if (d.tolType == ToleranceType::Limits) {
            writeGC(out, 1070, 72);         // DIMLIM 코드
            writeGC(out, 1070, 1);          // 1 = 한계치 표시
        } else {
            writeGC(out, 1070, 71);         // DIMTOL 코드
            writeGC(out, 1070, 1);          // 1 = 공차 표시
        }
        writeGC(out, 1070, 47);             // DIMTP 코드 (상한 공차)
        writeGC(out, 1040, effUpper);
        writeGC(out, 1070, 48);             // DIMTM 코드 (하한 공차)
        writeGC(out, 1040, effLower);

        if (d.tolPrecision >= 0) {
            writeGC(out, 1070, 272);        // DIMTDEC 코드
            writeGC(out, 1070, d.tolPrecision);
        }
        writeGC(out, 1070, 79);             // DIMTZIN 코드
        writeGC(out, 1070, d.suppressTrailingZerosTol ? 8 : 0);
    }

    writeGC(out, 1002, QString("}"));
}

// =============================================================================
// exportDxf — 메인 내보내기 함수
// =============================================================================

void DxfDimensionHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    BaseDimensionItem* dimItem = dynamic_cast<BaseDimensionItem*>(item);
    if (!dimItem) return;

    const MyDimension& d = dimItem->data();

    // ── AC1015 공통 헤더: 0 DIMENSION + 5 handle + 330 owner + 100 AcDbEntity + 8 layer ──
    manager->writeEntityAcDbHeader(out, "DIMENSION", dimItem->layerName());
    writeGC(out, 100, QString("AcDbDimension"));
    // gc2를 생략하면 AutoCAD가 파일을 열 때 내부적으로 익명 블록을 생성하고 형상을 자동 재생성합니다.
    // 기존처럼 비어있는 *D 블록을 할당하면 재생성하지 않고 투명하게 렌더링되는 문제가 발생합니다.

    // ── 치수 종류별 처리 (gc10/20/30, gc11/21/31, gc70 공통 필드) ─────────────
    switch (d.dimensionType) {

    // ── 선형 치수 (Linear) ─────────────────────────────────────────────────────
    case DimensionType::Linear: {
        int dimType = d.useFixedAngle ? 0 : 1;

        MyVector dir, perp;
        calcDimDirections(d, dir, perp);
        MyVector textMid = calcLinearTextMid(d.start, d.end, d.dimLine, perp);

        writeGC(out, 10, d.dimLine.x);
        writeGC(out, 20, d.dimLine.y);
        writeGC(out, 30, 0.0);
        writeGC(out, 11, textMid.x);
        writeGC(out, 21, textMid.y);
        writeGC(out, 31, 0.0);
        double measurement = std::abs(MyMath::dotProduct(d.end - d.start, dir)); // 실제 측정값
        writeGC(out, 42, measurement);
        writeGC(out, 70, dimType | 32); // Bit 5(32): 블록을 이 치수 전용으로 사용 (익명 블록)
        writeGC(out, 71, 5);
        writeGC(out, 73, 0);
        writeGC(out, 74, 0);
        writeGC(out, 75, 0);
        QString txt = buildDxfText(d);
        if (txt != "<>") writeGC(out,  1, txt);
        writeGC(out,  3, QString("STANDARD"));

        // 타입별 서브클래스
        writeGC(out, 100, QString("AcDbAlignedDimension"));
        writeGC(out, 13, d.start.x);
        writeGC(out, 23, d.start.y);
        writeGC(out, 33, 0.0);
        writeGC(out, 14, d.end.x);
        writeGC(out, 24, d.end.y);
        writeGC(out, 34, 0.0);
        if (d.useFixedAngle) {
            writeGC(out, 50, d.fixedAngle);
        }
        writeGC(out, 100, QString("AcDbRotatedDimension"));
        writeDimXdata(out, d, false, measurement);
        break;
    }

    // ── 반치수 (HalfDimension) ─────────────────────────────────────────────────
    case DimensionType::HalfDimension: {
        int dimType = d.useFixedAngle ? 0 : 1;

        MyVector fullEnd(2.0 * d.end.x - d.start.x,
                         2.0 * d.end.y - d.start.y);

        MyVector dir, perp;
        calcDimDirections(d, dir, perp);
        MyVector textMid = calcLinearTextMid(d.start, fullEnd, d.dimLine, perp);

        writeGC(out, 10, d.dimLine.x);
        writeGC(out, 20, d.dimLine.y);
        writeGC(out, 30, 0.0);
        writeGC(out, 11, textMid.x);
        writeGC(out, 21, textMid.y);
        writeGC(out, 31, 0.0);
        double measurement = std::abs(MyMath::dotProduct(fullEnd - d.start, dir));
        writeGC(out, 42, measurement);
        writeGC(out, 70, dimType | 32);
        writeGC(out, 71, 5);
        writeGC(out, 73, 0);
        writeGC(out, 74, 0);
        writeGC(out, 75, 0);
        QString txt = buildDxfText(d);
        if (txt != "<>") writeGC(out,  1, txt);
        writeGC(out,  3, QString("STANDARD"));

        writeGC(out, 100, QString("AcDbAlignedDimension"));
        writeGC(out, 13, d.start.x);
        writeGC(out, 23, d.start.y);
        writeGC(out, 33, 0.0);
        writeGC(out, 14, fullEnd.x);
        writeGC(out, 24, fullEnd.y);
        writeGC(out, 34, 0.0);
        if (d.useFixedAngle) {
            writeGC(out, 50, d.fixedAngle);
        }
        writeGC(out, 100, QString("AcDbRotatedDimension"));

        writeDimXdata(out, d, true, measurement);
        break;
    }

    // ── 각도 치수 (Angular) ────────────────────────────────────────────────────
    case DimensionType::Angular: {
        writeGC(out, 10, d.dimLine.x);
        writeGC(out, 20, d.dimLine.y);
        writeGC(out, 30, 0.0);
        writeGC(out, 11, d.dimLine.x);
        writeGC(out, 21, d.dimLine.y);
        writeGC(out, 31, 0.0);

        MyVector v1 = d.end - d.start;
        MyVector v2 = d.end2 - d.start;
        double ang1   = 0.0, ang2   = 0.0, angArc = 0.0;
        double angle  = 0.0;
        bool   valid  = (v1.magnitude() > 1e-9 && v2.magnitude() > 1e-9);
        if (valid) {
            ang1   = MyMath::normalizeAngle(qRadiansToDegrees(qAtan2(v1.y, v1.x)));
            ang2   = MyMath::normalizeAngle(qRadiansToDegrees(qAtan2(v2.y, v2.x)));
            angArc = MyMath::normalizeAngle(qRadiansToDegrees(
                         qAtan2(d.dimLine.y - d.start.y, d.dimLine.x - d.start.x)));
            double span = ang2 - ang1;
            if (span < 0) span += 360.0;
            if (!MyMath::isAngleBetween(angArc, ang1, span))
                span = 360.0 - span;
            angle = span;
        }
        writeGC(out, 42, angle);
        writeGC(out, 70, 5 | 32); // 5: 3-point angular, 32: anonymous block
        writeGC(out, 71, 5);
        writeGC(out, 73, 0);
        writeGC(out, 74, 0);
        writeGC(out, 75, 0);
        QString txt = buildDxfText(d);
        if (txt != "<>") writeGC(out,  1, txt);
        writeGC(out,  3, QString("STANDARD"));

        // AcDb3PointAngularDimension 그룹코드 의미:
        //   gc13/23: 첫번째 팔 끝점 (first arc line endpoint)
        //   gc14/24: 두번째 팔 끝점 (second arc line endpoint)
        //   gc15/25: 꼭짓점 = gc10과 동일한 각도 꼭짓점 (angle vertex, same as gc10)
        //   gc16/26: 호 정의점 — 호의 어느 쪽을 그릴지 결정하는 호 위의 점
        //
        // AutoCAD 내부 규칙:
        // 13/23: 첫번째 보조선 끝점 (d.end)
        // 14/24: 두번째 보조선 끝점 (d.end2)
        // 15/25: 각도 꼭짓점 (d.start)
        // 10/20: 치수선 호 정의점 (d.dimLine) - 오토캐드는 이 점이 속한 사분면을 기준으로 각도를 그립니다.
        
        writeGC(out, 100, QString("AcDb3PointAngularDimension"));
        writeGC(out, 13, d.end.x);
        writeGC(out, 23, d.end.y);
        writeGC(out, 33, 0.0);
        writeGC(out, 14, d.end2.x);
        writeGC(out, 24, d.end2.y);
        writeGC(out, 34, 0.0);
        writeGC(out, 15, d.start.x);   // 꼭짓점 (gc10과 동일)
        writeGC(out, 25, d.start.y);
        writeGC(out, 35, 0.0);
        writeGC(out, 16, d.dimLine.x); // 호 정의점 — gc16 방향이 올바른 호 쪽을 가리킴
        writeGC(out, 26, d.dimLine.y);
        writeGC(out, 36, 0.0);

        writeDimXdata(out, d, false, angle);
        break;
    }

    // ── 지름 치수 (Diametric) ──────────────────────────────────────────────────
    // d.start = 중심, d.end = center+(radius,0) (반지름 크기만 보존),
    // d.dimLine = 사용자가 클릭한 텍스트 배치 위치
    // DXF: gc10=반대쪽 호 점, gc15=텍스트쪽 호 점, gc11=텍스트 위치
    case DimensionType::Diametric: {
        double radius = (d.end - d.start).magnitude();
        MyVector rawDir = d.dimLine - d.start;
        double dist = rawDir.magnitude();
        MyVector dir = (dist > 1e-9) ? rawDir / dist : MyVector(1.0, 0.0);
        MyVector nearPt = d.start + dir * radius;   // 텍스트쪽 호 점
        MyVector farPt  = d.start - dir * radius;   // 반대쪽 호 점

        writeGC(out, 10, farPt.x);       // AcDbDimension 정의점 (반대쪽)
        writeGC(out, 20, farPt.y);
        writeGC(out, 30, 0.0);
        writeGC(out, 11, d.dimLine.x);   // 텍스트 중심점
        writeGC(out, 21, d.dimLine.y);
        writeGC(out, 31, 0.0);
        double measurement = radius * 2.0; // 측정값 = 지름
        writeGC(out, 42, measurement);
        writeGC(out, 70, 3 | 32);
        writeGC(out, 71, 5);
        writeGC(out, 73, 0);
        writeGC(out, 74, 0);
        writeGC(out, 75, 0);
        QString txt = buildDxfText(d);
        if (txt != "<>") writeGC(out,  1, txt);
        writeGC(out,  3, QString("STANDARD"));

        writeGC(out, 100, QString("AcDbDiametricDimension"));
        writeGC(out, 15, nearPt.x);      // 텍스트쪽 호 점
        writeGC(out, 25, nearPt.y);
        writeGC(out, 35, 0.0);
        // 리더 길이: 호 점에서 텍스트 위치까지 거리 (안쪽이면 0)
        writeGC(out, 40, qMax(0.0, (d.dimLine - nearPt).magnitude()));

        writeDimXdata(out, d, false, measurement);
        break;
    }

    // ── 반지름 치수 (Radial) ──────────────────────────────────────────────────
    // d.start = 중심, d.end = center+(radius,0) (반지름 크기만 보존),
    // d.dimLine = 사용자가 클릭한 텍스트 배치 위치
    // DXF: gc10=중심, gc15=텍스트 방향 호 점, gc11=텍스트 위치
    case DimensionType::Radial: {
        double radius = (d.end - d.start).magnitude();
        MyVector rawDir = d.dimLine - d.start;
        double dist = rawDir.magnitude();
        MyVector dir = (dist > 1e-9) ? rawDir / dist : MyVector(1.0, 0.0);
        MyVector arcPt = d.start + dir * radius;   // 텍스트 방향 호 위의 점

        writeGC(out, 10, d.start.x);     // AcDbDimension 정의점 = 중심
        writeGC(out, 20, d.start.y);
        writeGC(out, 30, 0.0);
        writeGC(out, 11, d.dimLine.x);   // 텍스트 위치
        writeGC(out, 21, d.dimLine.y);
        writeGC(out, 31, 0.0);
        writeGC(out, 42, radius);         // 측정값 = 반지름
        writeGC(out, 70, 4 | 32);
        writeGC(out, 71, 5);
        writeGC(out, 73, 0);
        writeGC(out, 74, 0);
        writeGC(out, 75, 0);
        QString txt = buildDxfText(d);
        if (txt != "<>") writeGC(out,  1, txt);
        writeGC(out,  3, QString("STANDARD"));

        writeGC(out, 100, QString("AcDbRadialDimension"));
        writeGC(out, 15, arcPt.x);       // 텍스트 방향 호 위의 점
        writeGC(out, 25, arcPt.y);
        writeGC(out, 35, 0.0);
        // 리더 길이: 호 점에서 텍스트 위치까지 거리 (안쪽이면 0)
        writeGC(out, 40, qMax(0.0, (d.dimLine - arcPt).magnitude()));

        writeDimXdata(out, d, false, radius);
        break;
    }

    } // switch 끝
}
