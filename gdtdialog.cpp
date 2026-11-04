#include "gdtdialog.h"
#include "gdtitem.h"
#include "gdtcalculator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainter>
#include <QLabel>
#include <QStyleOptionGraphicsItem>
#include <QListWidget>
#include <QIcon>
#include <QPixmap>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>
#include <QAction>

namespace {

// 리스트 아이템에 파일명(확장자 제외)을 보관하는 데이터 role (아이콘만 표시하고 텍스트는 숨김)
const int kFileNameRole = Qt::UserRole;

// data 폴더 위치 탐색: 1) exe 옆 data, 2) 상위 폴더의 data(qmake debug/release 빌드),
// 3) 소스 루트의 data(개발 환경, APP_SOURCE_PATH)
QString resolveGdtDataDir() {
    QString dir = QCoreApplication::applicationDirPath() + "/data";
    if (!QDir(dir).exists()) {
        QString alt = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../data");
        if (QDir(alt).exists()) {
            dir = alt;
        }
#ifdef APP_SOURCE_PATH
        else {
            QString devDir = QDir::cleanPath(APP_SOURCE_PATH) + QDir::separator() + "data";
            if (QDir(devDir).exists())
                dir = devDir;
        }
#endif
    }
    return dir;
}

// GdtDialog 미리보기 위젯과 목록 아이콘이 동일한 모양을 그리도록 공유하는 렌더 함수.
// painter의 원점(0,0)을 좌상단으로 하는 size 영역에 그린다.
void paintGdtPreview(QPainter &p, const QSize &size, const GdtData &data,
                      const QColor &bg, const QColor &fg) {
    QRect r(QPoint(0, 0), size);
    p.fillRect(r, bg);

    if (data.toleranceValue.isEmpty() && data.datumRefs.isEmpty()) {
        p.setPen(Qt::gray);
        p.drawText(r, Qt::AlignCenter, QCoreApplication::translate("GdtDialog", "미리보기"));
        return;
    }

    GdtData tmp = data;
    tmp.rotation = 0.0;
    tmp.leaderPoints.clear(); // 미리보기/아이콘에서는 지시선 생략

    // 기준 문자높이로 전체 프레임 폭을 계산한 뒤, 박스 폭에 넘치면 축소해서
    // 데이텀 칸이 아이콘 밖으로 잘려 안 보이는 문제를 방지한다.
    const double margin = 8.0;
    double baseTextHeight = size.height() * 0.38;
    double availW = qMax(10.0, size.width() - margin * 2.0);
    double availH = qMax(10.0, static_cast<double>(size.height()) - 4.0);
    double frameW = GdtCalculator::totalGdtWidth(baseTextHeight, tmp.toleranceValue, tmp.hasDiameter,
                                                  tmp.datumRefs, tmp.symbol);
    double frameH = GdtCalculator::cellHeight(baseTextHeight);
    double fitScale = qMin(1.0, qMin(availW / frameW, availH / frameH));

    tmp.textHeight = baseTextHeight * fitScale;
    tmp.position = MyVector(margin, size.height() / 2.0);

    GdtItem tmpItem(tmp);
    tmpItem.setPen(QPen(fg, 1));

    QStyleOptionGraphicsItem styleOpt;
    tmpItem.paint(&p, &styleOpt, nullptr);
}

QPixmap renderGdtIcon(const GdtData &data, const QSize &size) {
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    paintGdtPreview(p, size, data, QColor(45, 45, 45), Qt::white);
    p.end();
    return pix;
}

// 프리셋으로 저장할 값만 직렬화한다 (위치/지시선/회전 등 배치 정보는 제외).
QJsonObject gdtDataToJson(const GdtData &d) {
    QJsonObject o;
    o["symbol"] = static_cast<int>(d.symbol);
    o["tolerance"] = d.toleranceValue;
    o["hasDiameter"] = d.hasDiameter;
    QJsonArray datums;
    for (const QString &s : d.datumRefs) datums.append(s);
    o["datum"] = datums;
    o["textHeight"] = d.textHeight;
    o["useLeader"] = d.useLeader;
    o["fontName"] = d.fontName;
    return o;
}

GdtData gdtDataFromJson(const QJsonObject &o) {
    GdtData d;
    d.symbol = static_cast<GdtSymbol>(o.value("symbol").toInt());
    d.toleranceValue = o.value("tolerance").toString();
    d.hasDiameter = o.value("hasDiameter").toBool();
    for (const QJsonValue &v : o.value("datum").toArray()) d.datumRefs.append(v.toString());
    d.textHeight = o.value("textHeight").toDouble(2.5);
    d.useLeader = o.value("useLeader").toBool(true);
    d.fontName = o.value("fontName").toString("Arial");
    return d;
}

bool saveGdtFile(const QString &filePath, const GdtData &d) {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QJsonDocument doc(gdtDataToJson(d));
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool loadGdtFile(const QString &filePath, GdtData &out) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    out = gdtDataFromJson(doc.object());
    return true;
}

} // namespace

