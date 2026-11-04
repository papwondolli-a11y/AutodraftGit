#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QGraphicsItem>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

class MyGraphicsScene;
class IAppContext;
class CadItem; // [수정] 클래스 밖으로 이동
class QProgressDialog;

class FileManager : public QObject {
  Q_OBJECT
public:
  explicit FileManager(IAppContext *ctx, QObject *parent = nullptr);
  ~FileManager();

  // 도면 저장 (파일 경로, 씬)
  bool saveDrawing(const QString &filePath, MyGraphicsScene *scene);

  // 도면 불러오기 (파일 경로, 씬)
  //   asTemplate=true : 템플릿(acad.dwt 개념)으로 여는 경우. 최근 파일 목록에
  //   추가하지 않고, 저장돼 있던 테마를 사용자 화면에 강제 적용하지 않는다.
  //   progress: 넘겨주면 엔티티 로딩 루프 중 주기적으로 갱신한다(응답 없음
  //   방지 — 큰 도면에서 DXF/DWG 가져오기와 동일한 방식).
  bool loadDrawing(const QString &filePath, MyGraphicsScene *scene,
                   bool asTemplate = false, QProgressDialog *progress = nullptr);

  // [신규] 선택된 아이템들만 내보내기 (WBLOCK)
  bool exportSelectedItems(const QString &filePath,
                           const QList<QGraphicsItem *> &items,
                           QPointF basePoint);

  // 최근 파일 리스트 관리
  void addRecentFile(const QString &filePath);
  void removeRecentFile(const QString &filePath);
  void clearRecentFiles();
  QStringList getRecentFiles() const;

  // [신규] 썸네일 불러오기
  QImage loadThumbnail(const QString &filePath);

  // [신규] 블록 불러오기 (객체 리스트 반환)
  QList<CadItem *> importBlock(const QString &filePath,
                               QString *originalLayerName = nullptr);

  // 미리보기 이미지 생성 (Scene -> PNG BLOB). 저장 시 내부적으로 쓰이지만,
  // 일괄 출력 다이얼로그 등에서 즉석 미리보기를 만들 때도 재사용한다.
  QByteArray capturePreview(MyGraphicsScene *scene);

signals:
  void libraryChanged();

private:
  // 데이터베이스 초기화 및 스케마 생성
  bool initDatabase(const QString &dbPath);

  // [신규] 객체 직렬화/역직렬화 헬퍼
  void serializeCadItem(CadItem *item, QDataStream &out, QJsonObject &propObj,
                        QPointF offset = QPointF(0, 0));
  CadItem *deserializeCadItem(const QString &type, const QJsonObject &propObj,
                              QDataStream &in, const QPen &layerPen);

  IAppContext *m_ctx;
};

#endif // FILEMANAGER_H
