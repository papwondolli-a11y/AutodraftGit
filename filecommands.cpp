#include "filecommands.h"
#include "drawsubwindow.h"
#include "mygraphicsscene.h"
#include "filemanager.h"
#include "mygraphicsview.h"
#include "sceneprinter.h"
#include "batchprintdialog.h"
#include "printwindowtool.h"
#include "printstyledialog.h"
#include "layermanager.h"
#include "layer.h"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>
#include <QMdiArea>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QProgressDialog>
#include <QMap>
#include <QSet>
#include <QColor>
#include "dxfmanager.h"
#include "acadrustimporter.h"
#include "dwgexporter.h"


// --- FileOpenCommand ---
FileOpenCommand::FileOpenCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileOpenCommand::execute() {
  if (!m_ctx || !m_ctx->fileManager())
    return;

  QWidget *parent = m_ctx->activeDrawWindow();

  QString filePath = m_filePath;
  m_filePath.clear(); // Reset for next time

  if (filePath.isEmpty()) {
    filePath =
        QFileDialog::getOpenFileName(parent, tr("Open Drawing"), "",
                                     tr("CAD Database (*.cdb);;All Files (*)"));
  }

  if (filePath.isEmpty())
    return;

  // 호출 측이 setProgressDialog()로 넘겨준 진행 다이얼로그(작업 영역 전환까지
  // 아우르는 수명)를 쓴다. 다음 호출에서 실수로 낡은(이미 파괴된) 포인터를
  // 재사용하지 않도록 즉시 비운다 — 메뉴 등 다이얼로그 없이 호출되는 경로는
  // nullptr로 전달되어 loadDrawing()이 진행률 갱신 없이 그냥 동작한다.
  QProgressDialog *progress = m_progress;
  m_progress = nullptr;

  DrawSubWindow *sub = new DrawSubWindow();
  m_ctx->addSubWindow(sub);

  bool loaded =
      m_ctx->fileManager()->loadDrawing(filePath, sub->scene(), false, progress);

  if (loaded) {
    sub->setWindowTitle(QFileInfo(filePath).fileName());
    sub->setProperty("filePath", filePath);
    m_ctx->showStatusText(tr("Opened: %1").arg(filePath));
    m_ctx->addToRecentFiles(filePath);

    // [추가] 방금 막 열린 '그 윈도우(sub)'의 언두 스택만 안전하게 비웁니다.
    if (sub->undoStack()) {
        sub->undoStack()->clear();
    }
    // [핵심 추가] 파일을 로드한 후 자동으로 줌 올(Zoom All) 실행
    // 비동기로 호출해야 뷰포트 크기가 완전히 확정된 후 정확히 계산됨
    QTimer::singleShot(100, sub, [sub]() {
      if (sub && sub->view()) {
        sub->view()->zoomAll();
      }
    });
  } else {
    QMessageBox::critical(parent, tr("Error"), tr("Failed to load drawing."));
    sub->close();
  }
}

// --- FileSaveCommand ---
FileSaveCommand::FileSaveCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{
}

void FileSaveCommand::execute() {
  if (!m_ctx || !m_ctx->fileManager())
    return;

  DrawSubWindow *sub = m_ctx->activeDrawWindow();
  if (!sub)
    return;

  QString filePath = sub->property("filePath").toString();
  if (filePath.isEmpty()) {
    FileSaveAsCommand saveAs(m_ctx);
    saveAs.execute();
    return;

    if (m_ctx->fileManager()->saveDrawing(filePath, sub->scene())) {
        m_ctx->showStatusText(tr("Saved: %1").arg(filePath));

        // [추가] 저장된 '그 윈도우(sub)'의 언두 스택만 Clean 상태로 만듭니다.
        if (sub->undoStack()) {
            sub->undoStack()->setClean();
        }
    } else {
        QMessageBox::critical(sub, tr("Error"), tr("Failed to save drawing."));
    }
  }

  if (m_ctx->fileManager()->saveDrawing(filePath, sub->scene())) {
    m_ctx->showStatusText(tr("Saved: %1").arg(filePath));
  } else {
    QMessageBox::critical(sub, tr("Error"), tr("Failed to save drawing."));
  }
}

