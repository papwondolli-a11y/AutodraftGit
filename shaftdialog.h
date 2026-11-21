#ifndef SHAFTDIALOG_H
#define SHAFTDIALOG_H

#include "shaftdata.h"

#include <QDialog>

class IAppContext;
class QCheckBox;
class QComboBox;
class QPushButton;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;

// ─────────────────────────────────────────────────────────────────────────
// ShaftDialog — 축(Shaft) 툴의 치수 입력 다이얼로그
//
// 표의 한 행이 축의 한 구간(왼쪽 끝 → 오른쪽 끝 순서).
//   열: 지름 / 길이 / 끼워맞춤 / 좌 모따기 / 우 모따기 / 지름치수 / 공차표시 / 길이치수 / 키홈
// 아래에 나사부(없음/왼쪽/오른쪽/양쪽), 중심선·치수 생성 옵션과 치수선 간격을 둔다.
// ─────────────────────────────────────────────────────────────────────────
class ShaftDialog : public QDialog {
    Q_OBJECT
public:
    // ctx: 레이어 목록(축 레이어 콤보)을 채우는 데 사용. nullptr 이면 기본 레이어만 보인다.
    explicit ShaftDialog(IAppContext *ctx = nullptr, QWidget *parent = nullptr);

    void setParams(const ShaftParams &params);
    ShaftParams params() const;

    void accept() override;

private slots:
    void addSegment();
    void removeSegment();
    void moveUp();
    void moveDown();
    void loadSample();
    void savePresetData();
    void loadPresetData();
    void updateTotal();
    void onThreadSideChanged(int index);
    void editThread();
    void onItemChanged(QTableWidgetItem *item);
    void onCellDoubleClicked(int row, int col);
    void editLengthTolerance(int row);

private:
    void setupUI();
    void appendRow(const ShaftSegment &seg);
    void swapRows(int a, int b);
    QString cellText(int row, int col) const;
    bool editThreadSizes();
    void syncThreadWidgets();
    // 키홈 칸: 값은 아이템 데이터(JSON)에 두고, 체크=사용, 글자=열림/막힘
    bool editKeyway(int row);
    ShaftKeyway rowKeyway(int row) const;
    void storeKeyway(int row, const ShaftKeyway &k);

    ShaftThreadSide m_threadSide = ShaftThreadSide::None;
    ShaftThread     m_leftThread;
    ShaftThread     m_rightThread;
    QComboBox      *m_threadCombo;
    QPushButton    *m_threadEdit;
    QTableWidget   *m_table;
    QLabel         *m_totalLabel;
    QCheckBox      *m_centerLine;
    QCheckBox      *m_diameterDims;
    QCheckBox      *m_lengthDims;
    QDoubleSpinBox *m_dimGap;
    QDoubleSpinBox *m_dimStep;
    QComboBox *m_layerCombo = nullptr;
    IAppContext *m_ctx = nullptr;
};

#endif // SHAFTDIALOG_H