// ─────────────────────────────────────────────
// 미리보기 위젯
// ─────────────────────────────────────────────
class GdtPreviewWidget : public QWidget {
public:
    explicit GdtPreviewWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(260, 50);
        setStyleSheet("background: #1e1e1e; border: 1px solid #555;");
    }

    void setData(const GdtData &d) {
        m_data = d;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p;
        if (!p.begin(this)) return;

        p.setRenderHint(QPainter::Antialiasing);
        paintGdtPreview(p, size(), m_data, QColor(30, 30, 30), Qt::white);

        p.end();
    }
private:
    GdtData m_data;
};

// ─────────────────────────────────────────────
// GdtDialog 구현
// ─────────────────────────────────────────────
static const struct {
    GdtSymbol   sym;
    const char *label;
    const char *tip;
} kSymbols[] = {
    // 모양 (6)
    { GdtSymbol::Straightness,       "─",   QT_TRANSLATE_NOOP("GdtDialog", "직진도")        },
    { GdtSymbol::Flatness,           "▱",   QT_TRANSLATE_NOOP("GdtDialog", "평면도")        },
    { GdtSymbol::Circularity,        "○",   QT_TRANSLATE_NOOP("GdtDialog", "진원도")        },
    { GdtSymbol::Cylindricity,       "⌀",   QT_TRANSLATE_NOOP("GdtDialog", "원통도")        },
    { GdtSymbol::LineProfile,        "⌒",   QT_TRANSLATE_NOOP("GdtDialog", "선의 윤곽도")   },
    { GdtSymbol::SurfaceProfile,     "⌓",   QT_TRANSLATE_NOOP("GdtDialog", "면의 윤곽도")   },
    // 자세 (3)
    { GdtSymbol::Parallelism,        "//",  QT_TRANSLATE_NOOP("GdtDialog", "평행도")        },
    { GdtSymbol::Perpendicularity,   "⊥",   QT_TRANSLATE_NOOP("GdtDialog", "직각도")        },
    { GdtSymbol::Angularity,         "∠",   QT_TRANSLATE_NOOP("GdtDialog", "경사도")        },
    // 위치 (3)
    { GdtSymbol::Position,           "⊕",   QT_TRANSLATE_NOOP("GdtDialog", "위치도")        },
    { GdtSymbol::Concentricity,      "◎",   QT_TRANSLATE_NOOP("GdtDialog", "동심도/동축도") },
    { GdtSymbol::Symmetry,           "≡",   QT_TRANSLATE_NOOP("GdtDialog", "대칭도")        },
    // 흔들림 (2)
    { GdtSymbol::CircularRunout,     "↗",   QT_TRANSLATE_NOOP("GdtDialog", "원주 흔들림")   },
    { GdtSymbol::TotalRunout,        "↗↗",  QT_TRANSLATE_NOOP("GdtDialog", "전체 흔들림")   },
};
static const int kSymCount = (int)(sizeof(kSymbols) / sizeof(kSymbols[0]));

