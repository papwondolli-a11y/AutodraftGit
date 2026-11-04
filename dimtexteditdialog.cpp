#include "dimtexteditdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QOverload>

DimTextEditDialog::DimTextEditDialog(const DimTextEditData &initialData, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("치수 텍스트 편집"));
    setMinimumWidth(430); // 체크박스 공간 확보를 위해 조금 넓힘

    setupUi();
    loadData(initialData);
}

void DimTextEditDialog::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- 1. 문자열 그룹 ---
    QGroupBox *textGroup = new QGroupBox(tr("치수 문자"), this);
    QHBoxLayout *textLayout = new QHBoxLayout(textGroup);
    m_prefixCombo = new QComboBox(this);
    m_prefixCombo->setEditable(true);
    m_prefixCombo->addItem(tr("없음"), "");
    m_prefixCombo->addItem(tr("Ø (지름)"), QString::fromUtf8("Ø"));
    m_prefixCombo->addItem(tr("R (반지름)"), "R");
    m_prefixCombo->addItem(tr("SR (구면반지름)"), "SR");
    m_prefixCombo->addItem(tr("M (미터나사)"), "M");
    m_contentEdit = new QLineEdit(this);
    m_suffixEdit = new QLineEdit(this);
    m_suffixEdit->setFixedWidth(80);
    textLayout->addWidget(new QLabel(tr("머리말:")));
    textLayout->addWidget(m_prefixCombo, 1);
    textLayout->addWidget(new QLabel(tr("본문:")));
    textLayout->addWidget(m_contentEdit, 2);
    // [신규] 치수선 연동 체크박스 (본문 바로 뒤)
    m_syncGeomCheck = new QCheckBox(tr("연동"), this);
    m_syncGeomCheck->setChecked(true);
    m_syncGeomCheck->setToolTip(tr("체크 시: 본문 숫자값에 맞게 치수선 길이/각도도 자동 변경됩니다.\n"
                                    "해제 시: 치수 문자만 변경되고 치수선 geometry는 유지됩니다."));
    textLayout->addWidget(m_syncGeomCheck);
    textLayout->addWidget(new QLabel(tr("꼬리말:")));
    textLayout->addWidget(m_suffixEdit, 1);
    mainLayout->addWidget(textGroup);

    // --- 2. 공차 설정 그룹 ---
    QGroupBox *tolGroup = new QGroupBox(tr("공차 (Tolerance)"), this);
    QVBoxLayout *tolLayout = new QVBoxLayout(tolGroup);

    QHBoxLayout *radioLayout = new QHBoxLayout();
    m_radioNone = new QRadioButton(tr("없음"), this);
    m_radioSym = new QRadioButton(tr("대칭 (±)"), this);
    m_radioDev = new QRadioButton(tr("편차 (+/-)"), this);
    m_radioFit = new QRadioButton(tr("끼워맞춤"), this);
    radioLayout->addWidget(m_radioNone);
    radioLayout->addWidget(m_radioSym);
    radioLayout->addWidget(m_radioDev);
    radioLayout->addWidget(m_radioFit);
    radioLayout->addStretch();
    tolLayout->addLayout(radioLayout);

    QHBoxLayout *precLayout = new QHBoxLayout();
    m_tolPrecision = new QSpinBox(this);
    m_tolPrecision->setRange(0, 8);
    precLayout->addWidget(new QLabel(tr("공차 정밀도(소수점):")));
    precLayout->addWidget(m_tolPrecision);
    precLayout->addStretch();
    tolLayout->addLayout(precLayout);

    // [수정] 대칭 공차값 입력 그룹박스 레이아웃 변경 (HBoxLayout 활용)
    m_symGroup = new QGroupBox(tr("대칭 공차값"), this);
    QHBoxLayout *symLayout = new QHBoxLayout(m_symGroup);
    symLayout->addWidget(new QLabel(tr("값 (±):")));
    m_symValue = new QDoubleSpinBox(this);
    m_symValue->setRange(0.0, 1000.0);
    m_symValue->setDecimals(3);
    m_symValue->setSingleStep(0.01);
    symLayout->addWidget(m_symValue, 1); // 스핀박스가 좁아지지 않도록 stretch 1 부여

    // [신규] 대칭값 옆에 각도 체크박스 추가
    m_tolAngleCheck = new QCheckBox(tr("각도 (°)"), this);
    m_tolAngleCheck->setToolTip(tr("체크 시 공차값 뒤에 ° 부호가 붙습니다. (예: ±1.5°)"));
    symLayout->addWidget(m_tolAngleCheck);

    tolLayout->addWidget(m_symGroup);

    m_devGroup = new QGroupBox(tr("편차 공차값"), this);
    QFormLayout *devForm = new QFormLayout(m_devGroup);
    m_devUpper = new QDoubleSpinBox(this);
    m_devUpper->setRange(-1000.0, 1000.0);
    m_devUpper->setDecimals(3);
    m_devUpper->setSingleStep(0.01);
    m_devLower = new QDoubleSpinBox(this);
    m_devLower->setRange(-1000.0, 1000.0);
    m_devLower->setDecimals(3);
    m_devLower->setSingleStep(0.01);
    devForm->addRow(tr("상한값 (+):"), m_devUpper);
    devForm->addRow(tr("하한값 (-):"), m_devLower);
    tolLayout->addWidget(m_devGroup);

    // [신규] 끼워맞춤 등급기호 그룹 - IT등급표에서 상/하한을 자동 계산하므로 숫자 입력 없이 기호만 받는다.
    m_fitGroup = new QGroupBox(tr("끼워맞춤 등급기호"), this);
    QHBoxLayout *fitLayout = new QHBoxLayout(m_fitGroup);
    fitLayout->addWidget(new QLabel(tr("기호:")));
    m_fitCodeCombo = new QComboBox(this);
    m_fitCodeCombo->setEditable(true);
    m_fitCodeCombo->addItem("H7"); m_fitCodeCombo->addItem("H8"); m_fitCodeCombo->addItem("H9");
    m_fitCodeCombo->addItem("h6"); m_fitCodeCombo->addItem("js6");
    m_fitCodeCombo->addItem("g6"); m_fitCodeCombo->addItem("f7");
    m_fitCodeCombo->addItem("k6"); m_fitCodeCombo->addItem("m6");
    m_fitCodeCombo->addItem("n6"); m_fitCodeCombo->addItem("p6");
    fitLayout->addWidget(m_fitCodeCombo, 1);
    // [신규] 기본은 꺼진 상태 - 실무 관례상 "30H7"처럼 기호만 표기하고 숫자는 생략
    m_fitShowValuesCheck = new QCheckBox(tr("공차값도 표시"), this);
    m_fitShowValuesCheck->setToolTip(tr("체크 시 등급기호 옆에 계산된 숫자 상/하한(+0.021/0 등)도 함께 표기합니다.\n"
                                          "해제 시(기본): \"30H7\"처럼 등급기호만 표기합니다."));
    fitLayout->addWidget(m_fitShowValuesCheck);
    tolLayout->addWidget(m_fitGroup);

    mainLayout->addWidget(tolGroup);

    // --- 3. 하단 버튼 ---
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // 이벤트 연결
    connect(m_radioNone, &QRadioButton::toggled, this, &DimTextEditDialog::onTolTypeChanged);
    connect(m_radioSym, &QRadioButton::toggled, this, &DimTextEditDialog::onTolTypeChanged);
    connect(m_radioDev, &QRadioButton::toggled, this, &DimTextEditDialog::onTolTypeChanged);
    connect(m_radioFit, &QRadioButton::toggled, this, &DimTextEditDialog::onTolTypeChanged);
    connect(m_prefixCombo, QOverload<int>::of(&QComboBox::activated), this, &DimTextEditDialog::onPrefixIndexChanged);
    connect(m_fitCodeCombo, &QComboBox::currentTextChanged, this, &DimTextEditDialog::onFitCodeTextChanged);
}

