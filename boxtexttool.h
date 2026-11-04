#ifndef BOXTEXTTOOL_H
#define BOXTEXTTOOL_H

#include "dtexttool.h"

class TextItem;

// BOXTEXT
//  - 기본: DTEXT와 동일하게 글자를 입력하고, 확정(엔터/다음 시작점 클릭)되면
//          생성된 글자 객체에 맞춰 박스를 자동 생성
//  - [Pick] 옵션: 기존 글자 객체를 선택해 박스 생성 (연속 선택 가능)
//  - [Setup] 옵션: 박스/글자 레이어, 박스크기 연동 설정 (BoxTextSetupDialog)
class BoxTextTool : public DTextTool {
    Q_OBJECT
public:
    explicit BoxTextTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

protected:
    QString commandLogName() const override { return QStringLiteral("BOXTEXT Command"); }
    void showStartPrompt() override;
    void onTextCommitted(QGraphicsItem *textItem) override;
    QString targetTextLayer() const override;

private:
    void enterPickMode();
    void finishTool();
    void createBoxForText(TextItem *textItem);
    void applyTextLayer(TextItem *textItem);

    void loadSettings(MyGraphicsScene *scene);
    void saveSettings(MyGraphicsScene *scene);
    void openSetupDialog();
    // name이 현재 도면에 있는 레이어면 name, 아니면 fallback
    QString resolveLayer(const QString &name, const QString &fallback) const;

    bool m_pickMode = false;

    // Setup 설정 (빈 문자열 = 미지정 → 기본 레이어 사용)
    QString m_boxLayer;
    QString m_textLayer;
    bool m_applyTextLayerToPicked = true; // Pick 모드: 선택한 기존 글자를 글자 레이어로 변경
    bool m_linkBoxSize = true;
};

#endif // BOXTEXTTOOL_H
