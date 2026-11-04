#ifndef HATCHDIALOG_H
#define HATCHDIALOG_H

#include <QDialog>

class QListWidget;
class QDoubleSpinBox;
class QRadioButton;
class QComboBox;
class QListWidgetItem;
class HatchPatternManager;

class HatchDialog : public QDialog {
    Q_OBJECT
public:
    // 생성자에 HatchPatternManager 포인터를 주입받습니다.
    explicit HatchDialog(HatchPatternManager* patManager, QWidget* parent = nullptr);

    QString selectedPattern() const;
    double selectedScale() const;
    double selectedAngle() const;
    
    // [신규] Box 모드 및 레이어 선택 결과 반환
    bool isBoxMode() const;
    QString selectedLayer() const;

    void setScale(double scale);
    void setAngle(double angle);
    void setPattern(const QString& name);
    void setBoxMode(bool box);
    
    // [신규] 화면 초기화 시 레이어 목록 주입
    void initLayers(const QStringList& layers, const QString& currentLayer);

private slots:
    void onPatternDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void loadPatterns();
    HatchPatternManager* m_patternManager; // 의존성 저장
    QListWidget* m_patternList;
    QDoubleSpinBox* m_scaleSpin;
    QDoubleSpinBox* m_angleSpin;
    
    QRadioButton* m_radioBox;
    QRadioButton* m_radioSelect;
    QComboBox* m_layerCombo;
    
    QPushButton* m_btnOk;
    QPushButton* m_btnCancel;
};

#endif // HATCHDIALOG_H
