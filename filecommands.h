#ifndef FILECOMMANDS_H
#define FILECOMMANDS_H

#include "commandbase.h"
#include "iappcontext.h"
#include <QRectF>

class QProgressDialog;

class FileOpenCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileOpenCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;
  void setFilePath(const QString &path) { m_filePath = path; }
  // 호출 측(작업 영역 전환까지 아우르는 쪽)이 소유한 진행 다이얼로그를 전달
  // 받아 로딩 루프 중 갱신에 사용한다. 매 호출 전에 설정하고, execute() 이후
  // 다시 nullptr로 되돌릴 필요는 없다(다음 호출에서 다시 설정됨).
  void setProgressDialog(QProgressDialog *dlg) { m_progress = dlg; }

private:
  IAppContext *m_ctx;
  QString m_filePath;
  QProgressDialog *m_progress = nullptr;
};

class FileSaveCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileSaveCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class FileSaveAsCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileSaveAsCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class ExitCommand : public CommandBase {
  Q_OBJECT
public:
  explicit ExitCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class FilePrintCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FilePrintCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private slots:
  // "윈도우 지정" 선택 시 PrintWindowTool이 사각형을 다 그린 뒤 호출됨 (비동기 콜백)
  void onPrintWindowSelected(const QRectF &rect);
  // "윈도우 지정" 도중 ESC 등으로 취소했을 때 호출됨
  void onPrintWindowCancelled();

private:
  IAppContext *m_ctx;
};

class FileDxfImportCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileDxfImportCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

class FileDxfExportCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileDxfExportCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

// DWG 가져오기 (acadrust 기반 변환기 tools/dwg2dxf.exe 사용, AcadrustImporter 참고)
class FileDwgImportCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileDwgImportCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

// 도면을 DWG로 저장 (외부 변환기 tools/dxf2dwg.exe 사용, DwgExporter 참고)
class FileDwgExportCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileDwgExportCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

// 여러 도면(.cdb, .dwg, .dxf) 파일을 한 번에 인쇄하는 일괄 출력 명령
class FileBatchPrintCommand : public CommandBase {
  Q_OBJECT
public:
  explicit FileBatchPrintCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

// 레이어별 인쇄 전용 색상(출력 스타일)을 편집하는 다이얼로그를 여는 명령
class PrintStyleCommand : public CommandBase {
  Q_OBJECT
public:
  explicit PrintStyleCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

#endif // FILECOMMANDS_H
