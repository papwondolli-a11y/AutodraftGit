#ifndef GRIDCENTERSETUPDIALOG_H
#define GRIDCENTERSETUPDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QRadioButton;
class QButtonGroup;

// 그리드 중심선 기준으로 자동 생성할 치수선 방향 플래그
struct GridDimFlags {
    bool top    = false;  // 상단
    bool bottom = false;  // 하단
    bool left   = false;  // 좌측
    bool right  = false;  // 우측
    bool total  = false;  // 합계 치수선
};

// 그리드 중심선 기준으로 자동 생성할 주열번호(그리드 버블) 방향/라벨 플래그.
// hLabelType/vLabelType 값은 GridBubbleSetupDialog::LabelType과 동일한 순서
// (0=X, 1=Y, 2=A, 3=1(숫자))를 사용한다.
struct GridBubbleFlags {
    bool top       = false;  // 상단
    bool bottom    = false;  // 하단
    bool left      = false;  // 좌측
    bool right     = false;  // 우측
    int  hLabelType = 2;     // 상단/하단 라벨 타입 (기본: A)
    int  vLabelType = 2;     // 좌측/우측 라벨 타입 (기본: A)
};

class GridCenterSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit GridCenterSetupDialog(const QString &ySpacing, const QString &xSpacing,
                                    const GridDimFlags &dimFlags,
                                    const GridBubbleFlags &bubbleFlags,
                                    QWidget *parent = nullptr);

    QString ySpacing() const;
    QString xSpacing() const;
    GridDimFlags dimFlags() const;
    GridBubbleFlags bubbleFlags() const;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onFileItemClicked(QListWidgetItem *item);
    void updateActiveHighlight();

private:
    void refreshFileList();
    void loadCenFile(const QString &filePath);

    QListWidget *m_fileList;
    QPushButton *m_btnDelete;
    QLineEdit   *m_editYSpacing;
    QLineEdit   *m_editXSpacing;
    QPushButton *m_btnSave;

    QCheckBox *m_chkTop;
    QCheckBox *m_chkBottom;
    QCheckBox *m_chkLeft;
    QCheckBox *m_chkRight;
    QCheckBox *m_chkTotal;
    QCheckBox *m_chkDimNone;

    QCheckBox *m_chkBubbleTop;
    QCheckBox *m_chkBubbleBottom;
    QCheckBox *m_chkBubbleLeft;
    QCheckBox *m_chkBubbleRight;
    QCheckBox *m_chkBubbleHNone;
    QCheckBox *m_chkBubbleVNone;

    QButtonGroup *m_bubbleHGroup;  // 상단/하단 라벨 타입 (X/Y/A/1)
    QRadioButton *m_radioHX;
    QRadioButton *m_radioHY;
    QRadioButton *m_radioHA;
    QRadioButton *m_radioHNum;

    QButtonGroup *m_bubbleVGroup;  // 좌측/우측 라벨 타입 (X/Y/A/1)
    QRadioButton *m_radioVX;
    QRadioButton *m_radioVY;
    QRadioButton *m_radioVA;
    QRadioButton *m_radioVNum;

    QPushButton *m_btnOk;
    QPushButton *m_btnCancel;
};

#endif // GRIDCENTERSETUPDIALOG_H
