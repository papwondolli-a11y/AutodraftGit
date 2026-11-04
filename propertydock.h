#ifndef PROPERTYDOCK_H
#define PROPERTYDOCK_H

#include "scenesymbolregistry.h"
#include "holesectiongenerator.h"
#include <QDockWidget>
#include <QMap>
#include <QPointer>
#include <QTableWidget>
#include <QVariant>

class SelectionManager;
class LayerManager;
class IAppContext;
class MyGraphicsScene;
class QGraphicsItem;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class CadItem;

class PropertyDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PropertyDock(IAppContext *appContext, QWidget *parent = nullptr);
    void setSelectionManager(SelectionManager *mgr);
    void onSelectionChanged(const QList<QGraphicsItem *> &items);

public slots:
    void onPropertyChanged(int row, int column);
    void onLayerChanged(const QString &layerName);
    // [신규] SymbolLayer/TextLayer처럼 객체 자체에 독립적으로 저장된 "레이어 이름" 값을
    // 콤보박스로 편집할 때 쓰는 범용 슬롯 (onLayerChanged와 달리 아이템의 layerName()/pen()이
    // 아니라 setProperty(key, ...)로 반영되는 개별 속성이다). index로 받아야
    // " 현재 레이어 "(빈 문자열 데이터) 항목을 텍스트가 아닌 itemData로 정확히 구분할 수 있다.
    void onLayerNamePropertyChanged(int index);
    void onBoolPropertyChanged(const QString &value);
    // [신규] Hole의 탭가공/카운터보어/카운터싱크 On-Off 체크박스 전용
    // (onBoolPropertyChanged는 QComboBox 전용이라 QCheckBox::toggled(bool) 시그니처와 안 맞음)
    void onCheckBoxPropertyChanged(bool checked);
    void onTolTypeChanged(int index);
    void onDoublePropertyChanged(double value);
    // [신규] 구멍 지름 스핀박스 전용 - 화살표 클릭마다 뜨면 안 되는 "연결된 구멍 단면"
    // 확인 다이얼로그를, 편집이 끝난 시점(포커스 아웃/Enter)에 한 번만 띄운다.
    void onHoleDiameterEditingFinished();

    void onPrefixChanged(const QString &text);
    void onPrecisionChanged(int index);
    void onFitCodeChanged(const QString &text); // [신규] 끼워맞춤 등급기호(FitCode) 편집 슬롯
    // [신규] 편집 가능한 콤보박스(현재는 ThreadSpec)를 위한 범용 텍스트 속성 편집 슬롯.
    // onFitCodeChanged와 달리 다른 필드에 부가 반영(Suffix 동기화 등) 없이 그 키 하나만 갱신한다.
    void onComboTextPropertyChanged(const QString &text);

    // 심볼 편집 슬롯
    void onSymbolSpinboxFinished();
    void onSymbolFrameTypeChanged(int index);
    void onSymbolDoorShapeClicked(int shapeIndex);   // 문 형태(5종) 치환
    void onSymbolWindowShapeClicked(int shapeIndex); // 창문 형태(7종) 치환
    void onSymbolWindowDoubleChanged(int index);     // 단창(0)/복창(1) 전환
    void onSymbolWindowGlassToggled(bool on);        // 유리 표현선 on/off
    void onDeleteSymbol();

private:
    void updateDisplay();
    void showSymbolProperties(const SymbolInfo &info);
    static QString formatSymbolType(const QString &type);
    void rebuildSymbol(MyGraphicsScene *scene, const QString &groupId,
                       const SymbolInfo &oldInfo, const SymbolInfo &newInfo);
    void clearTables();
    QString formatValue(const QVariant &val);
    QString translateKey(const QString &key);
    // [신규] Hole의 지름에 영향을 주는 속성(Diameter/ThreadSpec/CbDiameter/CsDiameter 및
    // 관련 On-Off 체크박스)이 바뀔 때, HoleTool이 함께 만든 중심선(HOLE_DATA_GROUP_ID로
    // 연결된 독립 LineItem)의 길이도 같이 재계산한다. ci가 연결된 중심선이 없으면 아무 일도 안 한다.
    void syncHoleCenterLines(CadItem *ci);

    // ci(Hole)의 속성이 바뀐 뒤 호출. 연결된 단면이 있으면 하이라이트하고 확인 다이얼로그를
    // 띄워 "예"를 누른 경우에만 기존 단면을 지우고 같은 위치/방향으로 다시 생성한다.
    // [수정] 속성 위젯(체크박스 등)의 시그널 처리 도중에 호출되므로 즉시 실행하지 않고 큐에
    // 쌓았다가 이벤트 루프로 돌아온 뒤(flushPendingSectionSync) 처리한다. 하이라이트가 선택을
    // 바꿔 updateDisplay()가 위젯을 삭제하기 때문.
    void confirmAndSyncHoleSections(CadItem *ci);
    void flushPendingSectionSync();

    // [신규] 켜져 있는 카운터보어/카운터싱크 지름을 구멍의 현재 탭 규격에 맞춰 갱신한다
    // (MyHole::suggestedCbDiameter/suggestedCsDiameter). cb/cs로 대상을 고른다.
    void syncHoleHeadDiameters(CadItem *ci, bool cb, bool cs);

    // [신규] 탭가공된 구멍의 기본(드릴)지름이 바뀔 때(텍스트 편집/스핀박스 공용) 호출.
    // 기존 규격의 피치는 유지한 채 호칭지름(=골지름선 위치)을 새 드릴지름 + 피치로 다시
    // 계산해 ThreadSpec을 갱신한다 — onComboTextPropertyChanged의 ThreadSpec→Diameter
    // 갱신(탭드릴 근사식: 드릴지름 = 호칭지름 - 피치)을 역으로 적용한 것.
    void syncThreadedHoleMajorDiameter(CadItem *ci, double newDrillDiameter);

    IAppContext  *m_appContext;
    QTableWidget *m_topTable;
    QTableWidget *m_bottomTable;
    QTableWidget *m_extTable;
    QTableWidget *m_extTable2; // [신규] Hole 전용 3번째 그룹(탭가공/카운터보어/카운터싱크 중 3번째 활성 항목)
    QPushButton  *m_deleteSymbolBtn = nullptr;
    QList<QGraphicsItem *> m_selectedItems;
    QPointer<SelectionManager> m_currentManager;
    bool    m_isUpdating  = false;
    QList<CadItem *> m_pendingSectionSync;
    QString m_currentGroupId;   // 현재 선택된 심볼 UUID
};

#endif // PROPERTYDOCK_H