static const struct {
    const char *name;
    int count;
    int cols;
} kCategories[] = {
    { QT_TRANSLATE_NOOP("GdtDialog", "모양"),   6, 2 },
    { QT_TRANSLATE_NOOP("GdtDialog", "자세"),   3, 1 },
    { QT_TRANSLATE_NOOP("GdtDialog", "위치"),   3, 1 },
    { QT_TRANSLATE_NOOP("GdtDialog", "흔들림"), 2, 1 },
};
static const int kCatCount = (int)(sizeof(kCategories) / sizeof(kCategories[0]));

GdtDialog::GdtDialog(QWidget *parent)
    : QDialog(parent)
    , m_selectedSymbol(GdtSymbol::Circularity)
{
    setWindowTitle(tr("기하공차 (GD&T)"));
    setFixedWidth(640);
    setupUI();
    refreshFileList();
}

void GdtDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ── 왼쪽: 저장된 프리셋 목록 ──
    m_fileList = new QListWidget(this);
    m_fileList->setMinimumWidth(140);
    m_fileList->setMaximumWidth(140);
    m_fileList->setIconSize(QSize(126, 34));
    m_fileList->setSpacing(2);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::itemClicked, this, &GdtDialog::onListItemClicked);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &GdtDialog::onListContextMenu);

    QVBoxLayout *rightCol = new QVBoxLayout;

    // ── 기호 선택 (ISO 1101:2004 카테고리별) ──
    QGroupBox *symBox = new QGroupBox(tr("기호 선택"), this);
    QHBoxLayout *symHLay = new QHBoxLayout(symBox);
    symHLay->setSpacing(4);
    symHLay->setContentsMargins(4, 8, 4, 4);

    m_symGroup = new QButtonGroup(this);
    m_symGroup->setExclusive(true);

    int symIdx = 0;
    for (int c = 0; c < kCatCount; ++c) {
        QGroupBox *catBox = new QGroupBox(tr(kCategories[c].name), symBox);
        catBox->setStyleSheet("QGroupBox { font-size: 11px; font-weight: bold; }");
        QGridLayout *catGrid = new QGridLayout(catBox);
        catGrid->setSpacing(3);
        catGrid->setContentsMargins(4, 12, 4, 4);

        for (int j = 0; j < kCategories[c].count; ++j, ++symIdx) {
            QPushButton *btn = new QPushButton(QString::fromUtf8(kSymbols[symIdx].label), this);
            btn->setToolTip(tr(kSymbols[symIdx].tip));
            btn->setCheckable(true);
            btn->setFixedSize(40, 36);
            btn->setStyleSheet(
                "QPushButton { font-size: 14px; }"
                "QPushButton:checked { background-color: #0078d4; color: white; }");
            if (kSymbols[symIdx].sym == m_selectedSymbol) btn->setChecked(true);
            m_symGroup->addButton(btn, symIdx);
            catGrid->addWidget(btn, j / kCategories[c].cols, j % kCategories[c].cols);
        }
        symHLay->addWidget(catBox);
    }
    connect(m_symGroup, &QButtonGroup::idClicked, this, &GdtDialog::onSymbolSelected);
    rightCol->addWidget(symBox);

    // ── 공차값 입력 ──
    QGroupBox *tolBox = new QGroupBox(tr("공차값"), this);
    QHBoxLayout *tolLay = new QHBoxLayout(tolBox);
    m_chkDiam = new QCheckBox("Ø", this);
    m_chkDiam->setToolTip(tr("직경 공차 (Ø 접두어 추가)"));
    m_edTolerance = new QLineEdit(this);
    m_edTolerance->setPlaceholderText(tr("예: 0.011"));
    m_edTolerance->setMaxLength(20);
    tolLay->addWidget(m_chkDiam);
    tolLay->addWidget(m_edTolerance);
    rightCol->addWidget(tolBox);
    connect(m_chkDiam, &QCheckBox::toggled, this, &GdtDialog::onInputChanged);
    connect(m_edTolerance, &QLineEdit::textChanged, this, &GdtDialog::onInputChanged);

    // ── 데이텀 입력 ──
    QGroupBox *datBox = new QGroupBox(tr("데이텀 참조 (빈 칸은 생략)"), this);
    QHBoxLayout *datLay = new QHBoxLayout(datBox);
    const char *datLabels[3] = {"A", "B", "C"};
    for (int i = 0; i < 3; ++i) {
        QLabel *lbl = new QLabel(datLabels[i], this);
        m_edDatum[i] = new QLineEdit(this);
        m_edDatum[i]->setMaximumWidth(60);
        m_edDatum[i]->setPlaceholderText(datLabels[i]);
        m_edDatum[i]->setMaxLength(4);
        datLay->addWidget(lbl);
        datLay->addWidget(m_edDatum[i]);
        connect(m_edDatum[i], &QLineEdit::textChanged, this, &GdtDialog::onInputChanged);
    }
    rightCol->addWidget(datBox);

    // ── 텍스트 높이 및 지시선 옵션 ──
    QHBoxLayout *hLay = new QHBoxLayout;
    hLay->addWidget(new QLabel(tr("문자 높이:"), this));
    m_spnTextHeight = new QDoubleSpinBox(this);
    m_spnTextHeight->setRange(1.0, 50.0);
    m_spnTextHeight->setSingleStep(0.5);
    m_spnTextHeight->setValue(2.5);
    m_spnTextHeight->setSuffix(" mm");
    hLay->addWidget(m_spnTextHeight);

    // [수정] 지시선 활성화 체크박스 추가
    m_chkUseLeader = new QCheckBox(tr("지시선"), this);
    m_chkUseLeader->setChecked(true);
    hLay->addWidget(m_chkUseLeader);

    hLay->addStretch();
    rightCol->addLayout(hLay);
    connect(m_spnTextHeight, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this, &GdtDialog::onInputChanged);
    connect(m_chkUseLeader, &QCheckBox::toggled, this, &GdtDialog::onInputChanged);

    // ── 미리보기 ──
    QGroupBox *prevBox = new QGroupBox(tr("미리보기"), this);
    QVBoxLayout *prevLay = new QVBoxLayout(prevBox);
    m_preview = new GdtPreviewWidget(this);
    prevLay->addWidget(m_preview);
    rightCol->addWidget(prevBox);

    QHBoxLayout *bodyRow = new QHBoxLayout;
    bodyRow->addWidget(m_fileList);
    bodyRow->addLayout(rightCol, 1);
    mainLayout->addLayout(bodyRow);

    // ── 하단 버튼: 저장하기 | 확인 | 취소 ──
    m_btnSaveList = new QPushButton(tr("저장하기"), this);
    m_btnOk       = new QPushButton(tr("확인"), this);
    m_btnCancel   = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_btnSaveList);
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);
    mainLayout->addLayout(btnRow);

    connect(m_btnSaveList, &QPushButton::clicked, this, &GdtDialog::onSaveClicked);
    connect(m_btnOk,       &QPushButton::clicked, this, &QDialog::accept);
    connect(m_btnCancel,   &QPushButton::clicked, this, &QDialog::reject);

    updatePreview();
}

