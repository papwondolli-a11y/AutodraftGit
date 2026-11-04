#ifndef TWOLINETOOL_H
#define TWOLINETOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"
#include <QList>
#include <QPointF>

class MyGraphicsScene;
class QGraphicsItem;
class LineItem;

class TwoLineTool : public AbstractTool {
    Q_OBJECT

public:
    explicit TwoLineTool(bool isFourLine = false, bool isSixLine = false,
                         bool isEccentric = false,
                         QObject *parent = nullptr);
    ~TwoLineTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

    static double  globalSpacing();
    static void    setGlobalSpacing(double v);

    static QString globalLayerName();
    static void    setGlobalLayerName(const QString &name);

    static bool    globalCenterLineEnabled();
    static void    setGlobalCenterLineEnabled(bool v);
    static QString globalCenterLineLayerName();
    static void    setGlobalCenterLineLayerName(const QString &name);

    // 몰타르선 — 4-Line / 6-Line 모드에서 사용
    static double  globalMortarSpacing();
    static void    setGlobalMortarSpacing(double v);
    static QString globalMortarLayerName();
    static void    setGlobalMortarLayerName(const QString &name);

    // 공간선 — 6-Line 모드에서만 사용
    static double  globalSpaceSpacing();
    static void    setGlobalSpaceSpacing(double v);
    static QString globalSpaceLayerName();
    static void    setGlobalSpaceLayerName(const QString &name);

    // 편심(비대칭) 오프셋 — 편심2/4-Line 모드에서 사용
    // 중심선 기준으로 left 쪽에 s_eccentricOffset, right 쪽에 (s_spacing - s_eccentricOffset) 적용
    // 모드(modeKey)별로 따로 저장: 0=2L 1=4L 2=6L 3=편심2L 4=편심4L 5=편심6L
    static constexpr int kModeCount = 6;
    static int     modeKey(bool isFourLine, bool isSixLine, bool isEccentric);
    static double  globalEccentricOffset(int mode);
    static void    setGlobalEccentricOffset(int mode, double v);

    // 편심6-Line 전용: 안쪩/공간/바깥쪩 벽 두께 및 몰타르 활성 플래그
    // 기하: 내벽선 = 기준-ecc, 공간내벽 = 기준+(안쪩-ecc), 공간외벽 = 기준+(안쪩-ecc+공간), 외벽선 = 기준+(안쪩-ecc+공간+바깥쪩)
    static double  globalInnerWallThickness();
    static void    setGlobalInnerWallThickness(double v);
    static double  globalSpaceThickness();
    static void    setGlobalSpaceThickness(double v);
    static double  globalOuterWallThickness();
    static void    setGlobalOuterWallThickness(double v);
    static bool    globalInnerMortarEnabled(int mode);
    static void    setGlobalInnerMortarEnabled(int mode, bool v);
    static bool    globalOuterMortarEnabled(int mode);
    static void    setGlobalOuterMortarEnabled(int mode, bool v);

    // [3-3] 다중선 Setup 값을 도면 스코프(DocumentSettings "TwoLineTool/*")에서
    //   불러오거나 저장한다. 도면값이 없으면 전역 QSettings/코드 기본값을 쓴다.
    //   activate() 및 FileManager::loadDrawing(레거시 multiline_setup 이관 후)에서 호출.
    static void    loadDocDefaults(MyGraphicsScene *scene);
    static void    saveDocDefaults(MyGraphicsScene *scene);

    bool isFourLine()   const { return m_isFourLine;   }
    bool isSixLine()    const { return m_isSixLine;    }
    bool isEccentric()  const { return m_isEccentric;  }

protected:
    void onFinish() override;
    void onCancel() override;

private:
    struct SegmentPair {
        LineItem *left        = nullptr;
        LineItem *right       = nullptr;
        LineItem *center      = nullptr;
        LineItem *mortarLeft  = nullptr;  // 4-Line / 6-Line 전용
        LineItem *mortarRight = nullptr;  // 4-Line / 6-Line 전용
        LineItem *spaceLeft   = nullptr;  // 6-Line 전용
        LineItem *spaceRight  = nullptr;  // 6-Line 전용
    };

    // Rebuild all segments on scene from m_vertices (+ optional extraEnd for preview).
    // If extraEnd is valid (not NaN), a ghost preview segment is drawn instead.
    void rebuildSegments(const QList<QPointF> &vertices);
    void updateGhostDouble(const QList<QPointF> &vertices, const QPointF &mousePos);

    // 대칭 오프셋 체인: 중심에서 ±halfOff
    void computeOffsetChain(const QList<QPointF> &verts, double halfOff,
                            QList<QPointF> &outLeft, QList<QPointF> &outRight);

    // 비대칭 오프셋 체인: left쪽 +leftOff, right쪽 -rightOff (편심2-Line 전용)
    void computeAsymmetricOffsetChain(const QList<QPointF> &verts,
                                      double leftOff, double rightOff,
                                      QList<QPointF> &outLeft, QList<QPointF> &outRight);

