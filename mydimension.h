#ifndef MYDIMENSION_H
#define MYDIMENSION_H

#include "myentity.h"
#include "myvector.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// 치수선 화살표 끝 모양 (치수설정 매니저의 "화살표" 그룹에서 선택)
// [주의] 기존 저장 파일과의 JSON 호환을 위해 각 값의 번호(0~4)는 그대로 두고
// 새 항목(Hollow)은 맨 뒤에 추가한다 — 순서를 바꾸면 이미 저장된 도면의
// arrowStyle 값이 다른 모양으로 잘못 읽힌다.
enum class ArrowheadStyle {
  Filled = 0,   // 채운화살표 (속이 찬 삼각형)
  Open,         // 열린화살표 (닫히지 않은 V자형)
  FilledCircle, // 채운원
  OpenCircle,   // 빈원
  DoubleCircle, // 이중원
  Hollow        // [신규] 빈화살표 (속이 빈 닫힌 삼각형)
};

struct DimensionStyle {
  double arrowSize = 2.0;
  ArrowheadStyle arrowStyle = ArrowheadStyle::Filled;
  double textSize = 2.5;
  double toleranceSize = 2.0;
  double textGap = 0.5;
  double extOffset = 2.0;
  double extExtend = 2.0;
  int precision = 2;
  int tolPrecision = 2;
  bool suppressTrailingZeros = true;
  bool suppressTrailingZerosTol = true;
  bool useThousandSep = false;     // 천 단위 자릿점(,) 표시
  double baselineSpacing = 7.0;
  double dimScale = 1.0;        // 치수선 전체 스케일
  QString dimLineLayer = "";
  QString dimTextLayer = "";
  QString fontFamily = "Arial";

  QJsonObject toJson() const {
    QJsonObject obj;
    obj["arrowSize"] = arrowSize;
    obj["arrowStyle"] = static_cast<int>(arrowStyle);
    obj["textSize"] = textSize;
    obj["toleranceSize"] = toleranceSize;
    obj["textGap"] = textGap;
    obj["extOffset"] = extOffset;
    obj["extExtend"] = extExtend;
    obj["precision"] = precision;
    obj["tolPrecision"] = tolPrecision;
    obj["suppressTrailingZeros"] = suppressTrailingZeros;
    obj["suppressTrailingZerosTol"] = suppressTrailingZerosTol;
    obj["useThousandSep"] = useThousandSep;
    obj["baselineSpacing"] = baselineSpacing;
    obj["dimScale"] = dimScale;
    obj["dimLineLayer"] = dimLineLayer;
    obj["dimTextLayer"] = dimTextLayer;
    obj["fontFamily"] = fontFamily;
    return obj;
  }

  static DimensionStyle fromJson(const QJsonObject &obj) {
    DimensionStyle s;
    if (obj.contains("arrowSize"))
      s.arrowSize = obj["arrowSize"].toDouble();
    if (obj.contains("arrowStyle")) {
      int v = obj["arrowStyle"].toInt();
      if (v >= static_cast<int>(ArrowheadStyle::Filled) && v <= static_cast<int>(ArrowheadStyle::Hollow))
        s.arrowStyle = static_cast<ArrowheadStyle>(v);
    }
    if (obj.contains("textSize"))
      s.textSize = obj["textSize"].toDouble();
    if (obj.contains("toleranceSize"))
      s.toleranceSize = obj["toleranceSize"].toDouble();
    if (obj.contains("textGap"))
      s.textGap = obj["textGap"].toDouble();
    if (obj.contains("extOffset"))
      s.extOffset = obj["extOffset"].toDouble();
    if (obj.contains("extExtend"))
      s.extExtend = obj["extExtend"].toDouble();
    if (obj.contains("precision"))
      s.precision = obj["precision"].toInt();
    if (obj.contains("tolPrecision"))
      s.tolPrecision = obj["tolPrecision"].toInt();
    if (obj.contains("suppressTrailingZeros"))
      s.suppressTrailingZeros = obj["suppressTrailingZeros"].toBool();
    if (obj.contains("suppressTrailingZerosTol"))
      s.suppressTrailingZerosTol = obj["suppressTrailingZerosTol"].toBool();
    if (obj.contains("useThousandSep"))
      s.useThousandSep = obj["useThousandSep"].toBool();
    if (obj.contains("baselineSpacing"))
      s.baselineSpacing = obj["baselineSpacing"].toDouble();
    if (obj.contains("dimScale"))
      s.dimScale = obj["dimScale"].toDouble();
    if (obj.contains("dimLineLayer"))
      s.dimLineLayer = obj["dimLineLayer"].toString();
    if (obj.contains("dimTextLayer"))
      s.dimTextLayer = obj["dimTextLayer"].toString();
    if (obj.contains("fontFamily"))
      s.fontFamily = obj["fontFamily"].toString();
    return s;
  }
};

