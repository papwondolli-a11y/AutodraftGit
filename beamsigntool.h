#ifndef BEAMSIGNTOOL_H
#define BEAMSIGNTOOL_H

#include "abstracttool.h"
#include "beamsign.h"
#include "commandbase.h"
#include <QDialog>
#include <QPointF>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QTableWidget;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignParams — 보 부호 설정
// ─────────────────────────────────────────────────────────────────────────────
struct BeamSignParams {
    QString prefix = QStringLiteral("1");   // 층 구분
    QString type   = QStringLiteral("G");   // 종류
    int     number = 1;                     // 번호
    BeamSignSizes sizes;                    // 층 구분 / 종류 / 번호 글자 크기 (축척 1 기준)
    bool    autoIncrement = true;           // 배치할 때마다 번호 자동 증가 (같은 층 구분·종류의 다음 번호)
    bool    setSize = false;                // 일람표에 보 크기(b, D)도 함께 등록
    double  width = 600.0;
    double  depth = 800.0;
    QString outLayer  = BeamSignBuilder::kOutLayer;    // 외곽 레이어
    QString textLayer = BeamSignBuilder::kTextLayer;   // 문자 레이어

    BeamMarkParts parts() const {
        BeamMarkParts p;
        p.prefix = prefix;
        p.type   = type;
        p.number = QString::number(number);
        return p;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignTool — 보 부호 (blocks/etc/g.cdb)
//   블록 "BEAMSIGN" (속성 PREFIX / TYPE / NO) 으로 삽입한다.
//   배치한 부호는 보 일람표 목록에 없으면 빈 줄로 자동 등록된다 (철근 데이터는 일람표에서 입력).
//   S : 설정 다시 열기   우클릭 / Esc : 종료
// ─────────────────────────────────────────────────────────────────────────────
class BeamSignTool : public AbstractTool {
    Q_OBJECT

public:
    explicit BeamSignTool(const BeamSignParams &params, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static BeamSignParams loadParams(MyGraphicsScene *scene);
    static void           saveParams(MyGraphicsScene *scene, const BeamSignParams &p);

    // 도면의 BEAMSIGN 블록에서 부호 읽기 ({부호 → 수량})
    static QMap<QString, int> signCountsInDrawing(MyGraphicsScene *scene);
    static QStringList signNamesInDrawing(MyGraphicsScene *scene);
    // 일람표 목록 + 도면의 부호 전체
    static QStringList allMarkNames(MyGraphicsScene *scene);
    // 같은 층 구분·종류의 다음 번호
    static int suggestNumber(MyGraphicsScene *scene, const QString &prefix, const QString &type);

    // 부호 도형 (문자 폭은 폰트로 계산)
    static BeamSignGeom buildGeom(const BeamMarkParts &parts, const BeamSignSizes &sizes,
                                  const QString &fontFamily);

private:
    void showPrompt();
    void finishTool();
    void place(const QPointF &pos);
    void registerInList(const QString &name);
    void reopenSettings();
    double scaleValue() const;
    void ensureLayer(IAppContext *ctx, const QString &name, const QColor &color);

    BeamSignParams m_params;
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignDialog — 층 구분 / 종류 / 번호 / (선택) 크기 / 레이어
// ─────────────────────────────────────────────────────────────────────────────
class BeamSignDialog : public QDialog {
    Q_OBJECT

public:
    BeamSignDialog(const BeamSignParams &params, MyGraphicsScene *scene,
                   LayerManager *layerMgr, QWidget *parent = nullptr);

    BeamSignParams params() const;

private slots:
    void onKeyChanged();            // 층 구분/종류가 바뀌면 다음 번호 제안
    void onAutoToggled(bool on);
    void updatePreview();

private:
    static void populateLayerCombo(QComboBox *combo, LayerManager *mgr, const QString &selected);

    MyGraphicsScene *m_scene;
    QComboBox       *m_comboPrefix;
    QComboBox       *m_comboType;
    QSpinBox        *m_spinNumber;
    QDoubleSpinBox  *m_spinPrefixSize;
    QDoubleSpinBox  *m_spinTypeSize;
    QDoubleSpinBox  *m_spinNumberSize;
    QCheckBox       *m_checkAuto;
    QCheckBox       *m_checkSize;
    QDoubleSpinBox  *m_spinWidth;
    QDoubleSpinBox  *m_spinDepth;
    QComboBox       *m_comboOut;
    QComboBox       *m_comboText;
    QLabel          *m_labelPreview;
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignImportDialog — 구조도(.cdb)에서 보 부호 가져오기
//   파일을 여러 개 고르면 그 안의 BEAMSIGN 부호를 읽어 목록으로 보여 주고,
//   층 구분으로 걸러서(층별 일람표) 체크한 부호만 가져온다.
// ─────────────────────────────────────────────────────────────────────────────
class BeamSignImportDialog : public QDialog {
    Q_OBJECT

public:
    BeamSignImportDialog(const QStringList &existingNames, MyGraphicsScene *scene,
                         QWidget *parent = nullptr);

    QStringList selectedNames() const;   // 체크한(표시 중인) 부호, 자연 정렬

private slots:
    void onAddFiles();
    void onRemoveFile();
    void onSourceChanged();
    void onFilterChanged();
    void onSelectAll();
    void onSelectNone();
    void onItemChanged();

private:
    struct Found {
        int         count = 0;
        QStringList sources;
    };

    void rescan();
    void rebuildTable();

    QStringList            m_existing;
    MyGraphicsScene       *m_scene;
    QStringList            m_knownTypes;
    QMap<QString, Found>   m_found;
    QMap<QString, bool>    m_checked;
    bool                   m_building = false;

    QListWidget  *m_listFiles;
    QCheckBox    *m_checkCurrent;
    QComboBox    *m_comboFloor;
    QTableWidget *m_table;
    QLabel       *m_labelInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignCommand — 설정창 → 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class BeamSignCommand : public CommandBase {
    Q_OBJECT

public:
    explicit BeamSignCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // BEAMSIGNTOOL_H