    // 편심6-Line 전용 세그먼트 재생성 (rebuildSegments에서 위임)
    void rebuildSegmentsEccentric6Line(const QList<QPointF> &vertices);

    static QPointF lineLineIntersect(QPointF P, QPointF d1, QPointF Q, QPointF d2, bool *ok);

    void removeAllSegmentsFromScene();
    void commitToUndoStack();
    void commitPairs(QList<SegmentPair> &pairs, const QString &macroName);
    void cancelDrawing();
    bool undoLastVertex();
    void closeLoop();
    void showPrompt();

    // ── Pick / Auto ───────────────────────────────────────────────────────────
    void enterPickMode();
    void exitPickMode(bool commit);
    void executeAuto();
    LineItem *findLineItemNear(const QPointF &scenePos);
    void      processPickItem(LineItem *li);
    void      finishPickBoxSelection(const QPointF &secondCorner);
    void      generateOffsetPairFromLine(const QLineF &center,
                                         const QString &layerName,
                                         const QPen   &pen,
                                         SegmentPair  &out);

    // 이 도구 인스턴스의 모드(4/6/편심…)에 해당하는 값
    int    mode() const;
    double eccOffset() const;
    bool   innerMortarOn() const;
    bool   outerMortarOn() const;

    QPen buildLayerPen(const QString &layerNameOverride = QString()) const;

    static double  s_spacing;
    static QString s_layerName;
    static bool    s_centerLineEnabled;
    static QString s_centerLineLayerName;
    static double  s_mortarSpacing;     // 기본값 24.0
    static QString s_mortarLayerName;
    static double  s_spaceSpacing;      // 기본값 50.0
    static QString s_spaceLayerName;
    static double  s_eccentricOffset[kModeCount];      // 모드별 중심선 위치, 기본값 175.0

    // 편심6-Line 전용 정적 변수
    static double  s_innerWallThickness;   // 안쪩, 기본값 200.0
    static double  s_spaceThickness;       // 공간, 기본값 50.0
    static double  s_outerWallThickness;   // 바깥쪩, 기본값 100.0
    static bool    s_innerMortarEnabled[kModeCount];   // 모드별 내벽 몰타르 활성, 기본값 true
    static bool    s_outerMortarEnabled[kModeCount];   // 모드별 외벽 몰타르 활성, 기본값 true

    const bool m_isFourLine;    // 생성 시 고정 — 4-Line / 6-Line 모드 여부
    const bool m_isSixLine;     // 생성 시 고정 — 6-Line 모드 여부
    const bool m_isEccentric;   // 생성 시 고정 — 편심2-Line 모드 여부
    bool    m_isDrawing;
    bool    m_pickMode;
    bool    m_pickBoxPending;   // Pick 박스 선택 진행 중
    QPointF m_pickBoxCorner;    // 박스 첫 번째 모서리
    QList<QPointF>      m_vertices;
    QList<SegmentPair>  m_segments;   // 진행 중인 일반 그리기 세그먼트
    QList<SegmentPair>  m_pickPairs;  // Pick 모드에서 생성된 쌍 (커밋 전 임시)
    QPointF             m_currentMousePos;
};

class TwoLineSetupCommand : public CommandBase {
    Q_OBJECT
public:
    explicit TwoLineSetupCommand(IAppContext *ctx,
                                 bool isFourLine  = false,
                                 bool isSixLine   = false,
                                 bool isEccentric = false,
                                 QObject *parent  = nullptr);
    void execute() override;
private:
    IAppContext *m_ctx;
    bool        m_isFourLine;
    bool        m_isSixLine;
    bool        m_isEccentric;
};

// ── 4-Line 전용 얇은 래퍼 ──────────────────────────────────────────────────────
class FourLineTool : public TwoLineTool {
    Q_OBJECT
public:
    explicit FourLineTool(QObject *parent = nullptr)
        : TwoLineTool(true, false, false, parent) {}
};

// ── 6-Line 전용 얇은 래퍼 ──────────────────────────────────────────────────────
class SixLineTool : public TwoLineTool {
    Q_OBJECT
public:
    explicit SixLineTool(QObject *parent = nullptr)
        : TwoLineTool(true, true, false, parent) {}
};

// ── 편심2-Line 전용 얇은 래퍼 ──────────────────────────────────────────────────
class EccentricTwoLineTool : public TwoLineTool {
    Q_OBJECT
public:
    explicit EccentricTwoLineTool(QObject *parent = nullptr)
        : TwoLineTool(false, false, true, parent) {}
};

// ── 편심4-Line 전용 얇은 래퍼 ──────────────────────────────────────────────────
class EccentricFourLineTool : public TwoLineTool {
    Q_OBJECT
public:
    explicit EccentricFourLineTool(QObject *parent = nullptr)
        : TwoLineTool(true, false, true, parent) {}
};

// ── 편심6-Line 전용 얇은 래퍼 ──────────────────────────────────────────────────
class EccentricSixLineTool : public TwoLineTool {
    Q_OBJECT
public:
    explicit EccentricSixLineTool(QObject *parent = nullptr)
        : TwoLineTool(true, true, true, parent) {}
};

#endif // TWOLINETOOL_H
