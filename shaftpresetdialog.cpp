#include "shaftpresetdialog.h"

#include "shaftbuilder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
const char *kExt = ".shaft";
const QSize kPreviewSize(380, 170);

bool readParams(const QString &path, ShaftParams &out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString json = QString::fromUtf8(f.readAll());
    ShaftParams empty;
    empty.segments.clear();
    out = ShaftParams::fromJsonString(json, empty);
    return !out.segments.isEmpty();
}

bool writeParams(const QString &path, const ShaftParams &p) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    return f.write(p.toJsonString().toUtf8()) >= 0;
}

// 축 외형 미리보기 그림 (도면 좌표는 위쪽 +Y 이므로 뒤집어 그린다)
QPixmap renderPreview(const ShaftParams &p) {
    QPixmap pix(kPreviewSize);
    pix.fill(QColor(30, 30, 30));
    const QPainterPath path = ShaftBuilder::buildPath(p, QPointF(0.0, 0.0));
    if (path.isEmpty()) return pix;
    const QRectF br = path.boundingRect();
    const double margin = 12.0;
    const double sx = (kPreviewSize.width() - 2 * margin) / qMax(br.width(), 1e-6);
    const double sy = (kPreviewSize.height() - 2 * margin) / qMax(br.height(), 1e-6);
    const double s = qMin(sx, sy);
    QPainter g(&pix);
    g.setRenderHint(QPainter::Antialiasing);
    g.translate(kPreviewSize.width() * 0.5, kPreviewSize.height() * 0.5);
    g.scale(s, -s);
    g.translate(-br.center());
    QPen pen(QColor(230, 230, 230));
    pen.setCosmetic(true);
    pen.setWidthF(1.2);
    g.setPen(pen);
    g.setBrush(Qt::NoBrush);
    g.drawPath(path);
    return pix;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────
QString ShaftPresetDialog::presetDir(bool create) {
    // data 폴더: exe 옆 → 상위 폴더 → 소스 폴더 (기하공차 프리셋과 같은 규칙)
    QString data = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    if (!QDir(data).exists()) {
        const QString alt = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../data"));
        if (QDir(alt).exists()) {
            data = alt;
        }
#ifdef APP_SOURCE_PATH
        else {
            const QString dev = QDir::cleanPath(APP_SOURCE_PATH) + QStringLiteral("/data");
            if (QDir(dev).exists()) data = dev;
        }
#endif
    }
    const QString dir = data + QStringLiteral("/shaft");
    if (create) QDir().mkpath(dir);
    return dir;
}

QString ShaftPresetDialog::sanitizeName(const QString &name) {
    QString s = name.trimmed();
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return s;
}

QString ShaftPresetDialog::summaryText(const ShaftParams &p) {
    int keyways = 0;
    for (int i = 0; i < p.segments.size(); ++i)
        if (p.segments.at(i).keyway.enabled) ++keyways;
    QString thread;
    switch (p.threadSide) {
    case ShaftThreadSide::Left:  thread = tr("왼쪽 M%1").arg(p.segments.first().diameter); break;
    case ShaftThreadSide::Right: thread = tr("오른쪽 M%1").arg(p.segments.last().diameter); break;
    case ShaftThreadSide::Both:
        thread = tr("양쪽 M%1 / M%2").arg(p.segments.first().diameter).arg(p.segments.last().diameter);
        break;
    default: thread = tr("없음"); break;
    }
    return tr("구간 %1 · 전체 %2 mm · 최대 Ø%3\n키홈 %4 · 나사 %5")
        .arg(p.segments.size()).arg(p.totalLength()).arg(p.maxDiameter()).arg(keyways).arg(thread);
}

bool ShaftPresetDialog::savePreset(QWidget *parent, const ShaftParams &params) {
    const QString dir = presetDir(true);
    QString name = QStringLiteral("축_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"));
    for (;;) {
        bool ok = false;
        name = QInputDialog::getText(parent, tr("축 데이터 저장"), tr("이름 :"), QLineEdit::Normal, name, &ok);
        if (!ok) return false;
        name = sanitizeName(name);
        if (name.isEmpty()) continue;
        const QString path = QDir(dir).filePath(name + QLatin1String(kExt));
        if (QFileInfo::exists(path)) {
            const QMessageBox::StandardButton b = QMessageBox::question(
                parent, tr("축 데이터 저장"), tr("\"%1\" 이(가) 이미 있습니다. 덮어쓸까요?").arg(name),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
            if (b == QMessageBox::Cancel) return false;
            if (b == QMessageBox::No) continue;   // 다른 이름 입력
        }
        if (!writeParams(path, params)) {
            QMessageBox::warning(parent, tr("저장 실패"), tr("파일을 저장할 수 없습니다.\n%1").arg(path));
            return false;
        }
        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────
ShaftPresetDialog::ShaftPresetDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("축 데이터 불러오기"));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *body = new QHBoxLayout;

    QVBoxLayout *left = new QVBoxLayout;
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(200);
    left->addWidget(m_list, 1);
    QHBoxLayout *listBtns = new QHBoxLayout;
    m_rename = new QPushButton(tr("이름 바꾸기"), this);
    m_delete = new QPushButton(tr("삭제"), this);
    listBtns->addWidget(m_rename);
    listBtns->addWidget(m_delete);
    left->addLayout(listBtns);
    body->addLayout(left);

    QVBoxLayout *right = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setFixedSize(kPreviewSize);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    right->addWidget(m_preview);
    right->addWidget(m_summary);
    right->addStretch(1);
    body->addLayout(right);
    mainLayout->addLayout(body);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    m_load = buttons->addButton(tr("불러오기"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttons);

    connect(m_list, &QListWidget::currentItemChanged, this, &ShaftPresetDialog::onCurrentChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ShaftPresetDialog::onItemDoubleClicked);
    connect(m_rename, &QPushButton::clicked, this, &ShaftPresetDialog::onRename);
    connect(m_delete, &QPushButton::clicked, this, &ShaftPresetDialog::onDelete);
    connect(buttons, &QDialogButtonBox::accepted, this, &ShaftPresetDialog::onLoad);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshList();
}

bool ShaftPresetDialog::hasPresets() const {
    return m_list->count() > 0;
}

void ShaftPresetDialog::refreshList(const QString &select) {
    m_list->clear();
    const QDir dir(presetDir(false));
    const QStringList files =
        dir.entryList(QStringList() << QStringLiteral("*") + QLatin1String(kExt), QDir::Files, QDir::Name);
    int selRow = 0;
    for (int i = 0; i < files.size(); ++i) {
        const QString base = QFileInfo(files.at(i)).completeBaseName();
        m_list->addItem(base);
        if (base == select) selRow = i;
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(selRow);
    } else {
        onCurrentChanged(nullptr, nullptr);
    }
}

QString ShaftPresetDialog::currentFile() const {
    const QListWidgetItem *it = m_list->currentItem();
    if (!it) return QString();
    return QDir(presetDir(false)).filePath(it->text() + QLatin1String(kExt));
}

void ShaftPresetDialog::onCurrentChanged(QListWidgetItem *current, QListWidgetItem *) {
    const bool has = current != nullptr;
    m_rename->setEnabled(has);
    m_delete->setEnabled(has);
    m_load->setEnabled(false);
    if (!has) {
        QPixmap empty(kPreviewSize);
        empty.fill(QColor(30, 30, 30));
        m_preview->setPixmap(empty);
        m_summary->setText(tr("저장된 축 데이터가 없습니다.\n축 설정창의 [데이터 저장]으로 만드세요."));
        return;
    }
    ShaftParams p;
    if (!readParams(currentFile(), p)) {
        m_preview->setPixmap(QPixmap());
        m_summary->setText(tr("파일을 읽을 수 없습니다."));
        return;
    }
    m_preview->setPixmap(renderPreview(p));
    m_summary->setText(summaryText(p));
    m_load->setEnabled(true);
}

void ShaftPresetDialog::onItemDoubleClicked(QListWidgetItem *item) {
    if (item) onLoad();
}

void ShaftPresetDialog::onRename() {
    QListWidgetItem *it = m_list->currentItem();
    if (!it) return;
    const QString oldName = it->text();
    bool ok = false;
    const QString name = sanitizeName(
        QInputDialog::getText(this, tr("이름 바꾸기"), tr("새 이름 :"), QLineEdit::Normal, oldName, &ok));
    if (!ok || name.isEmpty() || name == oldName) return;
    const QDir dir(presetDir(false));
    const QString to = dir.filePath(name + QLatin1String(kExt));
    if (QFileInfo::exists(to)) {
        QMessageBox::warning(this, tr("이름 바꾸기"), tr("\"%1\" 이(가) 이미 있습니다.").arg(name));
        return;
    }
    if (!QFile::rename(dir.filePath(oldName + QLatin1String(kExt)), to)) {
        QMessageBox::warning(this, tr("이름 바꾸기"), tr("이름을 바꿀 수 없습니다."));
        return;
    }
    refreshList(name);
}

void ShaftPresetDialog::onDelete() {
    QListWidgetItem *it = m_list->currentItem();
    if (!it) return;
    if (QMessageBox::question(this, tr("삭제"), tr("\"%1\" 을(를) 삭제할까요?").arg(it->text()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const int row = m_list->currentRow();
    QFile::remove(currentFile());
    refreshList();
    if (m_list->count() > 0) m_list->setCurrentRow(qMin(row, m_list->count() - 1));
}

void ShaftPresetDialog::onLoad() {
    ShaftParams p;
    if (!readParams(currentFile(), p)) return;
    m_params = p;
    accept();
}