// --- FileSaveAsCommand ---
FileSaveAsCommand::FileSaveAsCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileSaveAsCommand::execute() {
  if (!m_ctx || !m_ctx->fileManager())
    return;

  DrawSubWindow *sub = m_ctx->activeDrawWindow();
  if (!sub)
    return;

  QString filePath = QFileDialog::getSaveFileName(
      sub, tr("Save Drawing"), "", tr("CAD Database (*.cdb);;All Files (*)"));
  if (filePath.isEmpty())
    return;

  if (m_ctx->fileManager()->saveDrawing(filePath, sub->scene())) {
    sub->setProperty("filePath", filePath);
    sub->setWindowTitle(QFileInfo(filePath).fileName());
    m_ctx->showStatusText(tr("Saved As: %1").arg(filePath));
    m_ctx->addToRecentFiles(filePath);
  } else {
    QMessageBox::critical(sub, tr("Error"), tr("Failed to save drawing."));
  }
  if (m_ctx->fileManager()->saveDrawing(filePath, sub->scene())) {
      sub->setProperty("filePath", filePath);
      sub->setWindowTitle(QFileInfo(filePath).fileName());
      m_ctx->showStatusText(tr("Saved As: %1").arg(filePath));
      m_ctx->addToRecentFiles(filePath);

      // [추가] 저장된 '그 윈도우(sub)'의 언두 스택만 Clean 상태로 만듭니다.
      if (sub->undoStack()) {
          sub->undoStack()->setClean();
      }
  } else {
      QMessageBox::critical(sub, tr("Error"), tr("Failed to save drawing."));
  }

}

// --- ExitCommand ---
ExitCommand::ExitCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void ExitCommand::execute() { qApp->quit(); }

// --- FilePrintCommand ---
FilePrintCommand::FilePrintCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FilePrintCommand::execute() {
  if (!m_ctx) {
    deleteLater();
    return;
  }

  DrawSubWindow *sub = m_ctx->activeDrawWindow();
  if (!sub || !sub->scene()) {
    QMessageBox::information(nullptr, tr("Print"), tr("출력할 도면이 없습니다."));
    deleteLater();
    return;
  }

  // [윈도우 지정 출력] 전체 도면을 출력할지, 화면에서 사각형으로 영역을 지정해 그 부분만
  // 출력할지 먼저 선택받는다.
  QMessageBox rangeBox(sub);
  rangeBox.setWindowTitle(tr("출력 범위"));
  rangeBox.setText(tr("출력할 범위를 선택하세요."));
  QPushButton *allBtn = rangeBox.addButton(tr("전체 도면"), QMessageBox::AcceptRole);
  QPushButton *windowBtn = rangeBox.addButton(tr("윈도우 지정"), QMessageBox::ActionRole);
  rangeBox.addButton(QMessageBox::Cancel);
  rangeBox.exec();

  if (rangeBox.clickedButton() == windowBtn) {
    // 인터랙티브 윈도우 선택 도구를 시작한다. execute()는 여기서 즉시 리턴하고,
    // 실제 인쇄는 사용자가 사각형을 다 그린 뒤 onPrintWindowSelected() 콜백에서 이어진다.
    // (그동안 이 커맨드 객체는 살아 있어야 하므로, 호출부에서 반드시 힙 할당해야 한다.)
    PrintWindowTool *tool = new PrintWindowTool(m_ctx);
    connect(tool, &PrintWindowTool::windowRectSelected, this, &FilePrintCommand::onPrintWindowSelected);
    connect(tool, &PrintWindowTool::cancelled, this, &FilePrintCommand::onPrintWindowCancelled);
    m_ctx->startTool(tool);
    return;
  }

  if (rangeBox.clickedButton() != allBtn) {
    // 취소
    deleteLater();
    return;
  }

  // 전체 도면 출력
  ScenePrinter::print(sub->scene(), sub);
  deleteLater();
}

