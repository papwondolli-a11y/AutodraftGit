#ifndef LAYER3DDIALOG_H
#define LAYER3DDIALOG_H

#include <QDialog>

class AppContext;
class LayerManager;
class QTableWidget;
class QLabel;

// [3D Layer] 메뉴바 "3D Layer" > "레이어 높이 설정..."
// 현재(활성) 도면창의 레이어 목록을 보여주고, 레이어마다 기준높이·높이(mm)를 입력한다.
// 값은 Layer::elevation()/height() 에 저장되며 .cdb 에 함께 저장된다.
class Layer3DDialog : public QDialog {
  Q_OBJECT
public:
  explicit Layer3DDialog(AppContext *context, QWidget *parent = nullptr);
  ~Layer3DDialog() override;

private slots:
  void onAccept();

private:
  enum Column { ColName = 0, ColElevation, ColHeight, ColCount };

  void populate();

  AppContext *m_appContext = nullptr;
  LayerManager *m_layerManager = nullptr; // 다이얼로그를 연 시점의 도면 레이어
  QTableWidget *m_table = nullptr;
  QLabel *m_docLabel = nullptr;
};

#endif // LAYER3DDIALOG_H