void GdtDialog::refreshFileList() {
    m_fileList->clear();
    QDir dir(resolveGdtDataDir());
    const QStringList files = dir.entryList(QStringList() << "*.gdt", QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        GdtData d;
        if (!loadGdtFile(dir.filePath(fileName), d)) continue;
        QListWidgetItem *item = new QListWidgetItem(QIcon(renderGdtIcon(d, m_fileList->iconSize())), QString());
        item->setSizeHint(m_fileList->iconSize() + QSize(6, 6)); // 텍스트 없이 아이콘 크기에 딱 맞춤 (빈 공간 제거)
        item->setData(kFileNameRole, QFileInfo(fileName).completeBaseName());
        item->setToolTip(QFileInfo(fileName).completeBaseName());
        m_fileList->addItem(item);
    }
}

void GdtDialog::onSaveClicked() {
    QDir dir(resolveGdtDataDir());
    if (!dir.exists()) dir.mkpath(".");

    // 이름 입력 없이 "GDT_yyyyMMdd_HHmmss_zzz"로 바로 저장 (밀리초까지 포함해 연속 클릭 충돌 방지)
    QString name = "GDT_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString filePath = dir.filePath(name + ".gdt");

    if (!saveGdtFile(filePath, resultData())) {
        QMessageBox::warning(this, tr("저장 실패"), tr("파일을 저장할 수 없습니다."));
        return;
    }

    refreshFileList();
}

