#include "core/AppInfo.h"

namespace writero::app {

QString version()
{
    return QStringLiteral(WRITERO_VERSION);
}

QString displayVersion()
{
    return QStringLiteral("Writero %1").arg(version());
}

} // namespace writero::app
