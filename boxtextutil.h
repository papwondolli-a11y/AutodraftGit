#ifndef BOXTEXTUTIL_H
#define BOXTEXTUTIL_H

#include "myvector.h"
#include <QString>
#include <vector>

class TextItem;
class PolylineItem;
class MyGraphicsScene;
class QUndoStack;

// BOXTEXT 박스 계산·탐색·크기조정 공용 모듈.
//
// 글자와 박스 사이에 별도 링크(태그)를 저장하지 않는다. 대신 "편집 전 글자로 계산한
// 박스 네 꼭짓점과 정확히 겹치는 닫힌 4점 폴리선"을 그 글자의 박스로 간주한다.
// 그래서 CDB/DXF 저장·불러오기 후에도 별도 데이터 없이 연동이 유지된다.
// (글자나 박스를 따로 옮기면 더 이상 일치하지 않으므로 자동 조정 대상에서 빠진다.)
class BoxTextUtil {
public:
    // 글자 속성 변경 전후로 박스를 따라가게 할 때 쓰는 상태
    //   BoxFollowState st = captureBox(text);   // 변경 전
    //   ... 글자 속성 변경 ...
    //   followText(stack, text, st);            // 변경 후 (박스 없으면 아무것도 안 함)
    struct BoxFollowState {
        PolylineItem *box = nullptr;
        std::vector<MyVector> oldCorners;
        double tolerance = 0.0;
    };

    // 글자의 박스를 찾아 현재 꼭짓점을 기억. "박스크기 연동"이 꺼져 있거나 박스가 없으면 box=nullptr
    static BoxFollowState captureBox(TextItem *textItem);
    static void followText(QUndoStack *stack, TextItem *textItem, const BoxFollowState &state);

    // Setup 설정 저장 네임스페이스 (ToolDefaults: 도면 + 전역)
    //   BoxLayer(QString) / TextLayer(QString) / LinkBoxSize(bool, 기본 true)
    static QString settingsNamespace() { return QStringLiteral("BoxTextTool"); }

    // Setup의 "박스크기 연동" 설정값 (글자 수정 시 박스 크기 자동 변경 여부)
    static bool isLinkBoxSizeEnabled(MyGraphicsScene *scene);

    // 글자 객체에 맞는 박스 네 꼭짓점(씬 좌표, 좌상→우상→우하→좌하)
    static std::vector<MyVector> boxCorners(const TextItem *textItem);

    // 꼭짓점 비교 허용오차 (글자 크기 비례)
    static double matchTolerance(const TextItem *textItem);

    // corners(씬 좌표)와 네 꼭짓점이 모두 일치하는 닫힌 4점 폴리선을 찾는다. 없으면 nullptr.
    static PolylineItem *findBox(MyGraphicsScene *scene,
                                 const std::vector<MyVector> &corners,
                                 double tolerance);

    // 박스(oldCorners와 일치하는 폴리선)의 각 꼭짓점을 newCorners로 옮긴다.
    // stack이 있으면 꼭짓점별 StretchEntityCommand로 Undo 가능하게 처리.
    static void resizeBox(QUndoStack *stack, PolylineItem *box,
                          const std::vector<MyVector> &oldCorners,
                          const std::vector<MyVector> &newCorners,
                          double tolerance);

private:
    // box의 정점 중 corner(씬 좌표)와 일치하는 정점 인덱스. 없으면 -1.
    static int matchVertex(const PolylineItem *box, const MyVector &corner,
                           double tolerance);
};

#endif // BOXTEXTUTIL_H