void GdtDialog::onListItemClicked(QListWidgetItem *item) {
    if (!item) return;
    const QString baseName = item->data(kFileNameRole).toString();
    QString filePath = QDir(resolveGdtDataDir()).filePath(baseName + ".gdt");
    GdtData d;
    if (!loadGdtFile(filePath, d)) return;
    setData(d);
    m_activePreset = baseName; // [3-3] 도면이 기억할 활성 프리셋
}

void GdtDialog::preselectPreset(const QString &name) {
    if (name.isEmpty() || !m_fileList) return;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem *it = m_fileList->item(i);
        if (it && it->data(kFileNameRole).toString() == name) {
            m_fileList->setCurrentItem(it);
            onListItemClicked(it); // 값 로드 + m_activePreset 설정
            return;
        }
    }
}

void GdtDialog::onListContextMenu(const QPoint &pos) {
    QListWidgetItem *item = m_fileList->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction *deleteAction = menu.addAction(tr("삭제"));
    if (menu.exec(m_fileList->mapToGlobal(pos)) != deleteAction) return;

    QString filePath = QDir(resolveGdtDataDir()).filePath(item->data(kFileNameRole).toString() + ".gdt");
    QFile::remove(filePath);
    refreshFileList();
}

void GdtDialog::onSymbolSelected(int id) {
    if (id >= 0 && id < kSymCount) {
        m_selectedSymbol = kSymbols[id].sym;
        updatePreview();
    }
}
void GdtDialog::onInputChanged() { updatePreview(); }
void GdtDialog::updatePreview() { m_preview->setData(resultData()); }

GdtData GdtDialog::resultData() const {
    GdtData d;
    d.symbol         = m_selectedSymbol;
    d.toleranceValue = m_edTolerance->text().trimmed();
    d.hasDiameter    = m_chkDiam->isChecked();
    d.textHeight     = m_spnTextHeight->value();
    d.useLeader      = m_chkUseLeader->isChecked(); // [추가] 지시선 속성 전달

    for (int i = 0; i < 3; ++i) {
        QString txt = m_edDatum[i]->text().trimmed();
        if (!txt.isEmpty()) d.datumRefs.append(txt);
    }
    return d;
}

void GdtDialog::setData(const GdtData &data) {
    m_selectedSymbol = data.symbol;
    for (int i = 0; i < kSymCount; ++i) {
        if (kSymbols[i].sym == data.symbol) {
            QAbstractButton *btn = m_symGroup->button(i);
            if (btn) btn->setChecked(true);
            break;
        }
    }
    m_edTolerance->setText(data.toleranceValue);
    m_chkDiam->setChecked(data.hasDiameter);
    m_spnTextHeight->setValue(data.textHeight);
    m_chkUseLeader->setChecked(data.useLeader);

    for (int i = 0; i < 3 && i < data.datumRefs.size(); ++i)
        m_edDatum[i]->setText(data.datumRefs[i]);

    updatePreview();
}