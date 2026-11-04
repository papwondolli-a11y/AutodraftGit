#include "CreateLayerDialog.h"
#include "stylemanager.h"
#include "appcontext.h"
#include "layercomboboxmanager.h"
#include "layermanager.h"

#include "linetypemanager.h"

#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QLabel>
#include <QMessageBox>
#include <QButtonGroup>
#include <QRadioButton>

CreateLayerDialog::CreateLayerDialog(AppContext *context,QWidget *parent)
    : QDialog(parent), m_appContext(context)
{
    setWindowTitle(tr("새로운 레이어 만들기"));
  //  setWindowIcon(QIcon(":/icons/compass01.png"));

    m_centralWidget = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_centralWidget);

    // layer name-------------
    QLabel *m_nameEditLabel = new QLabel(tr("레이어 이름"),this);
    m_mainLayout -> addWidget(m_nameEditLabel);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit -> setPlaceholderText(tr("이름 입력"));
    m_mainLayout -> addWidget(m_nameEdit);
    // layer linetype ----------
    QLabel *m_typeComboLabel = new QLabel(tr("라인타입 선택"),this);
    m_mainLayout -> addWidget(m_typeComboLabel);
    linetypeCombo = new LineTypeComboBox(m_appContext, this);

    // 라인타입 그룹 선택 (콤보박스 아래) — 선택한 버튼에 따라 콤보 항목이 바뀐다.
    // 초기값은 마지막에 설정한 그룹(LineTypeManager::currentGroup, QSettings 유지).
    m_rbAutoDraft = new QRadioButton(tr("AutoDraft"), this);
    m_rbAcadStd   = new QRadioButton(tr("AutoCAD호환(S)"), this);
    m_rbAcadIso   = new QRadioButton(tr("AutoCAD호환(ISO)"), this);

    const LineTypeManager::LinetypeGroup startGroup = LineTypeManager::currentGroup();
    m_rbAcadStd  -> setChecked(startGroup == LineTypeManager::LinetypeGroup::AcadStandard);
    m_rbAcadIso  -> setChecked(startGroup == LineTypeManager::LinetypeGroup::AcadIso);
    m_rbAutoDraft-> setChecked(startGroup == LineTypeManager::LinetypeGroup::AutoDraft ||
                               (!m_rbAcadStd->isChecked() && !m_rbAcadIso->isChecked()));

    linetypeCombo -> populate(startGroup);
    m_mainLayout -> addWidget(linetypeCombo);

    QHBoxLayout *ltGroupLayout = new QHBoxLayout();
    ltGroupLayout -> addWidget(m_rbAutoDraft);
    ltGroupLayout -> addWidget(m_rbAcadStd);
    ltGroupLayout -> addWidget(m_rbAcadIso);
    m_mainLayout -> addLayout(ltGroupLayout);

    QButtonGroup *ltGroupButtons = new QButtonGroup(this);
    ltGroupButtons -> addButton(m_rbAutoDraft);
    ltGroupButtons -> addButton(m_rbAcadStd);
    ltGroupButtons -> addButton(m_rbAcadIso);
    connect(m_rbAutoDraft, &QRadioButton::toggled, this, &CreateLayerDialog::onLinetypeGroupChanged);
    connect(m_rbAcadStd,   &QRadioButton::toggled, this, &CreateLayerDialog::onLinetypeGroupChanged);
    connect(m_rbAcadIso,   &QRadioButton::toggled, this, &CreateLayerDialog::onLinetypeGroupChanged);

    //layer line-width ----------
    QLabel *m_widthComboLabel = new QLabel(tr("라인 두께 선택"), this);
    m_mainLayout -> addWidget(m_widthComboLabel);
    linewidthCombo = new LineWidthComboBox(m_appContext, this);
    linewidthCombo -> populate();
    m_mainLayout -> addWidget(linewidthCombo);
    //layer color select ----------
    QLabel *m_colorComboLabel = new QLabel(tr("컬러 선택"), this);
    m_mainLayout -> addWidget(m_colorComboLabel);
    colorCombo = new ColorComboBox(m_appContext, this);
    colorCombo -> populate();

    QPushButton *selectColor = new QPushButton("M..",this);
    selectColor -> setMaximumWidth(30);
    connect(selectColor,&QPushButton::clicked,this,&CreateLayerDialog::onSelectColor);

    QHBoxLayout *colorLayout = new QHBoxLayout();
    colorLayout -> addWidget(colorCombo);
    colorLayout -> addWidget(selectColor);

    m_mainLayout -> addLayout(colorLayout);

    m_okButton = new QPushButton(tr("Ok"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    connect(m_okButton, &QPushButton::clicked, this, &CreateLayerDialog::onAccept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_mainLayout ->addLayout(buttonLayout);
    setLayout(m_mainLayout);
}

CreateLayerDialog::~CreateLayerDialog()
{
}

QString CreateLayerDialog::layerName() const
{
    return m_nameEdit->text().trimmed();
}

QString CreateLayerDialog::layerColor() const
{
    return colorCombo->currentText();

}
QIcon CreateLayerDialog::colorIcon() const
{
    return colorCombo -> itemIcon(colorCombo -> currentIndex());
}

QString CreateLayerDialog::lineType() const
{
    return linetypeCombo->currentText();
}

QString CreateLayerDialog::lineWeight() const
{
    return linewidthCombo->currentText();
}

void CreateLayerDialog::onLinetypeGroupChanged()
{
    // toggled 는 해제/선택 두 번 오므로 실제 선택된 상태에서만 재구성
    LineTypeManager::LinetypeGroup group = LineTypeManager::LinetypeGroup::AutoDraft;
    if (m_rbAcadStd->isChecked())
        group = LineTypeManager::LinetypeGroup::AcadStandard;
    else if (m_rbAcadIso->isChecked())
        group = LineTypeManager::LinetypeGroup::AcadIso;
    else if (!m_rbAutoDraft->isChecked())
        return; // 아직 아무 것도 선택되지 않은 중간 상태

    // 선택한 그룹을 전역 설정으로 저장 → 레이어 dock 의 라인타입 변경 다이얼로그도 이 그룹만 표시
    LineTypeManager::setCurrentGroup(group);
    linetypeCombo->populate(group);
}

void CreateLayerDialog::onSelectColor()
{
    // QColorDialog를 통해 색상 선택 (기본색: 흰색)
    QColor color = QColorDialog::getColor(Qt::white, this, tr("Select Color"));
    if (color.isValid()) {
        QPixmap pix(20, 20);
        pix.fill(color);

        // 선택한 색상을 콤보박스에 추가
        colorCombo->addItem(QIcon(pix), color.name());
        // 새 항목을 현재 선택 항목으로 지정
        colorCombo->setCurrentIndex(colorCombo->count() - 1);

        // 커스텀 색상을 전역 설정에 추가 (g_selectColor 대체)
        if (m_appContext && m_appContext->styleManager()) {
            SelectColor sc; // SelectColor는 첫글자 대문자 구조체일 수 있으니 확인해주세요.
            sc.color = color;
            sc.name = color.name();
            m_appContext->styleManager()->addCustomColor(sc);
        }
    }
}

void CreateLayerDialog::onAccept()
{
    QString inputName = layerName();

    if(inputName.isEmpty()){
        QMessageBox::warning(this, tr("layer name Empty"), tr("Enter Layer Name"));
        return;
    }

    // [수정] 싱글톤 대신 AppContext를 통해 LayerManager에 접근
    if (m_appContext && m_appContext->layerManager()) {
        if(m_appContext->layerManager()->getLayer(inputName)) {
            QMessageBox::warning(this, tr("layer name Duplicate"), tr("Enter a different name"));
            return;
        }
    }
    accept();
}
