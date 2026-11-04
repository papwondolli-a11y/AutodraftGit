#include "boxtextutil.h"
#include "cadcommands.h"
#include "caditem.h"
#include "mygraphicsscene.h"
#include "mypolyline.h"
#include "mytext.h"
#include "polylineitem.h"
#include "textitem.h"
#include "tooldefaults.h"
#include <QFont>
#include <QFontMetricsF>
#include <QList>
#include <QUndoStack>

namespace {
const double kVerticalMarginRatio = 0.5; // 글자 상하 여유 공간 (대문자 높이 대비 비율)
const double kMatchToleranceRatio = 0.01; // 꼭짓점 일치 허용오차 (글자 크기 대비 비율)
}

bool BoxTextUtil::isLinkBoxSizeEnabled(MyGraphicsScene *scene) {
    return ToolDefaults(settingsNamespace(), scene).getBool("LinkBoxSize", true);
}

BoxTextUtil::BoxFollowState BoxTextUtil::captureBox(TextItem *textItem) {
    BoxFollowState state;
    if (!textItem) return state;

    MyGraphicsScene *scene = dynamic_cast<MyGraphicsScene *>(textItem->scene());
    if (!scene || !isLinkBoxSizeEnabled(scene)) return state;

    state.oldCorners = boxCorners(textItem);
    state.tolerance = matchTolerance(textItem);
    state.box = findBox(scene, state.oldCorners, state.tolerance);
    return state;
}

void BoxTextUtil::followText(QUndoStack *stack, TextItem *textItem,
                             const BoxFollowState &state) {
    if (!state.box || !textItem) return;
    resizeBox(stack, state.box, state.oldCorners, boxCorners(textItem), state.tolerance);
}

std::vector<MyVector> BoxTextUtil::boxCorners(const TextItem *textItem) {
    std::vector<MyVector> corners;
    if (!textItem) return corners;

    QRectF localRect = textItem->boundingRect();

    const MyText *textData = static_cast<const MyText *>(textItem->getEntityData());
    QFont font(textData->fontFamily);
    font.setPointSizeF(textData->fontSize);
    font.setBold(textData->bold);
    font.setItalic(textData->italic);
    QFontMetricsF fm(font);
    double charWidth = fm.averageCharWidth();

    // 세로 범위: boundingRect()는 lineSpacing(ascent+descent+leading) 기반이라
    // 실제 글자보다 크므로, 글자 외곽선(geometryPath)의 실제 범위를 사용한다.
    // 대문자 높이/베이스라인까지는 항상 포함해 글자 내용에 따라 박스 높이가 들쭉날쭉하지 않게 함.
    QRectF glyphRect = textItem->geometryPath().boundingRect();
    int lineCount = textData->text.split('\n').size();
    double top = -fm.capHeight();
    double bottom = (lineCount - 1) * fm.lineSpacing();
    if (!glyphRect.isEmpty()) {
        top = qMin(top, glyphRect.top());
        bottom = qMax(bottom, glyphRect.bottom());
    }

    // 상하 여유는 글자 크기에 비례 (고정값이면 작은 글자에서 박스가 과도하게 커짐)
    double vMargin = fm.capHeight() * kVerticalMarginRatio;

    QRectF boxRect(localRect.left() - charWidth,
                   top - vMargin,
                   localRect.width() + charWidth * 2.0,
                   (bottom - top) + vMargin * 2.0);

    corners.push_back(MyVector(textItem->mapToScene(boxRect.topLeft())));
    corners.push_back(MyVector(textItem->mapToScene(boxRect.topRight())));
    corners.push_back(MyVector(textItem->mapToScene(boxRect.bottomRight())));
    corners.push_back(MyVector(textItem->mapToScene(boxRect.bottomLeft())));
    return corners;
}

double BoxTextUtil::matchTolerance(const TextItem *textItem) {
    double tol = 1e-3;
    if (textItem) {
        const MyText *textData = static_cast<const MyText *>(textItem->getEntityData());
        tol = qMax(tol, textData->fontSize * kMatchToleranceRatio);
    }
    return tol;
}

int BoxTextUtil::matchVertex(const PolylineItem *box, const MyVector &corner,
                             double tolerance) {
    const std::vector<MyVector> verts = box->getPolyline().vertices;
    for (int i = 0; i < (int)verts.size(); ++i) {
        MyVector v(box->mapToScene(verts[i].toPointF()));
        if (v.distanceTo(corner) <= tolerance) return i;
    }
    return -1;
}

PolylineItem *BoxTextUtil::findBox(MyGraphicsScene *scene,
                                   const std::vector<MyVector> &corners,
                                   double tolerance) {
    if (!scene || corners.size() != 4) return nullptr;

    QRectF area;
    for (size_t i = 0; i < corners.size(); ++i)
        area = area.united(QRectF(corners[i].toPointF(), QSizeF(0, 0)));
    area.adjust(-tolerance, -tolerance, tolerance, tolerance);

    const QList<QGraphicsItem *> items = scene->items(area, Qt::IntersectsItemBoundingRect);
    for (int i = 0; i < items.size(); ++i) {
        PolylineItem *poly = dynamic_cast<PolylineItem *>(items.at(i));
        if (!poly) continue;
        if (dynamic_cast<CadItem *>(poly->parentItem())) continue; // 블록 내부 제외

        MyPolyline data = poly->getPolyline();
        if (!data.isClosed || data.vertices.size() != 4) continue;

        bool straight = true;
        for (size_t b = 0; b < data.bulges.size(); ++b) {
            if (qAbs(data.bulges[b]) > 1e-9) { straight = false; break; }
        }
        if (!straight) continue;

        // 꼭짓점 순서(시작점·방향)는 저장/불러오기에서 바뀔 수 있으므로 순서 무관하게 비교
        bool used[4] = {false, false, false, false};
        bool allMatched = true;
        for (size_t c = 0; c < 4; ++c) {
            int idx = matchVertex(poly, corners[c], tolerance);
            if (idx < 0 || used[idx]) { allMatched = false; break; }
            used[idx] = true;
        }
        if (allMatched) return poly;
    }
    return nullptr;
}

void BoxTextUtil::resizeBox(QUndoStack *stack, PolylineItem *box,
                            const std::vector<MyVector> &oldCorners,
                            const std::vector<MyVector> &newCorners,
                            double tolerance) {
    if (!box || oldCorners.size() != 4 || newCorners.size() != 4) return;

    // 먼저 모든 인덱스를 구한 뒤 이동 (이동 중 정점 위치가 바뀌어 매칭이 어긋나지 않게)
    int indices[4];
    for (int c = 0; c < 4; ++c) {
        indices[c] = matchVertex(box, oldCorners[c], tolerance);
        if (indices[c] < 0) return;
    }

    const std::vector<MyVector> verts = box->getPolyline().vertices;
    for (int c = 0; c < 4; ++c) {
        // 폴리선 로컬 좌표 기준 이동량
        MyVector target(box->mapFromScene(newCorners[c].toPointF()));
        MyVector delta = target - verts[indices[c]];
        if (delta.magnitude() < 1e-12) continue;

        QList<int> idxList;
        idxList.append(indices[c]);
        if (stack) {
            stack->push(new StretchEntityCommand(box, idxList, delta));
        } else {
            box->stretch(idxList, delta);
        }
    }
}
