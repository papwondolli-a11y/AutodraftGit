#include "mainwindow.h"
#include "settingsmigration.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[]) {
  // [개선] 빌드 환경에 따른 UI 스케일링 일관성 확보

  // [GL 안정성] 일부 GPU/드라이버에서 Qt의 기본 멀티샘플 설정으로
  // QOpenGLWidget 프레임버퍼를 만들면 "Framebuffer incomplete attachment"로
  // 실패한다. 도면 뷰는 2D 렌더링만 하므로 깊이/스텐실 버퍼가 필요 없고,
  // QApplication 생성 전에 설정해야 첫 OpenGL 컨텍스트부터 적용된다.
  // [화질] QOpenGLWidget 뷰포트의 QPainter::Antialiasing 은 멀티샘플 FBO 가 있어야
  // 실제로 동작한다. 샘플이 0 이면 대각선·곡선이 계단처럼 깨지므로 4x MSAA 를 쓴다.
  // 특정 GPU 에서 "Framebuffer incomplete" 가 나면 환경변수 AUTODRAFT_GL_SAMPLES=0
  // 으로 예전(멀티샘플 끔) 동작으로 되돌릴 수 있다.
  QSurfaceFormat glFormat;
  glFormat.setSamples(qEnvironmentVariableIsSet("AUTODRAFT_GL_SAMPLES")
                          ? qEnvironmentVariableIntValue("AUTODRAFT_GL_SAMPLES")
                          : 4);
  glFormat.setDepthBufferSize(0);
  glFormat.setStencilBufferSize(0);
  QSurfaceFormat::setDefaultFormat(glFormat);

  QApplication a(argc, argv);

  // [설정 저장소] 전역 QSettings 를 .ini 파일로 통일한다.
  //   어떤 QSettings 생성보다 먼저 포맷·org·app 을 박고, 과거 저장소(레지스트리)의
  //   값을 .ini 로 1회 이전한다. 이후 코드는 인자 없는 QSettings() 만 써도
  //   <RoamingAppData>/Autodraft/Autodraft.ini 를 본다.
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QCoreApplication::setOrganizationName(QStringLiteral("Autodraft"));
  QCoreApplication::setApplicationName(QStringLiteral("Autodraft"));
  migrateLegacySettings();

  // [i18n] 언어 설정("ui/language")에 따라 영문 UI 번역(.qm)을 로드한다.
  //   "ko" 면 원문(소스의 한글 msgid)을 그대로 쓰고, 그 외("en" 기본)면 번역을 건다.
  //   메뉴 [설정 > 언어 / Language] 에서 전환하며, 전환 시 앱 재시작으로 반영된다.
  //   번역을 갱신하려면 translations/build_translations.bat 실행.
  const bool uiKorean =
      QSettings().value(QStringLiteral("ui/language"), QStringLiteral("en")).toString()
      == QStringLiteral("ko");
  QTranslator appTranslator;
  if (!uiKorean && appTranslator.load(QStringLiteral(":/i18n/autodraft_en.qm")))
    a.installTranslator(&appTranslator);

  // [i18n] 한국어 모드면 Qt 표준 대화상자(QMessageBox 버튼, QFileDialog 등)도
  //   한글로 표시한다. 배포본은 <exe>/translations/qt_ko.qm(windeployqt 병합본),
  //   개발 환경은 Qt 설치 경로의 번역을 쓴다.
  QTranslator qtTranslator;
  if (uiKorean &&
      (qtTranslator.load(QStringLiteral("qt_ko"),
                         QCoreApplication::applicationDirPath() +
                             QStringLiteral("/translations")) ||
       qtTranslator.load(QStringLiteral("qtbase_ko"),
                         QLibraryInfo::path(QLibraryInfo::TranslationsPath))))
    a.installTranslator(&qtTranslator);

  MainWindow w;
  w.showNormal();
  QTimer::singleShot(0, &w, &MainWindow::showMaximized);

  return a.exec();
}