void DimTextEditDialog::loadData(const DimTextEditData &data) {
    m_contentEdit->setText(data.content);
    m_prefixCombo->setCurrentText(data.prefix);
    m_suffixEdit->setText(data.suffix);

    m_tolPrecision->setValue(data.tolPrecision);
    m_symValue->setValue(data.tolUpper);
    m_devUpper->setValue(data.tolUpper);
    m_devLower->setValue(data.tolLower);

    m_tolAngleCheck->setChecked(data.tolUseAngle);
    m_syncGeomCheck->setChecked(data.syncGeometry);
    m_fitCodeCombo->setCurrentText(data.fitCode);
    m_fitShowValuesCheck->setChecked(data.fitShowValues);

    if (data.tolType == 1) m_radioSym->setChecked(true);
    else if (data.tolType == 2) m_radioDev->setChecked(true);
    else if (data.tolType == 4) m_radioFit->setChecked(true);
    else m_radioNone->setChecked(true);

    onTolTypeChanged();
}

void DimTextEditDialog::onTolTypeChanged() {
    bool isSym = m_radioSym->isChecked();
    bool isDev = m_radioDev->isChecked();
    bool isFit = m_radioFit->isChecked();

    m_symGroup->setEnabled(isSym);
    m_devGroup->setEnabled(isDev);
    m_fitGroup->setEnabled(isFit);
    m_tolPrecision->setEnabled(isSym || isDev || isFit);

    // [신규] 각도 체크박스는 대칭 모드일 때만 활성화 (또는 원하시면 편차 모드도 포함 가능)
    m_tolAngleCheck->setEnabled(isSym);
}

