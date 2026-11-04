#ifndef HBBOLTSETUPDIALOG_H
#define HBBOLTSETUPDIALOG_H

#include "mechpartstandards.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class IAppContext;

// ─────────────────────────────────────────────────────────────────────────
// HBBoltSetupDialog — 둥근머리렌치볼트(육각구멍붙이 둥근머리볼트) 배치 전 규격을
// 고르는 다이얼로그. HHBoltSetupDialog(접시머리렌치볼트)와 항목 구성이 거의 동일하지만,
// hb.lsp에는 나사부 길이 표가 없어 "전나사" 체크박스가 없다.
//
// [규격]       KS(ISO 7380) (고정 표시, 현재는 이 규격만 지원)
// [호칭경]     M3~M16 목록 중 선택 (hb.lsp 원본 표에 정의된 범위)
// [길이 L]     KS 표준 호칭길이 계열 중 선택 — 머리 제외 몸통 기준(Lisp bl과 동일)
// [외형선 레이어]  볼트 외형선(머리/몸통/모따기/소켓 등)이 들어갈 레이어
// [골지름선 레이어] 나사 골지름 표시선 + 끝단 모따기 시작선이 들어갈 레이어
// ─────────────────────────────────────────────────────────────────────────
class HBBoltSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit HBBoltSetupDialog(IAppContext *ctx, QWidget *parent = nullptr);

    // 선택된 호칭경 규격 (예: M10 항목)
    HBBoltSize selectedSize() const;
    // 선택된 호칭길이(L, mm — 머리 제외 몸통 기준)
    double selectedLength() const;
    // 선택된 외형선 레이어 이름 (빈 문자열 = 현재 레이어)
    QString selectedOutlineLayer() const;
    // 선택된 골지름선 레이어 이름 (빈 문자열 = 현재 레이어)
    QString selectedThreadLayer() const;
    // 2점 지정 모드 여부 — true면 배치 시 클릭한 두 점 사이 거리가 길이(L)가 된다
    // (콤보박스에서 고른 길이는 무시됨)
    bool twoPointMode() const;
    // 중심선 표시 여부 — true(기본)면 입면도 중심선 + 평면도 중심 십자선을 함께 그리고,
    // false면 중심선을 아예 생성하지 않는다.
    bool centerlineEnabled() const;
    // 평면도(원형 러벤도) 표시 여부 — true(기본)면 머리 외곽원+상단평면원+육각형+중심십자선을
    // 함께 그리고, false면 평면도 자체를 아예 생성하지 않는다.
    bool sideViewEnabled() const;

    // 마지막으로 사용한 선택값 복원용
    void setInitialSelection(const QString &diameterLabel, double length,
                             const QString &outlineLayer, const QString &threadLayer,
                             bool twoPointMode,
                             bool centerlineEnabled, bool sideViewEnabled);

private slots:
    // 2점 지정 체크박스 토글 시 길이 콤보박스를 활성/비활성화한다 (콤보값이 무시됨을 시각적으로 표시)
    void onTwoPointModeToggled(bool checked);

private:
    void setupUI(IAppContext *ctx);
    void populateLayerCombo(IAppContext *ctx, QComboBox *combo);

    QComboBox *m_comboDiameter;
    QComboBox *m_comboLength;
    QCheckBox *m_chkTwoPoint;
    QComboBox *m_comboOutlineLayer;
    QComboBox *m_comboThreadLayer;
    QCheckBox *m_chkCenterline;
    QCheckBox *m_chkSideView;
};

#endif // HBBOLTSETUPDIALOG_H
