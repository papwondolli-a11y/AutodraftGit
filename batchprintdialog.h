#ifndef BATCHPRINTDIALOG_H
#define BATCHPRINTDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QComboBox>
#include <QLabel>
#include <QStringList>
#include <QPixmap>

class IAppContext;

// 여러 도면(.cdb, .dwg, .dxf) 파일을 한 번에 출력하기 위한 대상 파일 목록 구성 다이얼로그.
//
// [레이아웃]
//  - 오른쪽: 블록 팔레트 도크(BlockPaletteDock)와 동일한 방식의 폴더 브라우저.
//            라디오버튼(.cdb/.dwg/.dxf)으로 찾을 확장자를 고르고, 폴더 콤보박스 +
//            폴더 선택("...") + 초기화("✕") 버튼으로 폴더를 고르면 해당 폴더의
//            파일명 목록(미리보기 없음)이 나타난다.
//  - 왼쪽 상단: 오른쪽 목록에서 클릭한 파일의 실제 미리보기 이미지를 렌더링해서 보여준다.
//  - 왼쪽 중간: "추가" 버튼 - 클릭 시 현재 미리보기 중인 파일을 아래 목록으로 옮긴다.
//  - 왼쪽 하단: 실제 출력 대상으로 확정된 파일 목록 (작은 미리보기 아이콘 포함).
//              이 목록의 위-아래 순서가 그대로 출력 순서(페이지 순서)가 된다.
class BatchPrintDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchPrintDialog(IAppContext *ctx, QWidget *parent = nullptr);

    // 사용자가 최종 확정한 출력 대상 파일의 전체 경로 목록 (위->아래 = 출력 순서)
    QStringList filePaths() const;

private slots:
    // 오른쪽: 폴더 브라우저 (블록Dock 스타일)
    void onSourceTypeChanged();                      // 라디오버튼(.cdb/.dwg/.dxf) 변경
    void onBrowseFolderClicked();                     // 폴더 선택("...")
    void onClearHistoryClicked();                     // 폴더 기록 초기화("✕")
    void onFolderChanged(int index);                  // 폴더 콤보박스 선택 변경
    void onSourceItemClicked(QListWidgetItem *item);  // 폴더 목록에서 파일 클릭 -> 미리보기 생성

    // 왼쪽: 미리보기 + 출력 목록
    void onAddToList();       // "추가" - 미리보기 중인 파일을 출력 목록으로 이동
    void onSelectedItemClicked(QListWidgetItem *item); // 출력 목록의 작은 아이콘 클릭 -> 큰 미리보기와 동기화
    void onRemoveSelected();  // 출력 목록에서 선택 항목 제거
    void onMoveUp();          // 출력 목록에서 선택 항목을 위로
    void onMoveDown();        // 출력 목록에서 선택 항목을 아래로
    void onAccept();          // 확인(출력) - 목록이 비어있지 않을 때만 accept()

private:
    // 출력 목록 항목에 큰 미리보기 이미지(QPixmap)를 함께 저장해 두는 커스텀 데이터 역할
    static const int PreviewPixmapRole = Qt::UserRole + 1;

    void setupUI();
    void refreshSourceList();              // 현재 폴더 + 라디오 형식에 맞는 파일명 다시 나열
    QString currentExtensionGlob() const;  // 라디오버튼에 해당하는 "*.cdb" 등 glob 패턴

    // filePath의 미리보기 이미지를 만든다.
    // .cdb는 파일에 저장된 미리보기(FileManager::loadThumbnail)를 우선 사용하고,
    // 저장된 미리보기가 없거나 .dwg/.dxf인 경우 임시 도면 창에 실제로 로드해서 즉석 렌더링한다.
    QPixmap generatePreview(const QString &filePath);

    void saveFolderHistory();
    void loadFolderHistory();
    void addFolderToHistory(const QString &path);

    IAppContext *m_ctx;

    // 오른쪽: 폴더 브라우저 영역
    QRadioButton *m_radioCdb;
    QRadioButton *m_radioDwg;
    QRadioButton *m_radioDxf;
    QComboBox    *m_folderCombo;
    QPushButton  *m_browseButton;       // "..."
    QPushButton  *m_clearHistoryButton; // "✕"
    QListWidget  *m_sourceListWidget;   // 폴더 내 파일명 목록 (미리보기 없음)
    QString       m_currentFolderPath;

    // 왼쪽: 미리보기 + 출력 목록 영역
    QLabel       *m_previewLabel;       // 큰 미리보기 이미지
    QPushButton  *m_btnAddToList;       // "추가"
    QListWidget  *m_selectedListWidget; // 출력 대상 목록 (작은 아이콘)
    QPushButton  *m_btnRemove;
    QPushButton  *m_btnUp;
    QPushButton  *m_btnDown;

    QString       m_lastPreviewedPath;    // 가장 최근에 미리보기를 생성한 파일 경로
    QPixmap       m_lastPreviewedPixmap;  // 위 파일의 미리보기 이미지 ("추가" 시 아이콘으로 재사용)

    QPushButton  *m_btnOk;
    QPushButton  *m_btnCancel;
};

#endif // BATCHPRINTDIALOG_H
