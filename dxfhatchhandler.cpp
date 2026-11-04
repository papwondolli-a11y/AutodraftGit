#include "dxfhatchhandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "hatchitem.h"
#include "mygraphicsscene.h"
#include "iappcontext.h"
#include "hatchpatternmanager.h"

// =============================================================================
// DxfHatchHandler — HATCH 엔티티 복원
// HATCH 복원은 DxfManager에서 별도로 처리하므로 nullptr을 반환한다.
// =============================================================================

CadItem* DxfHatchHandler::restore(const DxfEntityData& /*data*/,
                                   EntityFactory& /*factory*/,
                                   MyGraphicsScene* /*scene*/,
                                   const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                   const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // DxfManager에서 직접 처리하므로 이 경로는 사용되지 않는다.
    return nullptr;
}

// =============================================================================
// DxfHatchHandler — HATCH 엔티티 내보내기
// HatchItem의 QPainterPath를 서브경로 폴리곤으로 변환하여 DXF 경계로 출력한다.
// 첫 번째 폴리곤(pIdx==0)이 외곽 경계, 나머지는 내부 섬(island)이다.
// =============================================================================

bool DxfHatchHandler::canExport(CadItem* item) const
{
    // HatchItem으로 캐스팅 가능한 경우에만 내보내기를 처리한다.
    return dynamic_cast<HatchItem*>(item) != nullptr;
}

void DxfHatchHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    HatchItem* hatch = dynamic_cast<HatchItem*>(item);
    if (!hatch) return;

    QMap<QString, QVariant> props = hatch->getProperties();
    QString wPat   = props.value("Pattern", "SOLID").toString().toUpper();
    if (wPat.isEmpty()) wPat = "SOLID";
    double  pAngle = props.value("Angle", 0.0).toDouble();
    double  pScale = props.value("Scale", 1.0).toDouble();

    QList<QPolygonF> polys = hatch->geometryPath().toSubpathPolygons();
    if (polys.isEmpty()) return;

    manager->writeEntityAcDbHeader(out, "HATCH", hatch->layerName());
    writeGC(out, 100, QString("AcDbHatch"));
    writeGC(out,  10, 0.0); // 기준점 X
    writeGC(out,  20, 0.0); // 기준점 Y
    writeGC(out,  30, 0.0); // 기준점 Z
    writeGC(out, 210, 0.0); // 법선 벡터 X
    writeGC(out, 220, 0.0); // 법선 벡터 Y
    writeGC(out, 230, 1.0); // 법선 벡터 Z
    writeGC(out,   2, wPat);
    writeGC(out,  70, (wPat == "SOLID") ? 1 : 0);
    writeGC(out,  71, 0); // 연관성 플래그
    writeGC(out,  91, static_cast<int>(polys.size()));

    // 각 서브경로(폴리곤)를 선분 경계 루프로 출력한다.
    for (int pIdx = 0; pIdx < polys.size(); ++pIdx) {
        const QPolygonF& poly = polys[pIdx];

        // 루프 타입: 0번이 외곽(External=1), 나머지는 내부 섬(0)
        writeGC(out, 92, (pIdx == 0) ? 1 : 0);
        // 폴리곤 마지막 점은 시작점과 같으므로 변(edge) 수는 size()-1 이다.
        writeGC(out, 93, static_cast<int>(poly.size()) - 1);

        // 각 변을 선분(edge type=1)으로 출력한다.
        for (int eIdx = 0; eIdx < poly.size() - 1; ++eIdx) {
            writeGC(out, 72, 1);                       // 변 타입 (1=선분)
            writeGC(out, 10, poly[eIdx].x());          // 시작점 X
            writeGC(out, 20, poly[eIdx].y());          // 시작점 Y
            writeGC(out, 11, poly[eIdx + 1].x());      // 끝점 X
            writeGC(out, 21, poly[eIdx + 1].y());      // 끝점 Y
        }

        // gc97: 소스 경계 객체 수 — 비연관 해치는 0, 루프마다 필수
        writeGC(out, 97, 0);
    }

    writeGC(out, 75, 0); // 해치 스타일 (0=Normal)
    writeGC(out, 76, 1); // 패턴 타입 (1=Predefined)

    // SOLID가 아닌 경우 패턴 정의를 추가로 출력한다.
    if (wPat != "SOLID") {
        // 패턴 매니저에서 실제 라인 정의를 가져와 gc53/43/44/45/46을 정확히 출력한다.
        // AutoCAD는 named pattern이 없으면 embedded 데이터를 사용하므로 반드시 올바른 값이어야 한다.
        HatchPatternManager* patMgr = nullptr;
        if (hatch->scene()) {
            MyGraphicsScene* myScene = qobject_cast<MyGraphicsScene*>(hatch->scene());
            if (myScene && myScene->context())
                patMgr = myScene->context()->hatchPatternManager();
        }
        HatchPattern pat;
        if (patMgr)
            pat = patMgr->getPattern(wPat);

        writeGC(out, 52, pAngle);  // 패턴 회전각 (사용자 설정)
        writeGC(out, 41, pScale);  // 패턴 스케일
        writeGC(out, 77, 0);       // 더블 해치 플래그

        if (pat.lines.empty()) {
            // 패턴을 찾지 못한 경우 최소한의 fallback 데이터
            writeGC(out, 78, 1);
            writeGC(out, 53, pAngle);
            writeGC(out, 43, 0.0);
            writeGC(out, 44, 0.0);
            writeGC(out, 45, 0.0);
            writeGC(out, 46, pScale);
            writeGC(out, 79, 0);
        } else {
            writeGC(out, 78, static_cast<int>(pat.lines.size()));
            for (const HatchLineDef& ld : pat.lines) {
                writeGC(out, 53, ld.angle + pAngle);   // 기본각 + 사용자 회전각
                writeGC(out, 43, ld.x0 * pScale);      // 기준점 X
                writeGC(out, 44, ld.y0 * pScale);      // 기준점 Y
                writeGC(out, 45, ld.dx * pScale);      // 라인 방향 오프셋
                writeGC(out, 46, ld.dy * pScale);      // 수직 방향 간격
                writeGC(out, 79, static_cast<int>(ld.dashes.size()));
                for (double d : ld.dashes)
                    writeGC(out, 49, d * pScale);       // 대시 길이
            }
        }
    }

    // gc98: 시드 포인트 수 — 패턴 재생성용 내부 기준점.
    // 외곽 폴리곤의 무게중심을 1개 시드 포인트로 사용한다.
    {
        const QPolygonF& outer = polys[0];
        double cx = 0.0, cy = 0.0;
        // 마지막 점은 시작점과 동일하므로 size()-1 개만 합산한다.
        int n = outer.size() - 1;
        for (int i = 0; i < n; ++i) {
            cx += outer[i].x();
            cy += outer[i].y();
        }
        if (n > 0) { cx /= n; cy /= n; }

        writeGC(out, 98, 1);   // 시드 포인트 1개
        writeGC(out, 10, cx);  // 시드 X
        writeGC(out, 20, cy);  // 시드 Y
    }
}
