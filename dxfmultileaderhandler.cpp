#include "dxfmultileaderhandler.h"
#include "caditem.h"
#include "entityfactory.h"
#include "mygraphicsscene.h"
#include "myleader.h"
#include "datumitem.h"   // DatumData 구조체
#include "mygdt.h"
#include "gdtitem.h"
#include "cadutils.h"

// =============================================================================
// DxfMultiLeaderHandler — MULTILEADER 엔티티 복원
//
// mlContentPos(텍스트 위치), mlDoglegPos(꺾임점), mlArrowTip(화살표 끝)
// 세 점을 조합하여 리더 경로를 구성한다.
//
// 판별 로직:
//  - associatedHandle이 비어 있고 텍스트가 2자 이하이면 → DatumItem (데이텀 기호)
//  - 그 외 → LeaderItem (지시선)
// =============================================================================

CadItem* DxfMultiLeaderHandler::restore(const DxfEntityData& data,
                                         EntityFactory& factory,
                                         MyGraphicsScene* scene,
                                         const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                         const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    // 텍스트가 없으면 복원하지 않는다.
    if (data.text.isEmpty()) return nullptr;

    // 리더 경로 점을 구성한다.
    // LEADER_LINE{} polyVertices: 화살표 끝점.
    // mlContentPos: 텍스트/박스 부착점(박스 중심).
    // MULTILEADER 내부의 도그레그(수평 훅라인) 경로는 AutoCAD가 자체 처리하므로
    // LEADER+TOLERANCE 변환 시에는 포함하지 않는다. 포함하면 꺾인 선이 그대로 노출된다.
    QList<MyVector> pts;

    if (!data.polyVertices.empty()) {
        // LEADER_LINE 정점 전체를 순서대로 추가한다 (화살표 끝 → 랜딩 방향).
        int vertCount = static_cast<int>(data.polyVertices.size());
        for (int i = 0; i < vertCount; ++i)
            pts << data.polyVertices.at(i);
    } else if (data.mlArrowTip.magnitude() > 1e-6) {
        pts << data.mlArrowTip;
    }

    // 유효한 content position이 있으면 pts 마지막에 추가한다.
    // DWG import에서 position 읽기에 실패한 경우(mlContentPosSet=false 또는 (0,0)):
    //   polyVertices의 마지막 점이 데이텀 박스 위치 대용으로 사용된다.
    bool hasValidContentPos = data.mlContentPosSet && data.mlContentPos.magnitude() > 1e-6;
    if (hasValidContentPos) {
        pts << data.mlContentPos;
    }

    // leader 정점도 없고 content pos도 없으면 복원 불가
    if (pts.isEmpty()) return nullptr;

    // 점이 하나뿐이면 왼쪽으로 5단위 오프셋한 점을 앞에 추가한다.
    if (pts.size() == 1) {
        MyVector p = pts.first();
        pts.clear();
        pts << p - MyVector(5, 0) << p;
    }

    QPen pen(Qt::white);

    // 연관 핸들 없고 텍스트가 짧으면 데이텀 기호로 처리한다.
    // 서식 문자와 특수 기호(하이픈, 공백 등)를 제거하여 순수 텍스트 길이를 판별한다.
    QString clean = data.text;
    clean = CadUtils::processText(clean);
    clean.remove('-');
    clean.remove(' ');
    clean.remove(QChar(0x1D));
    clean = clean.trimmed();

    if (data.associatedHandle.isEmpty() && clean.length() <= 2 && !clean.isEmpty()) {
        // 문자 크기: 씬 치수 스타일 → 엔티티 fontSize → 기본값 2.5 순으로 결정한다.
        double bS = scene ? scene->dimensionStyle().textSize
                          : (data.fontSize > 0.1 ? data.fontSize : 2.5);

        // 데이텀 지시선은 수직 또는 수평으로 일직선이어야 하므로,
        // 시작점(삼각형)과 끝점(상자) 2개로 단순화하고 축 정렬을 강제한다.
        if (pts.size() >= 2) {
            MyVector first = pts.first();
            MyVector last  = pts.last();

            double dx = std::abs(last.x - first.x);
            double dy = std::abs(last.y - first.y);
            if (dy > dx) {
                last.x = first.x;
            } else {
                last.y = first.y;
            }

            // [신규] 화살촉과 박스 간의 최소 거리 확보 로직
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
            
            pts.clear();
            pts << first << last;
        }

        // DWG/DXF 파일에서 명시된 content position이 있는 경우(mlContentPosSet=true)
        // 파일 원본 좌표를 존중하여 최소 길이 연장을 적용하지 않는다.
        // 연장은 사용자가 AutoDraft에서 직접 생성하는 경우에만 적용한다(mlContentPosSet=false).
        if (!data.mlContentPosSet) {
            double cL   = bS * 2.5;
            double curL = (pts.last() - pts[pts.size() - 2]).magnitude();
            if (curL > 1e-6 && curL < cL) {
                MyVector& last = pts.last();
                MyVector   prev = pts[pts.size() - 2];
                if (std::abs(last.y - prev.y) < std::abs(last.x - prev.x)) {
                    last.x = prev.x + (last.x > prev.x ? cL : -cL);
                } else {
                    double f = cL / curL;
                    last.x   = prev.x + (last.x - prev.x) * f;
                    last.y   = prev.y + (last.y - prev.y) * f;
                }
            }
        }

        DatumData d;
        d.text         = clean; // 하이픈 및 서식이 완전히 정제된 텍스트 대입
        d.leaderPoints = pts;
        d.startPoint  = d.leaderPoints.first();
        d.endPoint    = d.leaderPoints.last();
        d.textHeight  = bS;
        return dynamic_cast<CadItem*>(factory.createDatum(d, pen));

    } else if (data.text.contains("%%v")) {
        // [추가] 기하공차(GDT) 패턴이 포함된 경우 GdtItem으로 복원한다.
        QStringList p = data.text.split("%%v", Qt::KeepEmptyParts);
        GdtData g;
        g.textHeight   = (data.fontSize > 0.1 ? data.fontSize : 2.5);
        g.leaderPoints = pts;
        g.useLeader = true;
        g.position = g.leaderPoints.last();

        // 첫 번째 섹션에서 기호 추출
        if (!p.isEmpty()) {
            QString s = p[0];
            int idx = s.indexOf(';');
            QString sym = (idx != -1 && s.endsWith('}')) ? s.mid(idx+1, s.length()-idx-2) : s;
            // TOLERANCE 엔티티와 동일한 gdt.shx 문자 매핑을 사용한다.
            // (dxfgdthandler.cpp 기준 — 한국어 AutoCAD 2018 사용자 확인 값)
            if      (sym == "j") g.symbol = GdtSymbol::Position;         // j=위치도
            else if (sym == "r") g.symbol = GdtSymbol::Concentricity;    // r=동심도
            else if (sym == "i") g.symbol = GdtSymbol::Symmetry;         // i=대칭도
            else if (sym == "f") g.symbol = GdtSymbol::Parallelism;      // f=평행도
            else if (sym == "b") g.symbol = GdtSymbol::Perpendicularity; // b=직각도
            else if (sym == "a") g.symbol = GdtSymbol::Angularity;       // a=경사도
            else if (sym == "g") g.symbol = GdtSymbol::Cylindricity;     // g=원통도
            else if (sym == "c") g.symbol = GdtSymbol::Flatness;         // c=평면도
            else if (sym == "e") g.symbol = GdtSymbol::Circularity;      // e=진원도
            else if (sym == "u") g.symbol = GdtSymbol::Straightness;     // u=직진도
            else if (sym == "d") g.symbol = GdtSymbol::SurfaceProfile;   // d=면의윤곽도
            else if (sym == "k") g.symbol = GdtSymbol::LineProfile;      // k=선의윤곽도
            else if (sym == "h") g.symbol = GdtSymbol::CircularRunout;   // h=원주흔들림
            else if (sym == "t") g.symbol = GdtSymbol::TotalRunout;      // t=전체흔들림
            else                 g.symbol = GdtSymbol::Position;
        }
        // 공차값 및 데이텀 파싱
        if (p.size() > 1) {
            QString t = p[1];
            if (t.contains("{\\Fgdt;n}") || t.contains("%%c")) {
                g.hasDiameter = true;
                t.replace("{\\Fgdt;n}", ""); t.replace("%%c", "");
            }
            g.toleranceValue = t;
        }
        for (int i = 3; i < p.size(); i += 2) {
            if (!p[i].isEmpty()) g.datumRefs.append(p[i]);
        }

        CadItem* it = dynamic_cast<CadItem*>(factory.createGdt(g, pen));
        if (!data.associatedHandle.isEmpty()) it->setData(1, data.associatedHandle);
        return it;

    } else {
        // 일반 지시선(LeaderItem)으로 처리한다.
        MyLeader l;
        l.text     = data.text;
        l.points   = pts;
        l.textSize = (data.fontSize > 0.1 ? data.fontSize : 2.5);

        CadItem* it = dynamic_cast<CadItem*>(factory.createLeader(l, pen));
        // 연관 핸들이 있으면 아이템 데이터에 저장한다.
        if (!data.associatedHandle.isEmpty()) {
            it->setData(1, data.associatedHandle);
        }
        return it;
    }
}

// =============================================================================
// DxfMultiLeaderHandler — 내보내기 (미지원)
// =============================================================================

bool DxfMultiLeaderHandler::canExport(CadItem* /*item*/) const
{
    // MULTILEADER 내보내기는 현재 미지원이다.
    return false;
}

void DxfMultiLeaderHandler::exportDxf(QTextStream& /*out*/, CadItem* /*item*/, DxfManager* /*manager*/)
{
    // 내보내기 미구현
}
