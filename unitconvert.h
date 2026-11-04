#ifndef UNITCONVERT_H
#define UNITCONVERT_H

#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// UnitConvert
//   길이 단위(mm ↔ inch) 변환 코어. 상태를 갖지 않는 순수 정적 함수 모음.
//
//   [원칙]
//     · 내부 저장(model)은 항상 mm 로 고정한다.
//     · inch 는 좌표 입력·화면 표시(display) 단계에서만 환산한다.
//     · "현재 도면 단위" 는 DocumentSettings(키 "units/linear") 에 산다.
//       이 클래스는 그 값을 인자로만 받는다 — 전역 상태에 접근하지 않는다.
//
//   도면 축척(scene->drawingScale(), 예: 1:50)과는 완전히 별개 개념이다.
// ─────────────────────────────────────────────────────────────────────────────

enum class LinearUnit {
  Mm = 0,   // $INSUNITS = 4
  Inch = 1  // $INSUNITS = 1
};

class UnitConvert {
public:
  // 표시단위 1 당 mm 값.  Mm → 1.0,  Inch → 25.4
  static double mmPerUnit(LinearUnit u);

  // 표시값 → 모델값(mm).   displayValue * mmPerUnit(u)
  static double toModel(double displayValue, LinearUnit u);
  // 모델값(mm) → 표시값.    modelValue / mmPerUnit(u)
  static double toDisplay(double modelValue, LinearUnit u);

  // 면적 변환(배율 제곱).  모델(mm²) ↔ 표시(표시단위²)
  static double toModelArea(double displayArea, LinearUnit u);
  static double toDisplayArea(double modelArea, LinearUnit u);

  // 길이 표시에 붙는 단위 기호.  Mm → ""(없음),  Inch → "\""
  // 국제 통용 기호이므로 번역(tr) 대상이 아니다.
  static QString suffix(LinearUnit u);
  // 면적 단위 기호.  Mm → "mm²",  Inch → "in²"
  static QString areaSuffix(LinearUnit u);

  // 신규 치수 생성 시 기본 소수 자릿수.
  static int defaultLinearPrecision(LinearUnit u);    // Mm → 2,  Inch → 3
  static int defaultTolerancePrecision(LinearUnit u); // Mm → 2,  Inch → 4

  // DocumentSettings 직렬화용 문자열.  "mm" / "inch".  미인식 문자열 → Mm
  static LinearUnit fromString(const QString &s);
  static QString toString(LinearUnit u);

  // DXF/DWG $INSUNITS 코드 매핑.  Inch=1, Mm=4.  그 외 코드 → Mm
  static LinearUnit fromInsUnits(int insunits);
  static int toInsUnits(LinearUnit u);
};

#endif // UNITCONVERT_H