void DimTextEditDialog::onFitCodeTextChanged(const QString &text) {
    // [신규] 끼워맞춤 모드일 때만 동기화한다 - 다른 공차유형에서 콤보 내용이 프로그램적으로
    // 세팅될 때(loadData 등)까지 꼬리말을 건드리면 안 되므로 라디오 상태로 방어한다.
    if (!m_radioFit->isChecked()) return;
    m_suffixEdit->setText(text);
}

void DimTextEditDialog::onPrefixIndexChanged(int index) {
    if (index < 0) return;
    QString symbol = m_prefixCombo->itemData(index).toString();
    m_prefixCombo->setCurrentText(symbol);
}

DimTextEditData DimTextEditDialog::getData() const {
    DimTextEditData data;
    data.content = m_contentEdit->text();
    QString prefixText = m_prefixCombo->currentText();
    if (prefixText == tr("Ø (지름)")) data.prefix = QString::fromUtf8("Ø");
    else if (prefixText == tr("R (반지름)")) data.prefix = "R";
    else if (prefixText == tr("SR (구면반지름)")) data.prefix = "SR";
    else if (prefixText == tr("M (미터나사)")) data.prefix = "M";
    else if (prefixText == tr("없음")) data.prefix = "";
    else data.prefix = prefixText;
    data.suffix = m_suffixEdit->text();
    data.tolPrecision = m_tolPrecision->value();

    data.tolUseAngle = m_tolAngleCheck->isChecked();
    data.syncGeometry = m_syncGeomCheck->isChecked();

    if (m_radioSym->isChecked()) {
        data.tolType = 1;
        data.tolUpper = m_symValue->value();
        data.tolLower = m_symValue->value();
    } else if (m_radioDev->isChecked()) {
        data.tolType = 2;
        data.tolUpper = m_devUpper->value();
        data.tolLower = m_devLower->value();
    } else if (m_radioFit->isChecked()) {
        // 끼워맞춤 모드: 상/하한은 렌더링 시 등급기호로부터 자동 계산되므로 여기서는 기호만 전달
        data.tolType = 4;
        data.fitCode = m_fitCodeCombo->currentText();
        data.fitShowValues = m_fitShowValuesCheck->isChecked();
        data.tolUpper = 0.0;
        data.tolLower = 0.0;
    } else {
        data.tolType = 0;
        data.tolUpper = 0.0;
        data.tolLower = 0.0;
        data.tolUseAngle = false; // 없음 모드일 땐 각도 속성 강제 해제
    }

    return data;
}