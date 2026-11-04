#ifndef DXFCOMMON_H
#define DXFCOMMON_H

#include <QString>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QPainterPath>
#include <vector>
#include "myvector.h"

struct DxfDimStyle {
    QString name;
    int dimTol = 0;
    int dimLim = 0;
    double dimTp = 0;
    double dimTm = 0;
    int dimDec = 2;
    int dimTdec = 2;
    int dimZin = 0;
    int dimTzin = 0;
    QString dimPost;
    double textSize = 2.5;
    int dimSe1 = 0;
    int dimSe2 = 0;
    int dimSd1 = 0;
    int dimSd2 = 0;
    double dimScale = 1.0;        // DIMSCALE (gc40): 치수 전체 축척
    QString fontName = "simplex";
};

struct DxfEntityData {
    QString type;
    QString handle;
    QString associatedHandle;
    QString layer = "0";
    double x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0;
    double radius = 0, startAngle = 0, endAngle = 0;
    QString text;
    double fontSize = 10.0;
    bool hasFontSize = false;
    double rotation = 0.0;
    int textAlignH = 0; // TEXT horizontal alignment: 0=left, 1=center, 2=right
    std::vector<MyVector> polyVertices;
    std::vector<double> polyBulges;
    bool isClosed = false;
    QString blockName;
    double scaleX = 1.0, scaleY = 1.0;
    int entitiesFollow = 0;
    double extZ = 1.0;
    double dx10 = 0, dy10 = 0, dx11 = 0, dy11 = 0;
    double dx13 = 0, dy13 = 0, dx14 = 0, dy14 = 0, dx15 = 0, dy15 = 0, dx16 = 0, dy16 = 0;
    int dimType = 0;
    double dimRotation = 0;
    QString dimStyleName = "STANDARD";
    int dimTol = 0;
    int dimLim = 0;
    double dimTp = 0;
    double dimTm = 0;
    bool hasDimTol = false;
    bool hasDimLim = false;
    bool hasDimTp = false;
    bool hasDimTm = false;
    int dimPrecision = -1;
    int dimTdec = -1;
    int dimZin = -1;
    int dimTzin = -1;
    int dimSe1 = -1;
    int dimSe2 = -1;
    int dimSd1 = -1;
    int dimSd2 = -1;
    bool isHalfDim = false;
    bool isAttribute = false;
    QString attrTag;
    QString attrPrompt;

    int mlSection = 0;
    bool mlContentPosSet = false;
    MyVector mlContentPos;
    MyVector mlDoglegPos;
    MyVector mlArrowTip;
    double mlPendingX = 0;
    QString mlBinaryData;

    // 다중선 그룹 태그 (XDATA AUTODRAFT ML_GROUP_ID / ML_ROLE / ML_SPACING)
    QString mlGroupId;
    QString mlRole;
    double  mlSpacing = 0.0;

    QString patternName;
    bool solidFill;
    double hatchAngle;
    double hatchScale;
    QPolygonF currentHatchLoop;
    QList<double> currentHatchBulge;
    bool inHatchBoundary = false;
    bool hatchEdgesStarted = false;  // gc93 통과 후 true: 엣지 루프 헤더 gc72 오용 방지
    int currentHatchLoopType = 0;
    QList<int> hatchLoopTypes;
    QList<QPainterPath> hatchPaths;

    struct HatchEdge {
        int type = 0; // 1: Line, 2: Arc, 4: Spline
        QPointF p1, p2;
        QPointF center;
        double radius = 0.0;
        double startAngle = 0.0, endAngle = 0.0;
        bool ccw = true;
        int degree = 3;
        bool rational = false;    // gc73: Spline edge만 해당
        bool periodic = false;    // gc74: Spline edge만 해당
        QList<double>  knotVector;    // gc40 반복: DXF 제공 실제 노트 벡터 (Spline edge만)
        QList<double>  weights;       // gc42 반복: Spline 제어점 가중치 (Rational B-spline)
        QList<QPointF> controlPoints; // gc10/20 반복: B-스플라인 제어점
        QList<QPointF> fitPoints;     // gc11/21 반복: 피팅점 (스플라인 경계 전용)
        QPointF majorAxis;            // gc11/21: Elliptical Arc 장축 (상대 좌표)
        double ratio = 1.0;           // gc40: Elliptical Arc 단축 비율
    };
    int hatchEdgeType = 0;
    QPainterPath currentHatchEdgePath;
    QList<HatchEdge> currentHatchEdges;

    // ELLIPSE: gc10/20 중심, gc11/21 장축 끝점(중심 기준 상대 벡터, x2/y2),
    //          gc40 단축비(radius), gc41/42 시작·끝 매개각(라디안)
    double ellipseStartParam = 0.0;
    double ellipseEndParam   = 6.283185307179586;

    std::vector<MyVector> splineControlPoints;
    std::vector<MyVector> splineFitPoints;
    std::vector<double>   splineKnots;
    int splineDegree = 3;
    bool splineClosed = false;

    void reset(const QString& t = "") {
        type = t; layer = "0"; handle = ""; associatedHandle = "";
        x1 = y1 = z1 = x2 = y2 = radius = startAngle = endAngle = rotation = 0;
        dx10 = dy10 = dx11 = dy11 = dx13 = dy13 = dx14 = dy14 = dx15 = dy15 = dx16 = dy16 = 0;
        dimType = 0; dimRotation = 0; fontSize = 10.0; hasFontSize = false; text = "";
        dimStyleName = "STANDARD";
        dimTol = 0; dimLim = 0; dimTp = 0; dimTm = 0;
        hasDimTol = false; hasDimLim = false; hasDimTp = false; hasDimTm = false;
        dimPrecision = -1; dimTdec = -1;
        dimZin = -1; dimTzin = -1;
        dimSe1 = -1; dimSe2 = -1; dimSd1 = -1; dimSd2 = -1;
        isHalfDim = false;
        isAttribute = false;
        attrTag = "";
        attrPrompt = "";
        polyVertices.clear(); polyBulges.clear(); isClosed = false;
        blockName = ""; scaleX = 1.0; scaleY = 1.0; extZ = 1.0; entitiesFollow = 0;
        mlSection = 0; mlContentPosSet = false; mlContentPos = MyVector();
        mlDoglegPos = MyVector(); mlArrowTip = MyVector(); mlPendingX = 0;
        mlGroupId = ""; mlRole = ""; mlSpacing = 0.0;
        patternName = ""; solidFill = false; hatchAngle = 0.0; hatchScale = 1.0;
        currentHatchLoop.clear(); hatchPaths.clear(); currentHatchBulge.clear();
        inHatchBoundary = false; hatchEdgesStarted = false; currentHatchLoopType = 0; hatchLoopTypes.clear();
        hatchEdgeType = 0; currentHatchEdgePath = QPainterPath();
        currentHatchEdges.clear();
        splineControlPoints.clear(); splineFitPoints.clear(); splineKnots.clear();
        splineDegree = 3; splineClosed = false;
        ellipseStartParam = 0.0; ellipseEndParam = 6.283185307179586;
    }
};

#endif // DXFCOMMON_H
