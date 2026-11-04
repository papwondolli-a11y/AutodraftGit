#ifndef BEAMSCHEDULETOOL_H
#define BEAMSCHEDULETOOL_H

#include "abstracttool.h"
#include "beamschedule.h"
#include "commandbase.h"
#include <QDialog>
#include <QPointF>
#include <QRectF>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// BeamScheduleTool — 보 일람표
//   [기본 표 삽입]  표 삽입점(좌하단) 지정 → blocks/etc/beamlist.cdb 양식 표 + 보 내용 생성
//   [사용자표 사용] 도면에 이미 그려진 표의 "형태란" 내부 클릭 → 칸 크기 측정(표 정보와 비교)
//                   → 그 칸을 기준으로 표 정보대로 나머지 위치를 계산해 입력된 보 개수만큼 생성
//   우클릭 / Esc : 종료
// ─────────────────────────────────────────────────────────────────────────────
class BeamScheduleTool : public AbstractTool {
    Q_OBJECT

public:
    enum State { WaitInsert, WaitCell };

    explicit BeamScheduleTool(const BeamScheduleData &data, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static BeamScheduleData loadData(MyGraphicsScene *scene);
    static void             saveData(MyGraphicsScene *scene, const BeamScheduleData &d);

    // [기본 표] 도면에 이미 있는 일람표 찾기 (좌하단/축척). 없으면 false
    static bool findTable(MyGraphicsScene *scene, QRectF &rect, double &scale);
    // [기본 표] 일람표 생성. replaceExisting = true 이면 같은 영역의 기존 일람표 도형을 먼저 지운다 (Undo 1회)
    static void buildTable(IAppContext *ctx, MyGraphicsScene *scene, const BeamScheduleData &data,
                           const QPointF &origin, double scale, bool replaceExisting);

    // [사용자 표] 클릭점이 속한 칸(선으로 둘러싸인 사각형) 측정. 못 찾으면 false
    static bool measureCell(MyGraphicsScene *scene, const QPointF &pos, QRectF &cell,
                            const QStringList &excludeLayers = QStringList());
    // [사용자 표] 형태란 좌하단 기준으로 보 내용 생성 (이전에 만든 내용은 같은 영역에서 지움). 생성한 보 수 반환
    static int  buildCustomTable(IAppContext *ctx, MyGraphicsScene *scene,
                                 const BeamScheduleData &data, const QPointF &cellLL);

private:
    void   finishTool();
    void   place(const QPointF &origin);
    void   pickCell(const QPointF &pos);
    double scaleValue() const;

    BeamScheduleData m_data;
    State            m_state;
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamTableInfoDialog — 사용자표 정보 (칸 폭/높이, 도면 사이즈)
//   입력된 값으로 도면 사이즈 안에 들어가는 단·열을 계산해 보여준다.
// ─────────────────────────────────────────────────────────────────────────────
class BeamTableInfoDialog : public QDialog {
    Q_OBJECT

public:
    BeamTableInfoDialog(const BeamTableInfo &info, double drawingScale, QWidget *parent = nullptr);

    BeamTableInfo info() const;

private slots:
    void updateCapacity();
    void onResetDefaults();

private:
    QDoubleSpinBox *makeSpin(double value);
    void            setInfo(const BeamTableInfo &t);

    double          m_drawingScale;
    QComboBox      *m_comboPaper;
    QDoubleSpinBox *m_spinMarginW;
    QDoubleSpinBox *m_spinMarginH;
    QDoubleSpinBox *m_spinLabelW;
    QDoubleSpinBox *m_spinColW;
    QDoubleSpinBox *m_spinRightW;
    QDoubleSpinBox *m_spinRow[BeamTableInfo::RowCount];
    QDoubleSpinBox *m_spinGap;
    QLabel         *m_labelCapacity;
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamScheduleDialog — 보 입력
//   위 : 기본 표 삽입 / 사용자표 사용 (라디오) + 표 정보 버튼
//   왼쪽 : 입력된 보 목록(부호)   오른쪽 : 선택한 보의 부호/크기 + 3개 단면(상부근/하부근/스트럽)
// ─────────────────────────────────────────────────────────────────────────────
class BeamScheduleDialog : public QDialog {
    Q_OBJECT

public:
    BeamScheduleDialog(const BeamScheduleData &data, double drawingScale,
                       MyGraphicsScene *scene, LayerManager *layerMgr, QWidget *parent = nullptr);

    BeamScheduleData data();

    void accept() override;

private slots:
    void onSlotChanged(int row);
    void onNameEdited(const QString &text);
    void onNameEditingFinished();
    void onSlotContextMenu(const QPoint &pos);   // 목록 우클릭 : 삭제
    void onImportSigns();                        // 구조도에서 보 부호 가져오기
    void onResetList();                          // 리스트 초기화 (모든 슬롯 비움)
    void onModeChanged();
    void onTableInfoClicked();
    void onCopyCenterToEnds();
    void onCopyIntToExt();
    void onCopyFromPrevious();

private:
    struct SectionWidgets {
        QComboBox      *stirrupDia = nullptr;
        QDoubleSpinBox *pitch      = nullptr;
        QCheckBox      *high       = nullptr;
        QComboBox      *botDia     = nullptr;
        QSpinBox       *bot1       = nullptr;
        QSpinBox       *bot2       = nullptr;
        QComboBox      *topDia     = nullptr;
        QSpinBox       *top1       = nullptr;
        QSpinBox       *top2       = nullptr;
    };

    void commitCurrent();
    void loadSlot(int slot);
    void rebuildSlots();
    void refreshSlotText(int slot);
    void updateModeInfo();
    void compactSlots();
    QStringList duplicateNames() const;
    static void populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                                   const QString &selected);
    void fillDiaCombo(QComboBox *combo) const;
    void setSectionWidgets(int index, const BeamSection &s);
    BeamSection readSectionWidgets(int index) const;
    QString slotLabel(int slot) const;

    BeamScheduleData m_data;
    double           m_drawingScale;
    MyGraphicsScene *m_scene = nullptr;
    int              m_currentSlot = -1;
    bool             m_loading     = false;

    QRadioButton   *m_radioDefault;
    QRadioButton   *m_radioCustom;
    QPushButton    *m_btnTableInfo;
    QLabel         *m_labelModeInfo;
    QListWidget    *m_listSlots;
    QWidget        *m_editorPanel;
    QLineEdit      *m_editName;
    QDoubleSpinBox *m_spinWidth;
    QDoubleSpinBox *m_spinDepth;
    QComboBox      *m_comboBeamLayer;
    QComboBox      *m_comboStirrupLayer;
    QComboBox      *m_comboBarLayer;
    SectionWidgets  m_sec[3];
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamScheduleCommand — 설정창 → 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class BeamScheduleCommand : public CommandBase {
    Q_OBJECT

public:
    explicit BeamScheduleCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // BEAMSCHEDULETOOL_H
