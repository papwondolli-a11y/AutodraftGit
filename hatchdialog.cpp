#include "hatchdialog.h"
#include "hatchpatternmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QListWidgetItem>
#include <QIcon>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QComboBox>

HatchDialog::HatchDialog(HatchPatternManager* patManager, QWidget* parent)
    : QDialog(parent), m_patternManager(patManager)\
{
    setWindowTitle(tr(" 해치 패턴"));
    resize(400, 500);

    // 1순위: 배포 환경 — 실행 파일(.exe) 옆 Data 폴더
//     QString patFilePath = QCoreApplication::applicationDirPath() + "/Data/hatchiso.pat";

//     // 2순위: 개발 환경 — 소스코드 루트의 Data 폴더 (APP_SOURCE_PATH 매크로)
//     //   빌드 출력 경로(build/Debug 등)와 소스 경로가 다를 때 사용됩니다.
//     if (!QFile::exists(patFilePath)) {
// #ifdef APP_SOURCE_PATH
//         patFilePath = QString(APP_SOURCE_PATH) + "/Data/hatchiso.pat";
// #endif
//     }

//     HatchPatternManager::instance().loadPatterns(patFilePath);

    setupUI();
    loadPatterns();
}

void HatchDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- 패턴 리스트 위젯 ---
    m_patternList = new QListWidget(this);
    m_patternList->setViewMode(QListView::IconMode);
    m_patternList->setIconSize(QSize(64, 64));
    m_patternList->setSpacing(10);
    m_patternList->setResizeMode(QListView::Adjust);
    m_patternList->setWordWrap(true);
    
    connect(m_patternList, &QListWidget::itemDoubleClicked, this, &HatchDialog::onPatternDoubleClicked);
    //connect(m_patternList, &QListWidget::itemDoubleClicked, this, &HatchDialog::onPatternDoubleClicked);

    mainLayout->addWidget(new QLabel(tr(" 패턴 선택 :")));
    mainLayout->addWidget(m_patternList);

    // --- 속성 (Scale, Angle) ---
    QHBoxLayout* propLayout = new QHBoxLayout();
    
    propLayout->addWidget(new QLabel(tr(" 축척 : ")));
    m_scaleSpin = new QDoubleSpinBox(this);
    m_scaleSpin->setRange(0.01, 1000.0);
    m_scaleSpin->setValue(1.0);
    m_scaleSpin->setSingleStep(0.1);
    propLayout->addWidget(m_scaleSpin);  
    propLayout->addSpacing(10);

    propLayout->addWidget(new QLabel(tr(" 각도 :")));
    m_angleSpin = new QDoubleSpinBox(this);
    m_angleSpin->setRange(-360.0, 360.0);
    m_angleSpin->setValue(0.0);
    m_angleSpin->setSingleStep(15.0);
    m_angleSpin->setSuffix(QString::fromUtf8(" \xC2\xB0")); // degree symbol
    propLayout->addWidget(m_angleSpin);
    propLayout->addStretch();

    // [신규] Box / Select 모드 라디오 버튼
    QHBoxLayout* modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(tr(" 선택 형태  :")));
    m_radioBox = new QRadioButton(tr("상자모양선택"));
    m_radioSelect = new QRadioButton(tr("복합모양/해치공간선택"));
    m_radioSelect->setChecked(true); // 기본값 Select
    modeLayout->addSpacing(10);
    modeLayout->addWidget(m_radioBox);
    modeLayout->addWidget(m_radioSelect);
     modeLayout->addStretch();
    
    // [신규] 레이어 콤보박스
    QHBoxLayout* layerLayout = new QHBoxLayout();
    layerLayout->addWidget(new QLabel(tr(" 해치 레이어 : ")));
    m_layerCombo = new QComboBox();
    layerLayout->addSpacing(10);
    layerLayout->addWidget(m_layerCombo);
    layerLayout->addStretch();

    mainLayout->addLayout(propLayout);
    mainLayout->addLayout(modeLayout);
    mainLayout->addLayout(layerLayout);

    // --- 버튼 (OK, Cancel) ---
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void HatchDialog::loadPatterns() {
    if (!m_patternManager) return;

    QStringList names = m_patternManager->getPatternNames();

    // 인덱스 루프 사용
    for (int i = 0; i < names.size(); ++i) {
        QString name = names[i];
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(name);
        item->setData(Qt::UserRole, name);

        QPixmap pixmap = m_patternManager->generatePixmap(name, 2.0, 0.0, 64);
        item->setIcon(QIcon(pixmap));

        m_patternList->addItem(item);
    }
}

void HatchDialog::setBoxMode(bool box) {
    if (box) m_radioBox->setChecked(true);
    else m_radioSelect->setChecked(true);
}

void HatchDialog::onPatternDoubleClicked(QListWidgetItem* item) {
    Q_UNUSED(item);
    accept();
}

QString HatchDialog::selectedPattern() const {
    QListWidgetItem* current = m_patternList->currentItem();
    if (current) {
        return current->data(Qt::UserRole).toString();
    }
    
    // 선택된 게 없다면 리스트 중 첫 번째 반환 시도
    if (m_patternList->count() > 0) {
        return m_patternList->item(0)->data(Qt::UserRole).toString();
    }
    
    return QString();
}

double HatchDialog::selectedScale() const {
    return m_scaleSpin->value();
}

double HatchDialog::selectedAngle() const {
    return m_angleSpin->value();
}

void HatchDialog::setScale(double scale) {
    m_scaleSpin->setValue(scale);
}

void HatchDialog::setAngle(double angle) {
    m_angleSpin->setValue(angle);
}

void HatchDialog::setPattern(const QString& name) {
    for (int i = 0; i < m_patternList->count(); ++i) {
        QListWidgetItem* item = m_patternList->item(i);
        if (item->data(Qt::UserRole).toString() == name.toUpper()) {
            m_patternList->setCurrentItem(item);
            m_patternList->scrollToItem(item);
            break;
        }
    }
}

void HatchDialog::initLayers(const QStringList& layers, const QString& currentLayer) {
    m_layerCombo->clear();
    m_layerCombo->addItems(layers);
    
    int idx = m_layerCombo->findText(currentLayer);
    if (idx >= 0) {
        m_layerCombo->setCurrentIndex(idx);
    }
}

bool HatchDialog::isBoxMode() const {
    return m_radioBox->isChecked();
}

QString HatchDialog::selectedLayer() const {
    return m_layerCombo->currentText();
}
