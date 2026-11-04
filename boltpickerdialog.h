#ifndef BOLTPICKERDIALOG_H
#define BOLTPICKERDIALOG_H

#include "commandbase.h"
#include "iappcontext.h"
#include "bolttool.h"
#include "wrenchbolttool.h"
#include "hhbolttool.h"
#include "hbbolttool.h"

#include <QDialog>
#include <QAbstractButton>
#include <QPainterPath>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
// 볼트 종류 선택 — "기계부품" 탭의 "볼트" 버튼을 누르면 뜨는 아이콘 선택
// 다이얼로그. 4개 볼트 툴(BoltTool/WrenchBoltTool/HHBoltTool/HBBoltTool)이
// 각자 가진 buildWorldPath()의 QPainterPath를 그대로 아이콘으로 그려서
// 보여준다 — 별도 이미지 파일 없음(gdtdialog.cpp의 renderGdtIcon()/
// paintGdtPreview() 조합과 동일한 아이디어: QPainter로 직접 그려서 QIcon/버튼에
// 쓰는 패턴을 그대로 따름).
//
// 이 다이얼로그가 추가되기 전에는 볼트/렌치볼트/접시머리렌치볼트/둥근머리렌치볼트가
// "기계부품" 탭에 개별 버튼 4개로 따로 등록돼 있었다 — 이제 "볼트" 버튼 하나로
// 통합되고, 나머지 3개는 탭 버튼만 사라지고 타이핑 명령어(wbolt/hhbolt/hbbolt)는
// 그대로 남는다(appcommandregistry.cpp 참고).
// ─────────────────────────────────────────────────────────────────────────

enum class BoltKind { Hex, Wrench, Countersunk, Button };

// 볼트 하나의 미리보기(QPainterPath)를 그리는 클릭 가능한 아이콘 버튼.
// 명칭은 이 버튼이 직접 그리지 않고(툴팁으로만 보여줌) 별도 QLabel로 아이콘
// 아래 중앙에 배치한다 — BoltPickerDialog::setupUI() 참고.
class BoltIconButton : public QAbstractButton {
    Q_OBJECT
public:
    BoltIconButton(const QString &label, const QPainterPath &previewPath, BoltKind kind,
                   QWidget *parent = nullptr);

    BoltKind kind() const { return m_kind; }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    QPainterPath m_previewPath; // world 좌표(정규화 안 됨) — paintEvent에서 버튼 크기에 맞춰 스케일
    BoltKind     m_kind;
};

class BoltPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit BoltPickerDialog(QWidget *parent = nullptr);

    BoltKind selectedKind() const { return m_selectedKind; }

private slots:
    // 아이콘 버튼 4개가 전부 이 슬롯 하나로 연결된다 — 어느 버튼이 눌렸는지는
    // sender()를 BoltIconButton*으로 캐스팅해 kind()로 판별한다(람다 없이 처리).
    void onIconClicked();

private:
    void setupUI();
    static QPainterPath previewPathFor(BoltKind kind);

    BoltKind m_selectedKind;
};

// 아이콘 선택 다이얼로그를 띄운 뒤, 고른 볼트 종류에 맞는 툴을 시작한다.
// ToolCommand<T>(ctx->startTool(new T()))와 동일한 패턴이지만, 타입이 다이얼로그
// 결과에 따라 런타임에 갈리므로 템플릿으로 못 쓰고 직접 분기한다. BoltTool 등
// 4개 볼트 헤더에 직접 의존하므로(cadcommands.h가 이걸 몰라도 되게) 별도 파일
// 대신 여기 같이 둔다.
class BoltPickerCommand : public CommandBase {
public:
    explicit BoltPickerCommand(IAppContext *ctx, QObject *parent = nullptr)
        : CommandBase(parent), m_ctx(ctx) {}

    void execute() override {
        if (!m_ctx) return;
        BoltPickerDialog dlg;
        if (dlg.exec() != QDialog::Accepted) return;
        switch (dlg.selectedKind()) {
        case BoltKind::Hex:         m_ctx->startTool(new BoltTool());       break;
        case BoltKind::Wrench:      m_ctx->startTool(new WrenchBoltTool()); break;
        case BoltKind::Countersunk: m_ctx->startTool(new HHBoltTool());     break;
        case BoltKind::Button:      m_ctx->startTool(new HBBoltTool());     break;
        }
    }

private:
    IAppContext *m_ctx;
};

#endif // BOLTPICKERDIALOG_H