enum class DimensionType { Linear, Radial, Diametric, Angular, HalfDimension };

// [추가] 공차 유형을 정의하는 열거형
enum class ToleranceType {
    None = 0,       // 없음
    Symmetrical,    // 대칭 (±)
    Deviation,      // 편차 (+상한/-하한)
    Limits,         // 한계치 (최대/최소)
    FitCode         // [신규] 끼워맞춤 등급기호(H7, g6 등) - fitCode로부터 상/하한을 자동 계산
};

class MyDimension : public MyEntity {
public:
  MyVector start;
  MyVector end;
  MyVector dimLine; // This point determines the position/height of the
                    // dimension line
  MyVector end2;     // For angular dimension (second line/point)
  QString text;      // Custom text, empty if using automatic measurement

  DimensionType dimensionType = DimensionType::Linear;

  double fixedAngle = 0.0; // Angle for rotated dimension
  bool useFixedAngle = false;
  bool hideExt1 = false;
  bool hideExt2 = false;
  // [신규] 토탈 치수용: >0 이면 치수보조선을 기점 부근이 아니라 "기존 치수선 높이(|h|) + extExtend"
  // 지점에서 시작한다 → 기존 치수의 치수보조선과 겹치지 않고 끝에서 이어서 그려진다.
  double extChainH1 = 0.0;
  double extChainH2 = 0.0;
  bool hideArrow1 = false;
  bool hideArrow2 = false;
  bool flipArrow1 = false;
  bool flipArrow2 = false;

  // ==========================================
  // [신규 추가] 기호 및 공차 데이터 필드
  // ==========================================
  QString prefix;                 // 머리말 (ex: "Ø", "2-")
  QString suffix;                 // 꼬리말 (ex: " MIN")
  ToleranceType tolType = ToleranceType::None; // 공차 유형
  double tolUpper = 0.0;          // 상한 공차
  double tolLower = 0.0;          // 하한 공차
  QString fitCode;                // [신규] 끼워맞춤 등급기호 (ex: "H7", "g6") - tolType==FitCode일 때 사용
  bool fitShowValues = false;     // [신규] true면 등급기호 옆에 계산된 숫자 상/하한도 병기 (기본: 기호만 표기, 실무 관례)
  int tolPrecision = -1;           // -1: 스타일 상속, >=0: 개별 설정
  int precision = -1;              // -1: 스타일 상속, >=0: 개별 설정
  bool tolUseAngle = false;       // [신규 추가] 각도 부호(°) 사용 여부
  bool suppressTrailingZeros = false;    // [신규] 주치수 소수점 뒤 0 억제 (DIMZIN)
  bool suppressTrailingZerosTol = false; // [신규] 공차 소수점 뒤 0 억제 (DIMTZIN)
  // ==========================================
  // [신규 추가] 지시선(Leader) 커스텀 텍스트 위치 데이터
  // ==========================================
  bool useCustomTextPos = false;  // 지시선 모드 활성화 여부
  bool useLeader = false;
  MyVector customTextPos;         // 지시선이 꺾이는 지점 (Elbow)
  bool isFullCircle = false;      // 원(Circle) 선택 여부 (전체 지름선 표시용)
  double dimScaleOverride = 0.0; // >0이면 씬 스타일 dimScale 대신 이 값을 사용 (DXF/DWG 임포트)

  // [신규] 측정값 표시 배율 (AutoCAD DIMLFAC 대응). 표시 숫자 = 기하학적 측정값 * measureScale.
  //  · 화살표/글자 크기에는 영향 없음(그건 dimScale/dimScaleOverride 담당).
  //  · 상세도 영역(축척 s로 baked된 형상) 안에서 생성된 치수는 measureScale = 1/s 로 두어
  //    확대된 형상 위에서도 "원본 실측값"이 표시되도록 한다.
  double measureScale = 1.0;

  MyDimension(const MyVector &_start = MyVector(),
              const MyVector &_end = MyVector(),
              const MyVector &_dimLine = MyVector(), const QString &_text = "")
      : start(_start), end(_end), dimLine(_dimLine), text(_text) {}

  EntityType type() const override { return EntityType::Dimension; }

  std::vector<MyVector> intersect(const MyEntity *other) const override
  {
      Q_UNUSED(other)
    return std::vector<MyVector>();
  }

  MyVector getNearestPoint(const MyVector &pos) const override
  {
      Q_UNUSED(pos)
      return start; }
};

#endif // MYDIMENSION_H
