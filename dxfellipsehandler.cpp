#include "dxfellipsehandler.h"
#include "dxfwritehelper.h"
#include "dxfmanager.h"
#include "caditem.h"
#include "entityfactory.h"
#include "ellipseitem.h"
#include "myellipse.h"
#include <cmath>

namespace {
const double kTwoPi = 6.283185307179586;
const double kHalfPi = 1.5707963267948966;
}

// =============================================================================
// DxfEllipseHandler — ELLIPSE 엔티티 복원
// =============================================================================

CadItem* DxfEllipseHandler::restore(const DxfEntityData& data,
                                     EntityFactory& factory,
                                     MyGraphicsScene* /*scene*/,
                                     const QMap<QString, DxfDimStyle>& /*dimStyles*/,
                                     const QMap<QString, QList<CadItem*>>& /*blocks*/)
{
    MyVector center(data.x1, data.y1);
    MyVector major(data.x2, data.y2); // 중심 기준 장축 끝점 벡터
    double ratio  = data.radius;      // gc40: 단축/장축 비율
    double startP = data.ellipseStartParam;
    double endP   = data.ellipseEndParam;

    if (major.magnitude() < 1.0e-12 || ratio <= 1.0e-12) return nullptr;

    // 시작·끝 매개각 → 호의 범위(span). 같거나 2π 이상이면 전체 타원.
    double span = std::fmod(endP - startP, kTwoPi);
    if (span < 0.0) span += kTwoPi;
    bool full = (std::fabs(endP - startP) < 1.0e-9) ||
                (std::fabs(endP - startP) >= kTwoPi - 1.0e-9) ||
                (span < 1.0e-9);

    // 비정상 파일 방어: ratio > 1 이면 단축이 더 길다 → 축을 서로 교환한다.
    // (새 장축 = 기존 단축 방향 × 길이, 매개각은 π/2 만큼 밀린다)
    if (ratio > 1.0) {
        major = MyVector(-major.y * ratio, major.x * ratio);
        ratio = 1.0 / ratio;
        startP -= kHalfPi;
    }

    // 법선 Z 가 음수면 평면이 뒤집혀 있어 매개각 진행 방향이 반대가 된다.
    if (data.extZ < 0.0) {
        startP = -startP - span;
    }

    MyEllipse e;
    e.center = center;
    e.majorAxis = major;
    e.ratio = ratio;
    if (full) {
        e.startParam = 0.0;
        e.endParam = kTwoPi;
    } else {
        e.startParam = startP;
        e.endParam = startP + span;
        e.normalizeParams();
    }

    QPen pen(Qt::white);
    return dynamic_cast<CadItem*>(factory.createEllipse(e, pen));
}

// =============================================================================
// DxfEllipseHandler — ELLIPSE 엔티티 내보내기 (AC1015)
//   100 AcDbEllipse → 중심(10/20/30) → 장축 끝점(11/21/31) → 법선(210/220/230)
//   → 단축비(40) → 시작 매개각(41) → 끝 매개각(42)
// =============================================================================

bool DxfEllipseHandler::canExport(CadItem* item) const
{
    return dynamic_cast<EllipseItem*>(item) != nullptr;
}

void DxfEllipseHandler::exportDxf(QTextStream& out, CadItem* item, DxfManager* manager)
{
    EllipseItem* ellipse = dynamic_cast<EllipseItem*>(item);
    MyEllipse e = ellipse->getEllipse();

    manager->writeEntityAcDbHeader(out, "ELLIPSE", ellipse->layerName());
    writeGC(out, 100, QString("AcDbEllipse"));
    writeGC(out, 10, e.center.x);
    writeGC(out, 20, e.center.y);
    writeGC(out, 30, 0.0);
    writeGC(out, 11, e.majorAxis.x);
    writeGC(out, 21, e.majorAxis.y);
    writeGC(out, 31, 0.0);
    writeGC(out, 210, 0.0);
    writeGC(out, 220, 0.0);
    writeGC(out, 230, 1.0);
    writeGC(out, 40, e.ratio);
    if (e.isFull()) {
        writeGC(out, 41, 0.0);
        writeGC(out, 42, kTwoPi);
    } else {
        writeGC(out, 41, e.startParam);
        writeGC(out, 42, e.endParam);
    }
}