void FilePrintCommand::onPrintWindowSelected(const QRectF &rect) {
  DrawSubWindow *sub = m_ctx ? m_ctx->activeDrawWindow() : nullptr;
  if (sub && sub->scene()) {
    ScenePrinter::print(sub->scene(), sub, rect);
  }
  deleteLater();
}

void FilePrintCommand::onPrintWindowCancelled() {
  deleteLater();
}
// --- FileDxfImportCommand ---
FileDxfImportCommand::FileDxfImportCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileDxfImportCommand::execute() {
  if (!m_ctx) return;
  DrawSubWindow *sub = m_ctx->activeDrawWindow();
  if (!sub || !sub->scene()) {
      QMessageBox::warning(nullptr, tr("오류"), tr("도면 창이 활성화되어 있지 않습니다."));
      return;
  }
  QString filePath = QFileDialog::getOpenFileName(sub, tr("DXF 가져오기"), "", tr("DXF 파일 (*.dxf)"));
  if (filePath.isEmpty()) return;

  // 가져오기 진행 표시: 취소 버튼 없는 무한(busy) 모달 다이얼로그.
  // 모달 QProgressDialog::setValue()가 내부적으로 processEvents()를 호출하므로
  // 파싱이 진행되는 동안에도 라벨/애니메이션이 계속 갱신된다.
  QProgressDialog progress(tr("DXF 파일을 가져오는 중입니다..."), QString(), 0, 0, sub);
  progress.setWindowTitle(tr("DXF 가져오기"));
  // [버그 수정] Qt::WindowModal은 프레임리스 메인 윈도우의 최대화/복원 토글이
  // 다이얼로그가 닫힌 뒤 먹통이 되는 원인이 된다(mainwindow.cpp의 도면 열기
  // 진행 다이얼로그와 동일한 문제 — 그쪽 수정 참고). NonModal로 띄운다.
  progress.setWindowModality(Qt::NonModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.setValue(0);
  progress.show();
  QApplication::processEvents();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  DxfManager manager(this);
  bool ok = manager.importFromDxf(filePath, sub->scene(), &progress);

  QApplication::restoreOverrideCursor();
  progress.close();

  if (ok) {
      // 가져온 도형 전체가 보이도록 자동으로 줌 익스텐트(Zoom All) 실행
      if (sub->view())
          sub->view()->zoomAll();
      QMessageBox::information(sub, tr("성공"), tr("DXF 파일을 성공적으로 불러왔습니다."));
  } else {
      QMessageBox::warning(sub, tr("실패"), tr("DXF 파일 불러오기에 실패했습니다."));
  }
}

// --- FileDxfExportCommand ---
FileDxfExportCommand::FileDxfExportCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileDxfExportCommand::execute() {
  if (!m_ctx) return;
  DrawSubWindow *sub = m_ctx->activeDrawWindow();
  if (!sub || !sub->scene()) {
      QMessageBox::warning(nullptr, tr("오류"), tr("도면 창이 활성화되어 있지 않습니다."));
      return;
  }
  QString filePath = QFileDialog::getSaveFileName(sub, tr("DXF 내보내기"), "", tr("DXF 파일 (*.dxf)"));
  if (filePath.isEmpty()) return;

  DxfManager manager(this);
  if (manager.exportToDxf(filePath, sub->scene())) {
      QMessageBox::information(sub, tr("성공"), tr("DXF 파일을 성공적으로 저장했습니다."));
  } else {
      QMessageBox::warning(sub, tr("실패"), tr("DXF 파일 저장에 실패했습니다."));
  }
}

// --- FileDwgImportCommand ---
FileDwgImportCommand::FileDwgImportCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileDwgImportCommand::execute() {
    if (!m_ctx) return;
    DrawSubWindow *sub = m_ctx->activeDrawWindow();
    if (!sub || !sub->scene()) {
        QMessageBox::warning(nullptr, tr("오류"), tr("도면 창이 활성화되어 있지 않습니다."));
        return;
    }

    if (!AcadrustImporter::isAvailable()) {
        QMessageBox::warning(sub, tr("DWG 변환기 없음"),
            tr("DWG 가져오기에 필요한 변환기를 찾을 수 없습니다.\n%1")
                .arg(QDir::toNativeSeparators(AcadrustImporter::converterPath())));
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        sub, tr("DWG 가져오기"), "",
        tr("DWG 파일 (*.dwg);;모든 파일 (*.*)"));
    if (filePath.isEmpty()) return;

    // 가져오기 진행 표시 (DXF 가져오기와 동일한 방식, NonModal 사유는 FileDxfImportCommand 주석 참고)
    QProgressDialog progress(tr("DWG 파일을 변환하는 중입니다..."), QString(), 0, 0, sub);
    progress.setWindowTitle(tr("DWG 가져오기"));
    progress.setWindowModality(Qt::NonModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();
    QApplication::setOverrideCursor(Qt::WaitCursor);

    AcadrustImporter importer;
    QString errorMessage;
    bool ok = importer.importFromDwg(filePath, sub->scene(), &progress, &errorMessage);

    QApplication::restoreOverrideCursor();
    progress.close();

    if (ok) {
        // 가져온 도형 전체가 보이도록 자동으로 줌 익스텐트(Zoom All) 실행
        if (sub->view())
            sub->view()->zoomAll();
        QMessageBox::information(sub, tr("성공"), tr("DWG 파일을 성공적으로 불러왔습니다."));
    } else {
        QMessageBox::warning(sub, tr("실패"),
            tr("DWG 파일 불러오기에 실패했습니다.\n%1").arg(errorMessage));
    }
}

// --- FileDwgExportCommand ---
FileDwgExportCommand::FileDwgExportCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileDwgExportCommand::execute() {
    if (!m_ctx) return;
    DrawSubWindow *sub = m_ctx->activeDrawWindow();
    if (!sub || !sub->scene()) {
        QMessageBox::warning(nullptr, tr("오류"), tr("도면 창이 활성화되어 있지 않습니다."));
        return;
    }

    if (!DwgExporter::isAvailable()) {
        QMessageBox::warning(sub, tr("DWG 변환기 없음"),
            tr("DWG 저장에 필요한 변환기를 찾을 수 없습니다.\n%1")
                .arg(QDir::toNativeSeparators(DwgExporter::converterPath())));
        return;
    }

    // 파일 형식(필터)으로 저장 버전을 함께 선택한다.
    const QString filter2018 = tr("AutoCAD 2018 DWG (*.dwg)");
    const QString filter2010 = tr("AutoCAD 2010 DWG (*.dwg)");
    const QString filter2004 = tr("AutoCAD 2004 DWG (*.dwg)");
    const QString filter2000 = tr("AutoCAD 2000 DWG (*.dwg)");
    QString selectedFilter = filter2018;
    QString filePath = QFileDialog::getSaveFileName(
        sub, tr("DWG로 저장"), "",
        filter2018 + ";;" + filter2010 + ";;" + filter2004 + ";;" + filter2000,
        &selectedFilter);
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive))
        filePath += QStringLiteral(".dwg");

    QString version = QStringLiteral("2018");
    if (selectedFilter == filter2010)      version = QStringLiteral("2010");
    else if (selectedFilter == filter2004) version = QStringLiteral("2004");
    else if (selectedFilter == filter2000) version = QStringLiteral("2000");

    QProgressDialog progress(tr("DWG 파일로 저장하는 중입니다..."), QString(), 0, 0, sub);
    progress.setWindowTitle(tr("DWG로 저장"));
    progress.setWindowModality(Qt::NonModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();
    QApplication::setOverrideCursor(Qt::WaitCursor);

    DwgExporter exporter;
    QString errorMessage;
    bool ok = exporter.exportToDwg(filePath, sub->scene(), version, &errorMessage);

    QApplication::restoreOverrideCursor();
    progress.close();

    if (ok) {
        QMessageBox::information(sub, tr("성공"), tr("DWG 파일을 성공적으로 저장했습니다."));
    } else {
        QMessageBox::warning(sub, tr("실패"),
            tr("DWG 파일 저장에 실패했습니다.\n%1").arg(errorMessage));
    }
}

// --- FileBatchPrintCommand ---
FileBatchPrintCommand::FileBatchPrintCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void FileBatchPrintCommand::execute() {
  if (!m_ctx || !m_ctx->fileManager() || !m_ctx->mdiArea())
    return;

  QWidget *parentWidget = m_ctx->activeDrawWindow();

  // 1. 출력 대상 도면 파일 목록 구성 (순서 = 출력 순서)
  BatchPrintDialog fileDlg(m_ctx, parentWidget);
  if (fileDlg.exec() != QDialog::Accepted)
    return;

  QStringList filePaths = fileDlg.filePaths();
  if (filePaths.isEmpty())
    return;

  // 2. 프린터 설정은 배치 전체에 대해 한 번만 진행 (파일마다 다이얼로그를 다시 띄우지 않음)
  QPrinter printer(QPrinter::HighResolution);
  printer.setPageOrientation(QPageLayout::Landscape);

  QPrintDialog printDlg(&printer, parentWidget);
  if (printDlg.exec() != QDialog::Accepted)
    return;

  QPainter painter;
  if (!painter.begin(&printer)) {
    QMessageBox::warning(parentWidget, tr("프린트 오류"), tr("프린터를 시작할 수 없습니다."));
    return;
  }

  // 배치 처리 도중 사용자가 보고 있던 원래 활성 도면 창을 기억해 두었다가 끝나면 복원한다.
  DrawSubWindow *originalActive = m_ctx->activeDrawWindow();

  // [핵심] 활성 창에서 "출력 스타일 편집"으로 지정해 둔 레이어별 인쇄 색상 설정을 미리 읽어둔다.
  // (예: 전체 레이어를 "흑백"으로 일괄 지정한 경우 모든 레이어가 검정으로 오버라이드되어 있음)
  // 일괄 출력은 파일마다 새 LayerManager를 만들어 각 파일에 저장된 설정만 갖고 있으므로,
  // 이 값을 이름 기준으로 적용해 주지 않으면 활성 창에서 편집한 출력 스타일이 무시된다.
  QMap<QString, QColor> activePrintOverrides; // 오버라이드가 지정된 레이어: 이름 -> 인쇄 색상
  QSet<QString> activeLayerNames;             // 활성 창에 존재하는 레이어 이름 전체 (오버라이드 "해제" 여부 판단용)
  // [핵심] "출력 스타일 편집" 상단의 흑백/컬러 라디오는 전체 레이어를 한 번에 지정하는
  // 일괄 조작이다(PrintStyleDialog::onModeChanged 참고). 그런데 아래 이름 기준 매칭은
  // 활성 창과 배치 대상 파일의 레이어 "이름"이 같아야만 적용되므로, cdb/dwg/dxf처럼
  // 레이어 명명 규칙이 서로 다른 파일이 섞인 배치에서는 이름이 하나도 일치하지 않아
  // 흑백으로 지정했음에도 대부분(또는 전부)의 파일에 반영되지 않고 원래 색(자동)으로
  // 출력되는 문제가 있었다. 활성 창의 레이어가 "전부 검정" 또는 "전부 자동(오버라이드 없음)"
  // 상태(=라디오로 일괄 지정한 상태)인 경우를 감지해, 그 경우엔 이름 매칭과 무관하게
  // 모든 파일의 모든 레이어에 동일하게 강제 적용한다.
  bool activeAllBlack = false; // 흑백 일괄 지정 상태
  bool activeAllAuto  = false; // 컬러(자동) 일괄 지정 상태
  if (LayerManager *activeLm = m_ctx->layerManager()) {
    const QList<Layer> activeLayers = activeLm->getAllLayers();
    activeAllBlack = !activeLayers.isEmpty();
    activeAllAuto  = !activeLayers.isEmpty();
    for (const Layer &l : activeLayers) {
      activeLayerNames.insert(l.name());
      if (l.hasPrintColorOverride()) {
        activePrintOverrides.insert(l.name(), l.printColor());
        activeAllAuto = false;
        if (l.printColor() != QColor(Qt::black)) {
          activeAllBlack = false;
        }
      } else {
        activeAllBlack = false;
      }
    }
  }

  QProgressDialog progress(tr("일괄 출력 중..."), tr("취소"), 0, filePaths.size(), parentWidget);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);

  int successCount = 0;
  QStringList failedFiles;

  // 인덱스 기반 루프 사용 (C++11 range-loop detach 경고 방지)
  for (int i = 0; i < filePaths.size(); ++i) {
    const QString &filePath = filePaths.at(i);

    progress.setValue(i);
    progress.setLabelText(tr("출력 중: %1").arg(QFileInfo(filePath).fileName()));
    QApplication::processEvents();

    if (progress.wasCanceled())
      break;

    // [핵심] 파일마다 실제 DrawSubWindow를 새로 만들어 로드한다.
    // LayerManager는 AppContext가 아니라 "현재 활성화된 서브윈도우"가 각자 소유하므로,
    // 이 창을 활성화하지 않은 채 loadDrawing을 호출하면 엉뚱하게 원래 활성 창(originalActive)의
    // 레이어 테이블이 이 파일의 레이어로 덮어써지는 사고가 발생한다. 반드시 활성화 후 로드해야 한다.
    DrawSubWindow *tempWindow = new DrawSubWindow();
    m_ctx->addSubWindow(tempWindow);
    m_ctx->mdiArea()->setActiveSubWindow(tempWindow);

    // [핵심] AcadrustImporter/DxfManager는 레이어를 등록할 때 scene->context()->layerManager()로
    // "현재 활성 창"의 LayerManager를 다시 찾는다(scene을 직접 안 쓰고 ambient 상태에 의존).
    // 그런데 setActiveSubWindow() 직후에도 실제 포커스 전환(WM_ACTIVATE 등)은 Windows에서
    // 이벤트 큐를 통해 비동기로 처리될 수 있어, 아래 processEvents() 없이 바로 importFromDxf/
    // importFromDwg를 호출하면 activeDrawWindow()가 아직 이전 창(예: 배치 시작 전 활성 창)을
    // 가리키는 채로 레이어가 엉뚱한 LayerManager에 등록되는 경우가 있었다. 그 결과 (1) 이
    // tempWindow의 씬은 자기 소유가 아닌 레이어의 선두께 캐시를 못 받아 흐리게(두께 0) 출력되고,
    // (2) 원래 활성 창의 레이어가 새 파일의 값으로 덮어써져 "출력 스타일 편집"이 auto로 보이는
    // 문제로 이어졌다. 여기서 이벤트 큐를 비워 활성 창 전환을 확실히 반영한 뒤 로드한다.
    // (한 번의 processEvents()로 전환 이벤트가 다 안 빠질 수 있어 몇 차례 반복한다.)
    for (int retry = 0; retry < 10 && m_ctx->activeDrawWindow() != tempWindow; ++retry) {
      QApplication::processEvents();
    }

    // [안전장치] processEvents() 이후에도 활성 창이 tempWindow로 확실히 넘어오지 않았다면
    // (알 수 없는 포커스 처리 지연 등), 잘못된(엉뚱한) LayerManager에 이 파일의 레이어가
    // 등록되어 원래 열려 있던 창의 데이터를 조용히 덮어쓰는 사고로 이어질 수 있다.
    // 이런 경우 이 파일은 건너뛰고 실패 처리하여, 데이터 손상보다는 눈에 보이는 실패를 택한다.
    if (m_ctx->activeDrawWindow() != tempWindow) {
      failedFiles.append(filePath + tr(" (창 전환 실패)"));
      tempWindow->close();
      continue;
    }

    // 확장자에 따라 로더를 분기한다: .dwg는 AcadrustImporter, .dxf는 DxfManager, 그 외(.cdb)는 FileManager
    bool loaded = false;
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QLatin1String("dwg")) {
      AcadrustImporter importer;
      loaded = importer.importFromDwg(filePath, tempWindow->scene(), nullptr, nullptr);
    } else if (ext == QLatin1String("dxf")) {
      DxfManager dxfManager(this);
      loaded = dxfManager.importFromDxf(filePath, tempWindow->scene());
    } else {
      loaded = m_ctx->fileManager()->loadDrawing(filePath, tempWindow->scene());
    }

    if (loaded) {
      if (tempWindow->undoStack()) {
        tempWindow->undoStack()->clear();
      }

      // [핵심] 활성 창에서 편집한 출력 스타일(레이어별 인쇄 색상)을 적용한다.
      // - 활성 창이 "흑백"/"컬러" 라디오로 전체 레이어를 일괄 지정한 상태라면, 이름이
      //   달라도(예: cdb와 dwg/dxf의 레이어 명명 규칙이 다른 경우) 모든 파일의 모든
      //   레이어에 동일하게 강제 적용한다.
      // - 그 외(개별 레이어만 수동으로 색을 지정한 경우)에는 기존처럼 이름이 일치하는
      //   레이어에만 적용하고, 활성 창에 없던 레이어는 이 파일 자체의 설정을 유지한다.
      if (LayerManager *tempLm = tempWindow->layerManager()) {
        const QList<Layer> tempLayers = tempLm->getAllLayers();
        for (const Layer &tl : tempLayers) {
          Layer *layer = tempLm->getLayer(tl.name());
          if (!layer) continue;

          if (activeAllBlack) {
            layer->setPrintColor(Qt::black);
            continue;
          }
          if (activeAllAuto) {
            layer->clearPrintColorOverride();
            continue;
          }

          if (!activeLayerNames.contains(tl.name())) continue;
          auto it = activePrintOverrides.constFind(tl.name());
          if (it != activePrintOverrides.constEnd()) {
            layer->setPrintColor(it.value());
          } else {
            layer->clearPrintColorOverride();
          }
        }
      }

      if (successCount > 0) {
        printer.newPage();
      }

      if (ScenePrinter::renderPage(tempWindow->scene(), &printer, &painter)) {
        ++successCount;
      } else {
        failedFiles.append(filePath);
      }
    } else {
      failedFiles.append(filePath);
    }

    tempWindow->close(); // WA_DeleteOnClose 속성으로 자동 소멸됨
  }

  painter.end();
  progress.setValue(filePaths.size());

  // 원래 활성 창 복원
  if (originalActive) {
    m_ctx->mdiArea()->setActiveSubWindow(originalActive);
  }

  // 3. 결과 요약
  if (failedFiles.isEmpty()) {
    m_ctx->showStatusText(tr("일괄 출력 완료: %1개").arg(successCount));
  } else {
    QMessageBox::warning(parentWidget, tr("일괄 출력 결과"),
        tr("%1개 중 %2개 출력 완료.\n실패한 파일:\n%3")
            .arg(filePaths.size()).arg(successCount).arg(failedFiles.join("\n")));
  }
}

// --- PrintStyleCommand ---
PrintStyleCommand::PrintStyleCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void PrintStyleCommand::execute() {
  if (!m_ctx) return;

  QWidget *parentWidget = m_ctx->activeDrawWindow();
  PrintStyleDialog dlg(m_ctx, parentWidget);
  dlg.exec();
}
