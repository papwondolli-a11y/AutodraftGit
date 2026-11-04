#ifndef TEMPLATECOMMANDS_H
#define TEMPLATECOMMANDS_H

#include "commandbase.h"

class IAppContext;

// ─────────────────────────────────────────────────────────────────────────────
// NewFromTemplateCommand
//   "파일 > 템플릿으로 새로 만들기...". 템플릿 선택 다이얼로그를 띄우고,
//   고른 템플릿(acad.dwt 개념)을 빈 무제 문서로 복사해 새 창을 연다.
// ─────────────────────────────────────────────────────────────────────────────
class NewFromTemplateCommand : public CommandBase {
    Q_OBJECT
public:
    explicit NewFromTemplateCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
};

// ─────────────────────────────────────────────────────────────────────────────
// SaveAsTemplateCommand
//   "파일 > 템플릿으로 저장...". 현재 도면을 templates 폴더에 *.cdt 로 저장한다.
//   (일반 저장과 동일한 .cdb 스키마 — 확장자만 다르다.)
//   저장한 템플릿은 무조건 '새로만들기(Ctrl+N, QNEW)'의 기본값이 되며,
//   저장 후 열려 있는 모든 도면을 닫고 대시보드로 돌아간다.
// ─────────────────────────────────────────────────────────────────────────────
class SaveAsTemplateCommand : public CommandBase {
    Q_OBJECT
public:
    explicit SaveAsTemplateCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

private:
    IAppContext *m_ctx;
};

#endif // TEMPLATECOMMANDS_H
