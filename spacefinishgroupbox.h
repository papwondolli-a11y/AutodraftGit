#ifndef SPACEFINISHGROUPBOX_H
#define SPACEFINISHGROUPBOX_H

#include <QGroupBox>

class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// SpaceFinishGroupBox — "공간벽 마감형태" 그룹 (swtype1~4 아이콘 라디오)
//   평면도어·평면창문 Setup 다이얼로그 공용. 초기 선택: swtype1 (0)
// ─────────────────────────────────────────────────────────────────────────────
class SpaceFinishGroupBox : public QGroupBox {
    Q_OBJECT

public:
    explicit SpaceFinishGroupBox(QWidget *parent = nullptr);

    void setType(int type);
    int  type() const;

private:
    QRadioButton *m_radio[4];
};

#endif // SPACEFINISHGROUPBOX_H
