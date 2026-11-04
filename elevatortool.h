#ifndef ELEVATORTOOL_H
#define ELEVATORTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "mydimension.h"
#include <QDialog>
#include <QLineF>
#include <QPointF>
#include <QString>
#include <QVector>

class QCheckBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QDoubleSpinBox;
class QLabel;

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorParams — 엘리베이터 평면 설정 (사용자 입력값)
// ─────────────────────────────────────────────────────────────────────────────
struct ElevatorParams {
    double  carWidth  = 1790.0;   // 카폭
    double  carDepth  = 1647.0;   // 카깊이
    double  doorSize  = 1100.0;   // 문크기
    QString maker;                // 마지막에 선택한 제조사/규격 이름 (설정창 초기 선택용)
    bool    drawWalls = false;    // 승강로 벽체선(W1) 그리기
    bool    drawDims  = false;    // 치수선 그리기
    bool    compactCw = false;    // 8인승 이하 : 균형추 2/3 크기, 벽~균형추 틈 1/3 (설정창이 인승 선택으로 결정)
};

// ─────────────────────────────────────────────────────────────────────────────
// 인승별 규격 (Data/elevator/elevator_specs.json 에서 로드)
// ─────────────────────────────────────────────────────────────────────────────
struct ElevatorSpec {
    int    persons    = 0;     // 인승
    double loadKg     = 0.0;   // 정격하중
    double doorWidth  = 0.0;   // 출입구 폭
    double carWidth   = 0.0;   // 카 내부 폭
    double carDepth   = 0.0;   // 카 내부 깊이
    double shaftWidth = 0.0;   // 승강로 폭
    double shaftDepth = 0.0;   // 승강로 깊이
};

struct ElevatorMaker {
    QString               name;     // 제조사/제품/속도 구분 이름
    QString               source;   // 자료 출처(URL)
    QVector<ElevatorSpec> specs;
};

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorGeom — 생성될 도형 묶음 (레이어별)
// ─────────────────────────────────────────────────────────────────────────────
struct ElevatorArc {
    QPointF center;
    double  radius = 0.0;
    double  startAngle = 0.0;   // 도(deg)
    double  spanAngle  = 0.0;   // 도(deg)
};

struct ElevatorGeom {
    QVector<QLineF>        wallLines;   // W1 레이어 : 승강로 벽선
    QVector<QLineF>        elevLines;   // ELEV 레이어 : 카·문·카운터웨이트 등
    QVector<ElevatorArc>   elevArcs;    // ELEV 레이어 : 문틀 모서리 호
    QVector<MyDimension>   dims;        // 치수 (카폭·카깊이·문크기)
};

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorTool — 엘리베이터 평면
//
//   1) 승강로 P1(문 쪽 모서리) 지정   2) P2(대각 모서리) 지정
//   3) 설정창(카폭·카깊이·문크기)     → 승강로 벽선(W1) + 엘리베이터(ELEV) + 치수 생성
//   P1 이 있는 쪽 벽에 승강기 문이 만들어지고, 카는 승강로 가로 중앙에 놓인다.
//   원본 형상: data/elev.cdb (ELEV, W1 레이어)
//   우클릭 : 한 단계 되돌리기 (첫 단계에서는 종료)
// ─────────────────────────────────────────────────────────────────────────────
class ElevatorTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { WaitP1, WaitP2 };

    explicit ElevatorTool(const ElevatorParams &params, QObject *parent = nullptr);

    // true 면 설정창을 띄우지 않고 생성자에 준 값을 그대로 쓴다 (AI(MCP) 호출용, 저장 기본값 불변)
    void setHeadless(bool on) { m_headless = on; }

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static ElevatorParams loadParams(MyGraphicsScene *scene);
    static void           saveParams(MyGraphicsScene *scene, const ElevatorParams &p);

    // P1-P2 승강로와 설정값으로 전체 도형 계산
    static ElevatorGeom buildGeometry(const QPointF &p1, const QPointF &p2,
                                      const ElevatorParams &p);

    // 승강로 깊이에서 균형추 공간·앞 여유를 확보했을 때 들어갈 수 있는 최대 카깊이
    static double maxCarDepth(double shaftD, bool compactCw);

    // 승강로 안에 카/프레임이 들어가는지 검사 (들어가지 않으면 경고 문구 반환, 정상이면 빈 문자열)
    static QString checkFit(double shaftW, double shaftD, const ElevatorParams &p);

    // 규격 JSON 로드 (파일이 없으면 빈 목록)
    static QVector<ElevatorMaker> loadMakers();
    // 승강로 폭×깊이에 가장 가까운 인승 인덱스 (규격이 없으면 -1)
    static int nearestSpecIndex(const ElevatorMaker &maker, double shaftW, double shaftD);

    static const QString kWallLayer;   // "W1"
    static const QString kElevLayer;   // "ELEV"

private:
    void showPrompt();
    void cancelDrawing();
    bool showSetupDialog();
    void finish();
    void ensureLayer(IAppContext *ctx, const QString &name, const QColor &color);
    void addItem(QGraphicsItem *item, const QString &layerName);

    bool           m_headless = false;
    State          m_state;
    ElevatorParams m_params;
    QPointF        m_p1;
    QPointF        m_p2;
};

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorDialog — 카폭·카깊이·문크기 입력
// ─────────────────────────────────────────────────────────────────────────────
class ElevatorDialog : public QDialog {
    Q_OBJECT

public:
    ElevatorDialog(const ElevatorParams &params, double shaftW, double shaftD,
                   QWidget *parent = nullptr);

    ElevatorParams params() const;
    QString        selectedMakerName() const;

private slots:
    void updateInfo();
    void onMakerChanged(int index);
    void onSpecChanged(int index);
    void onSaveCustomClicked();                       // "사용자 크기" 저장
    void onCustomItemClicked(QListWidgetItem *item);  // 저장된 사용자 크기 불러오기
    void onCustomContextMenu(const QPoint &pos);      // 우클릭 삭제

private:
    void populateSpecCombo(bool selectNearest);
    void loadCustomList();
    bool saveCustomList();

    double                 m_shaftW;
    double                 m_shaftD;
    QVector<ElevatorMaker> m_makers;
    bool                   m_updating;
    bool                   m_compactCw = false;   // 선택 인승이 최소 인승 이하인지
    QString                m_capNote;   // 카깊이를 균형추 공간 때문에 줄였을 때 안내 문구
    class QComboBox       *m_comboMaker;
    class QComboBox       *m_comboSpec;
    QDoubleSpinBox *m_spinCarWidth;
    QDoubleSpinBox *m_spinCarDepth;
    QDoubleSpinBox *m_spinDoorSize;
    QListWidget    *m_listCustom;     // 왼쪽 : 저장된 사용자 크기 목록
    QPushButton    *m_btnSaveCustom;  // 문크기 아래 "사용자 크기" 버튼
    QCheckBox      *m_checkWalls;
    QCheckBox      *m_checkDims;
    QLabel         *m_labelInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// ElevatorCommand — 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class ElevatorCommand : public CommandBase {
    Q_OBJECT

public:
    explicit ElevatorCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // ELEVATORTOOL_H
