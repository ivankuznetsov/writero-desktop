#pragma once

#include <QString>

namespace writero {

/// Static application identity, used for settings paths, D-Bus names, and
/// the window title. Kept in one place so tests and packaging agree.
namespace app {
inline constexpr auto Name = "Writero";
inline constexpr auto Organization = "Writero";
inline constexpr auto OrganizationDomain = "writero.app";
inline constexpr auto DesktopFileName = "app.writero.Writero";

QString version();
QString displayVersion();
} // namespace app

} // namespace writero
