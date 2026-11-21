#ifndef SHAFTENDDIALOG_H
#define SHAFTENDDIALOG_H

#include "shaftdata.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────
// ShaftKeywayDialog — 축 한 구간의 키홈 크기·위치 입력
//   형태(끝 열림/막힘) · 위치 기준(구간 중앙/왼쪽 면/오른쪽 면)+거리 · 길이
//   상단/중앙/하단 · [KS 평행키 표준값] · 홈 깊이 · 단면 표시
//   끝 열림은 첫 구간(왼쪽)·마지막 구간(오른쪽)에서만 고를 수 있다.
// ─────────────────────────────────────────────────────────────────────────
class ShaftKeywayDialog : public QDialog {
    Q_OBJECT
public:
    // index = 구간 번호(0~), segCount = 구간 수, segDiameter/segLength = 그 구간의 지름·길이,
    // chamferLeft/Right = 그 구간의 좌/우 모따기 (왼쪽/오른쪽 면 기준이면 모따기만큼 안쪽에서 잰다)
    ShaftKeywayDialog(int index, int segCount, double segDiameter, double segLength,
                      double chamferLeft, double chamferRight, QWidget *parent = nullptr);

    void setKeyway(const ShaftKeyway &k);
    ShaftKeyway keyway() const;

    // 이 구간에 맞는 처음 값 (첫/마지막 구간 = 끝 열림, 중간 = 막힘·중앙, 크기는 KS 표준)
    static ShaftKeyway defaultFor(int index, int segCount, double segDiameter, double segLength);

private slots:
    void updateState();
    void applyStandard();

private:
    bool canOpen(ShaftKeywayRef ref) const;

    int    m_index;
    int    m_segCount;
    double m_segDiameter;
    double m_segLength;
    double m_chamferLeft;
    double m_chamferRight;

    QRadioButton   *m_open     = nullptr;
    QRadioButton   *m_closed   = nullptr;
    QComboBox      *m_ref      = nullptr;
    QDoubleSpinBox *m_offset   = nullptr;
    QDoubleSpinBox *m_length   = nullptr;
    QDoubleSpinBox *m_upper    = nullptr;
    QDoubleSpinBox *m_center   = nullptr;
    QDoubleSpinBox *m_lower    = nullptr;
    QDoubleSpinBox *m_depth    = nullptr;
    QCheckBox      *m_section  = nullptr;
    QLabel         *m_info     = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────
// ShaftThreadDialog — 축 양 끝 나사부의 길이·피치 입력
//   왼쪽/오른쪽 줄 중 고른 끝(threadSide)만 활성. 호칭경 = 끝 구간 지름.
// ─────────────────────────────────────────────────────────────────────────
class ShaftThreadDialog : public QDialog {
    Q_OBJECT
public:
    // leftD/leftL, rightD/rightL = 첫/마지막 구간의 지름·길이
    ShaftThreadDialog(ShaftThreadSide side, double leftD, double leftL, double rightD, double rightL,
                      QWidget *parent = nullptr);

    void setThreads(const ShaftThread &left, const ShaftThread &right);
    ShaftThread leftThread() const;
    ShaftThread rightThread() const;

private slots:
    void updateInfo();

private:
    double m_d[2];
    double m_l[2];
    QDoubleSpinBox *m_len[2]   = {nullptr, nullptr};
    QDoubleSpinBox *m_pitch[2] = {nullptr, nullptr};
    QLabel         *m_spec[2]  = {nullptr, nullptr};
};

#endif // SHAFTENDDIALOG_H
