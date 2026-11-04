#include "dxfleaderhandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "leaderitem.h"
#include "myleader.h"

// =============================================================================
// DxfLeaderHandler — LEADER 엔티티 복원
// polyVertices를 순서대로 화살표 경로 점으로 추가한다.
// associatedHandle이 있으면 아이템 데이터 슬롯 1번에 저장하여
// 후처리 단계에서 GDT/텍스트와 연결할 수 있게 한다.
// =============================================================================

CadItem* DxfLeaderHandler::restore(const DxfEntityData& data,
                                    EntityFactory& factory,
                                    MyGraphicsScene* /*scene*/,
                                    const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                    const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    MyLeader l;

    // LEADER의 모든 정점을 순서대로 점 목록에 추가한다.
    for (size_t i = 0; i < data.polyVertices.size(); ++i) {
        l.points.append(data.polyVertices[i]);
    }

    QPen pen(Qt::white);
    CadItem* item = dynamic_cast<CadItem*>(factory.createLeader(l, pen));

    // 연관 핸들이 있으면 아이템에 저장한다 (후처리 연결 목적).
    if (!data.associatedHandle.isEmpty()) {
        item->setData(1, data.associatedHandle);
    }

    return item;
}

// =============================================================================
// DxfLeaderHandler — 내보내기
// =============================================================================

bool DxfLeaderHandler::canExport(CadItem* item) const
{
    LeaderItem* li = dynamic_cast<LeaderItem*>(item);
    if (!li) return false;
    // LeaderItems that have data(1) set hold a TOLERANCE handle — they are
    // part of a LEADER+TOLERANCE pair and are already exported by DxfGdtHandler.
    // Exporting them here too would produce duplicate LEADER lines.
    return li->data(1).toString().isEmpty();
}

void DxfLeaderHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    LeaderItem* leaderItem = dynamic_cast<LeaderItem*>(item);
    if (!leaderItem) return;

    const MyLeader& d = leaderItem->leaderData();
    if (d.points.size() < 2) return;

    const QString layer = item->layerName().isEmpty()
                          ? QStringLiteral("0") : item->layerName();
    bool hasText = !d.text.isEmpty();

    if (hasText) {
        // LEADER↔MTEXT 교차 참조를 위해 핸들을 미리 할당한다.
        QString leaderH = manager->allocateHandle();
        QString mtextH  = manager->allocateHandle();
        QString msH     = manager->modelSpaceHandle();

        // --- LEADER 엔티티 ---
        writeGC(out, 0,   QString("LEADER"));
        writeGC(out, 5,   leaderH);
        if (!msH.isEmpty()) writeGC(out, 330, msH);
        writeGC(out, 100, QString("AcDbEntity"));
        writeGC(out, 8,   layer);
        writeGC(out, 100, QString("AcDbLeader"));
        writeGC(out, 3,   QString("Standard"));
        writeGC(out, 71,  1);               // 화살촉 플래그
        writeGC(out, 72,  0);               // 패스 타입 (0=직선)
        writeGC(out, 73,  1);               // 생성 플래그 (1=MTEXT 연관)
        writeGC(out, 74,  1);               // 훅라인 방향 동일
        writeGC(out, 75,  1);               // 훅라인 플래그
        writeGC(out, 40,  d.textSize);      // 텍스트 높이
        writeGC(out, 41,  d.landingLength); // 훅라인/문자 너비
        writeGC(out, 76,  static_cast<int>(d.points.size()));
        for (int i = 0; i < d.points.size(); ++i) {
            const MyVector& pt = d.points.at(i);
            writeGC(out, 10, pt.x);
            writeGC(out, 20, pt.y);
            writeGC(out, 30, 0.0);
        }
        writeGC(out, 340, mtextH);          // 연결된 MTEXT 핸들
        writeGC(out, 210, 0.0); writeGC(out, 220, 0.0); writeGC(out, 230, 1.0);
        double hdir_x = 1.0;
        if (d.points.size() >= 2) {
            double dx = d.points.last().x - d.points.at(d.points.size() - 2).x;
            if (std::abs(dx) > 1e-4) hdir_x = (dx > 0) ? -1.0 : 1.0;
        }
        writeGC(out, 211, hdir_x); writeGC(out, 221, 0.0); writeGC(out, 231, 0.0);
        writeGC(out, 212, 0.0); writeGC(out, 222, 0.0); writeGC(out, 232, 0.0);
        writeGC(out, 213, 0.0); writeGC(out, 223, 0.0); writeGC(out, 233, 0.0);

        // --- MTEXT 엔티티 ---
        writeGC(out, 0,   QString("MTEXT"));
        writeGC(out, 5,   mtextH);
        writeGC(out, 102, QString("{ACAD_REACTORS"));
        writeGC(out, 330, leaderH);         // LEADER 역방향 참조
        writeGC(out, 102, QString("}"));
        if (!msH.isEmpty()) writeGC(out, 330, msH);
        writeGC(out, 100, QString("AcDbEntity"));
        writeGC(out, 8,   layer);
        writeGC(out, 100, QString("AcDbMText"));
        writeGC(out, 10,  d.points.last().x);
        writeGC(out, 20,  d.points.last().y);
        writeGC(out, 30,  0.0);
        writeGC(out, 40,  d.textSize);      // 텍스트 높이
        writeGC(out, 41,  0.0);             // 열 너비 (제한 없음)
        writeGC(out, 71,  7);               // 부착점: 좌하단
        writeGC(out, 72,  1);               // 출력 방향: 좌→우
        writeGC(out, 1,   d.text);
        writeGC(out, 7,   QString("Standard"));
        writeGC(out, 73,  1);
        writeGC(out, 44,  1.0);

    } else {
        // 텍스트 없는 단순 지시선 — 주석 없이 선 경로만 출력한다.
        manager->writeEntityAcDbHeader(out, "LEADER", layer);
        writeGC(out, 100, QString("AcDbLeader"));
        writeGC(out, 3,   QString("Standard"));
        writeGC(out, 71,  1);               // 화살촉 플래그
        writeGC(out, 72,  0);               // 직선
        writeGC(out, 73,  0);               // 생성 플래그: 연관 주석 없음
        writeGC(out, 74,  0);
        writeGC(out, 75,  0);
        writeGC(out, 40,  1.0);
        writeGC(out, 41,  d.landingLength);
        writeGC(out, 76,  static_cast<int>(d.points.size()));
        for (int i = 0; i < d.points.size(); ++i) {
            const MyVector& pt = d.points.at(i);
            writeGC(out, 10, pt.x);
            writeGC(out, 20, pt.y);
            writeGC(out, 30, 0.0);
        }
        writeGC(out, 210, 0.0); writeGC(out, 220, 0.0); writeGC(out, 230, 1.0);
        double hdir_x = 1.0;
        if (d.points.size() >= 2) {
            double dx = d.points.last().x - d.points.at(d.points.size() - 2).x;
            if (std::abs(dx) > 1e-4) hdir_x = (dx > 0) ? -1.0 : 1.0;
        }
        writeGC(out, 211, hdir_x); writeGC(out, 221, 0.0); writeGC(out, 231, 0.0);
        writeGC(out, 212, 0.0); writeGC(out, 222, 0.0); writeGC(out, 232, 0.0);
        writeGC(out, 213, 0.0); writeGC(out, 223, 0.0); writeGC(out, 233, 0.0);
    }
}
