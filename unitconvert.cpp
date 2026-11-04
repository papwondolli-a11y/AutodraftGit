#include "unitconvert.h"

namespace {
constexpr double kMmPerInch = 25.4;
}

double UnitConvert::mmPerUnit(LinearUnit u) {
  switch (u) {
  case LinearUnit::Inch:
    return kMmPerInch;
  case LinearUnit::Mm:
  default:
    return 1.0;
  }
}

double UnitConvert::toModel(double displayValue, LinearUnit u) {
  return displayValue * mmPerUnit(u);
}

double UnitConvert::toDisplay(double modelValue, LinearUnit u) {
  return modelValue / mmPerUnit(u);
}

double UnitConvert::toModelArea(double displayArea, LinearUnit u) {
  const double f = mmPerUnit(u);
  return displayArea * f * f;
}

double UnitConvert::toDisplayArea(double modelArea, LinearUnit u) {
  const double f = mmPerUnit(u);
  return modelArea / (f * f);
}

QString UnitConvert::suffix(LinearUnit u) {
  switch (u) {
  case LinearUnit::Inch:
    return QStringLiteral("\"");
  case LinearUnit::Mm:
  default:
    return QString();
  }
}

QString UnitConvert::areaSuffix(LinearUnit u) {
  switch (u) {
  case LinearUnit::Inch:
    return QStringLiteral("in²"); // in²
  case LinearUnit::Mm:
  default:
    return QStringLiteral("mm²"); // mm²
  }
}

int UnitConvert::defaultLinearPrecision(LinearUnit u) {
  return (u == LinearUnit::Inch) ? 3 : 2;
}

int UnitConvert::defaultTolerancePrecision(LinearUnit u) {
  return (u == LinearUnit::Inch) ? 4 : 2;
}

LinearUnit UnitConvert::fromString(const QString &s) {
  const QString v = s.trimmed().toLower();
  if (v == QLatin1String("inch") || v == QLatin1String("in") ||
      v == QLatin1String("imperial"))
    return LinearUnit::Inch;
  return LinearUnit::Mm;
}

QString UnitConvert::toString(LinearUnit u) {
  return (u == LinearUnit::Inch) ? QStringLiteral("inch") : QStringLiteral("mm");
}

LinearUnit UnitConvert::fromInsUnits(int insunits) {
  // ODA/AutoCAD $INSUNITS: 1=Inches, 4=Millimeters.
  // 그 외 코드(피트·미터 등)는 이 앱에서 미지원 → mm 로 처리.
  return (insunits == 1) ? LinearUnit::Inch : LinearUnit::Mm;
}

int UnitConvert::toInsUnits(LinearUnit u) {
  return (u == LinearUnit::Inch) ? 1 : 4;
}
